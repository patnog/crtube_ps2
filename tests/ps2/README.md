# R5900 MMI instruction test ELF

The primary hardware test uses the same EE startup and IOP/network setup as
`bin/crtube-ps2-edge264-experimental.elf`, but it does not link or execute the
MP4 playback/decoder path.

Build after sourcing the PS2SDK environment:

```sh
make h264-r5900-mmi-runner
```

This produces `bin/crtube-ps2-edge264-experimental.elf`. The ELF initializes
the application runtime, network and SMB share, then writes the test report to
`smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt`. It does not open an MP4 or initialize
the H.264 decoder. The test result remains visible on screen; press
SELECT+START to return to OSDSYS.

The runner first executes `edge264_r5900_mmi_selftest()`, which checks 22 MMI
primitive operations against C reference results. It then runs the decoder
function equivalence suite in the same ELF and writes all test summaries and
reference CRCs to the same SMB log. The additional tests cover soft/hard luma
and chroma deblocking, IDCT 4x4/8x8, DC reconstruction 4x4/2x2, intra
prediction, inter interpolation, motion-vector scaling, residual scaling,
and deblock packing kernels. The larger function tests compare fixed CRCs
against the host-validated reference values. `PADDUB`, `PMADDH`, `PHMADH`, and `PSUBUB` execute raw EE
instructions directly, bypassing scalar wrappers. Each receives 20,000
deterministic pseudo-random vectors plus 32 boundary vectors.

The PMADDH oracle initializes HI and LO with known 128-bit seeds, executes the
instruction, then captures `rd`, `LO` (`PMFLO`) and `HI` (`PMFHI`). It verifies
all eight signed halfword products and all eight independent 32-bit
accumulators, including wraparound. The first mismatch is logged with both
input vectors, accumulator seeds, and expected/actual `rd`, `LO` and `HI` words.

PMADDH is not a horizontal dot-product instruction. It multiplies each
corresponding signed halfword pair independently and accumulates the eight
products in HI/LO. PHMADH is the horizontal operation: it adds adjacent
products into four 32-bit results. Both are tested separately so a mistaken
substitution cannot pass. The report records each PASS/FAIL result and the
complete failure bitmask.

The old `make -C tests/ps2` MMI-only ELF is not used by
`h264-r5900-mmi-runner`; use the root target above so the test report is
written through the application's SMB logger.


## Full R5900 decoder regression

After the decoder-independent suite passes, build the next-stage runner:

```sh
make h264-r5900-decoder-runner
```

This ELF repeats all 22 direct MMI operation tests and the decoder-function
equivalence tests, then decodes the embedded `tests/data/mc_coverage.mp4`
fixture through the actual R5900 Edge264 backend. The fixture contains 180
frames at 320x240; no MP4 needs to be copied to the SMB share. The test uses
the normal application startup and writes to the same
`smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt` log. If SMB cannot be mounted or the
log cannot be opened, the runner aborts before executing tests; it never
silently runs a test session without the requested SMB report.

Every displayed frame is fingerprinted using CRC-32/ISO-HDLC over visible
Y/Cb/Cr bytes (stride padding excluded). Nine zero-based frame positions
(0, 1, 2, 29, 59, 89, 119, 149 and 179) are checked on the PS2 against
independent FFmpeg `yuv420p` reference CRCs. Any mismatch fails the decoder
regression and is logged with actual and expected plane CRCs. The complete
per-frame CRC stream remains in the SMB log for diagnosis.

The host reference was validated with:

```sh
sh tests/run_host_edge264_plane_reference.sh tests/data/mc_coverage.mp4
```

The host script verifies all 180 generic Edge264 frames against FFmpeg and
also checks the SIMD=6 scalar-lane fallback. The PS2 runner is the first
hardware check of the actual EE MMI backend against those frame fingerprints.


### Diagnóstico MMI versus escalar

Se o runner MMI acusar divergência de CRC, construa e execute também:

```sh
make h264-r5900-decoder-scalar-runner
```

Esse alvo mantém os testes diretos das instruções MMI, mas compila o decoder
com `EDGE264_R5900_MMI=0`. O log continua em
`smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt` e identifica explicitamente
`R5900 MMI=DISABLED (scalar fallback)`. Compare as CRCs dos mesmos quadros:
se o escalar passar e o MMI falhar, a divergência está no backend MMI; se
ambos falharem com os mesmos valores, investigue a referência, o parser ou
o caminho comum do decoder.


### Isolamento de min/max unsigned-byte

Para manter o decoder MMI ativo, mas executar `minu8` e `maxu8` pelos loops
escalares, use:

```sh
make h264-r5900-decoder-no-minmax-runner
```

O runner mantém o self-test MMI direto, grava no mesmo `smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt`
e identifica no log `unsigned-byte min/max=SCALAR`. Se as CRCs dos 180 quadros
passarem, o defeito está no caminho MMI de min/max; caso contrário, esse grupo
fica descartado e o próximo diagnóstico deve isolar as operações de residual.

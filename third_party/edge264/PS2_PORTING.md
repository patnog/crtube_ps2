# edge264 PS2 porting plan

## Current migration state

- edge264 upstream sources and BSD-3-Clause license are vendored under
  `third_party/edge264`.
- `src/h264_edge264.c` is the project adapter for Annex-B NAL input and the
  existing length-prefixed MP4 sample helpers.
- The adapter exposes cropped Y/Cb/Cr planes, dimensions, and byte strides.
- The previous hand-written H.264 decoder is no longer linked into the default
  ELF. Its source files remain temporarily as historical reference.
- The default EE application still rejects `H264_BACKEND=edge264`; the R5900
  backend now has a host equivalence test and an EE translation-unit compile
  gate, but has not yet passed a PS2SDK cross-build in this environment or a
  hardware test.
- `n_threads=0` is mandatory. The PS2 pthread header is an ABI shim for code
  paths that must not be executed.
- A PS2 platform branch now supplies aligned allocation and a `clock()` timing
  fallback, and disables runtime CPU-count probing/thread creation.

## Target contract

Initial target: YouTube AVC streams at 240p, with 480p as a stretch target.

- AVC/H.264, progressive, 8-bit, 4:2:0.
- CAVLC and CABAC.
- I/P/B pictures, reference reordering and weighted prediction.
- Main and Progressive High features supported by edge264.
- Visible dimensions up to 426x240 initially; 854x480 only after profiling.
- Keep MP4/AVCC demuxing and network transport outside the decoder.
- Convert decoder output to the GS texture/upload layout in a separate stage.

The decoder reports coded plane strides separately from visible dimensions.
The renderer must use those strides; it must not assume `stride == width`.
The crop offsets are applied by the adapter before exposing visible plane
pointers.

## Work plan

### 1. R5900 portability backend (bring-up; not performance-complete)

- Added `make h264-r5900-probe`, which compiles a standalone object with the
  active `EE_CC` and checks 32-bit EE ABI assumptions, 128-bit GCC vector
  types, vector permutation support, 64-bit arithmetic, and MMI assembler
  acceptance (`paddub`, `qfsrv`, `pmaddh`). It does not link into the ELF.
- Run it after sourcing the PS2SDK environment. A passing host build is not
  accepted as evidence for the EE compiler.
- Record the exact compiler output before selecting the vector representation
  and implementing `SIMD=R5900`; do not assume GCC shuffle semantics or MMI
  assembler availability from the compiler name alone.
- Added `SIMD=R5900` using GCC vector types and explicit lane-wise C semantics
  for the complete edge264 primitive contract. The build requires
  `-flax-vector-conversions`, which is also exercised by the compiler probe.
- Added R5900-specific interpolation, intra, residual, deblocking, and motion
  scaling implementations. The first MMI-connected hot operations are
  saturating halfword addition (`paddsh`) and pairwise multiply-add (`pmaddh`);
  remaining operations are correctness-first C and are not yet optimized.
- `make h264-r5900-edge264-host-test` compares all decoded visible Y/Cb/Cr
  samples against the existing host backend on the 116-frame fixture.
- `make h264-r5900-edge264-ee-compile` compiles the decoder, MMI primitives,
  and adapter as EE objects without linking them into the application. Run it
  with PS2SDK loaded.
- Keep `H264_BACKEND=edge264` blocked until the EE compile, link, broader
  conformance, memory budget, and hardware playback gates succeed.

### 2. Memory and DPB policy

- Implement edge264 allocation callbacks backed by a bounded PS2 frame pool.
- Enforce coded-picture and DPB budgets before accepting SPS/PPS.
- Reject streams above the configured 240p/480p limits before allocating
  decoded surfaces.
- Measure peak RAM with B-frame reorder and reference retention enabled.

### 3. MP4 and YouTube stream integration

- Feed avcC SPS/PPS once per track and length-prefixed NAL samples in decode
  order.
- Add fragmented MP4 (`moof`/`traf`/`trun`) support to the demuxer; current
  `h264_mp4.c` only covers the existing sample-table path.
- Keep audio/video timestamps and separate adaptive audio tracks in the
  playback controller.
- Add stream selection that requests AVC (`avc1`/`avc3`) and AAC, never VP9,
  AV1, or Opus on the PS2 path.

### 4. GS output path

- Add a YUV420-to-GS upload/conversion stage with explicit source strides.
- Use PS2 DMA and GS transfer paths without retaining decoder-owned pointers.
- Add crop, aspect-ratio, and 240p/480p output handling.
- Profile EE CPU time, VU1 usage, upload bandwidth, and frame drops.

### 5. Conformance and regression

- Decode selected edge264 conformance streams on the host and compare output
  planes against FFmpeg.
- Add real YouTube AVC samples at 144p/240p and 360p/480p, including B-frames,
  CABAC, and High Profile features.
- Run the same vectors through the R5900 build and compare CRCs per plane.
- Test malformed NALs, oversized SPS dimensions, DPB exhaustion, and EOS flush.
- Hardware acceptance: sustained 240p playback first; 480p is a measured goal,
  not a promise.

## Known blockers

- Upstream edge264 does not currently document MIPS/R5900 or PS2SDK support.
- Its generic backend uses Clang vector builtins; the PS2SDK GCC toolchain does
  not provide that interface.
- Clang 17 builds the generic backend on the host, but the upstream conformance
  suite reports a CRC mismatch in `level-violations`; do not use that backend
  as a substitute for an R5900 port.
- The upstream scalar backend is explicitly unfinished.
- The current project has no connected MP4-to-video-to-GS playback path.
- The current MP4 parser does not yet implement fragmented MP4.

Do not mark the decoder complete until the R5900 build, conformance comparison,
and real-hardware playback gates all pass.


## R5900 MMI primitive bring-up

The isolated EE implementation lives in `ps2/edge264_r5900_mmi.c`. It
currently provides eleven 128-bit operations as pairs of 64-bit MMI operations:

- `paddub` / `psubub`: unsigned byte addition/subtraction with modulo-256 wraparound;
- `paddsh`: signed halfword addition with saturation;
- `pmaddh`: pairwise signed-halfword multiply-add into 32-bit lanes;
- `pminh` / `pmaxh`: signed halfword minimum/maximum;
- `pcgth` / `pceqh`: signed halfword greater-than/equality masks;
- `pand` / `por` / `pxor`: 128-bit bitwise operations.

The host implementation is a scalar reference used to verify lane ordering and
integer semantics. Run `make h264-r5900-mmi-host-test` for those checks. Run
`make h264-r5900-mmi-ee-test` with PS2SDK loaded to compile the actual inline
assembly and verify all eleven instructions are present in the object code.
The general compiler probe also checks the instruction mnemonics independently.
The `paddsh` and `pmaddh` operations are now used by the R5900 vector backend
for saturated arithmetic and interpolation multiply-add. Other operations in
the decoder backend remain correctness-first C implementations and are not yet
performance-optimized. Run `make h264-r5900-edge264-host-test` for decoded-plane
equivalence and `make h264-r5900-edge264-ee-compile` for an EE
translation-unit compile check. `make h264-edge264-ee-link` now builds a
separate `bin/crtube-ps2-edge264-experimental.elf` with edge264 and the R5900
MMI object linked in. This opt-in target does not alter the default ELF or
enable `H264_BACKEND`; it is a symbol-resolution/link gate only, not a runtime
or playback validation. The adapter uses `n_threads=0` and a bounded 12 MiB
default pool for decoded-frame and macroblock allocations. Applications can
select another pool limit with
`crtube_h264_edge264_create_with_memory_limit()` and inspect usage through
`crtube_h264_edge264_get_memory_stats()`. The input scratch buffer and decoder
control object are outside that pool limit.

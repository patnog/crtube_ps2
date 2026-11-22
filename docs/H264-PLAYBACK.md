# H.264 MP4 playback controller

`crtube_h264_decode_mp4_sample()` is the first sample-to-frame integration
point. It converts one MP4 AVC sample from length-prefixed NAL units to Annex B,
loads the first SPS/PPS pair from `avcC`, removes emulation-prevention bytes,
parses the slice header, dispatches to the existing I4x4 or P-slice path, and
stores the reconstructed picture in the single-reference DPB.

The caller owns all memory. Allocate two YUV420 frames (current and DPB), an
Annex-B buffer large enough for the largest MP4 sample plus start codes, an
RBSP buffer large enough for the largest NAL payload, and one
`crtube_h264_i4x4_mb_data_t` per macroblock. No heap allocation is performed
inside the controller.

## Supported subset

- MP4 AVC (`avc1`/`avc3`) with length-prefixed NAL units and `avcC` parameter
  sets; first SPS and first PPS only.
- 8-bit YUV420, frame-only pictures, CAVLC, no FMO.
- One complete VCL slice per MP4 sample.
- IDR I-slice through the existing Intra_4x4 path.
- P-slice with P_Skip and P_L0_16x16, one active reference.
- No B slices, weighted prediction, multiple reference frames, field pictures,
  or multiple slices per sample.

The controller intentionally returns `-2` for unsupported syntax instead of
producing a potentially corrupt frame.

## Current target-stream blocker

The checked-in `240p-baseline.mp4` was probed on the host. Its SPS is 320x240,
Baseline profile, 8-bit 4:2:0 and its PPS selects CAVLC. However, the first IDR
slice reaches an Intra macroblock type other than I_NxN (the current parser
supports only Intra_4x4), so sample 0 returns `-2`. This is an actual decoder
coverage gap, not a build failure. The next decoder implementation task is
Intra_16x16 macroblock syntax, prediction, DC transform and reconstruction;
then the sample controller can be validated against this file.

## Host checks

Run `sh tests/run_host_h264_tests.sh` for the P-slice and NAL-to-RBSP unit
tests. A strict host syntax check of the H.264 C modules is also recommended.
The PS2SDK build and console playback must still be run in the PS2SDK
environment; host checks do not establish PS2 runtime correctness.


## Intra slice integration

The playback controller now dispatches I-slice macroblocks between Intra_16x16
and Intra_4x4 syntax. The Intra_16x16 path parses luma DC/AC and 4:2:0 chroma
residuals, performs the 4x4 DC Hadamard/scaling step, reconstructs luma and
chroma, and updates CAVLC neighbor contexts in raster order. This path still
requires one slice beginning at macroblock zero, CAVLC, 8-bit 4:2:0, and no
8x8 transform. P-slice and reference-picture limitations remain unchanged.

The host synthetic I16x16 test validates zero-residual DC prediction and the
macroblock/slice API. It is not a conformance test. The current 240p-baseline.mp4
first sample still reaches an Intra_4x4 residual CAVLC failure at macroblock
124; further CAVLC/context validation is required before claiming full-frame
decoding of that asset.

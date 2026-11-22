#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MP4=${1:-${MP4:-}}
OUT=${TMPDIR:-/tmp}/crtube-edge264-plane-crc
R5900_OUT=${TMPDIR:-/tmp}/crtube-edge264-plane-crc-r5900-scalar
EDGE_OUT=${TMPDIR:-/tmp}/crtube-edge264-plane-edge264.csv
R5900_CSV=${TMPDIR:-/tmp}/crtube-edge264-plane-r5900-scalar.csv
RAW_OUT=${TMPDIR:-/tmp}/crtube-edge264-plane-reference.yuv

if [ -z "$MP4" ] || [ ! -f "$MP4" ]; then
    echo "usage: $0 path/to/240p-baseline.mp4" >&2
    exit 2
fi
command -v ffmpeg >/dev/null 2>&1 || {
    echo "ffmpeg is required for the independent YUV reference" >&2
    exit 2
}
cd "$ROOT"
COMMON="-std=gnu11 -O2 -flax-vector-conversions -Wno-override-init -Wno-unused-function -Wno-return-type -pthread -Iinclude -Ithird_party/edge264"
# shellcheck disable=SC2086
${HOST_CC:-cc} -std=gnu11 -O2 -flax-vector-conversions \
  -Wno-override-init -Wno-unused-function -Wno-return-type \
  -pthread -Iinclude -Ithird_party/edge264 \
  src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
  tests/test_h264_edge264_plane_crc.c -o "$OUT"
"$OUT" "$MP4" > "$EDGE_OUT"
# Build the exact SIMD=R5900 lane implementation without CRTUBE_R5900_MMI.
# On the host this exercises its scalar-lane fallback and excludes EE asm.
# shellcheck disable=SC2086
${HOST_CC:-cc} $COMMON -DSIMD=6 -DEDGE264_PS2 \
  src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
  tests/test_h264_edge264_plane_crc.c -o "$R5900_OUT"
"$R5900_OUT" "$MP4" > "$R5900_CSV"
ffmpeg -v error -i "$MP4" -map 0:v:0 -vsync 0 -pix_fmt yuv420p \
  -f rawvideo "$RAW_OUT" -y
python3 - "$EDGE_OUT" "$R5900_CSV" "$RAW_OUT" <<'PY'
import sys, zlib
edge_path, r5900_path, raw_path = sys.argv[1:]
w, h = 320, 240
cw, ch = w // 2, h // 2
frame_size = w*h + 2*cw*ch
edge = []
with open(edge_path, 'r', encoding='ascii') as f:
    for line in f:
        n, y, cb, cr = line.strip().split(',')
        edge.append((int(n), int(y,16), int(cb,16), int(cr,16)))
r5900 = []
with open(r5900_path, 'r', encoding='ascii') as f:
    for line in f:
        n, y, cb, cr = line.strip().split(',')
        r5900.append((int(n), int(y,16), int(cb,16), int(cr,16)))
if len(r5900) != len(edge):
    raise SystemExit(f'R5900 scalar frame count mismatch: Edge264={len(edge)} R5900={len(r5900)}')
raw = open(raw_path, 'rb').read()
if len(raw) % frame_size:
    raise SystemExit('FFmpeg raw output has a partial frame')
count = len(raw) // frame_size
if count != len(edge):
    raise SystemExit(f'frame count mismatch: Edge264={len(edge)} FFmpeg={count}')
r5900_mismatches = 0
for i, item in enumerate(edge):
    off = i * frame_size
    planes = (raw[off:off+w*h],
              raw[off+w*h:off+w*h+cw*ch],
              raw[off+w*h+cw*ch:off+frame_size])
    ref = tuple(zlib.crc32(p) & 0xffffffff for p in planes)
    got = item[1:]
    rgot = r5900[i][1:]
    if got != ref:
        print(f'FRAME {i+1}: generic Edge264 mismatch Edge264=' +
              '/'.join(f'{x:08x}' for x in got) + ' FFmpeg=' +
              '/'.join(f'{x:08x}' for x in ref))
        raise SystemExit(1)
    if rgot != ref:
        r5900_mismatches += 1
        if r5900_mismatches <= 5:
            print(f'FRAME {i+1}: R5900-lane mismatch R5900=' +
                  '/'.join(f'{x:08x}' for x in rgot) + ' FFmpeg=' +
                  '/'.join(f'{x:08x}' for x in ref))
print(f'Generic Edge264 vs FFmpeg: PASS ({count} frames)')
if r5900_mismatches:
    print(f'R5900 SIMD=6 scalar-lane diagnostic: DIVERGENCE ({r5900_mismatches}/{count} frames differ)')
else:
    print(f'R5900 SIMD=6 scalar-lane diagnostic: PASS ({count} frames)')
print('Reference fingerprints for PS2 dbg_crtube.txt:')
for n, y, cb, cr in edge:
    if n <= 3 or n % 30 == 0 or n == count:
        print(f'H.264 PLANES frame={n} crc_y={y:08x} crc_cb={cb:08x} crc_cr={cr:08x}')
PY

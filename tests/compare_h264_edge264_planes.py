#!/usr/bin/env python3
import os
import sys

if len(sys.argv) != 6:
    raise SystemExit("usage: compare_h264_edge264_planes.py width height reference.yuv generic.yuv r5900.yuv")
w, h = map(int, sys.argv[1:3])
if w <= 0 or h <= 0 or (w & 1) or (h & 1):
    raise SystemExit("only positive even-sized YUV420 frames are supported")
paths = sys.argv[3:]
frame_bytes = w * h * 3 // 2
cw, ch = w // 2, h // 2
plane_sizes = (w * h, cw * ch, cw * ch)
plane_names = ("Y", "Cb", "Cr")
files = [open(p, "rb") for p in paths]
try:
    sizes = [os.fstat(f.fileno()).st_size for f in files]
    if any(s % frame_bytes for s in sizes):
        raise SystemExit("one or more raw outputs contain a partial frame")
    counts = [s // frame_bytes for s in sizes]
    if len(set(counts)) != 1:
        raise SystemExit(f"frame count mismatch: FFmpeg={counts[0]} Edge264={counts[1]} R5900={counts[2]}")
    total_frames = counts[0]
    comparisons = ((1, "Generic Edge264", 0), (2, "R5900 SIMD=6 host lane", 0))
    mismatch_frames = [0, 0]
    mismatch_pixels = [[0, 0, 0], [0, 0, 0]]
    max_delta = [[0, 0, 0], [0, 0, 0]]
    first = [None, None]
    for frame in range(total_frames):
        chunks = [f.read(frame_bytes) for f in files]
        if any(len(c) != frame_bytes for c in chunks):
            raise SystemExit(f"unexpected EOF at frame {frame}")
        offsets = (0, plane_sizes[0], plane_sizes[0] + plane_sizes[1])
        for cmp_index, (file_index, label, _) in enumerate(comparisons):
            frame_bad = False
            for pi, (name, size, offset) in enumerate(zip(plane_names, plane_sizes, offsets)):
                ref = chunks[0][offset:offset + size]
                got = chunks[file_index][offset:offset + size]
                if ref == got:
                    continue
                frame_bad = True
                for pos, (a, b) in enumerate(zip(ref, got)):
                    if a != b:
                        mismatch_pixels[cmp_index][pi] += 1
                        d = abs(a - b)
                        if d > max_delta[cmp_index][pi]:
                            max_delta[cmp_index][pi] = d
                        if first[cmp_index] is None:
                            if pi == 0:
                                x, y = pos % w, pos // w
                            else:
                                pw = cw
                                x, y = pos % pw, pos // pw
                            first[cmp_index] = (frame + 1, name, x, y, a, b)
            if frame_bad:
                mismatch_frames[cmp_index] += 1
    for i, (_, label, _) in enumerate(comparisons):
        print(f"{label}: {'PASS' if mismatch_frames[i] == 0 else 'DIVERGENCE'} "
              f"frames={total_frames} mismatching_frames={mismatch_frames[i]}")
        for pi, name in enumerate(plane_names):
            print(f"  {name}: differing_pixels={mismatch_pixels[i][pi]} max_abs_delta={max_delta[i][pi]}")
        if first[i]:
            fr, plane, x, y, refv, gotv = first[i]
            print(f"  first_difference: frame={fr} plane={plane} x={x} y={y} "
                  f"ffmpeg={refv} edge264={gotv}")
    if mismatch_frames[0] or mismatch_frames[1]:
        raise SystemExit(1)
    print(f"PASS: all three YUV420 planes match FFmpeg exactly for {total_frames} frames")
finally:
    for f in files:
        f.close()

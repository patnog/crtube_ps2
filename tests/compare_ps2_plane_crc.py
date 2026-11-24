#!/usr/bin/env python3
"""Compare per-frame Y/Cb/Cr CRCs from the PS2 log against FFmpeg output."""
import argparse
import re
import subprocess
import sys
import zlib

LOG_RE = re.compile(
    r"H\.264 PLANES frame=(\d+).*?crc_y=([0-9a-fA-F]{8}) "
    r"crc_cb=([0-9a-fA-F]{8}) crc_cr=([0-9a-fA-F]{8})"
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video", help="original MP4 used on the PS2")
    ap.add_argument("ps2_log", help="dbg_crtube.txt copied from the PS2")
    ap.add_argument("--write-reference", help="write FFmpeg CRC CSV to this path")
    args = ap.parse_args()

    observed = {}
    with open(args.ps2_log, "r", encoding="utf-8", errors="replace") as src:
        for line in src:
            match = LOG_RE.search(line)
            if match:
                index = int(match.group(1))
                observed[index] = tuple(int(match.group(i), 16) for i in (2, 3, 4))

    if not observed:
        print("ERROR: no per-frame H.264 PLANES CRC records found in PS2 log")
        return 1

    # Decode only the prefix actually captured by the PS2. The default PS2
    # sample limit is 120 frames, approximately five seconds for 23.976 fps.
    frame_count = max(observed) + 1
    width, height = 320, 240
    cw, ch = width // 2, height // 2
    frame_size = width * height + 2 * cw * ch
    cmd = ["ffmpeg", "-v", "error", "-i", args.video, "-map", "0:v:0",
           "-vsync", "0", "-frames:v", str(frame_count), "-pix_fmt", "yuv420p",
           "-f", "rawvideo", "-"]
    try:
        raw = subprocess.run(cmd, check=True, stdout=subprocess.PIPE).stdout
    except FileNotFoundError:
        print("ERROR: ffmpeg not found", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as exc:
        print(f"ERROR: ffmpeg failed: {exc}", file=sys.stderr)
        return 2
    if len(raw) % frame_size:
        print("ERROR: FFmpeg output contains a partial YUV420 frame", file=sys.stderr)
        return 2

    reference = []
    for index in range(len(raw) // frame_size):
        off = index * frame_size
        y = raw[off:off + width * height]
        cb = raw[off + width * height:off + width * height + cw * ch]
        cr = raw[off + width * height + cw * ch:off + frame_size]
        reference.append(tuple(zlib.crc32(p) & 0xffffffff for p in (y, cb, cr)))

    if args.write_reference:
        with open(args.write_reference, "w", encoding="ascii") as out:
            out.write("frame,y,cb,cr\n")
            for i, (y, cb, cr) in enumerate(reference):
                out.write(f"{i},{y:08x},{cb:08x},{cr:08x}\n")

    mismatches = [i for i, got in sorted(observed.items())
                  if i >= len(reference) or got != reference[i]]
    missing = [i for i in range(min(len(reference), max(observed) + 1))
               if i not in observed]
    print(f"FFmpeg reference frames: {len(reference)} (limited to captured PS2 prefix)")
    print(f"PS2 CRC records: {len(observed)} (frame indices {min(observed)}..{max(observed)})")
    print(f"Matching frames: {len(observed) - len(mismatches)}")
    print(f"Mismatching frames: {len(mismatches)}")
    print(f"Missing CRC records inside captured range: {len(missing)}")
    for i in mismatches[:20]:
        got = observed[i]
        ref = reference[i] if i < len(reference) else (0, 0, 0)
        print(f"FRAME {i}: PS2 Y/Cb/Cr={'/'.join(f'{v:08x}' for v in got)} "
              f"FFmpeg={'/'.join(f'{v:08x}' for v in ref)}")
    if args.write_reference:
        print(f"Reference CSV: {args.write_reference}")
    if mismatches or missing or len(observed) != len(reference):
        return 1
    print("PASS: every PS2 Y/Cb/Cr frame CRC matches FFmpeg")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

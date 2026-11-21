# PS3-to-PS2 porting notes

Keep application policy and platform services separate.

## Shared-code candidates

- YouTube request construction and response interpretation.
- JSON traversal, video identifiers and metadata models.
- Search pagination and result ordering.
- Thumbnail URL selection and visible-item prioritization.
- Playback state-machine concepts that do not depend on PS3 APIs.

Each candidate must be checked for PS3-only types, threading assumptions,
allocator behavior, socket APIs and buffer sizes before it is moved.

## PS2-specific implementations

- EE/IOP startup and IRX module management.
- Ethernet, TCP/IP, DNS, HTTP and TLS integration.
- DualShock 2 input and GS rendering.
- JPEG decode and texture upload.
- Audio output through the PS2 audio subsystem.
- Container demuxing, video decode, frame scheduling and A/V synchronization.

## Decoder investigation

Do not port SPE intrinsics or PS3 VDEC calls to this target. First establish a
standalone software-decoder benchmark on the Emotion Engine. Compare a small
H.264 Baseline decoder against MPEG-2/IPU-assisted playback, and measure frame
time, memory use, dropped frames and audio/video synchronization on real
hardware. Do not claim a resolution/FPS target until measured on the console.

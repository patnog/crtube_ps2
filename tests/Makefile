HOSTCC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99

.PHONY: all test clean mp4-extract

all: h264_frame_test mp4_demux_test

test: h264_frame_test mp4_demux_test
	./h264_frame_test h264-test.ppm

h264_frame_test: h264_frame_test.c ../src/h264_frame.c ../include/h264_decoder.h
	$(HOSTCC) $(CFLAGS) -I../include -o $@ h264_frame_test.c ../src/h264_frame.c

mp4_demux_test: mp4_demux_test.c ../src/h264_mp4.c ../src/h264_decoder.c ../include/h264_mp4.h ../include/h264_decoder.h
	$(HOSTCC) $(CFLAGS) -I../include -o $@ mp4_demux_test.c ../src/h264_mp4.c ../src/h264_decoder.c

mp4-extract: mp4_demux_test
	@test -n "$(MP4)" || { echo "Use: make mp4-extract MP4=/path/to/video.mp4"; exit 1; }
	./mp4_demux_test "$(MP4)" firstsample.h264

clean:
	rm -f h264_frame_test mp4_demux_test h264-test.ppm firstsample.h264

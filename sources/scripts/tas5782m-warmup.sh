#!/bin/sh
# tas5782m-warmup: play 1 second of silence through PipeWire to pre-initialize
# the TAS5782M chip at boot. The chip needs ~700ms of preboot+firmware load
# after the first trigger(START); this service absorbs that cost at boot so
# all subsequent user-facing sounds play without delay.
#
# Run as the audio user (UID 1000) so PipeWire's session socket is accessible.

# Generate 1 second of silence: 48000 samples, S16LE stereo = 192000 bytes
dd if=/dev/zero bs=192000 count=1 2>/dev/null | \
    pw-cat --playback --format=s16 --rate=48000 --channels=2 - 2>/dev/null

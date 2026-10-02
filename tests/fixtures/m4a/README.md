# M4A import fixtures

Synthetic 440 Hz tone, 0.25 seconds, 48 kHz stereo. No third-party recording.
`piano-aac.m4a` uses AAC; `piano-alac.m4a` uses Apple Lossless. Generated with:

```sh
ffmpeg -f lavfi -i sine=frequency=440:sample_rate=48000:duration=0.25 \
  -ac 2 -c:a aac -map_metadata -1 piano-aac.m4a
ffmpeg -f lavfi -i sine=frequency=440:sample_rate=48000:duration=0.25 \
  -ac 2 -c:a alac -map_metadata -1 piano-alac.m4a
```

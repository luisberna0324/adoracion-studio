# PlayWorship Audio Engine v2

This branch incrementally replaces the current callback-centered engine.

## Realtime contract

The audio callback must not:

- perform disk or network I/O;
- allocate or resize containers;
- normalize strings or build UI state;
- wait for a mutex or spin lock held by another thread;
- reset or prepare expensive pitch/tempo processors;
- scan the complete arrangement to discover the next event.

The control side owns session mutation, source preparation and compilation of
the arrangement. The audio side consumes immutable or lock-free state.

## Timeline

`TransportClock` is the canonical playback clock. Its unit is an integer audio
frame. Seconds are a presentation and persistence format only.

`SectionScheduler` receives a precompiled, ordered list of frame ranges. It
resolves the active section and boundary transitions without strings, JUCE
objects or allocations.

## Migration order

Completed foundations:

1. `TransportClock` is authoritative for playback position.
2. `SectionScheduler` publishes triple-buffered frame snapshots.
3. MIDI bindings use triple-buffered realtime snapshots.
4. Per-stem reader threads were replaced by a bounded 2–4 worker pool.
5. Streaming reads never wait: contention produces a measurable silent miss.
6. Scratch audio buffers are preallocated up to 8192 frames.
7. The SoundTouch fallback output FIFO is a fixed-capacity ring buffer;
   callback-time `vector::insert`, `vector::erase` and resizing were removed.
8. Simple and grouped stem rendering now share `StemMixEngine`; the legacy
   `MixerAudioSource` path was removed.
9. Click phase is re-anchored from the absolute frame clock every block.
10. Dynamic-guide events are scheduled in frames and use pre-resolved integer
    trigger IDs instead of callback-time string normalization and map lookup.
11. Bungee Basic 2.4.24 is the required pitch/tempo backend. Configure now
    fails if its SDK or platform runtime is missing.
12. Tempo consumes a dithered variable number of source frames per output
    block, avoiding long-term fractional drift.
13. Melodic stems receive pitch plus tempo. Percussive, click and guide stems
    receive tempo with pitch fixed at unity.
14. Source-frame transport drives sections, loops and seeks. The metronome
    converts source time to playback time before phase synchronization.
15. Callback scratch buffers are fixed-capacity; per-block `setSize()` calls
    were removed from `processBlock` and `StemMixEngine`.
16. Bungee stream construction and preparation occur in `prepareToPlay`, not
    in the audio callback.

Remaining architectural boundary:

- Bungee synthesis is currently synchronous in the audio callback. The next
  boundary is a prepared producer/consumer voice layer so synthesis and stream
  replacement after discontinuities happen off-thread.
- Bungee's streaming wrapper has no allocation-free reset operation. Current
  realtime reset requests only clear PlayWorship mix/delay state; seek/loop
  transitions still need pre-armed replacement voices to guarantee that no
  old Bungee history reaches the new position.
- Seek and loop targets invalidate the current streaming cache and wake the
  reader pool, but do not yet maintain a second prefilled cache for guaranteed
  gapless jumps.
- Structural stem swaps suspend new callbacks, wait for the active callback to
  leave, then publish the new deck. The callback no longer acquires
  `stemListLock`.

## Bungee bootstrap

Run `scripts/bootstrap_bungee.sh` before configuring. The bootstrap downloads
the pinned SDK and MPL-2.0 license. Override the SDK location with
`-DMTCONTROLS_BUNGEE_DIR=/path/to/bungee`.

Release bundles include and verify:

- `libbungee.dylib` on macOS, extracted from the universal Bungee framework;
- `bungee.dll` on Windows x64;
- `libbungee.so` on Linux;
- `ThirdPartyLicenses/Bungee-MPL-2.0.txt`.

Windows x86 is not distributed because Bungee 2.4.24 has no Win32 runtime.

## Diagnostics

`audio_*.csv` records callback duration, budget, structural lock misses and the
cumulative streaming read-miss count. A streaming miss means the callback
chose silence instead of waiting for a worker or disk read.

The June 20, 2026 real-stem run (15 loaded stems, pitch active, click active,
including two forward seeks) measured:

- callback average: 0.347 ms;
- callback p95: 0.537 ms;
- callback maximum: 9.909 ms against an 11.610 ms budget;
- overruns: 0;
- streaming misses: 6.

The macOS process-level sampler averaged 88.47% CPU versus the previous 79.42%
reference. This is a resource regression even though the realtime callback met
its deadline, so moving Bungee synthesis to worker voices remains mandatory
before making this backend the final production path.

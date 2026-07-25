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
11. Bungee Basic 2.4.24 is the required pitch/tempo backend on macOS, iOS,
    Linux and Windows x64. Windows x86 intentionally retains SoundTouch
    because the Bungee SDK has no Win32 runtime.
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
17. Every Bungee processor owns two stable voice slots. A discontinuity swaps
    to a clean preconstructed slot with atomics only; one shared maintenance
    worker pre-warms and rebuilds retired slots outside the callback. A missing
    spare produces bounded silence, not old pre-seek history, until replacement.
18. Pitch-only grouped rendering delays percussion and guide by the melodic
    output latency. Bungee input-frame latency is converted to output samples
    with `inputLatency / tempoRatio`.
19. Empty and partial streaming ranges advance the logical cursor exactly one
    callback and report both missed callbacks and exact silent frames.
20. Routing processors are prepared for loaded stems instead of all 64 slots,
    and their scratch clear is limited to the active frame range.
21. Streaming range publication uses a non-spinning seqlock plus seek epochs;
    an incoherent or invalidated window becomes silence instead of stale ring
    data.
22. MIDI learn, transport, tempo and pad lifecycle publish fixed-size commands.
    Ordered control actions use a bounded SPSC queue; pad note edges use atomic
    bitmasks and are applied under the render path's existing try-lock. A
    processor-owned message-thread timer consumes commands even when no editor
    is open.
23. Time-processor reset recovery is preflighted as a mix-wide transaction. If
    one required clean voice is unavailable, song stems, click and guide advance
    but the whole song bus is discarded; delay history is invalidated in O(1).
    This trades a coherent gap for partial, rhythmically misaligned playback.

Remaining architectural boundary:

- Bungee synthesis is currently synchronous in the audio callback. The next
  boundary is a bounded producer/consumer layer that can move synthesis, not
  only retired-stream construction, off-thread.
- Replacement DSP voices are clean and pre-warmed with silence, but are not
  pre-fed with target audio.
  Guaranteed gapless seeks and loops still require a second prefilled source
  cache and a sample-accurate/crossfaded handoff.
- Seek and loop targets invalidate the current streaming cache and wake the
  reader pool, but do not yet maintain a second prefilled cache for guaranteed
  gapless jumps.
- Structural stem swaps suspend new callbacks, wait for the active callback to
  leave, then publish the new deck. The callback no longer acquires
  `stemListLock`.

## Bungee bootstrap

Run `scripts/bootstrap_bungee.sh` before configuring macOS, iOS, Linux or
Windows x64. The bootstrap downloads the pinned SDK and MPL-2.0 license.
Override the SDK location with `-DMTCONTROLS_BUNGEE_DIR=/path/to/bungee`.
Windows x86 neither downloads nor links Bungee.

Release bundles include and verify:

- `libbungee.dylib` on macOS, extracted from the universal Bungee framework;
- `bungee.dll` on Windows x64;
- `libbungee.so` on Linux;
- `ThirdPartyLicenses/Bungee-MPL-2.0.txt`.

Windows x86 is distributed separately with the legacy SoundTouch backend.
Its installer must not contain `bungee.dll`; Windows x64 remains the primary
and recommended build.

## Diagnostics

`audio_*.csv` records complete callback duration, budget, cumulative streaming
read misses, exact frames substituted by silence, Bungee reset misses and
background rebuild failures. A
streaming miss means the callback chose silence instead of waiting for a worker
or disk read. A reset miss means another discontinuity arrived before the
maintenance worker rebuilt a spare voice.
Persistent rebuild failures use exponential backoff rather than allocating at
the service polling frequency.

Wall timestamps are derived from one control-thread wall/monotonic calibration,
so block logging does not query the system wall clock. The summary script sorts
blocks in O(n log n) and distinguishes per-log deltas from last cumulative
counters.

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

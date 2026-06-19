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

1. Introduce the frame clock and section scheduler behind the existing API.
2. Publish immutable render-session snapshots.
3. Replace per-stem reader threads with a bounded shared worker pool.
4. Replace callback locks with single-producer/single-consumer queues.
5. Unify simple, separated and explicitly-routed rendering into one pipeline.
6. Prepare pitch/tempo discontinuities and section jumps off the audio thread.

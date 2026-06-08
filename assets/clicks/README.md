# Click pack

This folder stores the metronome click pack bundled with the app.

Canonical file names:

```text
click-<style>-accents.wav
click-<style>-quarter.wav
click-<style>-eighth.wav
click-<style>-sixteenth.wav
```

Accepted styles:

```text
classic
woodblock
cowbell
digital
gentle
percussive
saw
blip
```

The loader prefers canonical names first and falls back to the older
`New Click - <Style>-<Role>.wav` naming when needed.

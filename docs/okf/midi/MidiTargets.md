---
type: MIDI Reference
title: MidiTargets
description: Lista completa de targets MIDI configurables
resource: src/PluginProcessor.h
tags: [juce, midi, targets, binding]
timestamp: 2026-07-14T20:45:00Z
---

# MIDI Targets

Lista de todos los targets MIDI que pueden ser mapeados a mensajes MIDI.

## Master Controls

| ID | Constante | Descripción | Rango |
|----|-----------|-------------|-------|
| 0 | `midiTargetMasterVolume` | Volumen master | 0-127 |
| 1 | `midiTargetMasterMute` | Mute master | on/off |
| 12 | `midiTargetMasterGain` | Gain master | 0-127 |

## Transport

| ID | Constante | Descripción | Tipo |
|----|-----------|-------------|------|
| 2 | `midiTargetTransportPlayPause` | Play/Pause | toggle |
| 3 | `midiTargetTransportStop` | Stop | on/off |
| 7 | `midiTargetTransportPrevSong` | Canción anterior | on |
| 8 | `midiTargetTransportSeekBack` | Retroceder | hold |
| 9 | `midiTargetTransportSeekForward` | Avanzar | hold |
| 10 | `midiTargetTransportNextSong` | Siguiente canción | on |
| 11 | `midiTargetTransportFadeToggle` | Toggle fade | toggle |

## Metrónomo

| ID | Constante | Descripción | Rango |
|----|-----------|-------------|-------|
| 4 | `midiTargetMetronomeEnabled` | Habilitar | on/off |
| 5 | `midiTargetMetronomeVolume` | Volumen | 0-127 |
| 6 | `midiTargetMetronomeMute` | Mute | on/off |
| 31 | `midiTargetMetronomeGain` | Gain | 0-127 |
| 32 | `midiTargetMetronomeSolo` | Solo | on/off |

### Click Division

| ID | Constante | Descripción |
|----|-----------|-------------|
| 37 | `midiTargetClickDynDivisionQuarter` | Toggle 1/4 |
| 38 | `midiTargetClickDynDivisionEighth` | Toggle 1/8 |
| 39 | `midiTargetClickDynDivisionSixteenth` | Toggle 1/16 |

## Tempo y Pitch

| ID | Constante | Descripción | Rango |
|----|-----------|-------------|-------|
| 27 | `midiTargetTempoDown` | Bajar tempo | hold |
| 28 | `midiTargetTempoUp` | Subir tempo | hold |
| 29 | `midiTargetPitchDown` | Bajar pitch | hold |
| 30 | `midiTargetPitchUp` | Subir pitch | hold |

## Song Position

| ID | Constante | Descripción | Tipo |
|----|-----------|-------------|------|
| 18 | `midiTargetSongStartMinus` | Reducir inicio | on |
| 19 | `midiTargetSongStartPlus` | Aumentar inicio | on |
| 23 | `midiTargetSongEndMinus` | Reducir fin | on |
| 24 | `midiTargetSongEndPlus` | Aumentar fin | on |
| 25 | `midiTargetSongFadeMinus` | Reducir fade | on |
| 26 | `midiTargetSongFadePlus` | Aumentar fade | on |

## Pads

| ID | Constante | Descripción |
|----|-----------|-------------|
| 20 | `midiTargetPadsPower` | Toggle pads on/off |
| 21 | `midiTargetPadsVolume` | Volumen de pads |
| 22 | `midiTargetPadsGain` | Gain de pads |

**Notas de pads:** `midiTargetPadsNoteBase + noteIndex` (base 200)
- 200 = C
- 201 = C#
- ...
- 211 = B

## Mixer

| ID | Constante | Descripción | Tipo |
|----|-----------|-------------|------|
| 33 | `midiTargetMixerNudgeUp` | Subir fader | on |
| 34 | `midiTargetMixerNudgeDown` | Bajar fader | on |
| 35 | `midiTargetMixerApplyRouting` | Aplicar routing | on |
| 36 | `midiTargetWaveformEditToggle` | Toggle edit mode | toggle |

## UI Commands

| ID | Constante | Descripción |
|----|-----------|-------------|
| 13 | `midiTargetUiWindowModeToggle` | Fullscreen/ventana |
| 14 | `midiTargetUiSettingsToggle` | Panel settings |
| 15 | `midiTargetUiSongWindowPopover` | Popover canción |
| 16 | `midiTargetUiKeyPopover` | Popover tonalidad |
| 17 | `midiTargetUiMetronomePopover` | Popover metrónomo |

## Stem Controls

### Volumen de stems

Base: `midiTargetStemVolumeBase` (100)

```
100 + stemIndex = target de volumen
100 + 0 = stem 0 volumen
100 + 1 = stem 1 volumen
...
```

### Gain de stems

Base: `midiTargetStemGainBase` (300)

```
300 + stemIndex = target de gain
```

### Mute de stems

Base: `midiTargetStemMuteBase` (400)

```
400 + stemIndex = target de mute
```

### Solo de stems

Base: `midiTargetStemSoloBase` (500)

```
500 + stemIndex = target de solo
```

## Estructura MidiBinding

```cpp
struct MidiBinding {
    MidiBindingSourceType sourceType;  // CC, Note, PitchBend, etc.
    int channel;                        // 0-15
    int data1;                         // CC number o note number
    int targetId;                      // ID del target
    MidiBindingMode mode;              // Absolute, Relative, Toggle
    float min;                         // Valor mínimo
    float max;                         // Valor máximo
};

enum class MidiBindingSourceType {
    CC = 0,
    Note = 1,
    PitchBend = 2,
    Aftertouch = 3,
    // ...
};

enum class MidiBindingMode {
    Absolute = 0,
    Relative = 1,
    Toggle = 2
};
```

## Ejemplo de configuración

```cpp
// Bind CC1 (mod wheel) a volumen master
MidiBinding binding;
binding.sourceType = MidiBindingSourceType::CC;
binding.channel = 0;
binding.data1 = 1;
binding.targetId = midiTargetMasterVolume;
binding.mode = MidiBindingMode::Absolute;
binding.min = 0.0f;
binding.max = 1.0f;

processor.setMidiBinding(binding);

// Bind Note On a play/pause
MidiBinding playBinding;
playBinding.sourceType = MidiBindingSourceType::Note;
playBinding.channel = 0;
playBinding.data1 = 60;  // C3
playBinding.targetId = midiTargetTransportPlayPause;
playBinding.mode = MidiBindingMode::Toggle;

processor.setMidiBinding(playBinding);
```

## Rango de valores MIDI

| Tipo | Rango | Descripción |
|------|-------|-------------|
| Volume | 0-127 | Valor continuo |
| Toggle | on/off | 0-63=off, 64-127=on |
| Hold | 0-127 | Activo mientras >64 |
| Note | 0-127 | Número de nota MIDI |

## Archivos relacionados

- [MidiBindings](MidiBindings.md)
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)

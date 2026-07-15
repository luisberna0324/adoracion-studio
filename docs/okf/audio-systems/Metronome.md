---
type: Audio System Class
title: Metronome
description: Motor de metrónomo configurable con samples personalizados y subdivisions
resource: src/MetronomeEngine.h
tags: [juce, audio, metronome, click, rhythm]
timestamp: 2026-07-14T20:45:00Z
---

# Metronome

Motor de metrónomo que puede usar samples de audio personalizados o synthesis.

## Conceptos clave

### Subdivisiones

El motor soporta tres niveles de subdivisión:

| Valor | Nombre | Descripción |
|-------|--------|-------------|
| 1 | Quarter (1/4) | Negra |
| 2 | Eighth (1/8) | Corchea |
| 4 | Sixteenth (1/16) | Semicorchea |

### Division Mask

Máscara de bits para controlar qué subdivisiones suenan:

```cpp
divisionMask = 1;     // Solo negras
divisionMask = 1 | 2;  // Negras y corcheas
divisionMask = 1 | 2 | 4;  // Todas
```

### Click Tones

| Tone | Uso |
|------|-----|
| `downbeat` | Primera nota del compás |
| `eighth` | Nota de corchea |
| `sixteenth` | Nota de semicorchea |

## API Pública

### Preparación

```cpp
void prepare(double sampleRate);
void reset();
```

Configura el motor para el sample rate dado.

### Habilitación

```cpp
void setEnabled(bool enabled) noexcept;
bool isEnabled() const noexcept;
```

Activa/desactiva el metrónomo.

### Tempo

```cpp
void setBpm(double bpm) noexcept;
double getBpm() const noexcept;
```

Establece el tempo en BPM (20-300 típicamente).

### Compás

```cpp
void setBeatsPerBar(int beats) noexcept;
int getBeatsPerBar() const noexcept;
```

Define beats por compás (4 = 4/4, 3 = 3/4, etc).

### Volumen

```cpp
void setVolume(float volume) noexcept;  // 0.0 - 1.0
float getVolume() const noexcept;
```

Nivel de volumen.

### Mute

```cpp
void setMuted(bool muted) noexcept;
bool isMuted() const noexcept;
```

Silencia el metrónomo.

### Subdivisiones

```cpp
// Qué subdivisión se está usando (1, 2, o 4)
void setSubdivision(int subdiv) noexcept;
int getSubdivision() const noexcept;

// Máscara de bits de qué layers están activos
void setDivisionMask(int mask) noexcept;
int getDivisionMask() const noexcept;
```

### Sincronización

```cpp
void syncPhaseForTimeSeconds(double seconds) noexcept;
```

Sincroniza la fase del click con una posición en segundos.

### Trigger manual

```cpp
void triggerImmediateClick() noexcept;
```

Fuerza un click inmediato (para preview).

### Procesamiento

```cpp
void process(juce::AudioBuffer<float> &buffer) noexcept;
```

Procesa el buffer de audio, añadiendo clicks.

### Samples personalizados

```cpp
bool loadClickSamplesFromFolder(
    const juce::File &folder,
    const juce::String &style,
    juce::AudioFormatManager &formatManager,
    juce::String *errorMessage = nullptr
);

void clearClickSamples() noexcept;
bool hasClickSamples() const noexcept;
```

Carga samples de click desde carpeta. Busca archivos con sufijos:
- `*_downbeat.wav` - primera nota
- `*_quarter.wav` - negra
- `*_eighth.wav` - corchea
- `*_sixteenth.wav` - semicorchea

## Estructura interna

### ClickSample

```cpp
struct ClickSample {
    juce::AudioBuffer<float> data;
    double sourceSampleRate = 44100.0;
    float normalizationGain = 1.0f;
};
```

### ClickSampleSet

```cpp
struct ClickSampleSet {
    juce::String style;           // Nombre del estilo
    ClickSample downbeat;
    ClickSample quarter;
    ClickSample eighth;
    ClickSample sixteenth;
};
```

### SampleVoice

```cpp
struct SampleVoice {
    std::shared_ptr<const ClickSampleSet> sampleSet;
    const ClickSample *sample = nullptr;
    double position = 0.0;       // Posición en sample
    double increment = 1.0;       // Incremento por frame
    float gain = 1.0f;
};
```

Hay 16 voces de sample para permitir overlaps.

## Click synthesis

Si no hay samples cargados, el motor sintetiza clicks:

```cpp
float renderSyntheticClick() noexcept;
```

Genera clicks usando síntesis simple (sine + envelope).

## Constantes internas

| Variable | Tipo | Default |
|----------|------|---------|
| `clickGain` | `float` | 0.22f |
| `sampleRate` | `double` | 44100.0 |

## Ejemplo de uso

```cpp
MetronomeEngine metronome;
metronome.prepare(48000.0);

// Configurar tempo
metronome.setBpm(120.0);
metronome.setBeatsPerBar(4);

// Habilitar negras y semicorcheas
metronome.setDivisionMask(1 | 4);
metronome.setSubdivision(1);

// Cargar samples de click jazz
metronome.loadClickSamplesFromFolder(
    juce::File("/samples/clicks/jazz"),
    "jazz",
    formatManager
);

// Activar y procesar
metronome.setEnabled(true);

// En callback de audio
void audioCallback(AudioBuffer<float> &buffer) {
    // ... otros procesamiento ...
    metronome.process(buffer);
}
```

## Integración con MTControlsAudioProcessor

El processor tiene una API de más alto nivel:

```cpp
processor.setMetronomeEnabled(true);
processor.setMetronomeBpm(120.0);
processor.setMetronomeBeatsPerBar(4);
processor.setClickDivision(1 | 4);  // 1/4 + 1/16
```

## Archivos relacionados

- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)

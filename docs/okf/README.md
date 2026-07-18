# MTControls Plugin - Knowledge Base (OKF)

Base de conocimiento en formato **Open Knowledge Format (OKF)** para el plugin de Multitracks JUCE Controls.

## Qué es OKF

OKF es un formato abierto para representar conocimiento estructurado que puede ser consumido por:
- **Agentes de IA** para consulta de API
- **Desarrolladores** para documentación
- **Herramientas** para indexación y búsqueda

## Estructura

```
docs/okf/
├── index.md              # Raíz - navegación principal
├── log.md                # Registro de cambios
├── processor/            # Clase principal del plugin
├── engine/              # Motores de audio
├── audio-systems/       # Sistemas auxiliary
├── midi/                # Sistema MIDI
├── integrations/        # Integraciones externas
└── transport/           # Control de transporte
```

## Navegación rápida

| Módulo | Descripción |
|--------|-------------|
| [processor/MTControlsAudioProcessor](processor/MTControlsAudioProcessor.md) | Procesador principal |
| [engine/StemTrack](engine/StemTrack.md) | Track de audio |
| [audio-systems/Metronome](audio-systems/Metronome.md) | Metrónomo |
| [midi/MidiTargets](midi/MidiTargets.md) | Targets MIDI |
| [integrations/FreeShow](integrations/FreeShow.md) | Integración con FreeShow |
| [integrations/LyricsControllers](integrations/LyricsControllers.md) | Control de software de letras |

## Uso con Agentes de IA

Esta KB está diseñada para que un agente de IA pueda:

1. **Descubrir** la estructura mediante `index.md`
2. **Consultar** metadatos YAML (types, tags) sin leer todo el contenido
3. **Navegar** relaciones entre clases via enlaces Markdown
4. **Ejecutar** ejemplos de código verificados

### Ejemplo de query

```
User: "Cómo configurar el metrónomo para que solo suene en negras?"
Agent: [Busca en audio-systems/Metronome.md]
      [Encuentra setDivisionMask y subdivision]
      [Retorna ejemplo de código]
```

## Mantenimiento

Para actualizar esta KB:

1. Editar el archivo `.md` correspondiente
2. Actualizar el `timestamp` en Front Matter
3. Registrar el cambio en `log.md`

## Herramientas de consumo

- **Lectura humana**: Cualquier editor de Markdown o visor
- **Lectura de IA**: Cualquier LLM que soporte enlaces y YAML
- **Búsqueda**: `grep` o cualquier herramienta de búsqueda de texto

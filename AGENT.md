# AGENT.md - Reglas de desarrollo para PlayWorship (JUCE)

Este documento define reglas de producto, UI y audio para mantener consistencia en la app.
No es una transcripcion de cambios; es una guia de trabajo y decisiones.

## 1) Scope y estructura del proyecto

- El desarrollo activo de esta app ocurre en `juce-controls-plugin/`.
- La documentación `docs/okf/` es la fuente de datos y comportamiento técnico
  del proyecto. Consultar el módulo relevante antes de cambiar transporte,
  arrange, motor de audio, mixer, MIDI o sistemas de audio; mantenerla
  actualizada cuando esos contratos cambien.
- Codigo principal:
  - UI: `juce-controls-plugin/src/PluginEditor.cpp`, `PluginEditor.h`
  - Audio/engine: `juce-controls-plugin/src/PluginProcessor.cpp`, `PluginProcessor.h`
- El target usado para validar UI/flujo es `MTControls_Standalone`.

## 2) Principios de producto

- La app se comporta como reproductor de stems + mixer por canal.
- La UI debe ser compacta, legible y util en vivo.
- Los channel strips son componentes repetibles: se agregan segun cantidad de stems.
- Evitar comportamientos ambiguos en transporte (`play/pause/stop` deben ser predecibles).

## 3) Sistema visual (source of truth)

Usar el sistema de color definido en `UITheme`:

- Fondo app:
  - `appBg` `#322E2B`
  - `appBgTop` `#383431`
  - `appBgBottom` `#46413D`
  - `panelDark` `#262220`
- Cards/canales:
  - `cardBg` `#D4D4D4` (stems regulares)
  - `cardBgMaster` `#E8D8C8` (crema calido, fondo card Master)
  - `cardBgClickGuide` `#C8CDD6` (gris azulado, fondo card Click/Guide)
  - `cardBorder` gris medio
  - `textPrimary` gris oscuro
  - `textSecondary` gris medio
- Acentos:
  - `accent` terracota `#C17A5A`
  - `accentSoft` terracota claro
  - `accentMuted` rojo apagado para estado `MUTE`

Regla:

- Cualquier control nuevo debe heredar esta paleta y lenguaje visual del channel strip.
- No introducir colores saturados o estilos que rompan esta linea.

## 4) Layout general de la app

Layout actual (sin sidebar):

- Top bar fija (`~60px`)
- Waveform bar fija (`~100px`)
- Area principal:
  - Setlist a la izquierda (colapsable)
  - Handle vertical entre setlist y canales
  - Grid de channels a la derecha

Reglas:

- Mantener comportamiento responsive actual:
  - `setlistWidth` aprox 35% con clamp 280..460
  - Handle dedicado para colapsar/expandir setlist
- Mantener scroll funcional en setlist y channels.

## 5) Reglas del Channel Strip

Estado y forma esperada:

- Dimensiones base del card:
  - `width = 90`
  - `height = 268`
- Meter vertical centrado visualmente.
- Un solo knob de volumen (`Vol`).
- Botones `S` y `M` lado a lado, parte baja.
- Franja inferior en `accent` con nombre del canal en blanco.
- Titulos de canal van en la franja naranja (no fuera del card).

No reintroducir:

- Seccion `SENDS` superior ni sliders superiores.
- Knob de pan.
- Doble medidor L/R en channel strip.

## 6) Regla de medidor (VU/level)

- El meter de cada canal debe mostrar senal real del stem.
- La lectura del meter debe depender de la ganancia del canal (`Vol`) y de estados `M/S`.
- El meter no debe depender de la normalizacion global del mix para su visualizacion (para no apagarlo artificialmente cuando hay muchos stems).

## 7) Regla de controles por stem (S/M/Vol)

Cada strip controla un stem real por indice:

- `Vol` -> `setStemVolume(index, value)`
- `M` -> `setStemMute(index, bool)`
- `S` -> `setStemSolo(index, bool)`

Comportamiento de audio:

- `Mute`: el stem no suena.
- `Solo`: si existe al menos un solo activo, solo suenan stems solo y no muteados.
- Al cargar una cancion, el mapeo UI->stem debe construirse con los stems realmente cargados.

## 8) Transporte (Play/Pause/Stop/Seek)

Contrato funcional:

- `Play`: reproduce desde posicion actual.
- `Pause`: solo pausa (conserva posicion). No resetea.
- `Stop`: va a inicio (`0.0s`) y queda detenido.
- `Seek`: mueve posicion relativa sin romper estado de transporte.

Implementacion actual clave:

- Estado de transporte explicito: `stopped`, `playing`, `paused`.
- `processBlock` mezcla stems solo cuando estado == `playing`.
- En `paused/stopped`, salida de stems en silencio y posicion preservada (en `stop`, posicion 0).

Reglas de calidad:

- No bloquear el hilo UI con trabajo de transporte.
- Evitar logica duplicada de estado (una sola fuente de verdad para play/pause state).

## 9) Waveform principal

- La waveform se construye con mezcla offline de stems incluidos.
- Debe mostrar playhead/progreso en tiempo real usando posicion y duracion de cancion.
- Si no hay cancion cargada, mostrar estado vacio con mensaje de accion.

## 10) Setlist y flujo Add Song

Setlist:

- Lista compacta de canciones (estilo playlist), filas densas.
- Doble click en fila carga cancion completa (stems + waveform + channels).

Wizard `Add Song`:

- Flujo de 3 pasos:
  1. Cargar stems + datos basicos.
  2. Mapear/categorizar instrumentos.
  3. Confirmar stems a cargar.

## 11) Persistencia (contrato de datos)

Persistencia local:

- Root: `~/Library/Application Support/MTControls/`
- DB: `songs.json`
- Audio: `audio/<songId>/...`

Reglas:

- Copiar stems al storage administrado de la app.
- Guardar `storedPath` para reproduccion estable.
- Mantener compatibilidad del JSON (`version`, `songs`, `stems`).
- No serializar `DynamicObject` en stack envuelto en `var` (usar objeto en heap).

## 12) Reglas de performance y threading

- Evitar locks de larga duracion en callbacks de UI.
- Operaciones de disco solo en flujo de import/carga, no en `paint` ni en timer callbacks.
- Evitar asignaciones innecesarias por frame en componentes de alto refresh.
- Al tocar transporte, preferir cambios de estado simples y deterministicos.

## 13) Checklist obligatorio despues de cada cambio funcional

1. Compilar `MTControls_Standalone`.
2. Ejecutar app.
3. Validar manualmente:
   - Doble click song carga channels y waveform.
   - `Play/Pause/Stop` funciona sin congelamiento.
   - Barra espaciadora toggle play/pause.
   - `Esc` hace stop.
   - `Vol` altera nivel audible por canal.
   - `S` y `M` afectan solo su stem.
   - Meter se mueve por canal con audio real.
   - Playhead del waveform avanza con reproduccion.

## 14) Comandos de trabajo rapido

Build:

```bash
cmake --build /Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build --target MTControls_Standalone -j
```

Run:

```bash
open "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/MTControls_artefacts/Standalone/MT Controls.app"
```

## 15) Guardrails para cambios futuros

- No romper la estetica base del channel strip; extenderla al resto de controles.
- No agregar elementos visuales que ya fueron removidos por decision de producto.
- No sacrificar respuesta del transporte por animaciones o logica extra.
- Si un ajuste toca audio + UI, validar ambos lados en el mismo ciclo de prueba.

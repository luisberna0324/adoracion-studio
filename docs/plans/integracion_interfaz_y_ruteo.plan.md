---
name: Integracion interfaz y ruteo
overview: Disenar e implementar en Standalone un sistema modular de asignacion de salida por stem a canales fisicos individuales (Canal 1, Canal 2, etc.), manteniendo `PluginProcessor` liviano y con persistencia de ruteo.
todos:
  - id: map-current-audio-path
    content: Documentar flujo actual de mezcla y punto exacto para insertar etapa de routing final por stem.
    status: pending
  - id: design-routing-state
    content: Definir modelo de datos stem->output pair, defaults y serializacion en estado.
    status: pending
  - id: define-new-classes
    content: Crear clases dedicadas de enrutamiento/dispositivo para evitar sobrecargar clases existentes.
    status: pending
  - id: implement-standalone-device-adapter
    content: Agregar capa Standalone para enumerar salidas fisicas y notificar cambios de dispositivo.
    status: pending
  - id: implement-routing-engine
    content: Implementar etapa de ruteo por stem hacia pares fisicos y fallback a 1/2.
    status: pending
  - id: add-ui-assignment
    content: Agregar controles UI de asignacion por stem y validacion de opciones disponibles.
    status: pending
  - id: test-and-hardening
    content: Ejecutar pruebas funcionales de multisalida, cambios de interfaz y persistencia.
    status: pending
isProject: false
---

# Plan de integracion de interfaz y asignacion por canal

## Objetivo

Habilitar en modo Standalone la asignacion de cada stem a una salida fisica individual de la interfaz (por ejemplo guitarra -> Out 1, guia -> Out 2), manteniendo compatibilidad con el flujo actual y sin romper la reproduccion existente. Se prioriza una arquitectura por clases nuevas para no recargar `PluginProcessor`, `StemMixEngine` ni paneles actuales.

## Convenciones de termino (JUCE)

- **Canal fisico de interfaz**: una salida individual del dispositivo (`Output 1`, `Output 2`, etc.).
- **Destino por stem (fase 1)**: un canal fisico por stem (`Out 1`, `Out 2`, ...).
- **Bus del plugin**: agrupacion logica de canales dentro de `AudioProcessor`; no equivale directamente a un conector fisico.
- En UI se mostrara: `Stem X -> Canal 1`, `Stem Y -> Canal 2`, etc., con lenguaje de canal fisico individual.

## Estado actual relevante

- El procesador declara bus principal estereo y valida mono/estereo en `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/PluginProcessor.cpp](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/PluginProcessor.cpp)`.
- La mezcla se concentra en `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/StemMixEngine.cpp](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/StemMixEngine.cpp)` y la reproduccion por stem en `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/StemTrack.cpp](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/StemTrack.cpp)`.
- No existe capa propia de `AudioDeviceManager` ni mapeo de canales fisicos en el codigo del repo.

## Enfoque tecnico propuesto (fase 1)

1. **Separar mezcla logica de salida fisica**
  - Mantener el render por stem en buffers intermedios.
  - Introducir una etapa de routing final que copie/sume cada stem al canal fisico asignado.
2. **Crear clases nuevas (arquitectura modular)**
  - `OutputChannelCatalog`: enumera y formatea salidas fisicas disponibles como `Canal 1`, `Canal 2`, etc.
  - `StemOutputRoutingState`: mantiene `stemIndex -> outputChannelIndex`, aplica defaults y normaliza ante cambios de hardware.
  - `StemOutputRouter`: recibe buffers por stem y los enruta al `AudioBuffer` final segun el mapa.
  - `StandaloneAudioDeviceBridge` (si es necesario): adapta la informacion del dispositivo Standalone hacia las clases anteriores.
3. **Modelo de routing persistente**
  - Crear estructura de estado: `stemIndex -> outputChannelIndex` (por ejemplo 0=Out 1, 1=Out 2, etc.).
  - Guardar/cargar en estado del plugin (`getStateInformation`/`setStateInformation`) para que sobreviva sesiones.
4. **Descubrimiento de salidas disponibles en Standalone**
  - Exponer nombres/indices de salidas fisicas del dispositivo activo en la capa Standalone.
  - Generar opciones de pares validos segun cantidad de canales de salida reales.
5. **UI minima de asignacion**
  - En panel de ajustes o rejilla de canales, agregar selector por stem (combo) con canales individuales disponibles.
  - Mostrar nomenclatura clara para operador: `Canal 1`, `Canal 2`, etc. (alineado a uso en consola/interface).
  - Definir fallback cuando cambie el dispositivo (si 7/8 ya no existe, volver a 1/2 y avisar).
6. **Compatibilidad y seguridad operativa**
  - Si el dispositivo tiene solo 2 salidas, conservar comportamiento actual.
  - Limitar consumo: no duplicar trabajo DSP innecesario, solo cambiar etapa de sumado/ruteo.
  - Cuando un stem sea estereo y su destino sea un canal unico, realizar fold-down a mono con proteccion de ganancia para evitar clipping.

## Validacion

- Prueba con interfaz de 8 salidas: asignar stems a `Canal 1`, `Canal 2`, `Canal 3` y verificar audio independiente por salida.
- Prueba en interfaz tipo X32: validar que etiquetas y resultado de ruteo correspondan a canales USB esperados por operador.
- Cambiar de interfaz (8 -> 2 salidas) y validar fallback seguro.
- Guardar/cargar sesion y comprobar restauracion exacta de asignaciones.
- Revisar que metering/master no reporte valores incorrectos cuando stems salen por pares no-main.

## Riesgos y mitigacion

- **Dependencia del wrapper Standalone**: el acceso a canales fisicos vive fuera del `AudioProcessor`; encapsular adaptador para no acoplar UI/DSP a detalles de JUCE Standalone.
- **Regresion en mezcla estereo existente**: mantener ruta legacy como default y activar nuevo router solo cuando haya mapeo explicito.
- **Estados invalidos por cambios de hardware**: aplicar normalizacion de mapa en cada cambio de dispositivo.

## Archivos con mayor probabilidad de cambio

- `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/PluginProcessor.cpp](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/PluginProcessor.cpp)`
- `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/PluginProcessor.h](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/PluginProcessor.h)`
- `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/StemMixEngine.cpp](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/StemMixEngine.cpp)`
- `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/ui/panels/ChannelGridPanel.cpp](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/ui/panels/ChannelGridPanel.cpp)`
- `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/ui/panels/SettingsPanel.cpp](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/ui/panels/SettingsPanel.cpp)`
- `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/audio/OutputChannelCatalog.h](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/audio/OutputChannelCatalog.h)` (nuevo)
- `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/audio/StemOutputRoutingState.h](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/audio/StemOutputRoutingState.h)` (nuevo)
- `[/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/audio/StemOutputRouter.h](/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/src/audio/StemOutputRouter.h)` (nuevo)
- (posible) nuevo modulo Standalone para bridge de dispositivo/canales


---
type: Application Lifecycle
title: Aplicación nativa PlayWorship
description: Inicio, audio, MIDI y persistencia sin wrapper de plugin
resource: src/StandaloneApp.cpp
tags: [juce, standalone, lifecycle, audio, midi]
timestamp: 2026-10-12
---

# Aplicación nativa PlayWorship

CMake genera `MTControls_Standalone` con `juce_add_gui_app`. No genera formatos
VST/AU ni enlaza `juce_audio_plugin_client`. `MTControls` permanece como biblioteca
estática compartida por la aplicación y las pruebas: conserva el motor DSP,
`AudioProcessor`, APVTS y editor existentes, sin requerir un DAW o un host.
El nombre PlayWorship, bundle ID y rutas `MTControls_artefacts/<config>/Standalone`
se conservan para instaladores y CI.

## Propiedad y ciclo de vida

- `PlayWorshipStandaloneApp` posee el procesador, `StandaloneAudioRuntime` y una
  `DocumentWindow`. La ventana crea el editor y lo desregistra antes de destruirlo
- `NativeEditorBoundsConstrainer` delega a los límites actuales del editor,
  incluyendo el borde nativo; conserva los cambios login/workspace/logout
- El único `AudioDeviceManager` pertenece a `StandaloneAudioDeviceBridge`.
  La construcción del procesador no abre un segundo dispositivo. El runtime
  inicializa el bridge antes de crear el editor
- `AudioProcessorPlayer` se registra como callback de audio y MIDI. Su layout
  sigue los canales activos del dispositivo; se solicitan hasta 64 salidas,
  limitadas por el hardware y la selección guardada
- El procesador acepta el bus de entrada desactivado y entre 1 y 64 salidas,
  para que el player negocie el ancho físico sin recurrir al layout inicial
  de 64 canales. Las entradas mono/estéreo siguen siendo layouts válidos
- Ajustes, MIDI Learn, fallback de dispositivos, pads y diagnósticos usan ese
  mismo manager. No consultan un singleton de host
- La parada retira ambos callbacks, separa el procesador y cierra el dispositivo
  antes de destruir el procesador. Inicio y parada repetidos son idempotentes

## Compatibilidad de datos y UI

Se mantienen las claves `audioSetup` (XML), `filterState` (base64) y posición de
ventana `windowX`/`windowY` en las preferencias existentes PlayWorship `.settings`.
El XML se limpia de entradas de micrófono **antes** de abrir el dispositivo.
No se cambian autenticación, sesiones, proyectos, mezcla ni ajustes de canciones.

Se conservan apertura externa `.pwproj`, activación de una instancia existente,
confirmación asíncrona de salida y cancelación de modales. iOS conserva máscara
de orientación previa al arranque, sincronización fullscreen y `resumed()`.
Android conserva su raíz OpenGL/ventana y usa el mismo runtime de audio/MIDI;
no adopta los cambios de layout de escritorio.

## Ventana de escritorio al iniciar

macOS, Windows y Linux abren una ventana normal, redimensionable, que ocupa
el área de trabajo del monitor principal. No se activa fullscreen al arrancar:
se conservan la barra de título nativa y el espacio reservado por el sistema
para su menú, Dock o barra de tareas. `expandTopLevelWindowToPrimaryWorkArea`
descuenta el marco nativo de los límites del contenido; la misma geometría se
usa al detectar y alternar maximizado/restaurado. No existe una preferencia
para activar esta expansión: también se aplica al pasar del acceso al espacio
de reproducción. Las preferencias antiguas de expansión se ignoran.

iOS y Android conservan su ventana móvil y su comportamiento de orientación;
esta regla de escritorio no modifica sus superficies fullscreen.

## Validación

`standalone_audio_runtime` usa dispositivos de prueba de 2, 8 y 64 salidas para
comprobar apertura sin entradas, negociación del layout, un callback por bloque,
señal en cada salida, compatibilidad del estado guardado e inicio/parada/reinicio.
La negociación también se ejecuta con `MTControlsAudioProcessor` real y su
propio bridge: verifica cero entradas internas, restauración del último par de
salidas, reinicio, cambios de salidas activas y normalización de rutas. El
procesador sintético conserva la prueba de señal/callback en cada salida.
También comprueba que la restauración vea el ancho del dispositivo antes de
normalizar rutas y que los límites de ventana sigan los cambios del editor.
No acredita un controlador MIDI físico ni audio audible sobre una interfaz real.
Los flujos de proyecto, quit y orientación requieren además prueba de UI, y las
compilaciones Apple, Windows y Android requieren sus respectivos toolchains.

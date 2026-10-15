# Canción vacía: análisis de timeline expansible

Estado: propuesta para revisión, sin implementar el crecimiento ni cambiar la
regla actual. El alcance implementado es el botón directo en Detalles. El
prototipo completo anterior sigue recuperable en `11f804d`.

## Bloqueos confirmados

- `SongData::estimateSongLengthSeconds` devuelve cero para la nueva canción
  vacía. Es correcto para audio físico ausente y reproducción abierta, pero
  `WaveformBar::getPlayableRangeSeconds` lo convierte en rango `{0, 0}`.
- `WaveformBar::addArrangementPart` rechaza ese rango antes de crear una parte.
  La inserción normal limita la nueva sección al Fin actual. No basta con
  ocultar la waveform o habilitar el botón +.
- `mouseDrag` abandona con longitud cero y el resize derecho limita `nextEnd`
  a la longitud actual. La excepción de arrastre fuera de pantalla de una copia
  resuelve audio fuente existente; no es un lienzo vacío expansible.
- Zoom/scroll y conversión píxel/tiempo usan `viewportStart`/`viewportSpan`
  normalizados respecto a la longitud. Aumentarla durante un gesto cambia la
  escala y desplaza el punto sujeto. La regla compartida
  `PracticeTimelineRuler::build` expresa compases/tiempos musicales; la vista
  vacía abierta actual muestra un contador de compás, no una regla en segundos.
- `PluginProcessor::setSongArrangement` ya calcula el extremo del arreglo y
  conserva Fin manual o permite que siga las ediciones. El scheduler existente
  maneja IDs, saltos, loops y automatización sin necesitar lectores de audio.
  La resolución de rangos fuente de copias sigue dependiendo de la duración PCM.
  Con fuente cero, el mapper del scheduler devuelve ausencia de mapeo y el
  procesador conserva el frame de arreglo; al publicar, el scheduler admite
  tiempos lógicos de sección como fallback. Hay que conservar esa separación,
  sin convertir el extremo del arreglo en longitud de un archivo inexistente.
- `SongLibraryStore::reconcileWindowForSong` recorta Inicio/Fin a la duración
  estimada. Con fuente cero eliminaría un Fin guardado si no se distingue el
  extremo del arreglo. La persistencia de partes ya usa segundos, IDs estables,
  cues y automatización; no hace falta otro formato de secciones.
- El decoder del sidecar usa actualmente la presencia del ajuste histórico de
  pad para marcar la canción sintética que evita migrar cues legacy. Antes de
  persistir secciones vacías nuevas debe recibir contexto explícito del tipo
  de canción, independiente del acompañamiento.

## Contrato propuesto

Separar cuatro conceptos:

1. **Duración de audio fuente:** cero para la canción vacía. Nunca crear audio,
   bins, horas de silencio ni rangos PCM ficticios para suministrar espacio.
2. **Extremo del arreglo:** mayor `endSec` de sus secciones; se persiste mediante
   el arreglo existente. No depende del zoom ni del margen que muestra la vista.
3. **Ventana de reproducción:** Inicio/Fin y su indicador de personalización
   actuales. Sin secciones ni Fin explícito, seguir hasta Detener. Propuesta:
   al existir secciones, Fin automático sigue la última; Fin ajustado por el
   usuario conserva su recorte. Es una decisión de producto a revisar antes
   de implementar, no un nuevo selector Libre/Con secciones.
4. **Ventana visible:** inicio y amplitud en segundos. Por ejemplo, mostrar
   inicialmente 30 segundos es sólo encuadre, sin guardar una duración de 30 s
   ni imponerla como máximo. Scroll, pinch y zoom cambian la cámara; no cambian
   el arreglo, Fin ni duración. No reservar una extensión enorme por adelantado.

La regla muestra segundos con densidad adaptada al zoom y formato minuto:segundo
al aumentar el tiempo. El snap musical existente puede seguir activo y usar
BPM/compás. No escalar los límites guardados de sección al editar BPM.

## Reutilización de edición y motor

El + conserva nombres, colores, IDs, selección, snap, undo/redo y notificaciones
existentes. En la vacía puede insertar el tramo habitual de 8 segundos desde
el ancla o final del arreglo, aunque no exista audio. Su extremo pasa a ser
parte real del arreglo; el margen visible restante no lo es.

Al ensanchar, convertir el puntero a segundos con una escala estable durante
el gesto. Cerca del borde, desplazar la cámara para revelar más tiempo; no
reencuadrar todo ni comprimir la sección mientras se arrastra. Permitir
extenderla más allá del extremo anterior y publicar el cambio por los callbacks
actuales. Mantener sus límites temporales válidos y mínimos de edición. En
canciones con stems conservar las restricciones reales de fuente y semántica
de copias. Las copias sin audio necesitan rango lógico propio, sin fingir PCM.

Aplicar el arreglo al procesador/scheduler y persistencia actuales. Respetar
`windowCustomized`/Fin manual, undo/redo y revisiones autoritativas remotas.
El cliente remoto solicita la misma operación; no inventa otra duración local.
El estado compartido debe distinguir duración fuente, extremo del arreglo y
Fin, nunca transmitir el espacio de cámara como duración de la canción.

No añadir un máximo musical arbitrario de minutos/compases. Conservar los
límites de representación numérica y capacidades en tiempo real (por ejemplo,
256 secciones del scheduler) como restricciones técnicas independientes de la
extensión temporal; no retirarlos dentro de este cambio.

## Retirar y conservar del prototipo

Retirado ahora: selector de tipo, Libre/Con secciones, filas de nombre/compases,
configuración especial click/pad, cambio automático de tiempos por BPM y
gating del pad en Pause/Stop. También las herramientas de capturas de ese diseño.

Conservado: tipo explícito compatible, paquete sin audio/waveform, registro en
Biblioteca/setlist, carga sin lectores, reloj abierto, integración del scheduler,
controles Ir/Loop, ruteo existente y estado remoto. Los campos históricos de
compases/acompañamiento siguen legibles para recuperar proyectos del prototipo;
no deben convertirse en el contrato de duración de la edición expansible.

## Verificación necesaria para una implementación posterior

- Abrir vacío: regla legible, cero bins/stems; zoom/scroll no ensucian el proyecto
  ni alteran Fin. Reproducción abierta y pad/click habituales.
- Añadir primera sección, insertar con ancla y redimensionar más allá del borde
  manteniendo escala; repetir en móvil/tablet/escritorio con ratón y tacto.
- Undo/redo restaura extremos y Fin automático; Fin manual se conserva. Guardar,
  reabrir y añadir a setlist retiene IDs, segundos, cues y automatización.
- Loop/Ir, pausa/reanudación/stop, cambios de tempo y control remoto usan el nuevo
  extremo; límites y revisiones inválidos se rechazan sin cambios parciales.
- Migración del prototipo y proyectos anteriores con audio, resize/copia de
  stems, ventanas y waveform reales conservan su comportamiento.

No se ha implementado esta propuesta ni se ha acreditado su funcionamiento
mediante las pruebas actuales del botón o transporte sin stems.

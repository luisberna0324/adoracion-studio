# Controles de la barra de escritorio

La barra superior de escritorio ofrece un selector compacto `Onda / Secciones`
con ambas opciones siempre visibles y una sola seleccionada. Refleja el estado
real de `WaveformBar`; elegir la vista actual no vuelve a alternarla. Ambas
opciones quedan deshabilitadas cuando una edición impide cambiar de vista.
La selección no modifica el transporte ni la posición de la canción.

El botón independiente `?` abre el catálogo existente de guías de aprendizaje.
El menú de tres puntos de esta barra desaparece sólo en escritorio: MIDI Learn,
Configuración y Ventana de canción siguen accesibles en sus controles originales.
MIDI Learn conserva sus callbacks, estado y asignaciones. En las ventanas de
escritorio más estrechas (mínimo 940 puntos), los controles usan dimensiones
compactas; el indicador de CPU/MEM cede espacio antes que el título o el tempo.
Cambiar de vista no altera los límites de la tarjeta de título/artista.

Teléfono y tablet conservan su menú, disposición y comportamiento táctiles.
El menú de edición propio de la onda también conserva sus funciones.

La regresión `waveform_editing` comprueba la barra entre 940 y 1600 puntos en
los tres perfiles de escritorio, ausencia de solapamientos con título y entre
controles, selección idempotente, bloqueo de edición, cambios externos de vista,
acceso a ayuda/Configuración/MIDI y conservación del menú de teléfono. Las
capturas `PW_LIVE_CAPTURE_DIR` usan los componentes reales de la barra y onda.

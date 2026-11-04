# Controles de la barra de escritorio

La barra superior de escritorio ofrece un selector compacto `Onda / Secciones / Letras`
con una sola opción seleccionada. Letras abre el mapa completo de marcas
independientes para FreeShow; no se añade al menú de móvil ni del control remoto.
El selector refleja la vista real; elegir la vista actual no vuelve a alternarla. Las
opciones quedan deshabilitadas cuando una edición impide cambiar de vista.
La selección no modifica el transporte ni la posición de la canción.

Las marcas de Letras muestran un punto temporal y un preview compacto permanente.
Las secciones musicales se conservan como fondo de referencia. El menú contextual
permite cambiar destino, probar explícitamente en FreeShow y eliminar;
seleccionar o arrastrar no proyecta. El seguimiento conserva su control propio. Véase
[FreeShow](../integrations/FreeShow.md).

Letras reúne Show, conexión/estado y seguimiento en una sola línea compacta.
No ofrece selector de arreglo, botón superior para añadir ni buscador/panel de
diapositivas. Para crear una marca se coloca o arrastra el puntero temporal en
la regla/mapa y se pulsa su `+` contextual. El menú presenta grupos y un submenú
de diapositivas; elegir un destino crea una marca en el tiempo fijado al abrir
el menú. El mismo destino admite varias marcas con identidades y tiempos propios.
El puntero comparte estado, dibujo y geometría del `+` con la onda de guía a
través de `TimelineInsertionAnchor`; colocar el puntero en Letras no modifica
el transporte, las secciones ni la proyección. Escape lo retira.
Las marcas anteriores conservan sus destinos y persistencia; quitar el selector
visible no elimina los identificadores de arreglo guardados ni su validación.

El selector Show tiene un máximo de 220 puntos y muestra solo nombres. No hay
texto de estado a su derecha, ayuda debajo ni panel inferior de detalle. El
estado permanece en el badge y su tooltip. La conexión IP/LAN se edita en
Configuración → Integraciones → FreeShow y solo se aplica con Guardar y probar.
El mapa ocupa el alto liberado. Su zoom horizontal y barra de desplazamiento
conservan las coordenadas temporales de las marcas y las secciones tenues.
No hay botones de zoom ni una segunda fila: rueda vertical para ampliar/alejar,
gesto horizontal para recorrer y pellizco con la transformación existente de Onda
(`1 + amount`), sin modificadores. La rueda comparte clasificación, dirección y
sensibilidad con Onda mediante `TimelineNavigationGestures`, sin volver a invertir
la dirección configurada en el sistema. Ambas vistas limitan el zoom a 1–18×.
El zoom se ancla bajo el cursor. Durante el arrastre
de un punto o del puntero contextual, la navegación no cambia la vista. Seleccionar
una tarjeta en otra fila conserva el desplazamiento horizontal.
Letras reutiliza la regla musical de `WaveformBar` mediante `TimelineRulerStyle`;
el playhead de escritorio de la onda reutiliza la línea fina y bandera de Letras.

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

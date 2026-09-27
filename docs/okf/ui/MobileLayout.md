---
type: Technical Contract
title: Mobile Layout
description: Contrato responsive y de area segura para telefonos iOS y Android
resource: src/PluginEditor.cpp
timestamp: 2026-09-26T00:00:00-03:00
---

# Mobile Layout

La vista de performance en telefonos es horizontal. En iPhone y Android,
el runtime habilita `upright` para Biblioteca y setlists mientras su panel
esta abierto y restaura ambas orientaciones landscape al cerrarlo. La
orientacion inicial se restringe a landscape. En iPhone, el plist admite
tambien `Portrait`; en Android, `screenOrientation=landscape` define el inicio
y JUCE usa `Activity.setRequestedOrientation` para cambiarla durante la sesion.
El manifest maneja `orientation|screenSize` mediante `configChanges`, sin
recrear la actividad al girar. Las tablets conservan la vista horizontal.
La ventana Android entra en fullscreen despues de asignar contenido y limites
iniciales: cambiar sus limites despues desactiva el fullscreen del peer nativo.
El contenido no redimensiona la ventana. Al cambiar el display se reafirma
fullscreen y el layout nativo `MATCH_PARENT` propaga la nueva medida a la
ventana, la raiz OpenGL y el editor, incluida el area segura actualizada.
Los cambios de area segura se propagan tambien cuando el tamano de la ventana
y del contenido permanece igual. Asi se retira el margen superior al ocultar
la barra del sistema, sin esperar a otra rotacion o redimension.

En iOS, el cambio de orientaciones permitidas se difiere fuera de `resized()`
y agrupa solicitudes usando el estado mas reciente de Biblioteca. La ventana
standalone sincroniza su geometria con `UIWindowScene.coordinateSpace` al
terminar la transicion nativa y al volver del background. Propaga el tamano al
contenedor y al editor aun si los limites de la ventana no cambiaron. Una vez
adjunto, el editor obtiene su tamano del host y no del cache de displays: este
puede conservar temporalmente las dimensiones de la orientacion anterior.
La mascara de orientaciones puede fijarse antes de crear `UIWindowScene`.
Por eso, en iOS 16+ tambien se compara la orientacion efectiva de la escena
con la mascara del controlador y se solicita `requestGeometryUpdate` si no
coinciden, aunque la mascara de JUCE no haya cambiado. Se agrupan solicitudes
iguales hasta completar la transicion. Esto evita arrancar con la escena en
portrait y el contenido de performance recortado a un cuadrado.

## Deteccion

Un dispositivo tactil se trata como telefono cuando el lado corto disponible es
menor de `600 pt/dp`. En iOS se prioriza la familia nativa del dispositivo,
se convierte el canvas fisico a points y se conserva un respaldo por pixeles
fisicos para simuladores que reportan escala unitaria. Android ya entrega el
layout en dp. Esto separa telefonos de tablets sin depender del nombre o modelo
del dispositivo.

El layout del telefono usa tres rangos sobre el ancho util, despues de aplicar el
area segura:

| Rango | Ancho util | Uso |
|---|---:|---|
| Small | `< 760` | SE, mini y telefonos Android compactos |
| Regular | `760...879` | iPhone estandar/Pro y Android medio |
| Large | `>= 880` | Plus, Air, Pro Max y Android ancho/foldable |

En el `TopBar`, Android conserva las medidas del rango Regular hasta `939 dp`.
El conjunto Large se activa desde `940 dp`, porque algunos dispositivos
landscape no reportan un inset horizontal suficiente y el conjunto de botones
grandes puede recortar la accion final del bloque de metronomo/tono.

## Area segura

- Los controles fijos se colocan dentro de `Displays::Display::safeAreaInsets`.
- En iOS, la familia reportada por el sistema tiene prioridad sobre los
  breakpoints: `iPad` siempre usa el perfil tablet/escritorio e `iPhone` usa el
  perfil movil. Esto evita clasificaciones incorrectas por la escala del
  framebuffer del simulador.
- El fondo del mixer se prolonga visualmente por el inset inferior hasta el
  borde fisico de la ventana, evitando una franja vacia bajo los canales.
- En iPhone y Android cada lateral usa su inset real, actualizado al girar. El `TopBar`
  dibuja su fondo a ancho completo y coloca los botones en su area segura con
  4 pt de aire adicional. No se igualan ambos insets usando el mayor.
- La waveform de telefono llega a ambos bordes. Sus botones flotantes, mensajes,
  etiquetas y acciones de seccion e indicador remoto permanecen dentro del
  area segura; el tintado de las secciones conserva el ancho completo.
- El viewport del mixer de telefono llega a ambos bordes. El contenido lleva
  padding desplazable inicial y final equivalente a los insets reales, de
  modo que el primer y ultimo canal pueden quedar completamente accesibles.
  La escala de los strips se calcula con el ancho seguro y los canales se
  desplazan por debajo de las zonas laterales durante el scroll.
- En Android se respetan los insets reportados por el sistema para los controles,
  incluyendo cutouts y barras visibles. No se recorta toda la superficie por el
  inset lateral: waveform y mixer se extienden bajo esa zona, y el fondo de la
  barra superior cubre tambien su inset superior si hay una barra visible.
- Los fondos y superficies desplazables se dibujan edge-to-edge; las acciones
  fijas mantienen su proteccion respecto de la camara y esquinas.
- No se mantienen tablas de margenes por modelo ni gutters fijos para notch.

## Controles

- Objetivo tactil frecuente minimo: `44 x 44 pt/dp`.
- El asistente de creacion/edicion de canciones ocupa toda la superficie fisica
  del telefono. Su fondo se dibuja edge-to-edge detras del notch, barras de
  sistema e indicador inferior; encabezado, campos y acciones descuentan
  `safeAreaInsets`. Usa el encabezado movil compartido de 56 pt, con cierre de
  44 pt en iOS o 48 dp en Android alineado a la derecha, un indicador
  de cuatro pasos de 86 pt y margenes laterales de 17 pt. En `Detalles`, la
  portada se centra con un lado de 112–136 pt, los editores y combos miden 48 pt
  de alto, y `BPM`, `Tono` y `Compas` comparten una fila de tres columnas. El
  resumen de metadata y el boton primario usan 70 pt y 48 pt respectivamente.
  El + de Biblioteca abre la creacion dentro del mismo panel en todas las
  plataformas, con esta jerarquia y controles de menor altura en tablet y
  escritorio. La edicion conserva su presentacion anterior.
- Con un teclado virtual acoplado, el asistente combina `safeAreaInsets` y
  `keyboardInsets`. En Detalles conserva encabezado, pasos y la geometria del
  formulario: un viewport reduce su ventana visible sobre el teclado y desplaza
  suavemente el campo enfocado junto a su etiqueta. Portada y tarjeta de metadata
  se dibujan con el mismo desplazamiento y recorte que los controles.
- La portada de Detalles mide 128 pt y el contenido desplazable 550 pt,
  independientemente de la altura del teclado. El usuario puede desplazarlo
  manualmente sin que el seguimiento del foco lo devuelva a otra posicion.
  Se toleran 200 ms de inset vacio solamente mientras otro editor del asistente
  conserva el foco, para evitar que el formulario se expanda entre dos campos.
  Una finalizacion explicita de la edicion no conserva ese inset.
- Sobre el teclado aparecen `Anterior`, `Siguiente` y `Listo` con altura tactil
  de 44 pt. Los dos primeros transfieren directamente el foco entre titulo,
  artista y BPM, o entre nombres de pistas en Secciones; se deshabilitan en los
  extremos. No toman foco al pulsarlos. Return avanza al siguiente editor y
  termina en el ultimo; en iOS la tecla indica `Siguiente` o `Listo`.
  `Listo`, cambiar de paso y cerrar el asistente finalizan la edicion.
- BPM usa teclado numerico. En iOS los campos del asistente conservan la sesion
  nativa de entrada al cambiar de editor, actualizando texto, seleccion y rasgos
  del teclado mediante `reloadInputViews`. Deslizar el formulario no lo cierra.
  Al cerrar el teclado se recupera el pie con `Siguiente` sin reconstruir campos.
- Los combos del asistente movil abren al completar un tap; arrastrar sobre ellos
  permite desplazar el formulario. El selector toma el foco y espera a que el
  teclado desaparezca y su anclaje se estabilice antes de mostrar la lista JUCE
  (espera acotada a 600 ms para teclados flotantes o insets no disponibles).
  Cerrar la lista no devuelve el foco al editor ni reabre el teclado. Escape,
  perdida de foco, deshabilitacion u ocultacion cancelan una apertura pendiente.
- En el paso `Secciones`, el viewport termina sobre el teclado y desplaza la
  fila enfocada solo cuando queda fuera de su area visible. Esto se aplica a
  iOS y a Android 11 o superior, donde JUCE reporta `keyboardInsets`.
- Los pasos `Archivos`, `Secciones` y `Revisar` reutilizan el mismo encabezado,
  indicador y pie. En telefono, las filas de mapeo se apilan para evitar
  columnas comprimidas: miden 104 pt, con toggle de 44 pt y campos de 48 pt.
  La creacion de escritorio tambien apila las filas, con 80 pt y campos de
  36 pt; la edicion de escritorio conserva las filas tabulares de 42 pt. El scroll manual no vuelve continuamente al editor
  enfocado. Al retroceder, los nombres/categorias/Cargar se guardan en el borrador.
- La tarjeta de progreso se limita al ancho disponible menos 32 pt; conserva
  sus 440 pt en escritorio. La creacion portable movil y de escritorio procesa el paquete en
  background, mantiene el asistente bloqueado contra doble envio/cierre y
  entrega progreso/resultado al message thread. Ver
  [Song Creation](../importing/SongCreation.md).
- El telefono muestra transporte basico, tiempo, waveform y mixer.
- El toggle contextual `Fade Down/Fade Up` aparece junto a `Siguiente`. El
  tiempo usa dos lineas (`actual /` y `total`) con tipografia reducida.
- El bloque de metronomo conserva siempre en telefono tanto el boton de BPM
  como el toggle de click.
- La tarjeta de cancion activa aparece inmediatamente despues del toggle de
  fade con portada, titulo y artista, usando la presentacion de escritorio
  ajustada al ancho disponible. Toda la tarjeta es un boton accesible y abre
  el popover con flecha del setlist activo, anclado a la tarjeta. Elegir una
  fila carga esa cancion y cierra el popover; el selector tambien esta disponible
  sin una cancion cargada. Se elimina el boton separado de nota musical.
- En la tarjeta del `TopBar`, titulo y artista mantienen su tamano de fuente.
  Cada linea que excede el ancho se desplaza de lado a lado con aceleracion
  suave y una pausa de 1,5 segundos en cada extremo; las lineas que caben
  permanecen quietas. No se comprime el texto ni se agregan puntos suspensivos.
  El recorrido reinicia al cambiar la cancion o el tamano de la tarjeta y se
  repinta con el timer de UI existente, sin intervenir en el audio. Este
  comportamiento tambien aplica a la tarjeta de escritorio y tablet.
- Pads permanece inmediatamente a la izquierda del cronometro. Abre un popover
  anclado al boton, con encabezado y cierre del mismo estilo que la lista de
  canciones del setlist. Los controles de interpretacion siguen en la misma
  instancia de Pads y se ajustan al espacio seguro disponible.
- El `+` del encabezado del selector abre Biblioteca y setlists. En telefono gira
  a vertical y ocupa toda el area segura. El titulo, volver, Biblioteca, Agregar
  y cerrar comparten una sola fila; las acciones usan objetivos tactiles planos
  de 44 x 44 pt en iOS y 48 x 48 dp en Android, con iconos y sin etiquetas. En Biblioteca, abrir proyecto
  y crear cancion tambien se ubican en el encabezado como acciones compactas;
  el buscador ocupa todo el ancho de su propia fila. La `X` cierra el panel desde
  cualquiera de sus paginas y recupera la vista horizontal de performance.
- La superficie de Biblioteca/setlists cubre la ventana completa, incluidos los
  fondos bajo la barra de estado y el indicador inferior. Los controles aplican
  los insets seguros dentro de la pagina. En telefono, el encabezado adopta una
  barra plana compartida por Repertorio, canciones, Biblioteca y creacion de
  canciones: 56 pt de alto, 8 pt de margen lateral, 2 pt entre acciones,
  objetivos tactiles de 44 pt en iOS y 48 dp en Android, e iconos visibles de
  21 pt sin circulos decorativos. Titulos, subtitulos y acciones conservan la
  misma alineacion; el `+` usa el color de acento. La vista de canciones muestra
  bajo el nombre del setlist un resumen compacto con cantidad y duracion.
- El `+` de Biblioteca abre la creacion como la pagina siguiente del mismo
  contenedor, con entrada horizontal desde la derecha. La `X` de creacion y el
  guardado exitoso regresan con la transicion inversa, mostrando de nuevo
  Biblioteca durante el desplazamiento. El asistente se libera al acabar el
  movimiento para que su contenido no desaparezca a mitad de la salida. Este
  recorrido se usa tambien en Android, tablets y escritorio.
- El overlay incluye un boton `X` de `44 x 44 pt` en iOS o `48 x 48 dp` en
  Android, igual que las acciones de
  biblioteca y agregar, para cerrarlo desde las vistas de setlists, canciones y
  biblioteca.
- Configuración usa desplazamiento vertical con rueda/trackpad y también admite
  arrastrar el contenido con clic o toque sostenido. No expone desplazamiento
  horizontal en el contenedor principal. Ruteo participa del mismo viewport,
  evitando que un contenedor anidado capture gestos sin poder desplazarse.
- La altura desplazable de Ruteo se calcula con todas las categorias, el
  encabezado, la nota inferior y los margenes; no se limita a 900 px ni depende
  de que la ventana mida menos de 820 px. El ancho descuenta la barra vertical.
  Con menos de 520 px utiles por fila, la categoria queda encima de los dos
  selectores, que conservan 48 px de alto. En filas anchas los controles tienen
  48 px en tactil y 36 px en escritorio. La nota aparece despues de la ultima
  fila, sin superponerse. Un relayout conserva/clampa el scroll; cambiar de
  pestaña vuelve al inicio.
- Los selectores de Ruteo abren al completar un clic o tap, en movil y escritorio.
  Arrastrar sobre ellos no abre un menu. La rueda/trackpad se propaga al viewport
  y nunca cambia Mono/Stereo ni el canal de salida al pasar sobre un selector.
  `settings_panel` verifica el acceso al ultimo control, limites de filas,
  rueda sobre selectores, gesto arrastrado y cambio de pestaña en nueve tamaños.
- Configuración mantiene Diagnóstico en una pestaña propia, después de Ruteo.
  Audio contiene sólo los controles del dispositivo; el informe técnico se
  comparte desde una tarjeta compacta en Diagnóstico y no expone un marcador
  manual de crujidos.
- Los botones flotantes de exportar y seguimiento forman un grupo horizontal
  con separacion uniforme y margen respecto al borde inferior de la waveform.
  El editor de secciones se reserva para tablet y escritorio, por lo que el
  boton de editar no se muestra en telefono.
- En el overlay de telefono, la cantidad de canciones y la duracion total se
  muestran como subtitulo compacto bajo el nombre del setlist. El BPM promedio
  permanece oculto para mantener la cabecera despejada.
- En telefono, tanto el wizard de creacion de canciones como el drawer de
  Biblioteca/setlists usan `drawStudioBackground(...)` sobre toda la superficie
  fisica. El contenido de performance queda cubierto mientras estas vistas estan
  abiertas; la barra de estado y el indicador inferior conservan sus areas
  seguras.
- Pads, expansion vertical de waveform y minimap/navegador inferior de la
  waveform se ocultan en telefono. Tambien se oculta la regla de compases
  superior; la onda principal usa todo ese alto liberado.
- La waveform de telefono tampoco usa tarjeta, borde ni padding externo:
  usa todo el ancho en iPhone y Android; sus acciones conservan el area segura.
- En telefono, un arrastre horizontal sobre la onda desplaza su contenido en la
  misma direccion del dedo;
  un toque conserva el seek y un gesto de pinza amplia o reduce la escala con
  el punto de contacto como ancla. La edicion de secciones y el mapa de tempo
  no estan disponibles en telefono.
- El mixer de telefono no dibuja una tarjeta o division propia: sus canales
  comienzan directamente debajo de la waveform, sin relleno superior ni marco.
- Los channel strips forman una sola fila, ajustan su altura al mixer y se
  recorren horizontalmente mediante drag; nunca se envuelven en filas nuevas.
- Los telefonos (Android e iOS) usan el mismo calculo natural que el resto de
  los viewports: `widthAdaptiveScale * profileScale` con tope en `1.25x`. No se
  fuerza un minimo adicional en Android para no inflar simultaneamente ancho y
  alto y empujar fuera de pantalla la banda S/M y los botones en viewports
  angostos. Los canales adicionales permanecen accesibles con desplazamiento
  horizontal. iPad sigue garantizando al menos `1.0x` para mantener knobs,
  medidores y titulos legibles.
- En iPad, que conserva el layout de tablet/escritorio, la escala minima del
  strip es `1.0x` para mantener knobs, medidores y titulos legibles.
- Las tablets compactas conservan las funciones de tablet, pero el `TopBar`
  usa controles de `44 pt/dp` y presenta el tiempo actual/total en dos lineas,
  como telefono. El segmento numerico de BPM y el toggle de click permanecen
  siempre visibles; el espacio se recupera reduciendo el contador y la tarjeta
  central de la cancion.
- En Android, los limites del panel incorporan el safe area. En iPhone, el
  fondo del mixer y la banda del titulo se extienden por el safe area inferior
  para evitar una franja vacia. En ambos casos la banda queda fija al borde
  visual inferior del strip y la fila S/M se coloca inmediatamente encima; no
  se vuelve a aplicar un inset dentro del canal. El
  viewport mantiene su posicion vertical en cero y solo conserva el
  desplazamiento horizontal entre canales. La altura del strip se limita a la
  altura visible del viewport aunque su escala horizontal sea mayor; fader y
  medidor se comprimen para reservar siempre el footer completo.
- La altura de top bar y waveform se adapta al alto util (`<=375`, `<=410`,
  `>410`) para preservar espacio para el mixer.
- En telefonos Android, la waveform ocupa como maximo el 24% del alto util
  para dar mas recorrido a los faders sin ocultar la banda del canal ni S/M.

## Verificacion

Probar ambas direcciones landscape en iOS y, como minimo, estos canvas utiles:

- `667 x 375` (telefono pequeno)
- `844 x 390` (telefono regular)
- `874 x 402` (iPhone Pro actual)
- `912 x 420` y `956 x 440` (telefonos grandes)
- Android compacto, medio y tablet con cutout/navigation insets activos

## Rendimiento en Android

- El wrapper Android compila `juce_opengl` y conecta un `OpenGLContext` al
  componente raiz que contiene el editor. JUCE conserva un framebuffer y
  repinta sus regiones invalidadas con el renderer 2D de OpenGL. El peer
  software anterior rasterizaba y transferia la ventana completa incluso ante
  invalidaciones pequenas, ocupando el message thread durante los gestos.
- Cada slot del mixer conserva una imagen rasterizada en memoria de CPU. El
  desplazamiento horizontal reutiliza esa imagen y los cambios de VU, volumen
  o botones invalidan solo su region. Se usa `SoftwareImageType` explicitamente
  para evitar render targets OpenGL anidados. La cache se recrea cuando cambia
  el tamano fisico del slot y se libera con el componente. El cache de texturas
  del contexto tiene un presupuesto de `32 MiB`.
- El contexto solo dibuja cuando hay invalidaciones (`setContinuousRepainting(false)`).
  No hay un bucle de render permanente mientras la UI esta quieta. La raiz es
  opaca y desconecta el contexto antes de destruir el editor y sus controles.
- `assembleDebug` optimiza el codigo nativo con `-O2`, incluidos los modulos JUCE
  y el motor de audio. Conserva los simbolos de debug y el APK depurable. No
  activa `fast-math` ni cambia las opciones de Release. Para depuracion paso a
  paso sin optimizacion: `./gradlew :app:assembleDebug -PplayworshipOptimizeDebug=OFF`.
  El siguiente build sin esa propiedad vuelve a activar la optimizacion.
- Con una sesion local valida, Android presenta primero la superficie ligera de
  bienvenida y difiere brevemente la construccion del mixer, waveform y
  biblioteca. Los componentes JUCE siguen creandose en el message thread.
- `makeAppFont` usa la familia sans-serif nativa y su cache en Android para
  evitar resolver repetidamente las tipografias embebidas durante el arranque y
  los repaints. iOS y escritorio conservan la familia Inter embebida.
- La waveform y los VU usan `30 Hz`; MIDI Learn usa `12 Hz`. El único VU de
  Click Dyn usa `60 Hz`, ataque inmediato y caída de 120 ms para seguir sus
  transitorios; consume un pico acumulado por el audio thread. Los demás VU
  omiten lectura y repaint cuando quedan fuera del viewport o están ocultos.
  Sólo invalidan la columna de LEDs al cambiar un segmento o el indicador de
  pico, conservando la escala y el fader en caché. El suavizado depende del
  tiempo transcurrido (ataque 55 ms y caída 350 ms, constantes exponenciales),
  para que un timer demorado no ralentice artificialmente la respuesta.
  Estas frecuencias no modifican el transporte ni el callback de audio.
- Editor, top bar y waveform usan fondos planos en Android. La waveform es
  opaca y su cache de playback usa resolución lógica con overscan de hasta `2x`,
  reservando más espacio hacia delante. Su origen está alineado a una grilla de
  dos píxeles anclada al tiempo cero de la canción y conserva la misma escala
  de muestreo entre reconstrucciones: las crestas no cambian de fase al renovar
  la imagen. El redondeo admite unas columnas extra en los extremos para cubrir
  viewports de ancho fraccionario.
  La cache se reutiliza con follow encendido o apagado; no se transforma una
  segunda máscara para realzar progresivamente la porción reproducida; Android
  tampoco asigna ni rasteriza esa máscara auxiliar.
- En una ventana fija, el transporte invalida solamente la franja entre las
  posiciones anterior y nueva del playhead. Si follow mueve la ventana,
  `setVisibleWindow` solicita un repaint completo para desplazar tambien la
  onda y sus secciones. El overscan reduce reconstrucciones de la cache, pero
  no sustituye ese repaint cuando cambia la ventana visible.
- Los cambios entre reproduccion y pausa/stop invalidan toda la waveform,
  porque alternan entre su imagen de playback y el dibujo detallado. Despues
  de esa transicion vuelven a usarse las invalidaciones parciales del playhead.
- La carga de una canción reinicia la cámara al comienzo y descarta el historial
  de suavizado y la inercia de la canción anterior. Conserva el estado de follow;
  recibir bins de waveform en background para la misma canción no rebobina la vista.

Mediciones y limites de la verificacion:
[`perf/analysis_20260915/INFORME.md`](../../../perf/analysis_20260915/INFORME.md).

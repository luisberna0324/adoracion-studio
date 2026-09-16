---
type: Technical Contract
title: Mobile Layout
description: Contrato responsive y de area segura para telefonos iOS y Android
resource: src/PluginEditor.cpp
timestamp: 2026-09-15T00:00:00-03:00
---

# Mobile Layout

La vista de performance en telefonos es exclusivamente horizontal. iOS admite
`LandscapeLeft` y `LandscapeRight`; Android declara `screenOrientation=landscape`.

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

- El contenido interactivo se coloca dentro de `Displays::Display::safeAreaInsets`.
- En iOS, la familia reportada por el sistema tiene prioridad sobre los
  breakpoints: `iPad` siempre usa el perfil tablet/escritorio e `iPhone` usa el
  perfil movil. Esto evita clasificaciones incorrectas por la escala del
  framebuffer del simulador.
- El fondo del mixer se prolonga visualmente por el inset inferior hasta el
  borde fisico de la ventana, evitando una franja vacia bajo los canales.
- En iOS landscape se usa el mayor inset horizontal en ambos lados para que la
  superficie sea equivalente con la camara a izquierda o derecha.
- En Android se respetan los cuatro insets reportados por el sistema, incluyendo
  cutouts y barras de navegacion.
- Los fondos pueden dibujarse edge-to-edge; transporte, waveform y mixer no.
- No se mantienen tablas de margenes por modelo ni gutters fijos para notch.

## Controles

- Objetivo tactil frecuente minimo: `44 x 44 pt/dp`.
- El telefono muestra transporte basico, tiempo, waveform y mixer.
- El toggle contextual `Fade Down/Fade Up` aparece junto a `Siguiente`. El
  tiempo usa dos lineas (`actual /` y `total`) con tipografia reducida.
- El bloque de metronomo conserva en telefono tanto el boton de BPM como el
  toggle de click. La decision de compactarlo usa como reserva central solo el
  artwork cuadrado; no exige el ancho de la tarjeta de cancion de escritorio.
- El artwork cuadrado de la cancion activa aparece inmediatamente despues del
  toggle de fade, sin mostrar titulo ni artista.
- El setlist lateral se reemplaza por un boton compacto con icono de musica,
  ubicado inmediatamente a la izquierda del cronometro y con el mismo estilo de
  los controles del header. Abre un overlay que ocupa
  toda el area segura sin reducir ni desplazar
  el mixer. Mientras el overlay esta abierto, el boton musical se oculta.
- El overlay incluye un boton `X` de `40 x 40 pt/dp`, igual que las acciones de
  biblioteca y agregar, para cerrarlo desde las vistas de setlists, canciones y
  biblioteca.
- Los botones flotantes de exportar y seguimiento forman un grupo horizontal
  con separacion uniforme y margen respecto al borde inferior de la waveform.
  El editor de secciones se reserva para tablet y escritorio, por lo que el
  boton de editar no se muestra en telefono.
- En el overlay de telefono se oculta el resumen de duracion total, cantidad de
  canciones y BPM promedio, de modo que la lista comienza inmediatamente bajo
  el encabezado.
- Pads, expansion vertical de waveform y minimap/navegador inferior de la
  waveform se ocultan en telefono. Tambien se oculta la regla de compases
  superior; la onda principal usa todo ese alto liberado.
- La waveform de telefono tampoco usa tarjeta, borde ni padding externo: la
  onda ocupa directamente toda su region segura.
- En telefono, un arrastre horizontal sobre la onda desplaza su contenido en la
  misma direccion del dedo;
  un toque conserva el seek y un gesto de pinza amplia o reduce la escala con
  el punto de contacto como ancla. La edicion de secciones y el mapa de tempo
  no estan disponibles en telefono.
- El mixer de telefono no dibuja una tarjeta o division propia: sus canales
  comienzan directamente debajo de la waveform, sin relleno superior ni marco.
- Los channel strips forman una sola fila, ajustan su altura al mixer y se
  recorren horizontalmente mediante drag; nunca se envuelven en filas nuevas.
- Android e iPhone calculan la misma escala proporcional del strip a partir
  del ancho disponible. No se amplian verticalmente de forma independiente;
  los canales adicionales permanecen accesibles con desplazamiento horizontal.
- En iPad, que conserva el layout de tablet/escritorio, la escala minima del
  strip es `1.0x` para mantener knobs, medidores y titulos legibles.
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
- Los timers visuales conservan su frecuencia: waveform a `30 Hz`, medidores
  VU a `8 Hz` y parpadeo de MIDI Learn a `12 Hz`. La aceleracion grafica no
  modifica el transporte ni la frecuencia del callback de audio.
- Editor, top bar y waveform usan fondos planos en Android. La waveform es
  opaca y su cache de playback usa resolucion logica con overscan `1.25x`.
  La cache se reutiliza con follow encendido o apagado; no se transforma una
  segunda mascara para realzar progresivamente la porcion reproducida.
- En una ventana fija, el transporte invalida solamente la franja entre las
  posiciones anterior y nueva del playhead. Si follow mueve la ventana,
  `setVisibleWindow` solicita un repaint completo para desplazar tambien la
  onda y sus secciones. El overscan reduce reconstrucciones de la cache, pero
  no sustituye ese repaint cuando cambia la ventana visible.
- Los cambios entre reproduccion y pausa/stop invalidan toda la waveform,
  porque alternan entre su imagen de playback y el dibujo detallado. Despues
  de esa transicion vuelven a usarse las invalidaciones parciales del playhead.

Mediciones y limites de la verificacion:
[`perf/analysis_20260915/INFORME.md`](../../../perf/analysis_20260915/INFORME.md).

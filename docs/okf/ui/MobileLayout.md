---
type: Technical Contract
title: Mobile Layout
description: Contrato responsive y de area segura para telefonos y tablets iOS y Android
resource: src/PluginEditor.cpp
timestamp: 2026-10-12T00:00:00-03:00
---

# Mobile Layout

La vista de performance en telefonos es horizontal. En iPhone y Android,
el runtime habilita `upright` para Biblioteca, setlists, Configuración, Teams y Ensayo mientras su panel
esta abierto y restaura ambas orientaciones landscape al volver a performance.
En iPhone, el acceso (bienvenida, correo, recuperación y renovación de sesión)
también usa `upright`. La máscara inicial es portrait para presentar el login;
una sesión guardada válida restaura landscape al abrir la interfaz principal.
En iPhone, el plist admite
tambien `Portrait`; en Android, `screenOrientation=landscape` define el inicio
y JUCE usa `Activity.setRequestedOrientation` para cambiarla durante la sesion.
El manifest maneja `orientation|screenSize` mediante `configChanges`, sin
recrear la actividad al girar. Las tablets Android conservan la vista horizontal.

En iPad, el plist y el runtime admiten ambas orientaciones verticales y ambas
horizontales desde antes de crear la ventana. La familia nativa conserva el
perfil tablet al girar: una ventana alta no activa el perfil de teléfono ni
sus restricciones de edición o de presentación. UIKit determina la orientación
y el tamaño efectivo de la escena; el editor distribuye sus controles dentro
de esa geometría y del área segura vigente, sin imponer la proporción horizontal
anterior. `UIRequiresFullScreen=false` permite que iPadOS cambie el tamaño de
la escena además de rotarla. El layout responde a la superficie que UIKit
entrega, también cuando la ventana no ocupa toda la pantalla. Esto requiere
validar el resize y la orientación reales; no acredita por sí solo todos los
flujos de multitarea de Split View o Stage Manager.

En iPad se conserva la distribución habitual de tablet/escritorio en ambas
orientaciones. `resized()` recibe el tamaño efectivo de la escena y aplica
las reglas existentes de TopBar, setlist, mixer y Pads. No se introduce una
cabecera de varias filas, un tirador distinto ni se oculta automáticamente
el setlist al girar. Su columna, el drawer y el estado de los paneles siguen
las mismas condiciones que antes de habilitar portrait. Girar o redimensionar
no carga otra canción ni cambia transporte, mezcla, ruteo o el estado musical
de las secciones.
La ventana Android entra en fullscreen despues de asignar contenido y limites
iniciales: cambiar sus limites despues desactiva el fullscreen del peer nativo.
El contenido no redimensiona la ventana. Al cambiar el display se reafirma
fullscreen y el layout nativo `MATCH_PARENT` propaga la nueva medida a la
ventana, la raiz OpenGL y el editor, incluida el area segura actualizada.
Los cambios de area segura se propagan tambien cuando el tamano de la ventana
y del contenido permanece igual. Asi se retira el margen superior al ocultar
la barra del sistema, sin esperar a otra rotacion o redimension.

En teléfonos, el botón de tres puntos horizontales del `TopBar` incluye
`Ensayo`. Abre `PracticePanel` como superficie completa y solicita portrait;
al cerrar restaura las orientaciones landscape de performance. La vista usa
los insets seguros reales, muestra inicialmente una ventana de 12 segundos y usa
`PlaybackSession` para posición, Play/Pausa y Stop. El encabezado de la tarjeta
muestra `PracticeInstrumentSelector`, que reutiliza la rueda horizontal real de
Tono/Tempo. Elegir un instrumento cambia el nombre y la onda individual; con una escucha de ensayo activa, también retargetea esa
escucha al nuevo instrumento. La onda individual se decodifica en background en
tiempo fuente y se proyecta al tiempo del arreglo mediante una copia inmutable
del `SectionScheduler` real. Las copias reutilizan los picos del mismo rango
fuente y desplazan correctamente el audio posterior; no duplican archivos ni
normalizan cada sección por separado. En Cliente se muestran los instrumentos del servidor,
pero el protocolo remoto actual solo transporta la onda mezclada, así que la
onda individual indica su falta de disponibilidad. Play/Pausa y Stop no se
envían cuando la conexión está caída.

La onda y la regla ocupan todo el ancho de la tarjeta. `getWaveformLayout()`
comparte esos límites entre el dibujo, las marcas A–B, el cursor y la conversión
de píxeles a segundos del arrastre; el zoom conserva la misma correspondencia.
Nombre del instrumento, ayudas y tiempos mantienen 18 puntos de margen lateral.
Un marco interior redondeado aplica degradados de 9 puntos sobre la onda, con
un borde fino y luz inferior discreta. No usa desenfoque ni crea imágenes por
frame. Cursor y marcas se dibujan sobre la sombra; las etiquetas A/B se mantienen
dentro de la tarjeta sin desplazar las líneas de tiempo que representan.

Ensayo reutiliza los controles de la aplicación: `TransportIconButton`,
definido en `UiCommon` y usado también por el alias `TopBar::IconButton`,
presenta Play/Pausa y Stop junto a A, B, Repetir y Borrar región en una única
fila al pie de la vista. Play/Pausa y Stop miden 56 × 56 puntos, o 44 × 44 en
teléfonos estrechos; los otros cuatro conservan 44 × 44. Cerrar usa
`HeaderIconButton`, con una X circular de 44 × 44 puntos en
el encabezado. `Ensayo` y el nombre de la canción comparten una línea base
en la cabecera de 64 puntos, separados por 12 puntos. El nombre usa color
secundario y elipsis cuando no cabe, sin comprimir su fuente ni invadir
los 12 puntos de separación con Cerrar.
La fila anterior al transporte queda fuera de la tarjeta de onda y conserva
sus 44 puntos de alto. Contiene dos botones de igual ancho, `Guía` y
`Metrónomo`, separados por 12 puntos. Toda la superficie conmuta; no son
switches pequeños. Encendido usa un fondo de acento tenue, marca de verificación
y etiqueta en una sola línea `Guía • ON` o `Metrónomo • ON`; apagado usa fondo
oscuro, borde discreto, signo menos y la misma etiqueta con `OFF`. Se conservan
los bounds exteriores, la tipografía base de 12 puntos y el alto de 44 puntos;
el ajuste horizontal del texto evita recortar Metrónomo en teléfonos estrechos.
El estado nativo accesible también expone checked, la etiqueta,
la disponibilidad y el estado pendiente. Tab y Espacio activan los botones.

`Guía` silencia/desilencia la fuente ya elegida, original o dinámica; nunca
cambia esa selección. Si no existe una fuente fiable/disponible, el botón queda
deshabilitado con motivo accesible. `Metrónomo` controla Click Dyn: encenderlo
habilita su motor/divisiones y quita mute; conserva la máscara de divisiones
existente o recupera negras si estaba vacía. No modifica click original, Solo,
faders, routing ni escucha de ensayo. Ambos controles permanecen independientes
de cambiar el instrumento enfocado.

Los botones muestran siempre el estado confirmado por la sesión. Durante un
comando asíncrono conservan ese estado `ON`/`OFF` en la misma línea; la descripción
accesible y el tooltip indican `Confirmando cambio`. El botón bloquea otros
toques hasta confirmación o un timeout de 2 segundos. Cambiar fuente,
canción, modo local/remoto o disponibilidad cancela la intención pendiente. Un
toque llegado antes del siguiente tick no se aplica a una fuente que cambió.
El timeout avisa sin afirmar éxito ni invertir el estado de la fuente.

No hay fila permanente de estado de mezcla ni botón grande `Restaurar mezcla`.
Los avisos A–B y errores sustituyen temporalmente las pistas de doble toque en
los 30 puntos ya reservados arriba de la regla; no cambian tamaño de onda, tiempos
ni transporte. Una selección A–B conserva allí su estado hasta borrarla. Al
terminar el aviso/selección vuelven las pistas de doble toque.

Sobre la tarjeta de onda, tono y tempo comparten una fila en dos columnas con
12 puntos de separación. Los títulos `Tono` (`Key` en inglés) y `Tempo` se
dibujan encima de cada selector, en tipografía Medium de 12 puntos y color
secundario, alineados a la izquierda con 12 puntos de margen. Cada título
ocupa 18 puntos de alto y deja 4 puntos hasta el control de 44 puntos.
`PracticePitchSelector` presenta el tono como una
rueda horizontal de notas cromáticas: el centro queda resaltado y los extremos
se desvanecen hacia el fondo. Mide 44 puntos de alto y al menos 136 de ancho
en teléfonos de al menos 320 puntos. Cada nota ocupa un paso de 44 puntos.
Arrastrar hacia la izquierda sube el tono y hacia la derecha lo baja; durante
el gesto solo cambia la previsualización. Al soltar se confirma una única
transposición entera y la rueda se centra suavemente en 160 ms, sin inercia que
seleccione notas adicionales. Tocar una nota lateral la selecciona; un doble
toque restaura el tono original. En escritorio admite flechas, Home para
restaurar y rueda del ratón; conserva la semántica accesible de un Slider.

El nombre parte de `PlaybackSession::getLoadedSongKey()` y reutiliza el índice
y las notas de `PadsEngine`, con sostenidos y sufijo menor cuando corresponde.
Si falta la tonalidad, presenta el desplazamiento en semitonos. Las ruedas no
muestran leyendas inferiores: un relleno azul grisáceo acompaña a la nota o al
BPM original; la selección central usa el relleno de acento del tema y énfasis
tipográfico. Si coinciden, prevalece el relleno de selección.
En tono, el relleno identifica el original exacto (0 semitonos), no sus octavas
dentro del rango de −12..+12. Los nombres y ayudas siguen disponibles para accesibilidad.
El valor optimista se conserva hasta la confirmación del servidor o timeout;
un cambio de canción, modo o conexión cancela un arrastre pendiente. Usa la misma
transposición y persistencia por canción y setlist que el `TopBar`.

`PracticeTempoSelector`, a la derecha, reutiliza `PracticeWheelSelector` con los
mismos gestos, centrado y degradados del tono. Cada paso cambia 1 BPM y solo
confirma el cambio al soltar. Conserva valores fraccionarios recibidos del motor
y restaura el tempo original con doble toque o Home. Respeta los límites del
motor (75 %–200 % del BPM original, dentro del rango admitido de 20–300 BPM)
y usa `PlaybackSession::setPlaybackTempoBpm`, incluida su ruta remota y la
persistencia local por setlist. Cambiar tempo altera la velocidad de reproducción;
la onda y la grilla mantienen las posiciones del arreglo, con el mapa fuente
de cada copia, sin estirar sus tiempos al cambiar BPM.

El selector de instrumentos permanece bajo el handle de la tarjeta, con 18
puntos de margen lateral y 44 puntos de alto. Comparte `PracticeWheelSelector`
con tono y tempo: selección central resaltada, vecinos visibles, degradados y
centrado de 160 ms. Cada casilla usa 104–180 puntos según el ancho disponible,
con dos líneas y elipsis para nombres largos sin compresión horizontal; el nombre
completo se conserva en la interfaz accesible del Slider. Los otros selectores
conservan sus pasos de 44 puntos. Instrumentos no tiene un valor musical original:
no dibuja el segundo relleno ni restaura otro instrumento al hacer doble toque.
Flechas y rueda recorren la lista; Home/End eligen sus extremos.

Arrastrar el carrusel solo previsualiza y confirma una vez al soltar; tocar un
vecino selecciona el elemento visible. La selección usa el ID estable del stem,
incluso si la lista se reordena antes del siguiente sondeo. Al quitar el seleccionado
se muestra el primer disponible; una lista vacía deshabilita el control.
Cambios de canción, contexto, lista, tamaño o conexión cancelan la selección
pendiente. Sus gestos horizontales y toques no producen scrub, seek, salto de compás
ni menú. Los verticales y el pinch siguen usando el arbitraje de la tarjeta,
con un solo dueño por gesto y sin acción tardía al levantar el último dedo.
Se mantienen los callbacks existentes de onda individual y retarget de escucha;
no se modifica el motor de audio, los faders ni las ganancias de ensayo.

En la fila inferior, A marca el inicio y B cierra la región e inicia la repetición.
Ambos se ajustan al pulso musical más cercano dentro de Inicio/Fin; un empate
elige el pulso posterior. El ajuste respeta el mapa de tempo y el denominador de
la métrica, independientemente del zoom o de cuántas marcas sean visibles.
A debe dejar al menos un pulso válido posterior para B que respete la duración
mínima del motor; B permanece deshabilitado si su pulso coincide con A o queda
demasiado cerca. Repetir desactiva el loop conservando las marcas. Borrar región
(papelera) elimina ambas marcas y libera el loop o la preparación, incluso si
solo se marcó A. No modifica la posición ni Play/Pausa, no borra un loop ajeno,
y evita reconstruir una región eliminada desde un snapshot remoto atrasado.
La selección A–B es temporal: cerrar Ensayo libera el loop si la vista conserva
su propiedad.

Pellizcar la tarjeta modifica solo la escala visual, manteniendo el cursor
central como referencia; no busca otra posición ni cambia Play/Pausa. Abrir los
dedos amplía y cerrarlos muestra más tiempo. `PracticeViewport` limita la
ventana a 2–48 segundos, sin superar la duración de la canción; una canción
de menos de 2 segundos conserva su duración completa. El zoom se mantiene al
cambiar de instrumento y vuelve a 12 segundos al cambiar de canción o contexto
local/remoto. En escritorio, la magnificación del trackpad usa el mismo factor
de escala. Dos dedos cancelan el gesto simple en curso y sus ayudas pendientes;
el pinch sigue consumiendo los eventos hasta que se levantan todos los dedos,
para que el último dedo no dispare un swipe, un salto o el selector.

Una regla discreta de 24 puntos dentro de la tarjeta muestra números de compás
y marcas secundarias de pulsos cuando caben. `PracticeTimelineRuler` usa el
mapa de tempo vigente de `WaveformBar`, incluidas las ediciones sin guardar,
y conserva la fase musical al cambiar el BPM dentro de un compás. Un cambio
de métrica inicia un compás nuevo. La duración usa el denominador real de la
métrica; por ejemplo, 7/8 equivale a 3.5 negras por compás. Las etiquetas se
separan al menos 72 puntos y los pulsos requieren al menos 10 puntos de espacio,
sin subdivisiones finas ni snap al recorrer la onda. Las marcas se calculan en
tiempo fuente y no cambian al alterar la velocidad de reproducción.

`TimelineRulerStyle` comparte el dibujo de esta regla con la onda principal
de escritorio y tablets iOS/Android: números encima de marcas cortas,
tipografía de 10 puntos, color secundario y altura de 24 puntos. En esas vistas
se reutiliza también el proveedor del mapa musical de Ensayo; el cursor y la
marca de inserción se dibujan después. Las etiquetas de los extremos quedan
dentro del área visible para evitar números recortados.

Los gestos verticales empiezan en cualquier punto de la tarjeta de onda,
incluido encabezado, cuerpo y footer. Un recorrido de al menos 44 puntos con
predominio vertical (1.2 veces el horizontal) activa una sola acción por gesto:

- Los tres niveles, de abajo hacia arriba, son instrumento reducido,
  mezcla normal y enfoque. Cada swipe sube o baja exactamente un nivel.
- Desde mezcla normal, arriba enfoca: seleccionado +2 dB y otros −12 dB.
  Abajo reduce el seleccionado a −12 dB y mantiene los otros en su nivel normal.
- Desde enfoque, abajo restaura la mezcla normal; desde reducido, arriba
  restaura la mezcla normal. Otro gesto permite seguir al nivel opuesto.
- Arriba estando enfocado y abajo estando reducido no envían comandos.
  Un gesto largo o que cambia de dirección no salta más de un nivel.
- Clic y guía conservan siempre su nivel configurado.

Es una ganancia temporal suavizada durante 200 ms, independiente de faders,
trim, Solo y Mute; los niveles no acumulan ganancia. El footer indica
el estado actual y el siguiente gesto disponible. El marco exterior de la tarjeta
muestra un halo persistente tenue: superior y laterales hasta media altura para
Enfoque, inferior y laterales desde media altura para Reducido. Normal no tiene
halo. Solo se ilumina cuando el instrumento visible es el objetivo real de la
sesión confirmado por el servidor y hay control disponible; el preview optimista
sigue sirviendo únicamente para encadenar gestos, con ayuda `Confirmando mezcla…`
mientras llega la confirmación. Si el objetivo es otro, el footer y la ayuda
indican su nombre. No se pinta encima de la onda: los trazos exteriores se
renderizan antes de la tarjeta opaca. Es estático también con reducción de
movimiento; no hay pulsos, parpadeo, blur ni buffers de imagen por frame.

Las dos leyendas inferiores se centran respecto de toda la tarjeta. La franja
reserva 48 puntos simétricos a izquierda y derecha (44 para ayuda y 4 de aire),
independientemente de la visibilidad de `?`. El texto breve es
`↔ Recorrer · Pellizca: zoom` y el estado/siguiente gesto; el nombre completo
del instrumento se conserva en la ayuda. No cambian la tarjeta, la onda, la
regla, los tiempos, el transporte ni los controles Guía/Metrónomo.

El botón `?` de 44 × 44 abre el recorrido existente **Ensaya por instrumento**
(`practice-sections`), mediante un callback hacia el editor. No crea otro popover
ni duplica los controles de Ensayo. Conserva el estilo, navegación, resaltado,
scroll y progreso del sistema de aprendizaje. Peticiones repetidas mantienen el
paso actual; pausar con X/Escape devuelve el foco al botón si Ensayo sigue visible.
Reabrir reanuda el paso guardado y un curso completado vuelve al inicio. Véase
[LearningGuide](LearningGuide.md).

Para restaurar por gesto, selecciona el instrumento indicado por Enfoque o
Reducido: bajar desde Enfoque o subir desde Reducido vuelve a Normal. Hacerlo
sobre otro instrumento inicia un modo nuevo, no un reset. R conserva la
restauración directa de la mezcla incluso si está seleccionado otro instrumento,
sin seek ni cambio de Guía/Metrónomo. No hay un botón Restaurar dentro de la ayuda.
Cerrar Ensayo restaura solo si la vista sigue siendo propietaria de la escucha.
Cargar otra canción, cambiar de modo local/cliente/servidor o desconectar al cliente
propietario también la restaura. El deslizamiento hacia abajo no cambia de
instrumento. En Cliente, la intención viaja por `practice.listen` y se aplica al
audio del servidor; los gestos no envían comandos mientras la conexión está caída.

La tarjeta, fuera del carrusel de instrumentos, se divide virtualmente en dos
mitades iguales: un doble toque en la izquierda retrocede un compás y en la derecha avanza un compás. Ambos toques
deben pertenecer a la misma mitad, canción y modo local/remoto. El salto usa
el BPM original y la métrica de la canción, conserva Play/Pausa y muestra una
confirmación en la mitad tocada. Los saltos consecutivos en el mismo sentido
acumulan el contador (`−1 compás`, `−2 compases`, etc.) durante 1.5 segundos
desde el último salto. Invertir el sentido, iniciar un arrastre, detener o
cambiar canción reinicia esa cuenta; tocar un límite sin desplazamiento no
incrementa el contador. Cada mitad lleva una ayuda pequeña y secundaria para
el doble toque, fuera del cuerpo de la onda. El carrusel elige instrumentos
sin esperar el intervalo de un doble toque ni abrir un popup.

El arrastre horizontal fuera del carrusel desplaza la onda bajo un cursor central: hacia la
derecha busca audio anterior y hacia la izquierda audio posterior. Durante
el arrastre solo cambia la previsualización y pausa temporalmente el transporte
si estaba reproduciendo. La cámara sigue el dedo con un suavizado de 35 ms y
continúa con inercia proporcional a la velocidad reciente del dedo al soltar.
La posición del evento relativo al panel se conserva como `Point<float>`;
el arrastre admite desplazamientos menores que un punto sin redondear a entero.
El frenado suave dura entre 180 y 1600 ms y respeta Inicio/Fin y el loop de
sección activo, sin rebote en los límites. Alcanzar un límite acorta su duración
según la distancia restante, con un mínimo de 180 ms. Mantener el dedo quieto
al menos 100 ms antes de soltar elimina el impulso. El audio permanece pausado durante
el arrastre y la inercia; cuando la onda termina de moverse se aplica un único
seek con `PlaybackSession` y se reanuda solo si esa pausa sigue perteneciendo
al gesto. Tocar de nuevo interrumpe la inercia y conserva la propiedad de la
misma pausa para continuar recorriendo la onda.

`PracticeWaveform` recibe los picos fuente y un snapshot coherente del scheduler,
su sample rate, duración fuente, duración del arreglo y revisión de carga. La
proyección se ejecuta fuera del hilo de audio y divide cada intervalo en los
límites de las copias antes de consultar máximos: nunca toma como un solo rango
el salto entre dos posiciones fuente. La memoria se limita a los vectores de
picos y las secciones del snapshot, independientemente del tamaño del PCM.
Un stem más corto conserva silencio después de su final fuente, incluso cuando
se copia ese rango. El helper no aplica otra normalización.

`PracticePanel` invalida su caché por revisión del mapa/fuente, duración,
canción y local/remoto. Reordenar o recortar copias con igual duración también
invalida. Cada entrega asíncrona comprueba la identidad viva y la generación
antes de guardar o mostrar bins; una respuesta de una revisión anterior no
puede contaminar la caché al cambiar entre instrumentos. Cambiar el arreglo
mantiene la selección del instrumento. No se consultan archivos desde paint
ni desde el timer para calcular esta identidad.

`PracticeWaveformGeometry` agrupa los bins proyectados en intervalos fijos desde el tiempo
cero de la canción y conserva sus amplitudes al mover la cámara. Los puntos
se conectan con curvas cúbicas sin exceder las amplitudes adyacentes, formando
una envolvente continua y rellena. La geometría se cachea en tiles de 64
intervalos con vecinos solapados; solo los tiles visibles componen un `Path`
de unión con espacio preasignado. Ese `Path` se reutiliza hasta que entra o sale
un tile y se rellena una sola vez, evitando huecos y opacidad doble en las
uniones. Cambiar bins, duración o tamaño reconstruye la geometría con una
resolución base correspondiente a la ventana mínima de 2 segundos; desplazar
la cámara o hacer zoom solo transforma los puntos ya cacheados. El espacio
de unión admite la ventana máxima de 48 segundos sin crecer durante el gesto.
La transformación conserva posiciones subpíxel. La resolución
no supera los bins fuente ni 65536 puntos de envolvente.

`PracticePlaybackClock` presenta el movimiento durante reproducción a partir
del tiempo de cada frame y corrige gradualmente la cuantización de bloques o
paquetes, con una constante de 90 ms y una zona sin corrección de 10 ms como
base, ajustada a la velocidad. La posición visual es monótona durante playback
normal; un seek intencional, una discontinuidad nueva de al menos 0.25 segundos
(como un loop), una pausa o cambio de canción reinician esa estimación. Al
terminar la previsualización de seek se parte de su posición presentada. Si la
fuente deja de avanzar, la predicción se limita a 250 ms. Esto no cambia el
reloj ni la reproducción de audio.

En plataformas distintas de Android, `VBlankAttachment` actualiza la animación
con los frames de pantalla, siguiendo el mecanismo de la onda principal. Si
no llega un callback durante más de 100 ms, el timer de 30 Hz sirve de respaldo.
Android usa el timer de 60 Hz. Los repaints de movimiento se limitan a la
tarjeta de onda.

Se fija el eje dominante tras 12 puntos de movimiento (relación 1.2)
para que un gesto horizontal no cambie el volumen. Cambiar canción o modo,
o perder la conexión antes de terminar, cancela el salto. Tanto en Local como
en Cliente se conserva provisionalmente el destino mostrado mientras el
transporte confirma la nueva posición: un mínimo de 120 ms y dos lecturas
consecutivas consistentes antes de liberar esa previsualización, con un timeout
de respaldo de 1.5 segundos. El desfase residual converge suavemente a la
posición autoritativa para evitar mostrar
destino, posición anterior y destino entre bloques. `finishScrub` devuelve
`bool`: en Cliente confirma únicamente el envío a la cola, y un `ack` de rechazo
se recibe después mediante `scrubFinishRejected`. Un fallo de envío o un rechazo
descartan la previsualización; la vista no recupera una pausa que ya no posee.
Cerrar la vista devuelve el estado de reproducción anterior si todavía posee
la pausa; Detener o una nueva orden de transporte tienen prioridad.

En iOS, el cambio de orientaciones permitidas se difiere fuera de `resized()`
y agrupa solicitudes usando el estado más reciente del acceso y los paneles. La ventana
standalone sincroniza su geometria con `UIWindowScene.coordinateSpace` al
terminar la transicion nativa y al volver del background. UIKit es la autoridad
del tamaño y la orientación de la escena, también en iPad vertical. Propaga el tamano al
contenedor y al editor aun si los limites de la ventana no cambiaron. Una vez
adjunto, el editor obtiene su tamano del host y no del cache de displays: este
puede conservar temporalmente las dimensiones de la orientacion anterior.
La mascara de orientaciones puede fijarse antes de crear `UIWindowScene`.
Por eso, en iOS 16+ tambien se compara la orientacion efectiva de la escena
con la mascara del controlador y se solicita `requestGeometryUpdate` si no
coinciden, aunque la mascara de JUCE no haya cambiado. Se agrupan solicitudes
iguales hasta completar la transicion. En iPhone evita arrancar con la escena en
portrait y el contenido de performance recortado a un cuadrado; en iPad conserva
la orientación válida que el sistema ya eligió, en lugar de forzar landscape.

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
- En tablets iOS, el editor descuenta los cuatro `safeAreaInsets` una sola vez
  del area de performance. TopBar, waveform, setlist, mixer y Pads se colocan
  dentro de ese rectangulo, dejando libres la barra de estado y el indicador
  inferior. El drawer de Biblioteca y su scrim usan el mismo ancho y borde
  inferior seguros; empiezan debajo del limite real del TopBar. El fondo del
  editor cubre la ventana completa. La tablet no recibe insets internos ni
  prolongacion inferior del perfil de telefono.
- En iPhone, el fondo del mixer se prolonga visualmente por el inset inferior hasta el
  borde fisico de la ventana, evitando una franja vacia bajo los canales.
- En iPhone y Android cada lateral usa su inset real, actualizado al girar. El `TopBar`
  dibuja su fondo a ancho completo y coloca los botones en su area segura con
  4 pt de aire adicional. No se igualan ambos insets usando el mayor.
- La waveform de telefono llega a ambos bordes. Sus botones flotantes, mensajes,
  etiquetas y acciones de seccion e indicador remoto permanecen dentro del
  area segura; el tintado de las secciones conserva el ancho completo.
  En performance, Mas ocupa el extremo derecho seguro y Seguimiento queda a
  su izquierda, para acercar la accion principal al borde sin invadir el notch.
- El viewport del mixer de telefono llega a ambos bordes. El contenido lleva
  padding desplazable inicial y final equivalente a los insets reales, de
  modo que el primer y ultimo canal pueden quedar completamente accesibles.
  La escala de los strips se calcula con el ancho seguro y los canales se
  desplazan por debajo de las zonas laterales durante el scroll.
  El fondo y la banda inferior del primer y ultimo canal se prolongan por ese
  padding lateral para evitar una franja vacia. Esta prolongacion es decorativa:
  no replica controles ni cambia sus posiciones o el recorrido del scroll.
  El borde izquierdo real de Master se dibuja dentro del strip con un trazo
  de un punto en `cardBorder`, también sobre su banda inferior. Delimita el
  canal frente al padding decorativo sin agregar otra franja ni mover controles.
- En Android se respetan los insets reportados por el sistema para los controles,
  incluyendo cutouts y barras visibles. No se recorta toda la superficie por el
  inset lateral: waveform y mixer se extienden bajo esa zona, y el fondo de la
  barra superior cubre tambien su inset superior si hay una barra visible.
- En telefonos, los fondos y superficies desplazables se dibujan edge-to-edge; las acciones
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
- Un cambio de teclado, área segura o tamaño reajusta Biblioteca y su
  asistente sin ocultarlos temporalmente ni vaciar sus límites mientras el
  drawer esté abierto. Los mismos campos mantienen foco, texto, selección y
  caret durante el reflow. Ocultar y volver a mostrar su padre en un mismo
  `layoutMainUi` también descarta el foco en JUCE y puede cerrar/reabrir el IME
  de Android; no se fuerza el foco ni se reabre el teclado para compensarlo.
  Cerrar Biblioteca sí retira su superficie y finaliza la edición habitual.
### Estilo compartido de selección

`applyTheme` instala `SelectionLookAndFeel` como estilo global de combos y menús
JUCE. Lo usan los `AdaptiveComboBox`, los combos estándar del panel de audio de
escritorio y los `PopupMenu` independientes, incluido Instrumentos en Ensayo.
`InputControlLookAndFeel` extiende esa base y conserva la personalización de
editores de texto únicamente en los campos que llaman `applyTextEditorTheme`.

- Campo redondeado con relleno de tarjeta, borde fino, chevrón secundario y
  borde de acento al enfocarlo o abrirlo. Tipografía Medium de 17 pt en móvil y
  15 pt en escritorio, adaptada a campos compactos sin cambiar sus dimensiones.
- Lista en tarjeta redondeada de 12 pt, borde sutil y margen interior de 6 pt.
  Filas de al menos 44 pt en móvil y 32 pt en escritorio, selección con relleno
  de acento suave y check a la derecha. La selección se distingue del hover.
- Los títulos de grupo usan 13/12 pt y altura de 32/28 pt; los textos largos
  llevan elipsis. Se conservan iconos, atajos, submenús, separadores y estados
  deshabilitados. Los combos abren una columna, con ancho mínimo igual al campo
  y la opción actual visible. Las listas largas conservan el scroll de JUCE.
- El tema se actualiza tanto para combos explícitos como heredados. Las esquinas
  del popup son transparentes cuando la plataforma lo permite; en otros peers
  se usa el mismo fondo para evitar esquinas blancas.
- El dibujo no cambia callbacks, selección, accesibilidad ni gestión de foco.
  Teclado, Escape y confirmación siguen a cargo de JUCE. Los selectores de
  archivos del sistema conservan su apariencia nativa.

### Interacción de formularios móviles

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
- En iOS y Android, Configuración abre en el mismo overlay del editor. En
  teléfono ocupa toda la pantalla y gira a vertical; al cerrar recupera la
  orientación de la vista que permanece abierta. El fondo cubre la ventana,
  mientras el encabezado y contenido respetan los insets seguros y del teclado.
  En tablet permanece como panel centrado.
- La portada móvil agrupa filas de 72 pt con icono, título, descripción y flecha:
  Personalización (Apariencia e idioma, Reproducción), Audio y
  conexiones (Ruteo de salidas, Control remoto), Cuenta y
  soporte (Mi cuenta, Diagnóstico). Cada fila navega a los controles existentes;
  volver recupera el desplazamiento de la portada. Un drag sobre una fila no
  navega. Encabezado, volver y cerrar permanecen fijos. Escape vuelve a la
  portada desde el detalle y cierra desde la portada. El botón Atrás de Android
  usa el mismo recorrido. Los atajos de reproducción del editor no se ejecutan
  mientras Configuración ocupa el frente.
- Las preferencias se apilan con selectores de 48 pt. Control remoto conserva
  el selector Local/Cliente/Servidor, descubrimiento y alias en su propia página;
  Local compacta la tarjeta al estado; Cliente y Servidor muestran el campo
  correspondiente. Las actualizaciones de conexión no muestran esos controles
  en otras páginas.
  Mi cuenta apila cuotas y acciones en anchos estrechos. En escritorio, la app
  inicia en una ventana normal que ocupa el área de trabajo disponible; no usa
  pantalla completa ni ofrece una preferencia para cambiar este inicio.
- En los detalles de Configuración, Volver y Cerrar usan los mismos botones
  circulares del encabezado de Biblioteca y setlists, con objetivos táctiles
  de al menos 44 pt/dp. El contenido comienza inmediatamente bajo el encabezado.
  La jerarquía tipográfica móvil usa título de 20 pt semibold, etiquetas de
  15 pt medium y ayudas de 13.5 pt regular en color secundario. Cada ayuda queda
  dentro de su tarjeta, entre 4 y 6 pt bajo el control correspondiente, con
  12 pt antes de la siguiente opción. En escritorio las etiquetas y ayudas usan
  14 y 12.5 pt respectivamente. Apariencia, Reproducción y Control remoto
  explican cada selector junto a él. Ruteo explica
  el dispositivo y la asignación de canales. Mi cuenta explica las acciones,
  incluida la purga que vacía la
  Biblioteca local sin borrar proyectos `.pwproj` ni contenido en la nube.
  Diagnóstico conserva sus notas sobre el informe y su privacidad. No se ofrecen
  opciones de importación de Prime ni de optimización de stems. La importación
  de archivos compatibles optimiza el almacenamiento automáticamente cuando
  puede hacerlo y conserva los originales si la conversión no está disponible.
  Al retirar ambas opciones, se elimina la página de Biblioteca de Configuración.
- El escritorio ancho conserva sus pestañas. Las ventanas de menos de 760 px
  también usan la portada agrupada para evitar pestañas comprimidas.
- Configuración usa desplazamiento vertical con rueda/trackpad y también admite
  arrastrar el contenido con clic o toque sostenido. No expone desplazamiento
  horizontal en el contenedor principal. Ruteo participa del mismo viewport,
  evitando que un contenedor anidado capture gestos sin poder desplazarse.
- La altura desplazable de Ruteo se calcula con todas las categorias, el
  encabezado, la nota inferior y los margenes; no se limita a 900 px ni depende
  de que la ventana mida menos de 820 px. El ancho descuenta la barra vertical.
  En iOS y Android, teléfono y tablet muestran al inicio un selector de salida
  conectado al `AudioDeviceManager`; al cambiar de dispositivo se actualizan los
  canales disponibles y las asignaciones guardadas para ese dispositivo. El
  número de salidas disponibles se toma de los canales activos, no del total
  físico que anuncia el driver. Cambiar la selección de salidas dentro del mismo
  dispositivo también reaplica el ruteo. La tabla del dispositivo se aplica
  después de restaurar la mezcla de cada canción: Click y Cues gobiernan sus
  stems reconocidos por categoría/nombre y también Click Dyn y Guía Dyn cuando
  no hay stems equivalentes, sin restaurar posteriormente rutas antiguas del
  proyecto sobre esa tabla. La página separada Dispositivo de audio se omite en estas plataformas. En
  escritorio se conserva la página técnica actual.
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
- Configuración mantiene Diagnóstico en una página propia (pestaña en escritorio).
  Audio de escritorio contiene sólo los controles del dispositivo; el informe técnico se
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
  siempre visibles. En iPad se conserva esta misma cabecera de una fila al
  girar; el ancho disponible determina la visibilidad mediante las reglas
  responsive existentes. Las acciones adicionales permanecen disponibles en Más.
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

Probar ambas direcciones landscape en iPhone y las cuatro orientaciones en iPad,
incluida una rotación durante reproducción y el regreso desde background.
Comprobar también un cambio de tamaño de ventana, el drawer, Pads y que los
últimos controles del mixer sigan accesibles. Como mínimo, revisar estos tamaños
con sus insets vigentes:

- `667 x 375` (telefono pequeno)
- `844 x 390` (telefono regular)
- `874 x 402` (iPhone Pro actual)
- `912 x 420` y `956 x 440` (telefonos grandes)
- iPad mini: ventanas `744 x 1133` y `1133 x 744`
- iPad de 9.ª generación: ventanas `810 x 1080` y `1080 x 810`
- iPad con ventana redimensionada: comprobar los límites reales de la escena
  y las reglas responsive existentes, sin activar una distribución especial
  por orientación
- Android compacto, medio y tablet con cutout/navigation insets activos

En iPad, verificar que la cabecera conserva una fila, que el setlist mantiene
su estado y que mixer y Pads se ajustan al rectángulo disponible usando su
distribución habitual. Las comprobaciones de layout no sustituyen la revisión
de orientación, tamaño de escena, área segura, drawer y Pads en el dispositivo.

## Rendimiento en Android

- El wrapper Android compila `juce_opengl` y conecta un unico `OpenGLContext`
  a `MainWindow`, que contiene la raiz del editor y sus modales globales como
  componentes hermanos. La invitacion de la demo y las condiciones de Cloud
  quedan dentro del mismo arbol renderizado; no pueden bloquear los controles
  mientras sus pixeles quedan ocultos detras de una superficie GL del contenido.
  `AndroidRootComponent` conserva solamente la propiedad y el layout del editor.
  JUCE conserva un framebuffer y repinta sus regiones invalidadas con el renderer
  2D de OpenGL. El peer
  software anterior rasterizaba y transferia la ventana completa incluso ante
  invalidaciones pequenas, ocupando el message thread durante los gestos.
- Cada slot del mixer conserva una imagen rasterizada en memoria de CPU. El
  desplazamiento horizontal reutiliza esa imagen y los cambios de VU, volumen
  o botones invalidan solo su region. Se usa `SoftwareImageType` explicitamente
  para evitar render targets OpenGL anidados. La cache se recrea cuando cambia
  el tamano fisico del slot y se libera con el componente. El cache de texturas
  del contexto tiene un presupuesto de `32 MiB`.
- El contexto solo dibuja cuando hay invalidaciones (`setContinuousRepainting(false)`).
  No hay un bucle de render permanente mientras la UI esta quieta. La raiz de
  contenido sigue siendo opaca. `MainWindow` desconecta el contexto antes de
  detener el runtime de audio y destruir el contenido o los modales de ventana.
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

## Dibujo de la onda durante reproducción (todas las plataformas)

`WaveformBar` reutiliza su raster de playback también con Follow apagado.
El avance del transporte no vuelve a muestrear todos los picos ni a mapear cada
columna del arreglo en cada cuadro. Se conservan la máscara de brillo de la
porción reproducida en escritorio/iOS y el raster sin máscara de Android.
Los cambios de bins, arreglo, tamaño, tema o escala del viewport invalidan la
caché; mover Follow conserva el overscan mientras cubra la nueva ventana.
Los límites de reproducción se componen después de la onda y su máscara,
conservando el oscurecimiento de las partes excluidas también con caché.
El minimapa compone estos límites fuera de su raster, para que no conserve
un sombreado anterior al cambiar o quitar un recorte.

Con viewport fijo se invalidan las franjas entre la posición anterior y nueva
en las tres superficies: onda, regla y minimapa. Incluyen los márgenes de la
aguja y el tramo cuyo brillo cambia, también al retroceder o cruzar un loop.
Los saltos pendientes, la carga, la edición y las transiciones Play/Pausa/Stop
conservan el repaint completo. Follow sigue invalidando toda la vista cuando
`setVisibleWindow` desplaza la cámara. Los cambios de Preparing, salto pendiente
y Loop recibidos por remoto/MIDI invalidan también las secciones fuera de las
franjas. El pulso de automatización conserva su invalidación específica de
sección y toolbar.

El dibujo detallado limita el muestreo a las columnas incluidas en el clip,
sin alterar su fase de dos píxeles. Las secciones fuera del clip omiten el
trabajo de dibujo y texto. Se consulta la región exacta del clip, también si
contiene varias franjas separadas, para no confundirla con su rectángulo
envolvente; su geometría completa sigue disponible para hit testing. Esto no modifica el transporte, los buffers ni el callback de audio.

## Controles integrados de secciones y navegación de la onda

La vista Onda normal no muestra Ir, Loop ni Mute en ninguna plataforma.
No reserva objetivos táctiles, foco, nodos accesibles ni menús de acciones de
reproducción por sección. Estas acciones pertenecen únicamente a Secciones.
Los anchos y límites de Onda siguen siendo proporcionales al tiempo.

En iOS y Android, tanto teléfonos como tablets, arrastrar la onda en reproducción
sólo desplaza su viewport. El toque sobre el fondo mueve la posición al soltar;
en Secciones, los controles se ejecutan al soltar y no buscan la posición subyacente.
Superar 6 puntos de movimiento, soltar fuera del destino, cambiar de objetivo,
utilizar otro dedo o iniciar zoom cancela la acción pendiente. Volver al punto
inicial no convierte un arrastre en toque. Pinch conserva su ancla; el gesto no
produce saltos al levantar los dedos. El teléfono conserva su inercia de pan.
Cambiar canción, modo, borrador, tamaño o visibilidad cancela gestos pendientes.

Las tablets móviles no muestran ni reservan espacio para el minimapa. Escritorio
conserva el minimapa y la regla con su navegación y seek de mouse actuales.
Los gestos estructurales del editor arrange y el bloqueo de seek durante un
borrador de automatización permanecen vigentes. Ir reutiliza la solicitud de
salto existente, con su temporización pendiente actual; Loop reutiliza su
callback y Mute silencia instrumentos sin detener el click. No se modifica el
contrato del motor de audio.

### Vista de secciones

El menú principal de tres puntos contiene un único ítem activable `Vista de
secciones`, con el mismo patrón de acción que MIDI. Marcado muestra secciones;
desmarcado vuelve a la onda normal. La marca visual y el estado accesible se
sincronizan al abrir/reabrir el menú y con cambios de vista externos. Se retiró
el selector grande de la barra para recuperar espacio del título y controles.
La fila conserva 44 puntos de alto. No se añade persistencia entre sesiones.
No se duplica la acción en el menú de onda; ese menú conserva sus funciones en
normal y se oculta en Secciones sin reservar una cabecera vacía.
Es una presentación optativa, independiente del
editor arrange y de la automatización. No se activa al cargar ni cambia el
estado musical al alternar. Conserva el viewport y el ajuste de seguimiento
de la vista normal.

Secciones presenta una fila manual de bloques de al menos 176 puntos con Ir, Loop
y Mute persistentes de 44 × 44 puntos. El ancho se reparte entre un número
entero de tarjetas, con 12 puntos entre ellas. En teléfono, la fila, el color
y la onda de las secciones ocupan todo el ancho físico, igual que la onda normal,
sin bandas laterales ni padding exterior. Sólo las etiquetas y acciones se
limitan a la intersección de la tarjeta con el área segura; el grupo Ir/Loop/Mute
se centra en ese espacio. El ancho mínimo también reserva espacio para los tres
objetivos junto al mayor inset lateral. Cambiar los insets al rotar actualiza
esta distribución aunque el tamaño del componente no cambie. Escritorio y
tablet conservan su fila dentro del área segura. Al soltar un arrastre, la fila se
ajusta a la tarjeta más cercana. El trackpad conserva el desplazamiento continuo
y ajusta la fila tras 150 ms sin eventos; la rueda discreta avanza una tarjeta
por paso. Así no quedan títulos ni acciones
cortados en los extremos cuando la fila está detenida, incluidas ambas
orientaciones landscape con sus insets. Conserva la paleta de sección con una capa de
color de transparencia moderada sobre la onda real de su rango de audio. Las
copias usan su rango fuente explícito; las originales descuentan las inserciones
anteriores mediante `ArrangementSourceMapping`, con sesgo izquierdo en el
final del rango. Esto permite dibujar originales desplazadas más allá de la
duración fuente sin colapsar su onda. No se genera una señal de adorno. El progreso se
distingue mediante un relleno más claro y su indicador superior. El ancho
visual no representa ni modifica la duración musical.
El llenado interior usa la posición confirmada del transporte relativa al
rango real de la sección: vuelve a cero al repetir y cambia de bloque tras el
salto confirmado. El destino pendiente se marca en Ir, el loop en Loop y el
mute en Mute; no se confunden con la sección activa.

Secciones reserva un área de título centrada en el bloque completo,
por encima de los controles y separada de los indicadores. La fuente semibold
se adapta al ancho y alto (14–30 puntos en alturas habituales); nombres largos
usan hasta dos líneas y elipsis sin compresión horizontal ilegible. El padding
se calcula con el tamaño real del bloque. Scroll e inercia no cambian fuente,
anchura ni saltos de línea: el viewport sólo recorta el título al salir de pantalla,
sin recentrarlo ni encogerlo. La fila de acciones se reserva aunque el recorte
oculte sus botones; sólo cambiar el tamaño real recalcula el layout.
El nombre blanco con sombra y degradado de contraste se dibuja después de
Mute/progreso/pulso; Mute no atenúa sus glifos. En alturas extraordinariamente
bajas se reduce la fuente para no invadir los objetivos táctiles existentes.

El estado usa 14 puntos Medium, con una o dos líneas según la altura real,
sin compresión horizontal. Reserva su espacio incluso cuando no hay estado;
activarse, pausarse o pasar por el recorte del viewport nunca cambia el tamaño
ni el layout del título. `Sonando` depende de transporte reproduciendo;
`Pausada` identifica la sección bajo el cursor con transporte detenido/pausado.
`Preparando` permanece distinto de `Salto programado`, que requiere destino
pendiente confirmado. Loop futuro se presenta como `Loop armado`. Ir conserva
su etiqueta breve; Preparando no se duplica ni se comprime dentro del botón.
Ir/Loop/Mute usan etiquetas de 13 puntos. La onda y el relleno de progreso
ceden intensidad y una sombra gradual compartida mejora la lectura de las
acciones, sin cajas individuales ni cambios en sus blancos de 44 × 44.

Cada acción expone un nodo accesible con nombre/ordinal de sección, estado y
ayuda contextual. Loop y Mute son toggles con estado real; Mute explica que
el click continúa y la ayuda de Ir indica cancelación cuando corresponde.
Tab recorre acciones; izquierda/derecha recorren secciones, arriba/abajo
recorren acciones, Home/End van a los extremos, Return/Espacio ejecutan la
acción. Navegar o enfocar sólo revela la sección y no manda comandos musicales.
Foco de teclado muestra un borde temporal y suspende el centrado mientras se
opera. Acciones fuera de vista siguen accesibles y se revelan al enfocarse.
Los proxies de puntero delegan down/drag/up/pinch/rueda al arbitraje existente;
no añaden un click al soltar tras arrastrar ni cambian el segundo toque.

La onda normal de escritorio conserva regla, mouse y ancho proporcional, sin
Ir/Loop/Mute integrados ni elipsis por sección o compartida. Secciones mantiene
sus acciones en todas las plataformas.

Secciones prioriza el destino real de Ir pendiente para su centrado; mientras
existe no vuelve a la sección que suena por el timeout de exploración. Cambiar
destino actualiza el objetivo; cancelar o ejecutar retoma el bloque activo.
La navegación manual conserva la pausa de 3 segundos antes de volver al destino.
Sin Ir pendiente, centra la sección activa una vez al cambiar de sección durante la
reproducción, con transición suave de 280 ms y límites en los extremos.
La interacción interrumpe el centrado inmediatamente; durante el gesto y los
3 segundos posteriores al final del movimiento no compite con la exploración. Tras la inactividad vuelve
a centrar la sección activa. Dentro de cada sección la fila queda estática,
incluido el loop; sólo cambia su progreso. Este seguimiento es independiente
del follow de la onda normal y nunca envía seek ni comandos de transporte.
Drag y rueda sólo cambian su desplazamiento manual. Al soltar drag, la velocidad
filtrada (40 ms, máximo 2400 puntos/s) continúa con fricción exponencial de
6/s hasta 35 puntos/s y alineación suave de 180 ms. La integración usa tiempo
transcurrido, no frames; los límites se claman sin rebote. Un nuevo gesto
interrumpe la cola inmediatamente; un tap que detiene movimiento se consume
sin Ir/Loop/Mute. El seguimiento espera tres segundos desde el último movimiento
y mantiene prioridad del Ir confirmado. Rueda/trackpad smooth, incluidos eventos
inerciales nativos, se aplican directamente sin sumar otra inercia. Un nuevo
contacto descarta momentum nativo residual hasta otra secuencia de rueda; al cesar
150 ms, sólo se alinea suavemente. Rueda discreta conserva pasos por sección.
Reducir movimiento desactiva la inercia y las transiciones de alineación/centrado. No hay regla, minimapa ni
línea de seek, y el progreso no acepta interacción. Tocar el fondo no envía
comandos. Las acciones se confirman al soltar en su objetivo original; mover,
usar otro dedo, magnificar o soltar fuera cancela la acción. El transporte y
la vista normal conservan sus contratos existentes.

En escritorio, el menú principal omite MIDI Learn y Configuración si sus
botones directos están visibles, habilitados y completos dentro de la barra.
Al ocultarse por responsive reaparecen en el menú, incluso si permanece abierto;
el estado MIDI y los callbacks se conservan. Aprender PlayWorship siempre
permanece disponible. Teléfono/tablet conservan sus entradas actuales.
En teléfonos especialmente estrechos, el tiempo cede espacio al toggle y su
acción `Ventana de canción` permanece accesible desde ese menú. Los objetivos
del topbar siguen las dimensiones del perfil de dispositivo.

Los refrescos de layout sin cambios de geometría y los nuevos bins de preview
de la misma canción conservan una pulsación Secciones pendiente. Cambiar canción,
arrangement, tamaño o insets sí cancela el gesto para no activar otro destino.
Soltar un botón válido no ajusta el scroll a otra tarjeta; drag y rueda conservan
su snap. El callback de VBlank de la onda normal no modifica su viewport durante
Secciones. Ir en reproducción mantiene cola/cancelación existente y Loop mantiene
su alternancia; no se convierten en seek inmediato.

El bloque con Ir pendiente pulsa suavemente cada 1,8 segundos mediante su
relleno y borde; no atenúa el nombre ni los botones. Loop usa un borde punteado
interior distinto de progreso y pulso. Mute atenúa el fondo y la onda, dejando
el nombre y todas las acciones con su tinta habitual, incluido desmute.
Estas tres señales pueden coexistir y reflejan los estados reales; no cambian
colas, loops, mute musical ni click.

Con Reducir movimiento de macOS/iOS, el destino mantiene relleno/borde y
`Salto programado` estáticos; el pulso consulta la misma preferencia que Loop,
inercia y centrado. No se invalida periódicamente ese bloque sólo para pulsar.

El estado muestra Preparando mientras conserva el recorrido actual y Loop. Sólo al
estar listo en todos los stems se confirma Salto programado, libera Loop y espera el
próximo final válido, también hacia atrás o al revisitar una sección. El destino
no hereda Loop. Preparando aparece sólo en la línea de estado, sin pulso de cola,
sin anunciar el destino y sin reducir sus blancos táctiles.
Mute persiste hasta el desmute explícito: Ir no desmutea y Mute no cancela Ir.
Estas reglas se aplican en todas las presentaciones. Click conserva su excepción.
La representación usa el estado real del transporte para cola, Loop y progreso.

El borde de Loop en Secciones desplaza su patrón alrededor de un perímetro
continuo a 6 puntos por segundo, según tiempo transcurrido y no frames.
El patrón ajusta períodos completos al contorno para conservar continuidad en
las esquinas y la unión inicial/final. Al quitar Loop desaparece. No cambia
geometría ni hit targets y convive con progreso, pulso de Ir y Mute reales.
Con Reducir movimiento de macOS/iOS el borde permanece punteado y estático;
la preferencia se consulta en UI como máximo una vez por segundo. El proyecto
no expone actualmente una preferencia equivalente en Android.
El timer existente invalida sólo bordes de Loop cuando el bloque no cambia;
progreso/pulso invalidan sus bloques y el pan invalida la fila. Las secciones
fuera del clip se omiten en el render. No hay trabajo nuevo en audio.

### Canción en blanco sin zona de onda

Una canción cargada explícitamente como en blanco y sin stems colapsa toda
el área de WaveformBar a altura cero. No reserva padding ni altura expandida
o de toolbars. El mixer y el setlist comienzan debajo de TopBar y recuperan
ese alto; Pads conserva su drawer/popover existente. La misma distribución
común se usa en teléfono, tablet y escritorio, con sus insets habituales.
Al salir del perfil teléfono se restaura la visibilidad del contenido de Pads
según el estado abierto/cerrado guardado, sin apropiarse de un panel que siga
en su popover. No se deja su contenedor vacío por haber ocultado Pads en teléfono.

No se muestran controles, regla, cursor, compases ni texto vacío de la onda.
El selector Onda/Secciones de escritorio y la opción de Secciones del menú
táctil no se muestran; tampoco Inicio/Fin ni Ensayo en ese menú. Transporte,
Click, Pads, tono y tempo conservan sus componentes y su estado. El reloj
superior muestra sólo tiempo transcurrido.

`refreshWaveformPresentationForSong()` actualiza la presentación al cambiar
el tipo de canción local/remota y sólo pide un nuevo layout al cambiar el
estado colapsado. No toca el transporte ni recrea componentes. `layoutMainUi()`
recibe el perfil de teléfono resuelto por `resized()` y distribuye los mismos
componentes; permite comprobar esa geometría nativa en el harness Linux.
La preferencia de onda expandida se conserva para la próxima canción con audio.

La condición usa metadatos de la canción cargada: `blankSong` y `stems.empty()`.
No depende del nivel de señal, de la caché de picos, de la duración ni de cuántos
readers están disponibles durante una carga. Una canción normal silenciosa,
en carga o con audio ausente no se convierte visualmente en blanco. Al cargar
pistas, incluso para el mismo ID remoto, se restaura el área normal sin otra
interacción. Agregar stems mediante la edición existente convierte la canción
en blanco a metadatos de audio conservando su ID/proyecto. La carga y el guardado
siguen sus transacciones existentes; el layout no reinicia el motor de audio.

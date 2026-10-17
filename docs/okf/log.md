---
type: Change Log
title: OKF Knowledge Base Changelog
description: Registro de cambios en la base de conocimiento OKF
timestamp: 2026-10-05T00:00:00-03:00
---

# Changelog

## 2026-10-05

### Area segura de performance en tablets iOS

- El layout principal descuenta los insets reales del display en tablets iOS,
  evitando superponer transporte y contenido con la fecha/barra de estado y el
  indicador inferior. Biblioteca y su scrim se alinean al area segura y al borde
  inferior real del TopBar.
- Se conserva la rama de telefono, incluidos sus insets internos y extension
  visual del mixer. No se cambia el tamaño de ventana de escritorio ni el layout
  Android; tampoco los controles o dimensiones internas de los canales.
- Cambio local revisado en fuente; compilacion e inspeccion visual del simulador
  quedan pendientes para la siguiente captura.

## 2026-10-03

### Teams: nube compartida

- Nube compartida por defecto sin crear ni nombrar equipos. Administración de
  usuarios condicionada por el perfil Teams de suscripción. Los miembros solo ven y descargan canciones.
- Catálogo automático desde Cloud del propietario. Descargas ordinarias en
  Biblioteca sin publicación, invitaciones, coordinadores o setlists compartidos.
- Las canciones compartidas aparecen en Nube junto a las propias, con búsqueda,
  descarga y agregado al setlist habituales. Configuración → Teams solo aparece
  con capacidad explícita y administra usuarios, sin un segundo catálogo.
- Caché por cuenta y recuperación de audio conservan los cambios locales y la
  selección activa. Las copias y datos existentes no se borran.


### Visibilidad de indicaciones secundarias

- Fuera de edición se muestran sólo puntos sobre la onda. Las etiquetas y
  líneas aparecen al editar; el área interactiva se limita al punto cuando
  la etiqueta está oculta. Las etiquetas mantienen su apilado en edición.

### Indicaciones directas dentro de la onda

- Puntos y etiquetas siempre visibles sobre la onda; etiquetas cercanas se
  apilan y cada indicación conserva su target e identidad. La regla recupera
  su altura habitual de 24 puntos.
- Edición por arrastre y borrado con doble clic/doble toque sobre el punto,
  con Deshacer y tooltip breve. Se retira el popover con ±1 pulso.
- Breakdown/Baja intensidad y A capella se clasifican como secundarias;
  el catálogo musical queda en 14 familias y se conserva la migración segura.
- Standalone macOS Release compilado y validación local 4/4 (12,03 s).
  Capturas revisadas en `build/cue-wave-overlay-qa/`; cubren componentes reales
  de escritorio, teléfono y tooltip, sin instalación física móvil.


### Indicaciones y conteos independientes sobre la regla

- Secciones musicales conservan su estructura; las nuevas utilidades y cuentas
  son puntos de color con línea fina, detalle al tocar y agrupación de marcas
  próximas. Edición ofrece arrastre sin seek, avance/retroceso por pulso y borrar.
- Secuencias 1–4 y 1–7 usan una sola marca y offsets de la regla, incluidos
  cambios de tempo. Copia, movimiento, división, recorte y Deshacer conservan
  el vínculo y el momento de las indicaciones.
- Proyectos, sidecar, Cloud y protocolo remoto incluyen las marcas. La carga
  migra fragmentos antiguos solo cuando no pierde audio, automatización ni
  referencias. Casos ambiguos permanecen intactos.
- Reproducción y exportación programan todos los anuncios en su sample, con
  ocho voces acotadas para no cortar instrucciones cercanas. Los marcadores no
  cambian los límites de loop/salto/automatización de la sección musical.
- UI y remoto rechazan operaciones que exceden 512 anuncios expandidos, sin
  truncar documentos ni modificar parcialmente un arrange al copiar/pegar.
- macOS Standalone Release compilado y gate final 7/7 (12,91 s): creación,
  edición de onda, automatización, núcleo de audio, Pads y exportación. Revisión
  de componentes reales de escritorio, teléfono y popover en
  `build/cue-marker-qa/`; la sesión anterior de la app continuó reproduciendo.

### Pads: transiciones entre secciones y canciones

- El nivel se aplica por voz; la cola saliente conserva su ganancia instantánea
  y no aumenta al restaurar el nivel base o entrar una nota más fuerte.
- Cambios de sección usan el crossfade configurado; faders manuales mantienen
  su rampa corta. Apagados repetidos no reinician la cola.
- En canciones con Pads programados, secciones sin preset y huecos significan
  silencio. Tras abandonar una canción automatizada, el seguimiento de tono
  no reactiva un pad global; sigue disponible mediante un gesto manual.
- Build macOS Standalone correcto; CTest `section_automation` y `pads_engine`: 2/2 correctos (10,45 s). No se acredita escucha manual ni instalación física con este ajuste.


### Aprendizaje por recorridos completos de control remoto

- Cinco cursos: los tres introductorios conservan su avance manual y el curso
  `settings-remote` se sustituye por `remote-server` (10 pasos) y
  `remote-client` (12 pasos), con IDs/progreso independientes. Recorren la
  apertura de Configuración, navegación remota, modo, conexión, vuelta a la
  canción y comprobación de los controles. No añaden una lección de reconexión.
- La guía y el catálogo pueden vivir dentro del `SettingsPanel` real. Los
  cursos remotos siguen su navegación en teléfono, tablet y escritorio,
  resaltan controles existentes y revelan destinos fuera de vista mediante
  scroll. Al cerrar Configuración vuelven al editor; modales/pickers ajenos
  ocultan temporalmente la ayuda conservando el paso.
- Botones compactos: Anterior con flecha compartida y Continuar/Terminar con
  ancho según texto, target de 44 px y relleno de 35 px. Cursos/Reiniciar
  están en Más. Su menú es hijo de la guía y permanece visible dentro de
  Configuración; los modales ajenos siguen ocultando temporalmente la ayuda.
  El estado pendiente no repite el texto si ya aparece como tarea del paso.
  El avance por botón o teclado espera el requisito del paso;
  Anterior, Más y cerrar permanecen disponibles.
- Las condiciones remotas usan estado real de página, modo, servidor disponible,
  cliente conectado, snapshot inicial y canción recibida. Play/Stop requieren
  el comando propio del paso, ACK aceptado y snapshot posterior de la misma
  canción. El toggle desde reproducción registra intención de Pausa; una
  reanudación posterior de otro cliente no acredita el ejercicio de Play.
  Cambios ajenos no completan el ejercicio; rechazo, timeout,
  desconexión, cambio de canción/paso y cierre descartan evidencia.
- Al retomar un paso remoto, `RemotePrerequisite` revela el requisito faltante:
  abrir Configuración, entrar en Control remoto, elegir modo y esperar sesión
  disponible. El usuario acciona los controles y cierra Configuración cuando
  se necesita el reproductor. La guía conserva el paso persistido y vuelve a
  su ejercicio al recuperar los requisitos, sin enviar comandos automáticos
  ni añadir una lección de reconexión.
- Cobertura añadida en `learning_guide`, `learning_overlay`, `settings_panel`
  y `remote_learning_action`. Compilación macOS de `MTControls_Standalone` y
  los cuatro targets de pruebas correcta en `build/codex-modal-macos`.
  La app y el overlay se recompilaron después del ajuste final de menú y modo.
  CTest final: `learning_guide` 0,55 s, `remote_learning_action` 0,01 s,
  `learning_overlay` 2,22 s y `settings_panel` 0,16 s; 4/4, 100 %, 2,95 s total.
  La evidencia de 3/3 del 2 de octubre corresponde al contrato anterior.
  La ejecución final incluye las regresiones del menú embebido y el texto
  pendiente sin duplicado.
- Revisión manual parcial de escritorio: catálogo de cinco cursos, Servidor
  pasos 1–7 y Cliente pasos 1–5 con Configuración real; bloqueo al faltar
  cliente y espera de conexión. Reanudación de Cliente paso 5 desde Local
  pidió abrir Configuración y luego elegir Cliente sin perder el paso.
  Más mostró Cursos/Reiniciar dentro de Configuración y Cursos volvió al
  catálogo en el panel. Capturas `desktop-guide-options.png` y
  `desktop-server-guide.png`. El flujo integral y Play/Stop con dos equipos
  siguen pendientes; no se acreditan builds iOS/Android ni instalaciones físicas.
- Contratos: [LearningGuide](ui/LearningGuide.md) y [Testing](Testing.md).

## 2026-10-02

### Guías interactivas de aprendizaje

- `Aprender PlayWorship` abre un catálogo desde Más en teléfono/tablet y desde
  Configuración en todas las plataformas. Cuatro cursos localizados ES/EN:
  primeros pasos, mezcla/clic/tono, Ensayo en teléfono o secciones/automatización
  en escritorio/tablet, y configuración/remoto.
- Cada paso resalta un control real y permite usarlo a través de la capa de
  ayuda. Tarjetas dentro del área segura, lectura desplazable y acciones
  táctiles de 44 px. Se adaptan hasta 300 px de alto en pantallas altas y 210 px
  en teléfonos horizontales, reduciéndose cuando así dejan libre el destino.
- Anterior, Siguiente/Terminar, Cursos, Reiniciar y Pausar (X/Escape).
  Progreso local por IDs estables de curso/paso, JSON v1 y reemplazo atómico;
  errores de escritura visibles. No pertenece a canciones ni a Cloud/remoto.
- Los ejercicios usan los controles existentes. La guía no emite comandos
  automáticos de transporte, mezcla, edición o remoto. Se oculta mientras están
  abiertos Configuración o un modal y retoma el paso al cerrarlos. Si se elige
  otro curso desde Ensayo, indica y resalta su cierre sin cerrarlo por cuenta
  propia. Cambiar el idioma conserva el curso y el paso.
- Contrato: [LearningGuide](ui/LearningGuide.md).
- Verificación: compilación macOS de `MTControls_Standalone`,
  `MTControlsLearningGuideTests`, `MTControlsLearningOverlayTests` y
  `MTControlsSettingsPanelTests` en `build/codex-modal-macos`;
  CTest `learning_guide`, `learning_overlay` y `settings_panel`: 3/3 aprobadas
  (2,98 s). Capturas de componentes reales en teléfono vertical/horizontal,
  tablet y escritorio, incluidos los destinos de la vista Ensayo y acceso a
  reproducción a través del resaltado. El scroll sobre texto y el descarte
  del toque tras arrastrar la lista siguen los handlers reales.
- Queda pendiente la revisión manual de la app completa y la validación física;
  estas pruebas no acreditan una instalación iOS/Android.

### Automatización de secciones

- Borradores dispersos por ID de sección y stem para volumen, ganancia, mute
  instrumental y controles de Pads. Mix base independiente y Cancelar/Borrar.
- Acciones del borrador en una barra flotante persistente anclada por ID al
  borde superior de la sección automatizada. El desplazamiento y el zoom
  actualizan su posición; se limita al viewport y al área segura, sin seguir
  el cursor ni el hover. Si la sección queda fuera de vista, conserva su nombre
  inequívoco y una flecha de dirección junto al borde visible. El menú `Más`
  ofrece `Volver a la sección`, que ajusta el viewport sin cambiar la posición
  de reproducción ni el transporte. La toolbar global recupera el espacio
  antes reservado para las acciones de automatización.
- Barra `A · nombre`, `Cancelar`, `Listo` y `Más` en escritorio/tablet;
  `A · nombre`, `Listo` y `Más` en teléfono. El menú incluye
  `Borrar automatización` y, en teléfono, `Cancelar cambios`. Los faders y Pads
  existentes conservan su comportamiento y el contrato del motor. Los botones
  táctiles mantienen 44 px de altura mediante padding vertical adaptable a
  ondas de 108 y 140 px.
- Indicadores en los controles existentes y acceso a Automatizar en Más de
  teléfono. Historial del arrange conserva los valores y las sesiones remotas
  mantienen la propiedad autoritativa del borrador.
- Navegación del minimap y pan/pinch móvil disponibles durante el borrador;
  los cambios estructurales de secciones, el tap-seek y el doble tap/salto
  móvil permanecen bloqueados. Se ocultan los controles habituales de sección.
- Durante el borrador, la onda presenta el mapa de guía/secciones aunque
  estuviera seleccionado el mapa de tempo; conserva el `timelineViewMode`
  guardado y recupera su presentación al terminar la edición.
- Resaltado pulsante del borrador en la sección, canales y Pads, sincronizado
  con tiempo de interfaz para continuar en pausa; la automatización guardada
  conserva su indicador fijo y el repintado se limita a las zonas animadas.
- Selección desde el callback de audio para reproducción, seek y transiciones,
  cambios de ganancia suaves y propiedad del borrador en sesiones remotas.
- Persistencia local, proyecto, sidecar y Cloud; contratos de
  [interfaz](ui/SectionAutomation.md), [motor](engine/SectionAutomation.md),
  [datos](importing/LibraryPersistence.md) y
  [remoto](integrations/RemoteControl.md).
- Verificación local: compilación de `MTControls_Standalone` y
  `MTControlsWaveformEditingTests` en `build/codex-modal-macos` con `-j4`;
  CTest `waveform_editing` y `section_automation` aprobadas (2/2, 2,17 s y
  6,36 s). Revisión de capturas reales del componente con anchos de 320, 360,
  768 y 1200 px y geometrías móviles de 360 × 108 y 360 × 140 px: layout
  correcto, botones de 44 px y carril seguro de 44 px.
- Las capturas verifican el componente; quedan pendientes la revisión manual
  de la app completa y la comprobación en dispositivo físico. La Mac bloqueada
  impidió la revisión completa. Esta evidencia no incluye instalación iOS.

## 2026-09-26

### Bordes de performance en Android

- Waveform y mixer ocupan el ancho completo del telefono Android, reutilizando
  la proteccion de controles y padding desplazable de iPhone. La camara ya no
  produce una franja vacia a lo largo de toda la vista.
- Los cambios de insets del sistema fuerzan el relayout del editor aunque el
  tamano de ventana no cambie, para retirar el margen de una barra ya oculta.
  El fondo del TopBar tambien cubre el inset superior si la barra esta visible.
- Contrato: [Mobile Layout](ui/MobileLayout.md).

### Scroll completo de Ruteo

- El viewport de Configuracion abarca todas las categorias y el pie de Ruteo,
  tanto en movil como en escritorio. Se elimina el recorte de las ultimas filas
  causado por la altura fija del contenedor.
- Filas adaptables al ancho, controles tactiles de 48 px y espacio reservado
  para la barra vertical. La nota inferior sigue a la ultima fila.
- Los selectores distinguen tap de arrastre y la rueda desplaza la lista sin
  modificar salidas. Una redimension conserva el scroll y cambiar de pestaña
  vuelve al inicio. Contrato: [Mobile Layout](ui/MobileLayout.md).

### Teclado del asistente de canciones

- Navegacion directa entre campos mediante Anterior/Siguiente/Listo y Return.
  iOS conserva la sesion de teclado al transferir el foco entre campos del
  asistente y actualiza la tecla Siguiente/Listo; BPM mantiene teclado numerico.
- Los combos moviles esperan al fin del tap y al cierre del teclado antes de
  abrirse en una posicion estable. Deslizar sobre ellos no abre una lista;
  cerrar el menu no reactiva el editor anterior.
- La adaptacion de iOS se genera durante CMake, sin editar el checkout de JUCE,
  y exige revision si cambia el codigo de entrada de texto de la dependencia.
- Verificacion en iPhone 17 Pro Simulator (iOS 26.2): teclado nativo
  Siguiente, barra Anterior/Siguiente/Listo, BPM con NumberPad, conservacion de
  valores y selector de tono que no reactiva el teclado al cerrarse. Los eventos
  de UIKit confirman cambios de campo sin notificaciones de ocultacion.
  La experiencia tactil en iPhone/Android fisicos sigue pendiente.
- Contratos: [Song Creation](importing/SongCreation.md) y
  [Mobile Layout](ui/MobileLayout.md).

### Biblioteca y setlists con organizacion movil nativa

- El `+` de Biblioteca abre la creacion de canciones con la misma transicion
  horizontal entre paginas en iOS, Android y escritorio; la `X` y el guardado
  vuelven con el movimiento inverso a Biblioteca. El formulario incrustado
  tambien se ajusta al ancho del panel en tablets y escritorios tactiles.
- En telefono, Biblioteca y setlists pintan una superficie completa bajo las
  areas del sistema y protegen sus controles con los insets seguros reales.
- La barra superior usa acciones planas de 44 pt en iOS y 48 dp en Android,
  titulo jerarquico y un resumen de cantidad/duracion del setlist. Repertorio,
  canciones, Biblioteca y creacion de canciones comparten ahora una geometria
  de 56 pt, iconos de 21 pt, margenes, separacion y alineacion vertical. La
  navegacion y los datos siguen en el `SongListPanel` compartido, incluida la
  biblioteca recibida por control remoto.
- Contrato: [Mobile Layout](ui/MobileLayout.md).

### Pads en popover de telefono

- Pads abre el popover comun anclado al boton del `TopBar`, con encabezado
  compacto como el selector de canciones. Conserva el componente y el estado
  de las notas al cerrar. Tablet y escritorio mantienen el cajon inferior.
- Contratos: [Popovers](ui/Popovers.md) y [Mobile Layout](ui/MobileLayout.md).

## 2026-09-25

### Superficie de iPhone y rotacion

- La onda y el viewport del mixer de iPhone llegan a los laterales de la
  pantalla. Los controles fijos usan el inset real de cada lado y el mixer
  agrega padding desplazable para poder mostrar completos los canales extremos.
- La ventana iOS se ajusta a la escena activa despues de la rotacion y al
  reanudar; el editor adjunto sigue las dimensiones de su host. Los cambios de
  orientacion se agrupan fuera del callback de layout para evitar dimensiones
  antiguas y superficies recortadas.
- El arranque reconcilia la orientacion real de `UIWindowScene` con la mascara
  ya configurada en JUCE: si se fijo antes de crear la escena, solicita la
  rotacion nativa cuando la ventana ya esta adjunta.
- Verificacion: el simulador reproduce el recorte anterior y confirma el
  arranque corregido desde vertical y ambas orientaciones horizontales.
  Compilacion, firma, instalacion y apertura verificadas en iPhone 16 por
  Wi-Fi, sin USB; queda pendiente la comprobacion visual en ese dispositivo.
- Contrato: [Mobile Layout](ui/MobileLayout.md).

### Nombre del servidor remoto

- El anuncio LAN toma el nombre visible del dispositivo según plataforma y
  evita `localhost`; Configuración permite fijar un alias local que se anuncia
  sin interrumpir los WebSocket conectados. En iOS 16+ el alias resuelve el
  nombre genérico entregado por el sistema sin entitlement especial.
  Contrato: [RemoteControl](integrations/RemoteControl.md).

## 2026-09-24

### Control remoto LAN

- Modo Local/Servidor/Cliente de la app standalone, descubrimiento mDNS y
  sesión WebSocket con varios clientes y servidor autoritativo.
- Snapshot de canción, transporte, biblioteca, setlists y mezcla; waveform
  binaria verificada sin transferir stems o audio al cliente.
- UI cliente con etiqueta REMOTO, reconexión y controles bloqueados sin red.
  Contrato: [RemoteControl](integrations/RemoteControl.md).

## 2026-09-21

### Creacion portable movil y controles JUCE

- El selector ahora usa una sola lista JUCE en todas las plataformas, sin
  bridge UIKit; conserva la guarda de apertura entre mouseDown/mouseUp.
  Corrige el indent superior de texto y el espacio del boton limpiar.
- Mapeo movil con targets de 44–48 pt, scroll estable y borrador conservado al
  volver a Archivos; escritorio mantiene sus filas y distribucion.
- Creacion portable movil en background, protegida contra doble envio/cierre,
  con FLAC JUCE sin encoder externo y portada manual dentro del paquete.
- Los documentos elegidos en iOS/Android se copian desde sus URLs autorizadas
  a temporales privados del borrador, sin depender de rutas externas ni borrar
  originales. Las pruebas de persistencia usan almacenamiento aislado.
- Verificacion local: 19 pruebas aprobadas; la prueba de interaccion del popup
  se omite porque el sistema no concede foco a su ventana. Save/open y registro
  real en Biblioteca pasan con audio/portada y fallos de escritura simulados.
  Compilan standalone macOS, codigo compartido iOS Simulator y APK Android;
  quedan pendientes teclado y proveedores de documentos en dispositivo real.
- Contratos: [Song Creation](importing/SongCreation.md),
  [Input Controls](ui/InputControls.md), [Mobile Layout](ui/MobileLayout.md).

### Asistente de canciones a pantalla completa

- En telefonos, el fondo del asistente de creacion y edicion ahora cubre la
  superficie fisica completa, incluidas las regiones del notch y del indicador
  inferior.
- Los controles siguen descontando `safeAreaInsets`; el cambio es visual y no
  coloca acciones interactivas debajo de elementos del sistema.
- Contrato: [Mobile Layout](ui/MobileLayout.md).

### Teclado movil en el asistente de canciones

- El asistente observa los `keyboardInsets` de JUCE mientras esta abierto y
  mantiene los campos editables por encima de un teclado acoplado.
- En Detalles mantiene un formulario desplazable con geometria estable;
  desplaza suavemente el campo enfocado sin ocultar ni reordenar la portada y
  los pasos. Portada, camara y metadata comparten el recorte del viewport.
  En Secciones conserva el viewport y revela automaticamente la fila enfocada.
- Return finaliza el campo activo y descarta el teclado sin dejar foco
  residual; BPM solicita teclado numerico. `Listo` sobre el teclado y abrir un
  combo tambien finalizan la edicion. Deslizar el formulario conserva el teclado.
- Contratos: [Mobile Layout](ui/MobileLayout.md) e
  [Input Controls](ui/InputControls.md).

### Selectores moviles en una sola lista

- `AdaptiveComboBox` conserva su superficie compartida de PlayWorship y abre
  el selector UIKit nativo en iPhone/iPad.
- `Cancelar` ahora pertenece al mismo grupo de opciones; mantiene la operacion
  sin cambios pero evita la segunda tarjeta que UIKit reserva para acciones con
  estilo `Cancel`.
- Los campos de texto y combos comparten un borde en reposo con mejor contraste,
  y los combos liberan el foco al cerrar la lista para no dejar el acento verde
  persistente.
- Contrato: [Input Controls](ui/InputControls.md).

### Biblioteca iOS: conservar canciones entre instalaciones

- Una actualizacion de la app puede conservar `Library/MTControls` y asignarle
  otro UUID de contenedor. Antes, las rutas absolutas de stems y artwork
  seguian apuntando al UUID anterior y la carga descartaba la cancion como si
  sus archivos hubieran sido eliminados.
- `SongLibraryStore` ahora reubica rutas administradas de stems, artwork y
  proyectos al root actual antes de validar disponibilidad. Las rutas externas
  del usuario no se modifican.
- Contrato: [Library Persistence on iOS](importing/LibraryPersistence.md).

## 2026-09-20

### Campos de texto y selectores adaptativos

- `TextEditor` y `AdaptiveComboBox` comparten fondo, borde, foco, radio,
  tipografia, padding y estados deshabilitados mediante un unico look-and-feel.
- Los selectores usan action sheet nativo en iPhone, popover UIKit anclado en
  iPad y menu JUCE equivalente en escritorio y Android.
- Las filas de menu y campos tactiles adoptan metricas de `44 pt`; la opcion
  actual se marca antes de abrir el selector.
- `SearchField` incorpora una accion final para limpiar texto que cede su lugar
  al indicador de carga cuando corresponde.
- Contrato: [Input Controls](ui/InputControls.md).

## 2026-09-16

### Routing agrupado para tempo/pitch en Android

- Cuando todos los stems activos resuelven al mismo `(channel, width)`
  destino, `processBlock` deja de lado el loop por stem que ejecuta
  `BungeeProcessor::process()` una vez por stem. En su lugar, reparte
  los stems entre `stemMixBuffer`, `percussionBuffer` y `dryBuffer`
  con `StemMixEngine::mixStems` y corre las **tres** voces
  `TimeStretchProcessor` que `StemMixEngine` ya mantenía agrupadas.
  El resultado se enruta con `StemOutputRouter.routeStemToOutputChannels`
  **una sola vez**, plegando L+R cuando el destino común es mono. En
  una canción de 19 stems con tempo ≠ original, las llamadas Bungee
  caen de 19 a 3 por callback; el callback promedio baja de ~45 ms a
  el rango del presupuesto de 40 ms.

- Si los stems se distribuyen entre canales o anchos, se conserva la
  ruta por stem original — el agrupado es opt-in por homogeneidad del
  routing, no invasivo. Cuando no hay pitch/tempo activo también se
  cae a la ruta por stem para no pagar el costo extra de los tres
  `addFrom` internos.

- Las mismas garantías auditivas se preservan: volumen, trim, mute,
  solo por stem (vía `StemTrack::setMuted/setSoloed/setVolume/setTrim`);
  fade de instrumentos y excepción de fade para click/guía (vía
  `applyFade` dentro de `renderToOutput`); compensación de latencia
  reportada por `mixEngine.getOutputLatencySamples()`; y routing mono/
  estéreo hacia el canal configurado.

- `AudioDiagnostics::StemTimingEvent` agrega `routing_grouped_eligible`
  a `audio_stems_<ts>.csv` para que el script de análisis distinga
  bloques agrupados de bloques por stem dentro del mismo escenario.
- Contratos actualizados: [StemMixEngine](engine/StemMixEngine.md),
  [MTControlsAudioProcessor](processor/MTControlsAudioProcessor.md),
  [AudioDiagnostics](processor/AudioDiagnostics.md).

### Fluidez de la interfaz Android

- El APK Debug optimiza el codigo nativo con `-O2` y conserva los simbolos y la
  depuracion. `-PplayworshipOptimizeDebug=OFF` permite desactivarlo explicitamente.
- La raiz Android usa el renderer 2D OpenGL de JUCE con repintado por demanda;
  desconecta el contexto antes de destruir el editor.
- El mixer conserva imagenes software de sus slots para reutilizarlas al hacer
  scroll. VU y controles invalidan sus regiones; no se crean render targets GL
  anidados por canal.
- Los cambios entre playback y pausa/stop repintan la waveform completa antes
  de volver a las franjas parciales del playhead.
- Contrato: [Mobile Layout](ui/MobileLayout.md).

## 2026-09-05

### Biblioteca iOS: proyectos importados idempotentes y diagnóstico de Cloud upload

- Los `.pwproj` importados desde Files se identifican por `projectId`, no por
  su ruta absoluta ni por el nombre con timestamp. Reabrir el mismo proyecto
  ya no añade otra canción ni conserva otra copia administrada bajo
  `imports/projects`; las referencias de setlists existentes se preservan.
- Si un build de desarrollo cambia el UUID del contenedor iOS y el paquete aún
  existe bajo el root actual, la Biblioteca repara el `projectPath` en
  `songs.json`. Una desinstalación que borra el sandbox no es recuperable sin
  Cloud o una copia del `.pwproj` fuera de la app.
- `CloudTransferClient` registra en `session_auth.log` el resultado de refresh,
  prepare-upload, cada objeto firmado, descriptor y commit. Esto diferencia un
  fallo de autenticación, API, URL firmada o commit sin registrar tokens.

### Biblioteca Cloud: descarga única por canción

- Una fila Cloud vinculada a una canción local ya materializada muestra
  `Ya descargada` fuera de un flujo de setlist, y `Agregar al setlist` o
  `Ya agregada` dentro de él. Nunca vuelve a ofrecer descarga.
- El callback de descarga comprueba nuevamente `remoteSongId -> projectId ->`
  canción local antes de transferir, evitando una segunda caché aun si la lista
  Cloud quedó desactualizada.

### Cloud download: stems reproducibles antes de registrar

- La importación Cloud valida que cada stem materializado exista y tenga tamaño
  antes de añadir la canción a Biblioteca. Una descarga incompleta falla en vez
  de crear una fila que el motor no pueda reproducir.
- `session_auth.log` registra solicitud, objetos y finalización de descarga, y
  el cargador registra cuántos stems pudo resolver antes y después de llamar al
  motor. Esto permite diagnosticar una carga detenida en 0% sin exponer URLs
  firmadas ni tokens.

### Auth: Google OAuth backend-driven para iOS y macOS

- `PluginEditor::launchExternalLogin` ahora apunta a
  `getAuthBaseUrl() + "/api/auth/google/start?..."` en lugar de
  `/login?..." tanto en la rama `JUCE_IOS` (vía
  `ASWebAuthenticationSession`) como en la rama desktop/macOS (vía
  `LocalAuthCallbackServer` + `launchInDefaultBrowser`). El primer
  frame es el account picker de Google en ambas plataformas; ya no se
  muestra el formulario de PlayWorship.
- Backend nuevo: `lib/googleOAuth.ts` con allowlist de
  `redirect_uri`, token `state` HMAC autocontenido (TTL 10 min), armado del
  auth URL de Google con `prompt=select_account`, exchange de code →
  tokens, verificación RS256 del `id_token` contra el JWKS de Google
  y armado del `session` JSON.
- `app/api/auth/google/start/route.ts`: valida `redirect_uri`, persiste
  `state` con `deviceId`/`licenseKey`, redirige 302 a Google.
- `app/api/auth/google/callback/route.ts`: valida `state`, intercambia
  el code, valida el `id_token` de Google, hace upsert del usuario
  Firebase, minte un custom token, lo cambia por `idToken` +
  `refreshToken` vía Identity Toolkit, llama `/api/validate-access` y
  redirige 302 a `playworship://auth/callback?session=...` con el
  mismo payload que ya producía `/login`.
- Errores se traducen a 302 al `redirect_uri` original con
  `error=<codigo>&message=<detalle>&state=<state del iOS>` para no
  romper la validación de state en el cliente.
- Variables de entorno requeridas: `GOOGLE_CLIENT_ID`,
  `GOOGLE_CLIENT_SECRET`, `GOOGLE_OAUTH_CALLBACK_URL` (debe coincidir
  con un Authorized redirect URI del client de Google).
- `MTCONTROLS_AUTH_BASE_URL` permite que un build de iPad consuma APIs en
  una IP local (seleccionada con `MTCONTROLS_BACKEND_MODE=local`) pero arranque OAuth desde un
  backend HTTPS compatible con Google.
- Documentación: nueva página
  [`docs/okf/integrations/Auth.md`](integrations/Auth.md) con el
  contrato end-to-end, allowlist, errores y la lista de lo que este
  flujo NO hace.

## 2026-09-04

### Autenticacion nativa en iPad

- iOS usa `ASWebAuthenticationSession` para presentar Google dentro de una
  hoja segura del sistema, permitir selección de cuenta y regresar a la app.
- El callback registrado es `playworship://auth/callback` y conserva la
  validación `state`. macOS mantiene su callback HTTP efímero en localhost.
- El frontend sólo admite ese callback de aplicación exacto, además de
  `localhost` y `127.0.0.1`, evitando redirects arbitrarios.
- El flujo no usa `signInWithRedirect`: Safari y Chrome bloquean el estado
  cross-origin del helper alojado en `firebaseapp.com`. El frontend conserva
  `signInWithPopup` hasta mover el intercambio OAuth completamente al backend.

### Alertas nativas multiplataforma

- Los avisos y confirmaciones simples de la UI principal, Biblioteca e
  importación usan `juce::NativeMessageBox`, por lo que iOS, macOS y Windows
  presentan el diálogo propio del sistema.
- Los formularios con campos, selección de destino, descarga de encoder o
  progreso permanecen como componentes JUCE porque requieren controles que una
  alerta nativa simple no ofrece.

### Backend local accesible desde iPad

- El plist iOS permite conexiones HTTP exclusivamente hacia la red local y
  declara el motivo del permiso de red local.
- La resolución de `MTCONTROLS_API_BASE_URL` permite HTTP para loopback,
  nombres `.local` y direcciones privadas RFC1918. HTTP público continúa
  rechazado y usa el backend HTTPS de producción como fallback.
- Los builds físicos de desarrollo pueden definir `MTCONTROLS_API_BASE_URL` con
  el hostname Bonjour del Mac; la URL HTTPS de producción sigue siendo el valor
  predeterminado.
- La URL HTTPS predeterminada de producción es
  `https://api.playworship.app`, tanto para la API como para OAuth cuando
  `MTCONTROLS_AUTH_BASE_URL` no se define.

### Acceso responsive en iPad y escritorio

- El modo de bienvenida usa todo el viewport disponible en iPad y conserva una
  ventana redimensionable en escritorio; cerrar sesión ya no fuerza el editor
  a volver a un tamaño fijo.
- Al completar el login, las rutas de inicialización nueva y reutilizada del
  workspace conservan el `userArea` completo en iPad en lugar de aplicar el
  tamaño inicial `1120x700` de escritorio.
- La bienvenida se presenta como tarjeta centrada, horizontal en superficies
  anchas y vertical en tamaños compactos. Pads, mixer, setlist y sus handles se
  ocultan completamente mientras no existe una sesión activa.
- Cerrar sesión desde Settings difiere el cambio al siguiente ciclo de UI y
  destruye primero la ventana u overlay de configuración; la bienvenida ya no
  queda superpuesta sobre Settings.
- Antes de cerrar sesión se solicita confirmación. Cancelar conserva la sesión
  y Settings sin cambios; confirmar ejecuta la limpieza y vuelve al login.
- Una validación iniciada antes de confirmar el cierre se descarta al regresar
  al message thread, para que no pueda restaurar la sesión eliminada.

### Carga de canción: feedback desde el primer frame

- El indicador del waveform ahora se activa antes de abrir un `.pwproj` o
  resolver stems y muestra inmediatamente el título solicitado.
- Se procesa un único frame inicial para que una extracción cold no parezca un
  bloqueo. El progreso por stem conserva el camino optimizado sin bombear la UI
  repetidamente.

### Cloud download: caché por hash sin `.pwproj` automático

- Descargar una canción Cloud registra una entrada local que reproduce los
  blobs FLAC verificados desde `cloud-content/objects/`; ya no reconstruye ni
  conserva un ZIP `.pwproj` duplicado.
- El transporte reutiliza un objeto existente cuando tamaño y SHA-256
  coinciden. Los FLAC cloud tampoco pasan por `decoded/`, por lo que cada stem
  queda almacenado una sola vez.
- `Convertir a proyecto...` sigue disponible como acción explícita y conserva
  el `projectId` portable. La purga elimina `cloud-content/`, pero nunca los
  `.pwproj` creados deliberadamente por el usuario.
- Al completar correctamente una descarga se elimina, como limpieza de mejor
  esfuerzo, el staging legado `projects/.cloud-downloads`; el caché activo por
  hash no se toca.

### Cloud sync: purga local segura

- Configuración incluye `Purgar` como reset local integral: borra
  `songs.json`, todos los setlists, el índice cloud, registros de proyectos,
  cache de audio/ondas/portadas/proyectos y staging temporal.
- Antes de borrar, la canción activa se libera de forma segura del motor de
  audio. La interfaz limpia mixer, waveform, arrange y selección persistida, e
  invalida previews asíncronos para que una tarea tardía no restaure la onda.
- Si el callback de audio no puede detenerse, la purga se cancela y conserva
  todos los archivos locales.
- La acción no toca R2, Firestore ni la carpeta física `projects/`; por eso
  ningún paquete `.pwproj` se elimina o modifica. Sus referencias de
  Biblioteca sí desaparecen y sólo vuelven mediante abrir, crear o descargar.

### Biblioteca Cloud: descarga de canciones

- Las filas de `Cloud` muestran el menú de tres puntos con la acción
  `Descargar canción`. La descarga conserva objetos verificados en el cache
  local y registra la canción en Biblioteca al completar.
- La fila remota usa el indicador de progreso durante la transferencia y se
  bloquean solicitudes duplicadas por `remoteSongId`.

### Biblioteca Cloud: cantidad de stems

- El resumen remoto incluye `stemCount`, derivado del descriptor validado al
  publicar. Las tarjetas Cloud ya no muestran `0 stems` para proyectos que
  contienen pistas.
- Para documentos Firestore publicados antes del campo, el listado resuelve
  una vez el descriptor R2 vigente y usa `descriptor.stems.length`; así la
  migración es compatible sin re-publicar canciones existentes.

### Cloud download: manifiesto legacy sin projectId

- Los `.pwproj` históricos guardaban `song.id` pero no necesariamente
  `song.projectId`; `ProjectStore` lo infería sólo en memoria. El descriptor
  cloud sí usa esa identidad, por lo que la descarga fallaba al compararlos.
- La subida normaliza `song.projectId` dentro del manifiesto enviado a R2. La
  descarga de revisiones existentes completa únicamente un `projectId` ausente
  desde el descriptor firmado; no acepta un ID explícito que difiera.

### Biblioteca: conservar destino de setlist

- Al entrar mediante `Añadir canciones`, `SongListPanel` captura el
  `setlistId` activo como contexto efímero. Las selecciones locales, abrir un
  `.pwproj` y descargar desde Cloud agregan la canción resultante a ese mismo
  setlist.
- La Biblioteca abierta desde el icono de música no tiene destino; abrir o
  descargar allí sólo registra localmente. El contexto se limpia al volver a
  las canciones del setlist, evitando que una operación posterior use un
  setlist obsoleto.
- En modo destino se ocultan los menús de filas locales para priorizar
  `Agregar`, pero las filas Cloud conservan los tres puntos: primero deben
  descargar el proyecto y luego se agregan al setlist de origen.
- La acción Cloud se nombra `Descargar y agregar` cuando existe destino de
  setlist; en Biblioteca general conserva `Descargar canción`.
- Antes de presentar la acción Cloud, la Biblioteca resuelve el vínculo
  `remoteSongId -> projectId -> localSongId`: una descarga ya materializada
  ofrece `Agregar al setlist` o `Ya agregada`, sin repetir la descarga.
- El área de interacción de `Agregar` también se anula para filas Cloud; sin
  ello se solapaba con los tres puntos y absorbía el clic antes de abrir el
  menú de descarga.

### Cloud sync: contrato cloud vs local y plan de preflight (sólo análisis)

Auditoría del flujo actual de subida a R2 + Firestore. Se detecta que
`CloudProjectArchive::buildUpload` (`src/data/CloudProjectArchive.cpp:59-148`)
toma el `manifest.json` íntegro del `.pwproj` y lo registra como el
objeto `settings` del `CloudProjectDescriptor`. Ese bloque arrastra
`mix.master`, `mix.stems[]` (volume/gain/mute/solo/outputChannel),
`window`, `arrangement`, `dynamicGuide*` y `clickDynMix` — todo lo
que el producto considera estado por dispositivo. El log del
2026-09-03 lo describe como decisión explícita para "preservar
arrangement, tempo map y settings sin duplicar los serializadores
internos de `ProjectStore`"; hoy ese mismo `settings` se reinyecta
tal cual en `rebuildProject` (`CloudProjectArchive.cpp:150-217`),
así que cualquier cambio futuro que use el `settings` remoto para
escribir el sidecar local sobrescribirá el mixer/UI de otro
dispositivo.

El nuevo contrato queda en
[`docs/okf/integrations/CloudSync.md`](integrations/CloudSync.md).
Decisiones:

- El objeto `settings` que sube a R2 deja de ser el `manifest.json`
  del `.pwproj` y pasa a ser un JSON portable nuevo con
  `schemaVersion: 2`, conteniendo sólo `song`, `stemsPortable`,
  `artworkFileName`, `waveformHash` opcional y
  `arrangementPortable.parts`/`tempoRegions` con los campos
  musicales compartibles.
- `mix`, `window`, `pitchSemitones`, `tempoBpm` como override,
  `dynamicGuide*`, `clickDynMix`, y los flags `*Customized` se
  quedan exclusivamente en el sidecar local
  `project-cache/<projectId>/song-settings.json`. Nunca forman
  parte de ningún blob subido.
- `rebuildProject` deja de reinyectar `manifest.json`: construye un
  `manifest.json` portable desde el descriptor cloud nuevo, abre el
  ZIP con `ProjectStore` y siembra el sidecar local con
  `ProjectStore::getCachedSettings(projectId, …)` si existe; si no,
  con los defaults de `SongLibraryStore`. Nunca llama a
  `ProjectStore::updateSettings` desde el import.
- Preflight: `GET /api/song-sync/songs/:songId/descriptor` (endpoint
  nuevo) devuelve el descriptor remoto vigente sin URLs firmadas.
  El cliente construye un descriptor local candidato y compara
  `settings.sha256`, `waveform`, `artwork`, cada `stem` (ordenado
  por `stemId`) y los campos portables de metadata/arrangement.
  Si nada cambió, no se llama a `prepare-upload`, no se sube nada y
  la UI muestra `Al día` con la acción deshabilitada. Si difiere,
  se continúa con la subida y `commit` contra `baseRevision =
  link.remoteRevision`; un `REVISION_CONFLICT` dispara un
  reintento único del preflight.
- UX: `Subir a nube` (sin vínculo), `Publicar cambios` (con
  vínculo + cambios), `Al día` deshabilitado (con vínculo + sin
  cambios). Sin modales de éxito; el progreso sigue siendo la
  barra de fila.

No se introducen commits de código en esta iteración: el
documento fija el contrato a implementar en commits separados con
tests, manteniendo las validaciones de hashes, el streaming de
`NSURLSessionUploadTask` en macOS/iOS y los límites de
`kMaxInMemoryUploadBytes` para Windows/Linux.

### Biblioteca: procedencia de canciones cloud

- El filtro `Cloud` ya expresa la procedencia de sus filas y no muestra un
  badge redundante. El filtro `Local` etiqueta `.pwproj` sólo para proyectos
  locales que aún no estén sincronizados; una publicación o vínculo cloud no
  obtiene esa etiqueta.
- El resumen remoto incluye una URL firmada exclusiva para `artwork` cuando
  existe. `CloudLibraryClient` descarga esa imagen pequeña a
  `cloud-artwork/`, valida que sea decodificable y la referencia desde la fila.
  Nunca descarga stems, waveform ni el proyecto completo al navegar Cloud.
- Mientras `CloudLibraryClient` renueva credenciales y consulta el catálogo,
  Biblioteca muestra un spinner animado en el extremo derecho del buscador. El
  campo tiene borde redondeado y el indicador se cierra ante éxito, error,
  cambio a `Local` o sesión no válida, sin bloquear la búsqueda.

### Cloud sync: preflight sin cambios

- Tras una publicación, el índice local conserva una huella SHA-256 del
  descriptor normalizado, sin `songId`, revisión ni timestamp. Antes de volver
  a publicar, el cliente crea el candidato local y compara esa huella sin
  realizar llamadas de red. Si coincide, no llama a `prepare-upload`, no
  genera revisión ni sube objetos; la app indica `Sin nada que sincronizar`.
  Los vínculos creados antes de esta mejora se publican una vez para sembrar la
  huella local.

### Cloud sync: progreso integral

- La barra de la fila cubre el flujo completo: apertura/extracción y armado
  del descriptor ocupa 0–50%; la transferencia de blobs y descriptor ocupa
  50–100%. Los eventos de preparación se reenvían al hilo de UI, evitando una
  barra vacía mientras se transforma el `.pwproj`.

## 2026-09-03

### Biblioteca cloud: contrato y cliente inicial

- `CloudSyncTypes` define el descriptor remoto versionado con referencias por
  SHA-256 para settings, waveform y stems FLAC.
- `CloudSyncIndexStore` persiste por separado el vínculo entre `projectId`
  local y `remoteSongId`; `ProjectStore` continúa sin dependencias de red.
- `CloudLibraryClient` renueva Firebase en background y consulta el catálogo
  paginado del backend. El filtro `Cloud` consume ese catálogo al abrirse y
  al buscar, y se refresca al terminar una publicación; sus filas son sólo
  referencias remotas, sin crear ni sobrescribir recursos locales. La
  biblioteca Local continúa leyendo sólo recursos presentes en el dispositivo.
  El cliente usa `POST /api/song-sync/songs` para mantener el bearer Firebase
  en JUCE; `GET` se conserva para clientes HTTP existentes.

### Cloud sync: descriptor completo y validación server-authoritative

- El contrato compartido de `CloudProjectDescriptor` incorpora `artwork`
  opcional, `createdAt` y helpers de serialización/deserialización JSON en C++
  para mantener compatibilidad explícita con el backend TypeScript.
- El backend de sync ya no publica una revisión confiando sólo en metadata
  enviada por el cliente: durante `commit` relee el descriptor vigente desde
  R2, valida schema/campos obligatorios, exige coincidencia exacta entre hashes
  declarados y objetos confirmados, y revalida tamaños/content types contra R2
  antes de escribir Firestore.
- La preparación de descarga deja de exponer `objectKey` arbitrario como input
  del cliente. El endpoint resuelve la revisión vigente desde Firestore,
  devuelve el descriptor validado y genera URLs firmadas únicamente para el
  manifiesto y los blobs autorizados de esa canción.

### Cloud sync: cliente de transferencia JUCE

- `CloudTransferClient` es el límite de red para publicar y recuperar blobs:
  renueva Firebase, prepara la revisión con la API, hace PUT/GET directos a
  URLs firmadas de R2 y sólo después ejecuta el commit server-authoritative.
  Sus callbacks siempre regresan al hilo de mensajes; `ProjectStore` no
  conoce credenciales, HTTP ni identidades remotas.
- Cada blob subido y descargado se verifica contra su SHA-256 y tamaño del
  descriptor. La primera publicación genera un `songId` UUID antes de crear
  el descriptor versionado.
- En macOS/iOS, el PUT firmado usa `NSURLSessionUploadTask` desde archivo y
  transmite el blob directo a R2 sin materializarlo en memoria. La ventana de
  transferencia es independiente del timeout de API para permitir stems grandes.
  Windows/Linux conservan el límite explícito de 128 MiB hasta incorporar sus
  propios backends de streaming.

### Cloud sync: archivo portable y coordinación local

- `CloudProjectArchive` exporta un `.pwproj` existente como blobs inmutables:
  conserva `manifest.json` íntegro como el objeto `settings`, extrae waveform,
  artwork y stems FLAC del cache portable, y construye el descriptor cloud con
  hashes SHA-256. Esto preserva arrangement, tempo map y settings sin duplicar
  los serializadores internos de `ProjectStore`.
- La importación valida descriptor y manifiesto, reconstruye el ZIP con paths
  internos seguros, lo abre mediante `ProjectStore` y recién entonces lo
  registra en Biblioteca. `SongSyncService` ejecuta export/import fuera del
  hilo de mensajes, coordina la transferencia y actualiza
  `CloudSyncIndexStore` al completar una revisión.

### Cloud sync: publicación desde la biblioteca

- El menú contextual de Biblioteca expone `Subir a nube` sólo para canciones
  respaldadas por un `.pwproj`; las canciones legacy mantienen `Convertir a
  proyecto...` como paso obligatorio previo. La acción valida sesión Firebase
  y archivo local, reutiliza la URL de API configurada para el build y delega
  el trabajo a `SongSyncService` fuera del hilo de mensajes.
- Cuando el proyecto ya tiene un vínculo local con una canción remota, la
  acción se presenta como `Sincronizar de nuevo`; conserva el `songId` remoto
  y publica una revisión nueva, en lugar de crear otra canción cloud. La
  detección de cambios requiere comparar los hashes del descriptor local con
  el descriptor remoto antes de habilitar una nueva revisión.
- El resultado se comunica en la UI y el progreso se registra en
  `SessionAuthLogger`; los errores de red o autorización no modifican la
  biblioteca local ni el proyecto fuente.
- Mientras una publicación está activa, las filas de Biblioteca y del setlist
  sustituyen su separador inferior por una barra de progreso. El avance
  representa objetos confirmados por R2 (stems y descriptor), no bytes
  parcialmente enviados; se inicia sin modal bloqueante, conserva brevemente
  el 100% al completar y se elimina al terminar o fallar. Una misma canción no
  puede iniciar dos publicaciones concurrentes y un éxito no genera diálogos.
- `CloudTransferClient` conserva el `juce::var` resultante de cada respuesta
  JSON durante toda la lectura de sus `DynamicObject` y `Array`. Esto evita
  referencias colgantes al consumir `prepare-upload`, `commit` y `download`.
- En macOS, el callback de `NSURLSessionUploadTask` copia body y error a tipos
  JUCE antes de despertar el worker. El target no usa ARC, por lo que mantener
  `NSData` o `NSError` en punteros `__block` fuera del callback causaba un
  acceso inválido durante el PUT firmado.

## 2026-08-29

### Mixer UI: Channel Strip con layout estable e instrumentación VU opt-in

`ChannelStripCard::resized()` ahora usa `juce::FlexBox` para repartir el bloque
fader + medidor y la fila de solo/mute, manteniendo los tamaños públicos de
`ChannelStripCard::Layout`. El flag existente `setMuteAlignedToFader(true)`
queda aplicado cuando el canal muestra sólo mute, útil para master y guías que
no exponen solo.

`StereoVUMeter` ahora evita repintar por variaciones flotantes que no cambian
ningún LED visible: el timer sólo pide `repaint()` cuando cambia el número de
segmentos activos o el LED de peak. Para medir la actividad sin coste en builds
normales, CMake suma la opción apagada por defecto
`MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS`; al activarla se loguean contadores
agregados de ticks, repaints pedidos, ticks omitidos y paints cada dos segundos.

### TopBar: layout por grupos y refresco visual incremental

`TopBar::resized()` ahora ordena el transporte y los controles derechos como
grupos `juce::FlexBox`, conservando la prioridad existente: en anchos reducidos
se ocultan primero el texto de BPM y luego CPU/MEM para reservar espacio a la
tarjeta de canción.

El refresco periódico evita reasignar texto, iconos, badges y bounds cuando no
cambian. `refreshFromProviders()` ya no repinta todo el TopBar en cada tick:
la tarjeta central sólo invalida su área cuando cambian título, artista, artwork
o acento. El transporte, metrónomo, fade y labels siguen actualizándose desde
los mismos providers y callbacks.

La tarjeta de canción ahora pinta una superficie propia con franja de acento y
artwork recortado, para separar mejor metadata del fondo del header. Play queda
como único botón primario; al reproducir se marca activo, Stop se marca cuando
la posición vuelve al inicio y Fade muestra badge mientras está activo.

El popover de tempo/compás mantiene las mismas acciones, pero separa `TAP`,
compás y BPM en filas más legibles. En MIDI Learn, el TopBar ahora resalta y
arma targets de ventana, settings, popover de canción, popover de tono y popover
de metrónomo, además de los controles de transporte ya existentes. Los botones
iconográficos muestran badges `CC`/`N` cuando existe una asignación.

### TopBar: jerarquía visual compacta

La pasada visual posterior conserva los mismos callbacks y reglas responsive,
pero reduce el peso del header: los controles estándar pasan a una superficie
más discreta, Play continúa como único botón de acento pleno y el selector de
tempo usa una lectura oscura con una franja de acento en lugar de un bloque
completo coloreado. La tarjeta de canción baja de altura, reduce la tintura del
acento y limita su ancho para dejar respiración entre los grupos. CPU/MEM queda
con menor contraste para funcionar como telemetría periférica.

### TopBar: botones de control inspirados en referencia oscura

Los botones de transporte y acciones adoptan una superficie oscura en degradado
vertical, un borde único de bajo contraste y radio medio. El botón primario
mantiene un gradiente verde, borde luminoso y halo corto para preservar su
jerarquía. BPM usa la misma superficie neutra y reserva el color de acento para
su valor, evitando competir con el control principal.

### TopBar: tarjeta de canción contenida

La tarjeta de canción evita ocupar el ancho libre completo: su ancho preferido
se calcula desde el texto visible, artwork y margen breve, con un crecimiento
acotado cercano al 30% y límites por perfil responsive. Su fondo usa ahora una
superficie neutra más oscura y borde fino, sin franja lateral ni borde de
progreso; el color específico de la canción queda reservado para el artwork o su
placeholder.

### Biblioteca: filas de setlist y canciones más densas

Las filas principales de setlists, canciones del setlist y biblioteca reducen
altura, artwork, sombra y radios para alinearse con la barra superior más
sobria. La barra vertical de estado se conserva como indicador rápido de
canción/selección, pero con menor grosor. El hover de las listas de canciones
ahora invalida sólo la fila anterior y la nueva en lugar de repintar el listado
completo.

### Biblioteca: header y estados visuales más sobrios

El header de canciones reduce la altura reservada para Biblioteca y métricas,
dejando más espacio efectivo al setlist. Los controles de header adoptan una
superficie oscura con borde fino; los chips de filtro quedan preparados con
estilo más compacto/segmentado. Las filas conservan su barra vertical, pero el
color queda reservado por estado: acento para reproducción, amarillo para
selección pendiente/siguiente y color propio sólo como pista secundaria. Los
estados vacíos agregan un icono tenue y texto compacto.

### Biblioteca: acciones primarias visibles y menú secundario

La vista Biblioteca mantiene `Cargar` y `Crear` como acciones primarias visibles
para mejorar descubrimiento en iPad/iPhone. El menú de tres puntos queda
reservado para opciones secundarias como ordenamiento y limpieza de selección,
evitando duplicar acciones principales. La fila de acciones limita su ancho en
pantallas amplias y mantiene altura táctil consistente en paneles compactos.

### iOS: firma de Xcode y assets del bundle

Los targets iOS usan deployment target 18.5 para coincidir con el mínimo del
`bungee.framework` incluido, sin propagar ese valor a las herramientas host de
JUCE. La generación CMake pasa el icono como ruta absoluta para que `ZERO_CHECK`
de Xcode no dependa del directorio de trabajo. En iOS, los targets auxiliares de
librería no se firman. El standalone usa launch storyboard de JUCE en lugar de
`LaunchImage.launchimage`, evitando que `codesign` trate PNGs legacy del asset
catalog como subcomponentes no firmados. Los packs de guías y clicks se copian a
`installer/` directamente dentro del bundle plano de iOS; usar el directorio
macOS `Resources/` hace que `codesign` deje de reconocer el producto como una
aplicación iOS y no enlace su `Info.plist`. Los archivos de datos quedan sellados
como recursos por la firma final de Xcode, sin firma manual ni `--deep`. El
post-build completa `CFBundleIdentifier` en el `bungee.framework` copiado al
`.app` y vuelve a firmar sólo ese framework antes de la firma de la aplicación,
porque el SDK iOS incluido no declara ese campo.

El standalone fija su runtime search path a
`@executable_path/Frameworks`. Bungee conserva el install name
`@rpath/bungee.framework/bungee`, por lo que el ejecutable instalado nunca debe
contener como `LC_RPATH` la ruta absoluta del SDK en la máquina de compilación.

### Repertorio: acción compacta y búsqueda integrada

La vista de Repertorio deja de usar **Crear setlist** como bloque ancho de
acento completo. La acción pasa al header como botón compacto, mientras la
búsqueda queda inmediatamente debajo con una superficie más integrada al panel.
La tarjeta de setlist activo mantiene su barra vertical, pero baja el tinte de
fondo y borde para evitar que el acento domine toda la vista.

### Setlist: acciones secundarias más discretas

En la vista de canciones, el acceso a Biblioteca queda centrado y limitado en
ancho para evitar una banda full-width. Las filas bajan levemente la tipografía
principal/secundaria y el menú de tres puntos pierde peso cuando no está en
hover. La fila para agregar canciones mantiene affordance de drop/action, pero
reduce altura visual, opacidad del borde punteado y tamaño del icono.

### Setlist: Biblioteca en el header

El acceso a Biblioteca se mueve al botón derecho del header de canciones,
reemplazando el menú de tres puntos verticales en esa vista. El botón usa el
icono SVG musical y conserva el texto **Biblioteca** para mantener claridad. El
panel de métricas oculta su acción central de Biblioteca y reduce la altura
reservada, dejando más espacio útil para la lista. Las métricas mantienen los
mismos callbacks y estados clicables.

Al quedar Biblioteca completamente abierta, el header y listado del setlist se
ocultan en lugar de permanecer desplazados fuera del borde izquierdo. Esto evita
que sombras o bordes redondeados de la vista anterior sobresalgan dentro de la
subvista de biblioteca; durante swipe/animación siguen visibles para conservar
la transición.

## 2026-08-31

### Proyectos: guardado portable explícito y aviso de cierre

Los ajustes `.pwproj` siguen escribiéndose inmediatamente en
`project-cache/<project-id>/song-settings.json`, pero ahora el snapshot en
memoria se actualiza junto con el sidecar. Esto corrige la regresión donde la
cola comparaba contra el sidecar recién escrito, descartaba el update y dejaba
una base vieja capaz de sobrescribir cambios previos de mixer, guía o click.

`ProjectStore` registra generaciones pendientes y expone una consolidación
explícita que reconstruye el ZIP mediante `<proyecto>.tmp`, reemplazo atómico y
actualización de `project-source.json`. El menú de Biblioteca/Setlist ofrece
**Guardar proyecto** y señala cuando hay cambios pendientes; el progreso se
muestra sobre la waveform sin bloquear el hilo de UI. La ventana Standalone
intercepta tanto el botón de cerrar como la salida del sistema y permite guardar
todos los proyectos pendientes, salir conservando sólo los sidecars locales o
cancelar.

## 2026-08-19

### Waveform: cierre de estado de carga

Al terminar la permanencia mínima del indicador de carga, `WaveformBar` ahora
solicita un repintado al limpiar el estado. Así el mensaje final de fase, como
`Finalizando...`, se elimina aun cuando no ocurra otro evento de interfaz.

### Waveform: regla compacta en iPad

La regla superior del waveform en iPad usa una altura de 22 px y dibuja los
pulsos musicales del compás, omitiendo sólo subdivisiones de edición. En 4/4
se ven cuatro divisiones por compás, con el inicio destacado. Las etiquetas se
espacian dinamicamente para mantener al menos 82 px entre ellas. Seek y snap
conservan toda su precisión; la regla detallada de escritorio no cambia.

### iPadOS: orientaciones declaradas

El `Info.plist` generado declara las cuatro orientaciones para iPad y iPhone,
incluyendo `UIInterfaceOrientationPortraitUpsideDown`. Esto elimina el aviso de
Xcode sobre soporte incompleto de orientacion; la composicion de rendimiento
mantiene landscape como su layout principal.

### Persistencia legacy: controles compartidos entre setlists

Las canciones legacy ya no conservan mix/controles divergentes por setlist. Las
APIs `update*ForActiveSetlist()` siguen usando el item activo como punto de
entrada, pero propagan el cambio a todas las apariciones de la misma canción en
todos los setlists. Al agregar una canción legacy a otro setlist se copia el
estado más reciente de la canción; sólo `itemId`, orden y `transitionToNext`
siguen siendo locales al setlist.

### Standalone audio: Bluetooth output-only

El standalone fuerza el `AudioDeviceManager` a operar sin input de audio. El
panel de Audio ya no muestra entradas y el polling de dispositivo limpia
`inputDeviceName`/`inputChannels` si un setup persistido o cambio externo las
reactiva. Esto evita que audífonos Bluetooth cambien al perfil manos libres de
baja fidelidad al activar micrófono; MIDI input sigue habilitándose aparte.

### Standalone: versión visible en título de ventana

La app standalone mantiene `JucePlugin_Name` como nombre interno, pero el título
de la ventana ahora usa `PlayWorship v<JucePlugin_VersionString>`. La versión
sigue saliendo de `MTCONTROLS_APP_VERSION` en CMake; el fallback local actual es
`1.1.4`, y GitHub Actions lo sobrescribe con el semver calculado del release.

### Import wizard: sugerencias no bloquean metadata manual

En creación/edición de canción, el autocomplete del título ya no fuerza elegir
una sugerencia. Al perder foco, presionar Enter/Escape o avanzar de metadata, se
cierran las sugerencias pendientes sin borrar el título escrito manualmente.

### Biblioteca: guardado rápido de metadata/artwork en edición

El wizard de edición de canciones ahora muestra **Guardar** en el primer paso
de metadata. Ese camino llama `SongLibraryStore::updateSongMetadata` y preserva
stems, arrangement, setlists y waveform cache; sólo actualiza metadata e imagen
en la biblioteca. El flujo completo de **Guardar cambios** sigue disponible en
el paso de revisión para ediciones que modifican stems o mapeos.

### Mixer: persistencia completa de controles en proyectos

El panel de canales ahora observa periódicamente el estado real del processor y
persiste un `SongMixSnapshot` completo cuando cambia master o cualquier stem.
Esto cubre volumen, gain, mute, solo y canal de salida de un número variable de
stems, incluso cuando el cambio no viene de `onDragEnded`/`onClick` sino de MIDI
o comandos programáticos. Para canciones `.pwproj`, el camino sigue pasando por
`SongLibraryStore::updateActiveSetlistItem`, que redirige al sidecar del proyecto
con `ProjectStore::scheduleSettingsSave`; para canciones legacy mantiene el
setlist activo como antes.

### Routing: legacy songs ahora también aplican los defaults por categoría

La lambda `loadSong` en `PluginEditor.cpp` arrancaba con un
`if (!isProjectBackedSong(song)) return false;` que salía antes de
llegar a `applyRoutingDefaultsToSong`. El comentario decía que las
canciones legacy debían pasar por un "explicit optimization flow"
(la conversión a `.pwproj`), pero en la práctica la rama legacy
nunca corría: el audio thread seguía tocando los stems anteriores
con el routing default (todos mono ch=1) hasta que el usuario
tocaba el panel.

Cambios:

- Se sacó el early-return. El bloque de apertura de `.pwproj` (que
  abre el ZIP, extrae stems, y carga el manifest con los settings)
  queda envuelto en `if (isProjectSong)`. El resto del lambda es
  compartido y ya funcionaba para legacy porque el `settings.restore`
  cae a `SongLibraryStore` cuando no hay `projectSettings`.
- Con esto, una canción legacy ahora también recibe el apply del
  routing panel y deja de sonar "todo en mono ch=1" hasta que el
  usuario toque el panel de ruteo.

## 2026-08-18

### Routing: per-stem width now applied on project load

Antes, `applyRoutingDefaultsToSong` se saltaba en el lazo de
`loadSong` para canciones `.pwproj` (`if (!hasProjectSettings)`). El
mix snapshot del proyecto solo guarda `outputChannel` por stem
(`StemMixSnapshot` no tiene `outputWidth`), así que el ancho de cada
stem quedaba en el default de `resetStemControls` (1 = mono). La
audible era "todo suena por L" hasta que el usuario tocaba el panel
de ruteo, que disparaba el apply completo y reseteaba todo a estéreo.

Cambios:

- `applyRoutingDefaultsToSong` se llama siempre, también para
  proyectos. El proyecto sigue siendo la fuente de verdad para el
  canal base; el ancho lo conduce el default de la categoría.
- `refreshPadsRoutingFromSettings` ya no se invoca al final de
  `applyRoutingDefaultsToSong`: el call site es responsable. Antes se
  ejecutaba 2 veces por click en el panel (1× en el callback del
  `onChanged`, 1× al final del apply). Ahora se ejecuta 1 vez.
- Marcadores en `SongLoadProbe` con phase `routing`:
  `loadSong.gate`, `apply.enter`, `apply.stem` (prevW/newW/prevC/
  newC por stem), `apply.exit`, `panel.onChanged`, `device.change`.
  Sirven para confirmar el gate (isProject=0 vs 1) y para ver en
  el CSV por qué tocar el panel re-aplica la tabla completa.

## 2026-08-17

### Performance: setlist swap cold 8-18x, warm 3-5x, sin cold repetido

El swap entre dos proyectos del setlist tardaba 5-30s en cold
(extract secuencial del ZIP) y 700-1000ms en warm. Con los fixes
aplicados baja a 1-2s cold y 180-300ms warm. También desaparece
el patrón de "cold repetido" donde volver a una canción recién
cargada forzaba otro re-extract completo.

Cambios principales:

- `ProjectStore::open` extrae los stems del .pwproj en paralelo
  con 4 workers (`std::thread` + `juce::ZipFile` propio por
  worker). El extract secuencial corría a 16-43 MB/s sobre SSD;
  en paralelo satura el ancho de banda y baja el cold de 19-26s
  a 1-2s para proyectos de 500-800MB.
- Se quita el `pumpUi()` del callback de `onLoadProgress` en
  `PluginEditor::loadSong`. Cada `pumpUi()` forzaba un
  `runDispatchLoopUntil(1)` que procesaba mensajes UI pendientes.
  Con 38 llamadas por load, dominaba el wall-clock de los warm
  swaps (400-600ms del "other_in_lss" en el probe, todo
  pumpUi). El redraw queda para el próximo vsync.
- `ProjectStore::updateSettings` ya no re-packea el .pwproj ZIP.
  Solo persiste al sidecar (`project-cache/<id>/song-settings.json`).
  `ProjectStore::open` ya prefiere el sidecar cuando su mtime es
  más nuevo, así que el efecto neto es idéntico. Antes, el
  re-pack cambiaba el mtime del .pwproj y `cacheMatchesProject()`
  invalidaba el cache, forzando un re-extract de todos los stems
  en la siguiente carga.
- El mismo skip de re-pack se aplica en `scheduleSettingsSave`,
  `settingsSaveQueue::schedule` interno,
  `ProjectSettingsSaveJob::runJob` y `writeNow`: si los
  settings del sidecar son byte-idénticos a los nuevos, no se
  re-packea nada.
- `updateWaveform` del callback async queda deshabilitado: el
  waveform regenerado tenía variación de 1-ULP respecto al
  embebido (orden de operaciones FP no determinístico), y la
  comparación bit-exact nunca saltaba. El sidecar
  `song-waveform-cache` ya tenía el waveform actual; re-embeberlo
  en el .pwproj no aporta.
- Nuevos sub-stages en `SongLoadProbe` para visibilidad:
  `createReaderFor` se separa en `.main` y `.prefetch`;
  `project.open` instrumenta `isDecodableFlac` per-stem;
  `processor.loadSongStems` mide `pauseMaintenance`,
  `swap.lockRegion` y `progress.{0.86,0.90,0.98,1.0}` por
  separado.
- Bug del RSS del probe: `getCurrentRssKb` usaba
  `getrusage().ru_maxrss` (pico histórico monotónico, no RSS
  actual). 6 de 10 CSVs tenían un único valor de RSS en todas
  sus filas. Documentado como limitación conocida, no fixado
  en este commit (el fix usa `task_info(TASK_VM_INFO,
  phys_footprint)` en macOS o `/proc/self/statm` en Linux).
- `CMakeLists.txt`: `target_include_directories(MTControls
  PRIVATE src)` para que `src/data/*.cpp` pueda incluir
  `SongLoadProbe.h` directamente.
- Informe y script reproducible en
  `perf/analysis_20260817/INFORME.md` y
  `perf/analysis_20260817/analyze_setlist_load.py`.

Speedup medido (2026-08-17):

| Métrica | Antes | Después |
|---|---|---|
| Cold 800MB project | 19.7s | 1.0-2.0s (8-18x) |
| Warm swap | 700-1000ms | 180-300ms (3-5x) |
| Cold repetido (mismo proyecto 2da vez) | 1-2s | 180-240ms (warm) |
| Audio gaps durante load | 70-406ms | eliminados |

Causas raíz identificadas (ya no aplican):

- `copyZipEntry` single-threaded → cuello del cold.
- `pumpUi` por stem → cuello del warm.
- `updateSettings` re-paqueando el .pwproj → causa del cold
  repetido.
- `updateWaveform` bit-exact compare → re-pack por 1-ULP.

## 2026-08-16

### Política FLAC y migración visible

- El almacenamiento de stems legacy usa FLAC en todas las plataformas; los
  imports nuevos aceptan WAV, AIFF y FLAC como formatos lossless de entrada.
- Los `.pwproj` se validan estrictamente como FLAC. Se retiraron la conversión
  M4A específica de Windows y el fallback de carga hacia `sourcePath` para un
  M4A existente.
- `loadSongStems()` reproduce directamente un source FLAC y no genera una
  segunda copia FLAC en `StemCache`.
- `setDynamicGuideSettings()` reutiliza el pack ya cargado. La restauración de
  una canción deja de suspender audio y decodificar nuevamente todas las guías;
  sólo el primer uso o un cambio real de idioma recarga assets.
- Esto corrige el diagnóstico inicial del `settings.restore` de 408 ms: el
  costo dominante era la recarga redundante del pack de guía, no los stores
  atómicos del mixer. El batching queda como mejora separada, no como causa raíz.
- Las canciones legacy del setlist se muestran atenuadas y exponen
  **Optimizar**. Confirmar pregunta dónde guardar el proyecto; la opción
  **Biblioteca local** usa el directorio `projects` de la biblioteca de la app.
  Luego lo valida, preserva setlists/ajustes/transiciones y elimina el audio
  anterior mediante staging transaccional.
- Las rutas de activación desde setlist y Biblioteca bloquean la carga legacy;
  `PluginEditor` conserva un guard adicional para callers futuros.
- Los pools de click y guía aceptan WAV/AIFF/FLAC/MP3, sin ALAC/M4A. El export
  de mezcla conserva WAV, MP3 y M4A porque es un formato de salida al usuario.
- Se confirmó que no existe un bug de anuncios de sección; no se modificaron
  `DynamicGuideTiming` ni `SectionScheduler`.

## 2026-08-13

### Agregado

- Nuevo módulo [`SongLoadProbe`](processor/SongLoadProbe.md) —
  telemetría del UI thread para la carga de canciones. Mide los
  sub-stages de `loadSong` y `loadSongStems` (per-stem:
  `cache.lookup`, `createReader`, `prepareToPlay`, `waitForReady`)
  + totales (`waitForReady.total`, `fadeOut.wait`,
  `suspendProcessing.wait`, `bungee.prepare`) +
  `StemCache.isValid` hit/miss con razón detallada
  (`noFlac`, `noMeta`, `badMeta`, `sizeMismatch`, `mtimeMismatch`).
  CSV en `~/Library/Logs/MTControls/song_load_<ts>.csv`. Memory
  snapshot (RSS) por fila via `getrusage`.
- Documento canónico de referencia para [`StemCache`](audio-systems/StemCache.md)
  (no existía antes). Cubre la cache FLAC, el sidecar `meta.txt`,
  el bug del split-path legacy, y el fix de `migrateLegacyCacheDirectories`
  3-stage (recuperación de espacio + walk recursivo + cleanup).
- Sección "Companion: `SongLoadProbe`" en
  [`AudioDiagnostics.md`](processor/AudioDiagnostics.md) explicando
  cuándo usar cada uno y cómo cruzar los CSVs.

### Modificado

- [`StemCache::migrateLegacyCacheDirectories`](audio-systems/StemCache.md#el-fix-sanitizecachekey--migratelegacycachedirectories)
  reescrito para manejar el caso de **directorios anidados** (cuando
  el bug del `/` partió el cache en dos sub-niveles, e.g.
  `decoded/Al Que Es Digno /` + `decoded/Al Que Es Digno / Vencio/`).
  El walker original era non-recursive y dejaba el cache sin migrar.
  Nueva versión: 3 etapas idempotentes (recupera espacio al final del
  padre si el hijo empieza con espacio, walk recursivo post-order,
  limpieza de subdirs vacíos).
- [`AudioDiagnostics.md`](processor/AudioDiagnostics.md): actualizada
  la sección de "Artefactos relacionados" para mencionar el
  companion.

### Diagnóstico

- Caso "Al Que Es Digno / Vencio" (proyecto .pwproj 449 MB, 12 stems
  M4A, setlist "Culto domingo 2 Agosto"):
  - **Causa raíz**: el cache FLAC vivía en
    `decoded/Al Que Es Digno / Vencio/` (split-path legacy) pero
    `sanitizeCacheKey("Al Que Es Digno / Vencio")` busca en
    `decoded/Al Que Es Digno _ Vencio/`. Miss permanente.
  - **Consecuencia**: cada carga usaba el M4A nativo (1.2-2.9 ms
    de decode por stem por bloque) y disparaba `precomputeAsync` en
    background sin beneficio para esa carga.
  - **Después del fix de migración**: 4/12 stems con `hit=1`
    (FLAC válido, 0.5 ms `createReader` vs 2.5 ms con M4A), 8/12
    con `miss.mtimeMismatch` (m4a source re-extraído del .pwproj
    cambió mtime, FLAC precomputado quedó stale). Pendiente:
    trigger re-precompute cuando el miss es mtime-only.
  - **Hallazgo secundario**: `settings.restore` (408 ms, 34% del
    total de 1.2 s) domina el swap. Son ~60 operaciones atómicas
    per-stem (setStemVolume/Gain/Mute/Solo/OutputChannel) que
    iteran sobre los stems y hacen JSON lookups por
    `getActiveSetlist*`. Candidata a batching.

### Estructura

```
docs/okf/
├── index.md                    # Raíz de la KB
├── log.md                      # Este archivo
├── processor/
│   ├── index.md
│   ├── MTControlsAudioProcessor.md
│   ├── AudioDiagnostics.md     # actualizado
│   └── SongLoadProbe.md        # NUEVO
├── engine/
│   ├── index.md
│   ├── StemMixEngine.md
│   ├── StemTrack.md
│   ├── TransportClock.md
│   └── SectionScheduler.md
├── audio-systems/
│   ├── index.md                # actualizado
│   ├── Metronome.md
│   ├── Pads.md
│   ├── DynamicGuide.md
│   ├── OutputChannelCatalog.md
│   ├── StemOutputRouter.md
│   ├── SongMixExporter.md
│   └── StemCache.md            # NUEVO
├── midi/
│   ├── index.md
│   ├── MidiTargets.md
│   └── MidiBindings.md
├── transport/
│   └── Transport.md
└── integrations/
    ├── index.md
    └── LyricsControllers.md
```

## 2026-07-26 (tarde)

### Agregado

- Documento canónico de referencia [`AudioDiagnostics`](processor/AudioDiagnostics.md) — descripción unificada del benchmark lock-free (6 stages, gap detector, worker drain, decode, Bungee) con tabla de optimizaciones medidas, CSVs, y guía para extender.

## 2026-07-26

### Agregado

- Benchmark de performance para `processBlock`: marcas por etapa
  (preRead, readNext, decodeRender, mixStems, mixOutput, rampTransition)
  serializadas a `audio_*.csv` en microsegundos.
- Detector de underrun (gap event): cuando el callback se ejecuta más
  tarde que `kDefaultGapThresholdMs = 15ms` después del callback
  anterior, se emite un evento a `audio_gaps_*.csv` con
  `wall_ms`, `delta_ms`, `song_position_sec`, `stem_count`,
  `is_playing`. Permite mapear "mini cortes" audibles a la posición
  de la canción donde ocurrieron.
- `AudioDiagnostics::getStats()` ahora reporta el conteo acumulado de
  gaps y el peor gap observado en milisegundos.

## 2026-07-14

### Agregado

- Base de conocimiento OKF inicial
- Documentación de MTControlsAudioProcessor
- Documentación de StemMixEngine y StemTrack
- Documentación de TransportClock y SectionScheduler
- Documentación de MetronomeEngine
- Documentación de PadsEngine
- Documentación de DynamicGuideEngine
- Documentación de MIDI Targets y Bindings
- Documentación de Transport API

## 2026-07-18

### Agregado

- Sección de integraciones externas para software de letras
- Documentación de FreeShow como primer destino compatible
- Contrato técnico inicial para disparo de cues externos desde secciones
- Priorización inicial de ProPresenter como primer destino compatible
- Transición de canciones `fade-through-zero` con handoff en silencio
- Verificación de read-ahead antes de publicar una canción nueva
- Restauración completa de mix, routing, click, guía y arrange antes del fade-in

### Estructura

```
docs/okf/
├── index.md                    # Raíz de la KB
├── log.md                      # Este archivo
├── processor/
│   ├── index.md
│   └── MTControlsAudioProcessor.md
├── engine/
│   ├── index.md
│   ├── StemMixEngine.md
│   ├── StemTrack.md
│   ├── TransportClock.md
│   └── SectionScheduler.md
├── audio-systems/
│   ├── index.md
│   ├── Metronome.md
│   ├── Pads.md
│   └── DynamicGuide.md
├── midi/
│   ├── index.md
│   ├── MidiTargets.md
│   └── MidiBindings.md
├── transport/
│   └── Transport.md
└── integrations/
    ├── index.md
    └── LyricsControllers.md
```
## 2026-09-08

### Cambiado

- Layout de telefono compartido entre iOS y Android basado en espacio util.
- Area segura dinamica para Dynamic Island, cutouts y barras del sistema.
- Tres escalas de top bar y waveform para telefonos pequenos, regulares y grandes.
- iPhone pasa a soportar exclusivamente ambas orientaciones landscape.
- Los limites minimos del editor movil dejan de heredar `940 x 600` de desktop.

## 2026-09-12

### Cambiado

- Fondo del editor: en Android se sustituye `drawStudioBackground` por un
  `fillAll(UITheme::appBgBottom)`. El WaveformBar y el ChannelGridPanel son
  opacos y cubren el area visible del fondo, asi que el gradiente, halo,
  viñeta y ruido tiled no aportan nada visible pero costaban ~50-60 ms por
  frame de playback en el emulador (API 36, 1080x2400). iOS y escritorio
  conservan el fondo completo.
- En el emulador la mediana por frame de playback bajo de ~750 ms a
  ~150 ms (4-5x), los `Missed Vsync` de 64 bajaron a 2, y el histograma se
  concentra en 150-200 ms (98% de los frames).

## 2026-09-25

- Control remoto integrado en la interfaz original: selector Local/Cliente/Servidor
  en Settings, datos remotos para los mismos strips, VU, transporte, onda y listas.
- Eliminada la vista remota separada y su badge. Biblioteca remota en memoria,
  comandos autoritativos y telemetría de VU/posición independiente de los snapshots.
- Click Dyn dispone de acumuladores de pico separados para UI local y LAN.
- Descubrimiento Apple mediante Bonjour nativo: corrige la búsqueda permanente
  en iPhone físico causada por multicast directo sin entitlement. El backend
  Android/Windows anuncia también TXT y A para resolución interoperable.
  Errores de permiso visibles en Settings y prueba de conexión entre backends.
- Faders remotos: previsualización inmediata de volumen/gain hasta recibir el
  snapshot posterior al ack del último movimiento. Los sliders conservan el
  gesto activo y el servidor agrupa el guardado de mezcla tras 150 ms sin cambios.
  Regresión verificada para respuestas atrasadas, rechazos, desconexión y cambio
  de canción; las tres pruebas de sesión y descubrimiento pasaron.
- Bloqueos de UI del servidor remoto: envío/JSON por conexión fuera del message
  thread, caché de portadas, catálogo solo cuando cambia y firma de controles
  sin serializar biblioteca/portadas. Ráfagas de volumen/gain agrupadas por peer
  con barreras para acciones discretas; máximo 32 eventos procesados por tick.
  Cliente TCP que deja de leer: publicación máxima inferior a 0,05 ms y cliente sano
  actualizado; ráfaga de 122 comandos reducida a 4 eventos conservando el orden.
  Snapshots incrementales negociados: canción/portada y ajustes solo cuando
  cambian, con reconstrucción en el cliente y compatibilidad con clientes previos.
  Prueba local en la app compilada: 477 comandos/477 confirmaciones en 16 s,
  p95 de confirmación 34,15 ms, sin desconexiones; tráfico de 47,7 MB a 4,1 MB
  frente al envío completo. Tres pruebas de sesión/descubrimiento aprobadas.
- Cliente indica `BUSCANDO`, `CONECTANDO`, `REMOTO` o `RECONECTANDO` en una etiqueta pequeña
  sobre la onda, incluso antes de recibirla. El estado conectado exige un
  snapshot aplicado; la etiqueta no cambia layout ni captura gestos.

## 2026-10-01: aplicación nativa

- El target de distribución usa `juce_add_gui_app` y `DocumentWindow`
- Audio y MIDI pasan por un único manager y `AudioProcessorPlayer`; se mantienen
  las preferencias y los flujos externos de proyecto/salida
- Contrato: [Aplicación nativa](processor/NativeApplication.md)

- 2026-10-05: bienvenida opcional para miembros de Teams y curso de Nube →
  descarga → setlist, con progreso por cuenta/fuente/alta y sin interrupción
  durante reproducción. Contratos: Teams y LearningGuide.


## 2026-10-05 — Pertenencia y baja de Teams

Pertenencia única transaccional, salida voluntaria y recibos persistentes de baja.
Confirmaciones y limpieza por procedencia al detener reproducción, preservando
archivos personales. Ver integrations/Teams.md.


## 2026-10-05 — Correos de Teams con Resend

Backend: alta, retirada y salida generan correo transaccional con cola durable,
reintentos e idempotencia. 60 pruebas aprobadas. Vistas previas en
output/teams-email-preview; prueba de proveedor dirigida solo al buzón técnico
de Resend, sin notificar a miembros existentes.

## 2026-10-17 · Letras independientes / FreeShow desktop

- Contrato actualizado para marcas independientes de secciones, catálogo real
  por show/arreglo y selección absoluta por IDs y aparición.
- REST verificado contra FreeShow v1.6.6; clave sólo en memoria y destinos LAN.
- Persistencia portable, despacho desde segmentos efectivos del transporte y
  estado separado de enviado/confirmado por la API.

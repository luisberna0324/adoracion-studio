# Vista de ensayo: revisión de experiencia e integración

Estado: implementación inicial integrada en `codex/iosnative`; pendiente de validación en dispositivos físicos.
Fecha: 2026-09-29.

## Objetivo y criterio principal

Ensayar una canción destacando un instrumento o grupo —bajo, batería, guitarras,
teclados u otros disponibles— y regulando cuánto se escucha frente al resto.
Esta es la función central de la primera versión. Transporte, tono, tempo y
repetición de secciones acompañan ese flujo, especialmente en teléfono vertical.

La vista debe conservar el lenguaje visual y de interacción de la aplicación.
Se reutilizan componentes, iconos, estilos y acciones existentes; cuando un
control esté encerrado en un panel, se comparte su implementación antes de
introducir otro equivalente. Cambiar de vista conserva la sesión de reproducción.

## Alcance de esta primera versión

Incluye selección de grupo instrumental, nivel del grupo y del acompañamiento,
regreso a la mezcla normal, navegación, distribución vertical, transporte,
forma de onda, secciones, loop, tono, BPM y click con el estilo de controles actual.

Letras, acordes, ChordPro, editores de hojas e integraciones con catálogos quedan
pospuestos por indicación del usuario. No forman parte del diseño, las etapas ni
los criterios de aceptación actuales. Tampoco se reservan espacios vacíos o
acciones deshabilitadas para esas funciones.

«Partes aumentadas» se refiere al protagonismo sonoro de instrumentos o grupos.
Por ejemplo, destacar el bajo conservando su nivel y bajando el acompañamiento;
los dos niveles pueden ajustarse de forma independiente.

## Base verificada en el proyecto

Los contratos de referencia son:

- [Mobile Layout](../okf/ui/MobileLayout.md): orientación, áreas seguras,
  encabezado móvil y tamaños táctiles.
- [Input Controls](../okf/ui/InputControls.md): campos y selectores compartidos.
- [Popovers](../okf/ui/Popovers.md): presentación anclada, foco y cierre.
- [AppModal](../okf/ui/AppModal.md): entrada breve y confirmaciones.
- [Transport](../okf/transport/Transport.md): reproducción, tempo, saltos y loop.
- [StemTrack](../okf/engine/StemTrack.md) y
  [StemMixEngine](../okf/engine/StemMixEngine.md): volumen por stem y mezcla.
- [Control remoto LAN](../okf/integrations/RemoteControl.md): sesión compartida,
  comandos y estado autoritativo.

`TopBar` ya contiene transporte, selector de canción, tono, BPM y click. Sus
controles usan `PlaybackSession`. `HeaderIconButton`, `UITheme`, `makeAppFont`,
los iconos y los presentadores comunes están en `UiCommon`.

Los stems tienen `stemId`, categoría y nombre. El mixer actual crea un strip por
stem incluido; todavía no ofrece esta función de énfasis por grupo. Los faders
actuales guardan cambios en la mezcla de la canción, por lo que el ajuste temporal
de ensayo requiere un estado específico y no debe usar esa persistencia directamente.

La orientación vertical actual del teléfono depende de Biblioteca y Configuración.
Ensayo requiere ampliar esa política para resolver la vista de fondo y los paneles
abiertos. El componente `WorkspaceTabsBar` existe, pero no está conectado al editor
actual; no se presupone una barra de pestañas visible para entrar en Ensayo.

`AppAudioMode::Rehearsal` clasifica las salidas de audio disponibles. La nueva vista
tendrá estado de navegación propio y no cambiará ese modo ni el ruteo.

## Recorrido propuesto

1. Abrir **Ensayo** desde una acción de la barra principal con los mismos estilos
   de botón. En espacios compactos se integra en el patrón existente de acciones
   adicionales. El prototipo debe confirmar que la entrada resulta fácil de encontrar.
2. Mantener la canción cargada y el setlist activo. La tarjeta de canción abre
   el mismo selector del setlist; desde allí se conserva el acceso a Biblioteca.
3. Elegir el grupo que se quiere escuchar con más claridad, por ejemplo **Bajo**.
   Mostrar qué pistas lo componen y activar un balance de ensayo que destaque ese grupo.
4. Ajustar los faders **Bajo** y **Resto de instrumentos**. Al elegir Batería,
   el mismo flujo se aplica a las pistas de ese grupo manteniendo su balance interno.
5. Reproducir, ubicar el fragmento y activar el loop con las acciones habituales.
   Tono, BPM, click y el mixer detallado siguen accesibles.
6. Pulsar **Mezcla normal** para retirar el balance de ensayo y comparar. Cerrar
   Ensayo también retira ese balance temporal, conservando canción, posición,
   tempo, tono y loop. Abrir un popover, Biblioteca o Configuración no lo retira.

Abrir o cerrar esta vista no inicia, pausa ni reinicia la canción. Los comandos
explícitos del transporte y el cambio de canción conservan su comportamiento actual.

## Distribución

### Teléfono vertical

- Encabezado móvil de 56 pt, título Ensayo y cierre del componente compartido.
- Información compacta de canción, tono y tempo. La tarjeta conserva portada,
  título, artista y acceso al selector actual.
- Banda fija de controles de reproducción; el layout se reparte en filas cuando
  sea necesario para mantener los objetivos táctiles. BPM y click permanecen accesibles.
- Bloque principal para seleccionar el instrumento/grupo y ajustar dos niveles:
  el grupo elegido y el resto de instrumentos. Usa los faders y estilos del mixer.
- Acción visible **Mezcla normal**, con estado activo claro mientras hay un
  balance de ensayo aplicado.
- Forma de onda compacta con posición, secciones y acciones de salto y loop
  existentes. Debe permitir reconocer el fragmento que está sonando y el que se repite.
- Mixer detallado accesible desde su botón, con los strips y desplazamiento
  habituales. Al cerrarlo se vuelve al grupo y balance elegidos.
- Transporte fijo y accesible mientras se desplazan la onda o los canales.

El layout deberá probarse con nombres largos, teléfono pequeño y muchos canales.
No se resolverá la falta de espacio reduciendo los objetivos táctiles.

### Tablet y escritorio

Conservar la barra principal y organizar el área de contenido para selección de
grupo, sus niveles, onda y secciones. El mixer detallado puede permanecer visible
cuando haya espacio. La distribución se adapta al espacio disponible y a los perfiles de
densidad existentes. Esta primera propuesta conserva el comportamiento horizontal
actual de las tablets.

### Orientación y paneles

En teléfono, Ensayo se presenta en vertical. Abrir Biblioteca o Configuración desde
Ensayo mantiene esa orientación; cerrarlas regresa a Ensayo. Salir de Ensayo vuelve
al layout horizontal de reproducción. La orientación se resuelve desde el editor,
con el mecanismo diferido existente en iOS, sin reconstruir la sesión de audio.

## Reutilización de controles

| Necesidad | Integración prevista |
|---|---|
| Reproducir, pausar, detener, anterior y siguiente | Controles, callbacks y estados de `TopBar`; mismo significado, atajos y etiquetas accesibles. |
| Tono, tempo y click | Mismos controles y popovers; valores leídos desde `PlaybackSession`. |
| Fade Down/Fade Up | Conservar la acción y estado existentes; no reasignarlos a funciones de ensayo. |
| Elegir canción | Tarjeta y selector del setlist existente; misma ruta de carga. |
| Elegir grupo de ensayo | Selector compartido con las categorías presentes y selección visible; cada opción identifica sus pistas. |
| Nivel del grupo y del resto | Faders con el `ChannelStripLookAndFeel` actual, etiquetas claras y valores visibles. |
| Mezcla normal | Botón con el estilo y estado compartidos que desactiva el ajuste temporal de ensayo. |
| Secciones y repetición | Reutilizar acciones de salto y loop del arrange y sus estados activo/pendiente. |
| Mezclar | `ChannelGridPanel` y `ChannelStripCard`, manteniendo faders, Mute, Solo y medidores. |
| Volver, cerrar y acciones de encabezado | `HeaderIconButton` y métricas compartidas. |
| Opciones y ajustes | `presentActionPopover()` y los campos/selectores compartidos; `AppModal` para mensajes y confirmaciones breves. |

`TopBar::IconButton` y algunos popovers son privados. La implementación deberá
reorganizar o extraer esas piezas para compartirlas, evitando copiar su dibujo
y crear callbacks paralelos. Cada control tendrá una sola fuente de estado.

Colores y fuentes proceden del tema activo. La vista debe responder a los cambios
de tema e idioma. Objetivos táctiles de al menos 44 pt en iOS y 48 dp para las
acciones de encabezado Android, siguiendo los contratos correspondientes.

## Balance de ensayo por grupo

### Grupos y controles

- Resolver grupos desde las categorías existentes y los IDs estables de stems
  incluidos. Una categoría puede contener varias pistas.
- Mostrar sólo grupos disponibles. Las pistas sin clasificación conservan sus
  nombres y permiten una selección explícita; no inferir pertenencia sólo por texto.
- La primera versión selecciona un grupo a la vez. El selector indica qué pistas
  se verán afectadas y permite revisar la composición antes de ajustar sus niveles.
- Dos controles independientes: nivel del grupo elegido y nivel del resto. Para
  destacar el grupo, se conserva su nivel y se atenúa el acompañamiento. El usuario
  también puede bajar el grupo para practicar su parte con el acompañamiento.
- El ajuste mantiene las proporciones entre pistas del mismo grupo. No fija todos
  sus faders al mismo valor ni modifica automáticamente Mute o Solo.
- Click y guía, tanto grabados como dinámicos, conservan sus controles propios y
  se excluyen del acompañamiento. Master, pads y ruteo mantienen sus ajustes.

### Mezcla temporal y continuidad

- Propuesta técnica: conservar el volumen base de cada stem y aplicar un factor
  temporal según pertenezca al grupo seleccionado o al acompañamiento. Inicialmente
  los factores van de silencio a nivel base; el énfasis se obtiene por atenuación
  relativa, evitando añadir ganancia automáticamente.
- Aplicar el factor en el camino de audio correspondiente y respetar Mute, Solo,
  Fade, procesamiento y salidas existentes. Debe funcionar en las rutas de render
  utilizadas por la canción; no basta modificar números en la interfaz.
- Suavizar cambios de nivel y de grupo durante la reproducción. Cambiar de grupo
  recalcula los factores desde la mezcla base, sin acumular atenuaciones sucesivas.
- El estado de ensayo muestra grupo activo y niveles. Los medidores reflejan el
  audio resultante; los faders individuales siguen representando la mezcla base.
- **Mezcla normal** deja los factores en unidad y desactiva el grupo. Retirar el
  ajuste no sobrescribe las ediciones explícitas que el usuario haya hecho en el
  mixer detallado: esas ediciones conservan la persistencia habitual.
- Los factores temporales no se guardan como volúmenes de canción, no se publican
  como una versión de ajustes Cloud ni se incorporan a una exportación normal.
- Al cambiar de canción o salir de Ensayo se desactiva el grupo. Las ventanas
  secundarias conservan el estado para poder regresar al mismo ensayo.

## Comportamiento durante el ensayo

- Posición, sección activa, salto pendiente y loop se muestran desde el estado
  existente de la sesión. La vista no mantiene un transporte paralelo.
- Saltar y repetir conservan la semántica y los tiempos del arrange actual.
  Desplazar la onda o el mixer conserva sus gestos actuales y no activa botones.
- El tono representa la transposición de audio existente; BPM conserva el cambio
  de velocidad y el comportamiento del click actual.
- Mute, Solo y volumen mantienen su significado. Se puede modificar la mezcla
  mientras se repite un fragmento sin reiniciar el loop.
- Cambiar de vista conserva los ajustes base y la reproducción; al salir de Ensayo
  se retira suavemente su balance temporal. Guardar y recuperar los ajustes base
  utiliza la persistencia existente.
- Los controles muestran estados activos, pendientes y deshabilitados con el
  mismo lenguaje visual que la pantalla de reproducción.

## Estados necesarios

- Sin canción: acceso al selector existente y disponibilidad de controles según
  las reglas actuales.
- Canción sin secciones: onda y transporte funcionales; las acciones que necesitan
  una sección respetan su estado de disponibilidad actual.
- Canción cargando: reutilizar la indicación de carga y evitar comandos contra
  una canción anterior.
- Canción cambiada: actualizar onda, secciones, tono, tempo y canales conjuntamente.
- Grupo silenciado por Mute o Solo: indicar por qué no se oye y conservar esas
  decisiones del mixer; elegirlo no lo desmutea de forma implícita.
- Canción sin pistas instrumentales separadas: mostrar las pistas realmente
  disponibles. Esta función no separa bajo, batería u otros instrumentos de un audio mezclado.
- Cliente remoto desconectado: conservar el estado visible y aplicar las restricciones
  existentes de comandos durante reconexión.

La experiencia usa los proyectos y datos de canción actuales y funciona con
canciones locales sin conexión. Esta etapa no necesita un formato nuevo de contenido.

## Implementación por etapas

1. **Prototipo visual integrado.** Componer Ensayo con los componentes y el tema
   reales en teléfono, tablet y escritorio. Priorizar selector de grupo, dos faders,
   Mezcla normal y transporte; revisar onda, popovers y acceso al mixer detallado.
2. **Vista y navegación.** Introducir el estado de vista, layout vertical,
   orientación y reutilización de controles. Conservar una única `PlaybackSession`.
3. **Balance por grupo.** Resolver pertenencia por IDs y categorías, añadir estado
   temporal y factores de nivel suavizados, restauración y protección de la mezcla
   persistida. Exponer el estado y acciones a través de la sesión compartida.
4. **Flujo de práctica.** Integrar onda, secciones, salto, loop y mixer con las
   acciones actuales; verificar ajustes de tono, tempo y click durante reproducción.
5. **Compatibilidad y validación.** Probar cambios de grupo y canción, mezcla, temas,
   idiomas, proyectos antiguos, orientación y control remoto.

En control remoto, transporte y mixer reutilizan `PlaybackSession`. El balance
por grupo es una capacidad nueva: necesita comandos y estado temporal explícitos,
compatibilidad de protocolo y validación de IDs en el servidor. No se debe simular
enviando ajustes persistentes de volumen a cada pista. Un servidor que no soporte
esa capacidad mantiene disponible la interfaz habitual y deshabilita el balance
por grupo con una explicación. El servidor conserva la autoridad; los clientes
reciben estado y onda, sin descargar stems ni producir audio remoto localmente.

El balance remoto afecta la reproducción del servidor y todos sus clientes ven
el mismo grupo activo. La interfaz debe dejar claro ese alcance; no representa
una mezcla personal de auriculares en cada cliente.

## Criterios de aceptación

- Los controles mantienen apariencia, nombre, estado y acción de la vista existente.
- Entrar/salir, girar, abrir un panel y volver no interrumpe ni reinicia el audio.
- Biblioteca y Configuración regresan a la vista y orientación que corresponden.
- Elegir Bajo o Batería afecta sólo las pistas del grupo y del acompañamiento
  correspondientes, respetando click, guía, Mute, Solo y balance entre pistas.
- Los dos niveles permiten destacar o reducir el grupo; cambiar de grupo no
  acumula cambios y Mezcla normal retira exactamente los factores de ensayo.
- Cambios de nivel/grupo y cierre de Ensayo no producen discontinuidades audibles.
- La mezcla guardada, versiones Cloud y exportaciones normales no incorporan
  el énfasis temporal; las ediciones explícitas del mixer conservan su comportamiento.
- El usuario puede ubicar un fragmento, reproducirlo, repetirlo y ajustar su instrumento
  sin abandonar el flujo de Ensayo.
- Arrastrar la onda o el mixer no dispara botones ni cambia su significado habitual.
- Popovers, teclado y área segura no ocultan controles esenciales.
- Tono, BPM, click y mezcla base coinciden con la sesión en las dos vistas.
- Cliente y servidor muestran el mismo estado de ensayo; una desconexión o cambio
  de canción no aplica factores a IDs obsoletos ni deja una mezcla local divergente.
- Los ajustes mantienen su persistencia actual y los proyectos existentes funcionan.
- La revisión visual incluye temas disponibles, nombres largos y pantallas pequeñas.
- Validación de audio/estado con pruebas dirigidas y revisión visual en macOS,
  iPhone/iPad y Android; registrar por separado compilación, simulador y dispositivo físico.

Al implementar, actualizar `docs/okf/` con el comportamiento confirmado de Ensayo
y orientación, y cualquier otro contrato que efectivamente cambie.

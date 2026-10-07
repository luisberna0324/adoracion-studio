---
type: UI Contract
title: Guías de aprendizaje
description: Cursos interactivos sobre los controles reales con progreso local por dispositivo
resource: src/LearningGuideIntegration.cpp
tags: [learning, onboarding, ui, mobile, tablet, desktop, accessibility]
timestamp: 2026-10-03
---

# Guías de aprendizaje

## Acceso

`Aprender PlayWorship` abre un catálogo de cursos desde:

- El menú `Más` de la barra superior en layouts táctiles de teléfono y tablet.
- Configuración en todas las plataformas. La portada agrupada contiene la
  entrada de aprendizaje y el layout de escritorio ancho incluye `Abrir guías`
  en General. El catálogo puede permanecer dentro de Configuración.

El sistema es optativo. Abrir el catálogo o una guía existente no carga una canción ni activa
reproducción, edición, Pads o control remoto. Tampoco cambia la mezcla,
tempo, tono, canción seleccionada ni el modo Local/Cliente/Servidor.
Las acciones de prueba se realizan sobre los controles habituales de la app.

La entrada explícita **Probar con una canción demo** prepara y carga una canción
local incluida antes de comenzar su curso. Detiene la reproducción anterior;
no inicia Play. Ver [Canción demo](DemoSong.md).

## Cursos y contenido

`makeLessons(phone, english)` define seis cursos con IDs estables. El
catálogo resuelve el idioma español o inglés y el layout táctil de teléfono
al abrirse. Los títulos, ayudas y tareas no se usan como identidad persistida.
Si se cambia el idioma desde Configuración, la guía actualiza sus textos al
volver, conservando el curso y el paso.

| ID | Curso | Contenido |
| --- | --- | --- |
| `demo-song` | Probar con una canción demo | Play, Pausa, reanudar, fader, Mute/Solo, loop de sección y Stop. Teléfono añade abrir Ensayo, enfoque/restauración, crear/borrar A–B y cerrar. |
| `first-steps` | Primeros pasos | Espacio de reproducción, Biblioteca/setlist, Play/Pausa, onda y Stop. |
| `mix-click` | Mezcla, clic y tono | Faders, Mute/Solo, metrónomo, tempo, tono y Pads. |
| `practice-sections` | Ensaya por instrumento, en teléfono | Acceso a Ensayo desde Más, navegación y zoom de la tarjeta, escucha por instrumento y loop A–B temporal. |
| `practice-sections` | Secciones y automatización, en escritorio/tablet | Edición de secciones, copia con referencia, borrador de automatización y acciones de la barra flotante. |
| `remote-server` | Preparar un servidor | Diez pasos: preparar equipos, abrir Configuración y Control remoto, activar Servidor, revisar su alias y estado, conectar un cliente, cerrar Configuración, cargar una canción y comprobarla en el cliente. |
| `remote-client` | Conectarse como cliente | Doce pasos: preparar equipos, abrir Configuración y Control remoto, activar Cliente, elegir/conectar servidor, confirmar estado, cerrar Configuración, recibir una canción, ubicar REMOTO, probar Play, probar Stop y terminar. |

`remote-server` y `remote-client` reemplazan al curso general `settings-remote`.
Su progreso se guarda de forma independiente; una finalización antigua no
declara terminado ninguno de estos recorridos. No incluyen una lección de
reconexión. Requieren dos equipos reales en la misma red para practicar el flujo
completo: el servidor reproduce el audio y el cliente utiliza sus controles.

`first-steps`, `mix-click` y `practice-sections` mantienen sus explicaciones y
navegación manual. Son introducciones; sus ejercicios todavía no tienen
condiciones de ejecución verificadas. Los dos recorridos remotos incorporan
condiciones reales de avance en sus pasos de acción y pasos explicativos sin
condición.

El curso de teléfono enseña los gestos de **Ensayo**; no atribuye esos gestos
de doble toque por compás a la onda principal. Sus marcas A y B se ajustan al
pulso más cercano y su región es temporal. Véase
[MobileLayout](MobileLayout.md) y [Transport](../transport/Transport.md).

Escritorio y tablet enseñan las secciones del mapa de guía. Si aún no existen,
la ayuda indica marcar un punto en la regla y agregar una con `+`. La
automatización conserva el contrato de valores parciales por sección, borrador
fijado por ID y barra flotante; no graba curvas. Véase
[SectionAutomation](SectionAutomation.md).

Las explicaciones remotas respetan el estado real de
[RemoteControl](../integrations/RemoteControl.md). La lista de servidores y la
etiqueta de la onda pertenecen al modo Cliente. La guía no selecciona un
servidor ni cambia de modo por cuenta propia.

`Step::needsSong` añade una ayuda cuando falta una canción; por sí mismo no
bloquea el avance. `Step::condition` establece qué hecho debe comprobar el
editor antes de habilitar Continuar. Biblioteca, Configuración y Pads se
explican también sin una canción cargada; las prácticas sobre audio necesitan
que el usuario cargue una por el flujo habitual o elija explícitamente la demo.

## Validación de los recorridos remotos

`StepFacts` y `evaluateStepCondition` describen el estado requerido. La
integración comprueba la apertura de Configuración, su página remota, el modo
vigente, el servidor escuchando, un cliente conectado, el snapshot inicial
aplicado, el cierre de Configuración y la canción disponible. Encontrar el
nombre de un servidor o abrir el WebSocket no equivale a una conexión lista.

Al reanudar un paso que necesita una sesión remota, `RemotePrerequisite` y
`remoteStepPrerequisite` comprueban los requisitos que falten antes de exponer
su ejercicio. La ayuda conduce a abrir Configuración, entrar en Control remoto,
elegir Cliente/Servidor y esperar conexión o disponibilidad del servidor,
según el estado real. Si Configuración sigue abierta cuando el ejercicio
necesita el reproductor, resalta su botón cerrar. Cada apertura, selección y
cierre lo realiza el usuario sobre el control existente.

Estas ayudas temporales conservan el ID del paso persistido. Cuando se recuperan
sus requisitos, el resaltado vuelve al ejercicio original y el avance depende
de su condición. También se utilizan si se pierde la sesión durante ese paso;
no añaden pasos de reconexión ni completan el ejercicio al recuperar la red.

Los pasos de Play y Stop requieren una acción enviada desde este cliente
mientras ese paso está activo. `RemoteLearningAction` observa el ID del
comando, la canción, su ACK aceptado y un snapshot con secuencia posterior al
ACK que confirme el estado esperado. Un estado cambiado por otro cliente,
un ACK ajeno, un snapshot anterior o una acción rechazada no completa el
ejercicio. La intención del toggle se toma del estado al enviarlo: pulsar
Play/Pausa mientras ya se reproduce es Pausa, y no acredita el ejercicio de
Play aunque otro cliente reanude antes del snapshot siguiente. Play exige
una intención propia de iniciar y reproducción confirmada; Stop exige detención confirmada
y admite comprobarla aunque la canción ya estuviera detenida.

La prueba de acción caduca a los cinco segundos. Desconectar, cambiar de canción,
cambiar de paso o cerrar la guía descarta la evidencia pendiente o confirmada.
Reconectar por sí solo no recupera la prueba del comando anterior. La guía
puede mostrar una tarea pendiente, un error remoto o una ayuda de permiso de
Red local, y permite pausar para resolverlo en el sistema.

Completar un requisito habilita Continuar, pero no salta automáticamente al
siguiente paso. La guía no abre páginas, elige un servidor, cambia de modo ni
envía acciones por su cuenta: observa los controles existentes.

## Interacción y navegación

Cada paso presenta título, descripción y una tarea breve. La guía oscurece
el resto de la vista y resalta el control o superficie relacionado con el paso.
La tarjeta se coloca dentro del área segura y evita el destino cuando hay
espacio. Tiene hasta 300 px de alto en pantallas altas y hasta 210 px en
teléfonos horizontales; reduce su altura si así puede dejar libre el destino.
Las descripciones largas tienen desplazamiento interno, conservando
accesibles las acciones de la guía.

`LearningGuideOverlay::hitTest` devuelve `false` dentro del destino resaltado
si éste queda fuera de la tarjeta. JUCE entrega ese clic, toque o gesto al
**componente real** situado debajo. La zona oscurecida sí intercepta acciones
para mantener el foco del paso. La guía no duplica controles ni simula sus
callbacks.

Durante los ejercicios de mezcla, loop y enfoque de instrumento de la demo,
`setSupplementaryTarget` mantiene accesible Play/Pausa para volver a escuchar.
Su hueco es independiente del destino del ejercicio: el espacio entre ambos
sigue interceptando acciones. Los dos destinos se recortan al área segura y
la tarjeta siempre conserva prioridad sobre cualquier hueco. Cambiar de paso
o volver al catálogo elimina este permiso adicional.

Los destinos se recalculan al actualizar el editor, cambiar tamaño, orientación
o área segura. Se resuelve el destino visible de la barra superior, una franja
instrumental del mixer, la onda, Pads o Ensayo según corresponda. En teléfono,
un paso de Ensayo todavía cerrado resalta `Más` y pide abrirlo. Dentro de
Ensayo se resaltan su tarjeta, la fila A–B, sus selectores o el encabezado.

Los controles de navegación son compactos:

- La flecha compartida de `Anterior` ocupa 44 × 44 px. Continuar/Terminar usa
  ancho según texto, no media tarjeta: conserva un área táctil de 44 px de alto
  con un relleno visible de 35 px.
- `Continuar` y `Terminar` permanecen deshabilitados mientras la condición del
  paso esté pendiente. Anterior, Más y cerrar conservan su disponibilidad;
  Anterior se deshabilita solamente en el primer paso.
- `Más` reúne `Cursos`, que vuelve al catálogo, y `Reiniciar guía`, que vuelve
  al primer paso y elimina el estado completado de ese curso.
  Su menú se dibuja como hijo de la guía, dentro de su ventana actual.
- La `X` (`Pausar guía`) y Escape cierran la guía conservando el paso actual.
- Con la guía enfocada, las flechas izquierda y derecha recorren pasos. No se
  utilizan para buscar audio ni para realizar la tarea sugerida. La flecha
  derecha respeta la misma validación que Continuar.

`setStepReadiness(bool ready, juce::String message)` permite al editor publicar
el resultado de la condición y una ayuda contextual. Un avance encolado se
descarta si cambió el paso o su estado de validación antes de ejecutarse.
La integración también vuelve a comprobar el requisito al recibir la acción.

Terminar marca completado el recorrido. En los tres cursos introductorios esta
marca significa que se leyeron sus pasos; en los recorridos remotos cada paso
de acción exige su condición durante la navegación. No certifica la mezcla,
el sonido escuchado ni un estado de conexión permanente.

## Paneles, menús y contexto

Durante los cursos remotos, la guía se agrega como hija del `SettingsPanel`
real en móvil, tablet y escritorio. Usa las coordenadas del panel y resalta
su entrada Control remoto, General en escritorio ancho, el selector de modo,
alias, estado, lista de servidores o botón cerrar. Al entrar en un paso nuevo,
`revealLearningTarget` desplaza el viewport si hace falta para hacer visible
el control. Cambiar a otra página resalta el retorno apropiado, sin navegar
automáticamente. Al cerrar Configuración, la guía vuelve al editor.

El catálogo también puede mostrarse dentro de Configuración. Los cursos
introductorios se ocultan mientras Configuración está abierta. Los modales
ajenos, la autenticación y los menús/pickers modales ajenos ocultan temporalmente
la ayuda para conservar sus controles y comportamiento. Al cerrarlos, vuelve
en el mismo paso. Abrir una página o cerrar un panel no avanza el curso.
El menú Más propio conserva la guía visible: la integración reconoce su modal
descendiente y no lo confunde con un menú ajeno ni abre una ventana separada.

Ensayo mantiene la guía disponible porque ofrece los destinos del curso de
teléfono. Si el usuario elige otro curso con Ensayo aún abierto, se resalta
su botón `X` y se pide cerrarlo para volver a los controles correspondientes.
La guía **no cierra Ensayo automáticamente**: esa decisión permanece en el
usuario y conserva las reglas de limpieza del loop y la escucha temporal.

Los callbacks de navegación se difieren al message thread y validan una
revisión de presentación con `SafePointer`. Una pulsación encolada para una
tarjeta anterior no avanza un curso nuevo ni accede a un overlay destruido.

## Progreso local

`ProgressStore` mantiene un archivo separado, bajo
`ApplicationDataPaths::applicationDataDirectory()/MTControls/learning_progress.json`.
El progreso pertenece a este dispositivo. No se guarda en canciones,
proyectos, biblioteca, cuentas, snapshots LAN ni sincronización Cloud.

Formato JSON v1:

```json
{
  "version": 1,
  "lessons": {
    "first-steps": {
      "stepId": "play-pause",
      "completed": false
    }
  }
}
```

La reanudación busca el `stepId` dentro del curso actual; no persiste índices.
Reordenar pasos conserva su identidad. Si el paso ya no existe, se retoma el
primero y ese registro no declara completado el curso actual. Esto también
evita reutilizar una finalización de los pasos de Ensayo como finalización de
los pasos de secciones de escritorio/tablet.

El parser ignora registros inválidos, tipos incorrectos e IDs no válidos.
Archivos malformados o con versión desconocida producen estado inicial. Los
límites son 64 KiB de archivo, 128 cursos y 96 caracteres por ID.

Los cambios se escriben mediante `TemporaryFile`: se comprueban apertura,
escritura completa, estado del stream y tamaño antes de reemplazar el archivo.
Un fallo de guardado revierte el nuevo estado en memoria y devuelve
`juce::Result`. La interfaz muestra el fallo durante la navegación; si falla
`Terminar`, permanece en ese paso para permitir reintentar. Repetir una
escritura sin cambiar paso o finalización no reescribe el archivo.

## Fuentes de implementación

- [LearningGuideModel](../../../src/ui/learning/LearningGuideModel.h): cursos,
  destinos, pasos y API del progreso.
- [LearningGuideModel.cpp](../../../src/ui/learning/LearningGuideModel.cpp):
  contenido localizado, parser y persistencia atómica.
- [LearningGuideOverlay](../../../src/ui/learning/LearningGuideOverlay.cpp):
  catálogo, tarjeta, resaltado, `hitTest` y acciones de navegación.
- [LearningGuideIntegration](../../../src/LearningGuideIntegration.cpp):
  contexto del editor, resolución de destinos, paneles y progreso.
- [RemoteLearningAction](../../../src/ui/learning/RemoteLearningAction.h):
  evidencia del comando propio, ACK y snapshot posterior para Play/Stop.
- [TopBar](../../../src/ui/panels/TopBar.cpp) y
  [SettingsPanel](../../../src/ui/panels/SettingsPanel.cpp): accesos al catálogo.
- [ChannelGridPanel](../../../src/ui/panels/ChannelGridPanel.cpp) y
  [PracticePanel](../../../src/ui/panels/PracticePanel.h): destinos de los
  controles existentes.

## Verificación

Pruebas registradas en CTest:

- `learning_guide`: contenido ES/EN, variantes teléfono/escritorio, destinos,
  disponibilidad sin canción, roundtrip, reanudación por IDs, reinicio,
  escrituras redundantes, parsing, fallos de guardado y condiciones reales de
  los dos cursos remotos, incluidos los requisitos temporales al reanudarlos.
- `learning_overlay`: layout y áreas seguras, tarjetas, acceso a los controles
  reales mediante `hitTest`, desplazamiento de las ayudas, botones compactos,
  navegación condicionada por botón/teclado y reparenting sobre el control
  Cliente de `SettingsPanel`.
- `settings_panel`: acceso desde Configuración junto con los contratos
  responsive, destinos de aprendizaje y revelado mediante scroll del panel.
- `remote_learning_action`: comando propio, ACK aceptado y snapshot posterior;
  rechazo de cambios ajenos, acciones erróneas, respuestas atrasadas, timeout,
  desconexión, cambio de canción/paso y cierre de guía; un toggle con intención
  de Pausa no acredita Play aunque otro cliente reanude.

Evidencia del contrato anterior, del 2 de octubre de 2026: compilación macOS de la app
Standalone y las tres pruebas, con CTest 3/3 aprobado (2,98 s). Se revisaron
capturas de los componentes de producción en teléfono vertical/horizontal,
tablet y escritorio. Las pruebas incluyen el acceso real a Play a través de la
guía, el scroll sobre el texto, arrastrar el catálogo sin abrir un curso y los
destinos de onda y A–B de Ensayo.

Verificación actualizada del 3 de octubre de 2026, en
`build/codex-modal-macos`: compilación macOS correcta de
`MTControls_Standalone`, `MTControlsLearningGuideTests`,
`MTControlsRemoteLearningTests`, `MTControlsLearningOverlayTests` y
`MTControlsSettingsPanelTests`. La app y el target del overlay se recompilaron
correctamente tras los últimos ajustes de menú y cambio de modo.
CTest final aprobó 4/4 (100 %, 2,95 s): `learning_guide` 0,55 s,
`remote_learning_action` 0,01 s, `learning_overlay` 2,22 s y
`settings_panel` 0,16 s.

Revisión manual de la app de escritorio del 3 de octubre: catálogo de cinco
cursos; recorrido Servidor en pasos 1–7 con modo, nombre y estado real,
incluido el bloqueo mientras falta un cliente; recorrido Cliente en pasos
1–5 con modo y espera de conexión reales. Tras reiniciar con Cliente pausado
en el paso 5 y el modo Local vigente, la ayuda pidió abrir Configuración y
luego elegir Cliente, conservando el ejercicio persistido.

El menú Más se comprobó dentro de Configuración: permaneció visible, expuso
Cursos/Reiniciar y Cursos devolvió el catálogo de cinco recorridos dentro del
panel. Las capturas `desktop-guide-options.png` y `desktop-server-guide.png`
registran el menú y el estado del servidor. Esta revisión parcial no completa
los ejercicios de conexión, canción remota ni Play/Stop entre dos dispositivos.

Los tests usan almacenamiento temporal aislado. El recorrido integral con dos
equipos sigue pendiente. La ejecución automatizada final incluye el menú
embebido y el texto pendiente sin duplicado.
Esta evidencia corresponde a esos targets de macOS; no acredita compilación
iOS/Android, instalación ni validación en dispositivos físicos.

## Próximos recorridos completos

La organización prevista separa objetivos completos: crear/editar una canción,
preparar y recorrer setlists, reproducir y mezclar, configurar clic/guía,
ajustar tono/tempo, tocar Pads, ensayar por instrumento y repetir A–B, editar
y operar secciones, automatizar, publicar/descargar Cloud, guardar/aplicar
versiones de ajustes, exportar y vincular MIDI. Estas funciones existen en la
app, pero sus guías completas con condiciones observables siguen pendientes;
los tres cursos introductorios se ampliarán en esos recorridos.


## Cursos de nube compartida

Además del catálogo base se agregan cursos de tres pasos para las membresías
Teams validadas de la cuenta actual. Reutilizan `LearningGuideOverlay`, botones
compactos y progreso persistente. `CloudOpen`, `SharedSongDownloaded` y
`SharedSongAdded` requieren acceso vigente; el último requiere también audio
local utilizable. El identificador incluye cuenta, fuente y alta de membresía,
por lo que otro usuario o un reingreso no heredan la bienvenida anterior.

Seleccionar este curso abre Biblioteca → Cloud por petición del usuario.
La guía destaca la biblioteca real, permite sus menús de descarga/setlist y
completa los pasos al verificar el resultado. Una respuesta de red fallida no
elimina progreso ni declara una baja. Véase [Teams](../integrations/Teams.md).

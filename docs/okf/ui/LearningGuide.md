---
type: UI Contract
title: Guías de aprendizaje
description: Cursos interactivos sobre los controles reales con progreso local por dispositivo
resource: src/LearningGuideIntegration.cpp
tags: [learning, onboarding, ui, mobile, tablet, desktop, accessibility]
timestamp: 2026-10-02
---

# Guías de aprendizaje

## Acceso

`Aprender PlayWorship` abre un catálogo de cursos desde:

- El menú `Más` de la barra superior en layouts táctiles de teléfono y tablet.
- Configuración en todas las plataformas. La portada agrupada contiene la
  entrada de aprendizaje y el layout de escritorio ancho incluye `Abrir guías`
  en General. La acción cierra Configuración antes de mostrar el catálogo.

El sistema es optativo. Abrir una guía no carga una canción ni activa
reproducción, edición, Pads o control remoto. Tampoco cambia la mezcla,
tempo, tono, canción seleccionada ni el modo Local/Cliente/Servidor.
Las acciones de prueba se realizan sobre los controles habituales de la app.

## Cursos y contenido

`makeLessons(phone, english)` define cuatro cursos con IDs estables. El
catálogo resuelve el idioma español o inglés y el layout táctil de teléfono
al abrirse. Los títulos, ayudas y tareas no se usan como identidad persistida.
Si se cambia el idioma desde Configuración, la guía actualiza sus textos al
volver, conservando el curso y el paso.

| ID | Curso | Contenido |
| --- | --- | --- |
| `first-steps` | Primeros pasos | Espacio de reproducción, Biblioteca/setlist, Play/Pausa, onda y Stop. |
| `mix-click` | Mezcla, clic y tono | Faders, Mute/Solo, metrónomo, tempo, tono y Pads. |
| `practice-sections` | Ensaya por instrumento, en teléfono | Acceso a Ensayo desde Más, navegación y zoom de la tarjeta, escucha por instrumento y loop A–B temporal. |
| `practice-sections` | Secciones y automatización, en escritorio/tablet | Edición de secciones, copia con referencia, borrador de automatización y acciones de la barra flotante. |
| `settings-remote` | Configuración y control remoto | Categorías de configuración, ruteo, Local/Cliente/Servidor, descubrimiento y reconexión. |

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

`Step::needsSong` añade una ayuda cuando falta una canción. El usuario puede
seguir leyendo y avanzar igualmente. Biblioteca, Configuración y Pads se
explican también sin una canción cargada; las prácticas sobre audio necesitan
que el usuario cargue una por el flujo habitual. No se crea una canción de
demostración ni se promete una validación automática de las tareas.

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

Los destinos se recalculan al actualizar el editor, cambiar tamaño, orientación
o área segura. Se resuelve el destino visible de la barra superior, una franja
instrumental del mixer, la onda, Pads o Ensayo según corresponda. En teléfono,
un paso de Ensayo todavía cerrado resalta `Más` y pide abrirlo. Dentro de
Ensayo se resaltan su tarjeta, la fila A–B, sus selectores o el encabezado.

La navegación es manual:

- `Anterior` y `Siguiente` recorren los pasos; `Terminar` marca completado el
  curso en su último paso.
- `Cursos` vuelve al catálogo sin ejecutar cambios de audio.
- `Reiniciar` vuelve al primer paso y elimina el estado completado de ese curso.
- La `X` (`Pausar guía`) y Escape cierran la guía conservando el paso actual.
- Con la guía enfocada, las flechas izquierda y derecha recorren pasos. No se
  utilizan para buscar audio ni para realizar la tarea sugerida.

La finalización significa que el usuario terminó de recorrer el curso. El
sistema no observa ni exige que haya ejecutado cada ejercicio.

## Paneles, menús y contexto

Al abrir Configuración o un componente modal, la guía se oculta para que ese
flujo conserve sus controles y comportamiento. Al cerrarlo, vuelve en el mismo
paso. No navega automáticamente a páginas internas ni avanza el curso al
cerrar un panel.

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
- [TopBar](../../../src/ui/panels/TopBar.cpp) y
  [SettingsPanel](../../../src/ui/panels/SettingsPanel.cpp): accesos al catálogo.
- [ChannelGridPanel](../../../src/ui/panels/ChannelGridPanel.cpp) y
  [PracticePanel](../../../src/ui/panels/PracticePanel.h): destinos de los
  controles existentes.

## Verificación

Pruebas registradas en CTest:

- `learning_guide`: contenido ES/EN, variantes teléfono/escritorio, destinos,
  disponibilidad sin canción, roundtrip, reanudación por IDs, reinicio,
  escrituras redundantes, parsing y fallos de guardado.
- `learning_overlay`: layout y áreas seguras, tarjetas, acceso a los controles
  reales mediante `hitTest`, desplazamiento de las ayudas y navegación.
- `settings_panel`: acceso desde Configuración junto con los contratos
  responsive y de navegación del panel.

Verificación local del 2 de octubre de 2026: compilación macOS de la app
Standalone y las tres pruebas, con CTest 3/3 aprobado (2,98 s). Se revisaron
capturas de los componentes de producción en teléfono vertical/horizontal,
tablet y escritorio. Las pruebas incluyen el acceso real a Play a través de la
guía, el scroll sobre el texto, arrastrar el catálogo sin abrir un curso y los
destinos de onda y A–B de Ensayo.

Los tests usan almacenamiento temporal aislado. La revisión manual integral
de la app queda pendiente. Esta evidencia no acredita instalación ni
validación en un dispositivo físico, ni resultados de compilación de todas
las plataformas.

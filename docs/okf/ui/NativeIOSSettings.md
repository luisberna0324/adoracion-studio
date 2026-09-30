---
type: UI Contract
title: Configuración nativa en iOS
description: Navegación UIKit y enlace con las preferencias existentes de PlayWorship
resource: src/platform/NativeIOSSettings.mm
timestamp: 2026-09-29T00:00:00-03:00
---

# Configuración nativa en iOS

El diseño sigue las guías de Apple para
[Settings](https://developer.apple.com/design/human-interface-guidelines/settings),
[Lists and tables](https://developer.apple.com/design/human-interface-guidelines/lists-and-tables),
[Pickers](https://developer.apple.com/design/human-interface-guidelines/pickers)
y [Accessibility](https://developer.apple.com/design/human-interface-guidelines/accessibility).

En iPhone y iPad, Configuración presenta un `UINavigationController` sobre el
editor JUCE. La portada y sus páginas usan `UITableViewController` con estilo
`insetGrouped`; UIKit aporta navegación, desplazamiento, áreas seguras,
tipografía dinámica, VoiceOver y respuestas táctiles. iPhone usa presentación
de pantalla completa e iPad una hoja centrada. El overlay JUCE permanece detrás
para conservar el contrato de orientación y el bloqueo de atajos del editor.

Las secciones son Personalización (Apariencia e idioma, Reproducción,
Biblioteca), Audio y conexiones (Ruteo de salidas, Control remoto) y Cuenta y
soporte (Mi cuenta, Diagnóstico). Las elecciones de tema, idioma, crossfade,
click, dispositivo, servidor, formato y canal abren listas UIKit con marca de
selección. Biblioteca usa `UISwitch`; Control remoto usa
`UISegmentedControl`; el alias usa `UITextField`; cierre, copia, soporte,
diagnóstico y acciones de cuenta usan controles UIKit. Las acciones destructivas
presentan una confirmación `UIAlertController` antes de invocar el callback.
Importar desde Prime aparece deshabilitado con la explicación de que solo está
disponible en macOS.

`SettingsPanel::iosState()` entrega una instantánea de las preferencias,
dispositivos, ruteo, sesión y estado remoto. Los métodos `iosSelect*`,
`iosSet*` e `iosRequest*` invocan los mismos callbacks y persistencia que la
vista JUCE. La lista de categorías muestra Click y Cues primero y mantiene sus
índices originales para guardar asignaciones por dispositivo. Cambiar salida
actualiza el número de canales y las asignaciones del dispositivo elegido.
Las actualizaciones asíncronas de cuotas, diagnóstico y conexión refrescan la
tabla visible sin reemplazar el campo de alias mientras se edita.

Android y escritorio conservan la vista JUCE. Los controles compartidos de
`UiCommon` siguen aplicando a las otras superficies iOS, pero no dibujan los
controles de esta pantalla nativa.

## Validación

- Compilar `MTControls_Standalone` para iOS Simulator después de regenerar el
  proyecto Xcode al agregar `NativeIOSSettings.mm`.
- En iPhone, abrir cada página, comprobar selección y retorno, cierre,
  Dynamic Type y VoiceOver; confirmar que las preferencias se guardan.
- En iPad, comprobar la hoja, navegación y cambios de tamaño.
- En Ruteo, comprobar salida, Mono/Estéreo y canal con un dispositivo que
  exponga varias salidas; confirmar que cambiar de dispositivo recupera sus
  asignaciones.
- En Control remoto, comprobar los tres modos, descubrimiento y alias en una
  red local real sin tocar el transporte de audio.

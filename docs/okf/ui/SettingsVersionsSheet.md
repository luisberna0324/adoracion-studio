---
type: Technical Contract
title: Hoja de acciones — selector de versiones
description: Selector compacto y confirmación compartida de versiones cloud
resource: src/ui/components/SettingsVersionsSheet.cpp
timestamp: 2026-09-21
---

# Hoja de acciones de versiones

## Referencia y decisión

Basada en [Apple HIG: Action sheets](https://developer.apple.com/design/human-interface-guidelines/action-sheets)
y [Sheets](https://developer.apple.com/design/human-interface-guidelines/sheets),
consultadas el 19 de septiembre de 2026. La hoja responde a una acción voluntaria,
ofrece alternativas breves y se descarta tocando fuera o con Escape. La hoja
inicial se limita a seleccionar; confirmar la aplicación ocurre después.

Es una implementación JUCE adaptada a la identidad visual de PlayWorship, **no**
un `UIAlertController` ni una reproducción exacta del material nativo de Apple.
No usa APIs privadas ni depende de UIKit en Mac/Android.

## Entrada y presentación

- Biblioteca → menú de la canción → **Versiones de ajustes**.
- Canción cargada → botón **…** sobre la onda → **Ver versiones**. El botón
  **…** reemplaza al botón exclusivo de exportación y conserva esa función como
  **Exportar canción** dentro del mismo popover; queda inmediatamente antes del
  control de seguimiento.
- El menú transmite el ID estable de la canción y conserva como respaldo el
  rectángulo global de su botón de origen. La apertura se difiere hasta que
  termine el callback del botón.
- **Versiones de ajustes** es la única acción de ese menú que no lo descarta al
  ejecutarse. El panel de acciones permanece visible como contexto y el
  selector se ancla a su rectángulo global.
- El popover de la onda sigue la misma regla: permanece como contexto mientras
  se presenta el selector lateral y se cierra al elegir una versión.
- Todas las plataformas usan una capa transparente y una tarjeta plana de 300
  puntos, sin flecha ni fondo oscurecido. La tarjeta intenta ubicarse a la
  derecha del panel de acciones que la invocó; si no cabe, usa el lado izquierdo
  y limita su posición al área segura. Si el panel ya no existe, usa el botón de
  la fila como respaldo.
- La capa consume el toque exterior para cerrar sin mover un fader ni activar
  otro control detrás. Escape ofrece la misma salida. No incluye un botón de
  cierre dentro de la tarjeta.
- No se abre una ventana independiente. La reproducción no se pausa al consultar.
- Una apertura reemplaza la anterior. Al elegir una opción, el selector y el
  menú contextual se cierran antes de abrir la confirmación compartida.

## Estados y acciones

1. **Encabezado**: título fijo **Versiones disponibles** y descripción breve
   **Selecciona una versión para ver los cambios.** No repite la canción,
   porque el menú que abrió la hoja ya aporta ese contexto.
2. **Carga**: se presenta inmediatamente y conserva las mismas dimensiones
   compactas.
3. **Historial**: cada acción muestra únicamente `vN · fecha y hora`. No muestra
   resumen, canción ni **En uso**. Se ven hasta tres opciones; historiales más
   largos se recorren dentro de la lista, sin agregar Anteriores/Siguientes.
4. **Selección**: tocar una versión cierra la hoja sin cambiar ajustes y abre el
   diálogo compartido **Confirmar versión**, con **Aplicar versión** y **Cancelar**. No
   muestra resumen ni un segundo nivel de detalles.
5. **Aplicar**: sólo **Confirmar** llama a `applyCloudSettings`. Si guarda,
   refresca Biblioteca; si falla, muestra el error. No publica otra versión.
6. **Vacío / error de consulta**: mensaje dentro de la hoja, con Reintentar
   cuando corresponda.

Escape y tocar fuera descartan el selector sin aplicar ajustes. Cancelar existe
en el diálogo de confirmación, no en la hoja inicial. Consultar el historial no
edita borradores. Una aplicación fallida no implica
rollback transaccional de todos los stores locales: conserva el contrato vigente
de `applyCloudSettings`; no modifica versiones remotas.

## Teclado, accesibilidad y ciclo de vida

- Acciones compactas de 40 puntos y navegación Tab/Shift-Tab.
- Botones con nombre, descripción accesible y contorno visible de foco.
- El selector enfoca la primera versión. El modal posterior usa la tarjeta
  `AppModal`, con acciones de 48 puntos. Escape/Cancelar descarta y Enter o
  **Aplicar versión** confirma. La tarjeta se centra dentro del área segura.
- Escape cierra el selector. No existe un modal adicional de resumen o detalle.
- `SafePointer` protege la respuesta asíncrona: no actualiza ni vuelve a abrir
  una hoja que se cerró, se reemplazó o cuyo editor fue destruido.
- Las acciones que reconstruyen filas se ejecutan después del callback del
  botón, evitando destruir el emisor mientras JUCE lo está procesando.
- El editor posee la capa transparente del selector en todos los layouts; el
  menú contextual continúa bajo propiedad del presentador común de callouts.
- Seleccionar una versión no crea pendientes cuando coincide con un snapshot
  conocido: ver [CloudSync](../integrations/CloudSync.md).

## Verificación

`MTControlsSettingsSheetTests` prueba carga, vacío, error/reintento, selección
sin aplicar y Escape. Verifica ancho 300, alto compacto, ausencia de Cancelar y
de paginadores, y que cada opción contenga sólo revisión y fecha. Usa
preferencias/tema aislados; no toca canciones del usuario ni llama a la nube.
En Linux requiere un display (por ejemplo Xvfb); sin `DISPLAY` se informa como
omitida, no como aprobada.

```sh
cmake --build build/macos-release --target MTControlsSettingsSheetTests
ctest --test-dir build/macos-release -R 'settings_versions_sheet|cloud_settings_sync_state' --output-on-failure
```

La prueba del componente no sustituye pruebas de VoiceOver ni ejecución física
en iPad/iPhone/Android. Para QA manual: abrir una canción con versiones,
seleccionar una revisión, cancelar la confirmación y comprobar que no cambia el
mixer ni se crea una revisión cloud; después probar la confirmación con una
canción de prueba.

La validación manual anterior del flujo combinado quedó reemplazada por este
contrato de selector compacto + confirmación compartida y debe repetirse tras compilar.

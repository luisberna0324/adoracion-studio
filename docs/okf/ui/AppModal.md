---
type: Technical Contract
title: Diálogos de entrada y confirmación
description: Presentación compartida de modales breves
resource: src/ui/components/AppModal.cpp
timestamp: 2026-10-23
---

# Diálogos breves

`AppModal` presenta una tarjeta centrada sobre la aplicación, con fondo atenuado,
tipografía y colores de `UITheme`, área segura y acciones de 48 puntos. Cuando
aparece el teclado, la tarjeta se centra en el espacio visible restante. Usa la
misma jerarquía visual para entrada corta, confirmación e información. Se adjunta
al componente superior para cubrir también los controles que rodean al panel
que invoca la acción. El componente llamador conserva su propiedad y lo libera
al cerrar; los callbacks asíncronos verifican que el llamador siga vivo.

## Entrada de setlists

Crear y renombrar setlists, tanto en Biblioteca como en el gestor de escritorio,
abren el mismo formulario. Se muestra la finalidad del campo, etiqueta y texto
de ayuda. Un nombre vacío o compuesto sólo por espacios deshabilita la acción
primaria y Return no la ejecuta. Crear/Guardar entrega el nombre recortado;
Cancelar/Escape no realiza cambios. Eliminar utiliza la variante destructiva;
el aviso de que debe permanecer al menos un setlist utiliza la informativa.

## Confirmación de versiones

Tras elegir una versión de ajustes, se cierra la hoja de versiones y se abre
una tarjeta que explica que los ajustes actuales de este dispositivo serán
reemplazados. **Aplicar versión** ejecuta `applyCloudSettings` sólo después de
la confirmación. **Cancelar** y Escape no escriben cambios. Consultar o
seleccionar una versión no publica otra versión cloud. Si la aplicación falla,
el error se presenta en una tarjeta informativa del mismo estilo.

La tarjeta es interna a JUCE y funciona con la misma presentación en iOS,
Android y escritorio. La entrada de texto sigue usando el teclado del sistema.

## Procesamiento de sugerencias

`AppModal::paintSurface` comparte el fondo atenuado y la tarjeta con el modal
específico de `SectionDetectionPanel`. Durante el análisis local de la guía,
ese modal cubre el componente superior y presenta una barra de progreso,
fase y Cancelar en una tarjeta compacta. La selección de pistas o la descarga
explícita del modelo se presentan dentro de la tarjeta cuando hacen falta.
El modal se libera antes de entregar las sugerencias a la onda; un error o
cancelación conserva una configuración recuperable. Véase
[Sugerir secciones](SectionDetection.md#sugerir-secciones-sobre-la-onda).

## Renderizado en Android

El contexto OpenGL pertenece a `MainWindow` y renderiza la ventana completa,
incluidos `AppModal` y los demas overlays globales agregados al componente
superior. No se adjunta solo a la raiz del editor: estos modales son hermanos
de esa raiz y deben verse en la misma superficie que bloquea sus controles.
Se conserva el host superior y la cobertura habitual de cada modal, sin
excepciones de alojamiento por tipo de dialogo. El contexto se desconecta
antes de detener el runtime y destruir los hijos de la ventana.

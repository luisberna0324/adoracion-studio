---
type: UI Contract
title: Densidad de escritorio
description: Tamaños coherentes de controles según el espacio lógico disponible
resource: src/PluginEditor.cpp
timestamp: 2026-11-06
---

# Densidad de escritorio

En macOS, Windows y Linux, Configuración ofrece `Tamaño de interfaz` con
`Automático`, `Compacto`, `Normal` y `Grande`. Se reutiliza la preferencia local
`layoutMode` de `AppSettings`; no se guarda en la canción, el proyecto ni el
setlist. Una selección se guarda y recalcula la interfaz abierta sin volver a
cargar la canción ni reiniciar la aplicación.

La ventana de Configuración también limita su tamaño inicial al área de trabajo
del monitor que contiene el editor, reservando margen para el marco y los
controles del sistema. Su viewport conserva acceso a las opciones que excedan
la altura disponible.

Las medidas son puntos lógicos de la ventana y de sus paneles, tal como los
entrega JUCE. La escala Retina/DPI sigue correspondiendo al sistema operativo;
no se aplica una segunda división por la densidad del monitor ni una
transformación global que reduzca fuentes, campos y áreas de interacción.

## Perfil de la ventana

`Automático` usa `Compacto` si el ancho es menor de 1180 puntos o la altura es
de 780 puntos o menos. Usa `Grande` desde 1680 × 900 y `Normal` en el resto de
ventanas. Elegir un perfil explícito conserva esa preferencia al redimensionar;
los paneles mantienen sus límites de legibilidad y su desplazamiento cuando el
contenido excede el espacio disponible.

El perfil compacto reduce márgenes, la columna de setlist y la altura habitual
de la onda para reservar más espacio al mixer. La onda continúa respetando su
expansión explícita y los controles adicionales del editor de secciones. Los
controles conservan sus callbacks, foco y estado musical al cambiar el perfil.

## Mixer

El cálculo del mixer considera el ancho **y la altura útil de su viewport**.
Primero obtiene una escala de acuerdo con el ancho y el perfil: Compacto no
crece por encima de 0.90, Normal de 1.08 y Grande de 1.25 respecto del strip
base. A continuación puede reducirla hasta 0.82 para que las filas completas
quepan en la altura disponible. La búsqueda no reconstruye pistas ni modifica
los valores de mezcla.

Si las filas no caben ni siquiera en el mínimo legible, el contenido conserva
su altura completa y el viewport permite llegar hasta la última fila. No se
ocultan canales ni se reducen indefinidamente para mostrar todos a la vez.
Cada fila mantiene perilla, medidor, fader, botones y banda del nombre dentro
de su tarjeta. Master conserva la misma fila M/vinculación y sus separaciones
que el resto de los canales; véase [LinkedFaders](LinkedFaders.md).

Redimensionar o elegir densidad conserva las instancias de los canales,
volumen, ganancia, mute, solo, ruteo y el estado temporal de vinculación. No
altera transporte, automatización, archivos de audio ni DSP.

## Agregar y editar canciones

El asistente de canciones usa la misma preferencia, con adaptación al espacio
real de su panel. En Automático, un panel estrecho o bajo activa sus medidas
compactas aunque la ventana principal sea amplia. Cabecera, pasos, pie,
campos y filas de archivos/mapeo ajustan sus medidas como conjunto; las listas
y formularios conservan desplazamiento y todos sus campos.

El flujo de creación, edición, validación y guardado mantiene el contrato de
[SongCreation](../importing/SongCreation.md). Cambiar el tamaño no recrea el
borrador ni solicita foco de nuevo.

## Móviles y verificación

Este ajuste se limita a escritorio. iPhone, iPad y Android mantienen sus
medidas táctiles, área segura, orientaciones y reglas de
[MobileLayout](MobileLayout.md); no muestran el selector de densidad de
escritorio.

La regresión `--mixer-layout-only` de `MTControlsStandaloneAudioRuntimeTests`
ejercita un mixer real con doce stems y sus buses virtuales: Compacto frente a
Normal, filas completas con alturas limitadas, acceso por scroll a la última
fila y conservación de mezcla y vinculación al redimensionar. La variable
opcional `PLAYWORSHIP_LAYOUT_SNAPSHOTS` guarda imágenes de esos componentes
reales para inspección visual. Estas pruebas de componentes no sustituyen la
revisión de la aplicación abierta en el monitor objetivo.

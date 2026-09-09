---
type: Technical Contract
title: Mobile Layout
description: Contrato responsive y de area segura para telefonos iOS y Android
resource: src/PluginEditor.cpp
timestamp: 2026-09-08T19:30:00-03:00
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

## Area segura

- El contenido interactivo se coloca dentro de `Displays::Display::safeAreaInsets`.
- En iOS landscape se usa el mayor inset horizontal en ambos lados para que la
  superficie sea equivalente con la camara a izquierda o derecha.
- En Android se respetan los cuatro insets reportados por el sistema, incluyendo
  cutouts y barras de navegacion.
- Los fondos pueden dibujarse edge-to-edge; transporte, waveform y mixer no.
- No se mantienen tablas de margenes por modelo ni gutters fijos para notch.

## Controles

- Objetivo tactil frecuente minimo: `44 x 44 pt/dp`.
- El telefono muestra transporte basico, tiempo, waveform y mixer.
- El setlist lateral se reemplaza por un boton `SETLIST` flotante sobre el
  mixer; abre un overlay desde la derecha sin reducir ni desplazar el mixer.
  Tocar fuera del overlay lo cierra.
- Pads, expansion vertical de waveform y minimap/navegador inferior de la
  waveform se ocultan en telefono. Tambien se oculta la regla de compases
  superior; la onda principal usa todo ese alto liberado.
- La altura de top bar y waveform se adapta al alto util (`<=375`, `<=410`,
  `>410`) para preservar espacio para el mixer.

## Verificacion

Probar ambas direcciones landscape en iOS y, como minimo, estos canvas utiles:

- `667 x 375` (telefono pequeno)
- `844 x 390` (telefono regular)
- `874 x 402` (iPhone Pro actual)
- `912 x 420` y `956 x 440` (telefonos grandes)
- Android compacto, medio y tablet con cutout/navigation insets activos

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
- El fondo del mixer se prolonga visualmente por el inset inferior hasta el
  borde fisico de la ventana, evitando una franja vacia bajo los canales.
- En iOS landscape se usa el mayor inset horizontal en ambos lados para que la
  superficie sea equivalente con la camara a izquierda o derecha.
- En Android se respetan los cuatro insets reportados por el sistema, incluyendo
  cutouts y barras de navegacion.
- Los fondos pueden dibujarse edge-to-edge; transporte, waveform y mixer no.
- No se mantienen tablas de margenes por modelo ni gutters fijos para notch.

## Controles

- Objetivo tactil frecuente minimo: `44 x 44 pt/dp`.
- El telefono muestra transporte basico, tiempo, waveform y mixer.
- El toggle contextual `Fade Down/Fade Up` aparece junto a `Siguiente`. El
  tiempo usa dos lineas (`actual /` y `total`) con tipografia reducida.
- El artwork cuadrado de la cancion activa aparece inmediatamente despues del
  toggle de fade, sin mostrar titulo ni artista.
- El setlist lateral se reemplaza por un boton compacto con icono de musica,
  ubicado inmediatamente a la izquierda del cronometro y con el mismo estilo de
  los controles del header. Abre un overlay que ocupa
  toda el area segura sin reducir ni desplazar
  el mixer. Mientras el overlay esta abierto, el boton musical se oculta.
- El overlay incluye un boton `X` de `40 x 40 pt/dp`, igual que las acciones de
  biblioteca y agregar, para cerrarlo desde las vistas de setlists, canciones y
  biblioteca.
- Los botones flotantes de exportar, seguimiento y editar forman un grupo
  horizontal con separacion uniforme y margen respecto al borde inferior de la
  waveform.
- En el overlay de telefono se oculta el resumen de duracion total, cantidad de
  canciones y BPM promedio, de modo que la lista comienza inmediatamente bajo
  el encabezado.
- Pads, expansion vertical de waveform y minimap/navegador inferior de la
  waveform se ocultan en telefono. Tambien se oculta la regla de compases
  superior; la onda principal usa todo ese alto liberado.
- La waveform de telefono tampoco usa tarjeta, borde ni padding externo: la
  onda ocupa directamente toda su region segura.
- El mixer de telefono no dibuja una tarjeta o division propia: sus canales
  comienzan directamente debajo de la waveform, sin relleno superior ni marco.
- Los channel strips forman una sola fila, ajustan su altura al mixer y se
  recorren horizontalmente mediante drag; nunca se envuelven en filas nuevas.
- El fondo de cada strip llega al borde inferior, pero su footer y controles se
  mantienen por encima del inset inferior, con `8 pt/dp` adicionales para que
  los titulos no queden recortados por la mascara curva del dispositivo.
- La altura de top bar y waveform se adapta al alto util (`<=375`, `<=410`,
  `>410`) para preservar espacio para el mixer.

## Verificacion

Probar ambas direcciones landscape en iOS y, como minimo, estos canvas utiles:

- `667 x 375` (telefono pequeno)
- `844 x 390` (telefono regular)
- `874 x 402` (iPhone Pro actual)
- `912 x 420` y `956 x 440` (telefonos grandes)
- Android compacto, medio y tablet con cutout/navigation insets activos

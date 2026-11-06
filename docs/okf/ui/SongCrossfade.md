---
type: UI Contract
title: Crossfade entre canciones
resource: src/ui/panels/SongListPanel.cpp
---

# Crossfade entre canciones

Configuración → General (o Reproducción en la navegación agrupada) ofrece
«Crossfade entre canciones» con duraciones de 1, 2, 3, 5, 8 y 10 segundos. El
valor local `songCrossfadeSeconds` se obtiene mediante
`AppSettings::getSongCrossfadeSeconds()`, por defecto 3 s. Su setter rechaza
valores no finitos y limita el valor al intervalo [1, 10]. Este ajuste es
independiente del crossfade de pads. El disparo convierte el tiempo fuente
restante a segundos reales usando el tempo actual, para conservar la duración
elegida al acelerar o ralentizar una canción. En modo cliente remoto el selector está
deshabilitado e indica que la duración se configura en el servidor que reproduce.

El menú de cada canción del setlist conserva la opción de reproducir la
siguiente al terminar y añade «Crossfade a la siguiente (N s)». La marca se
aplica a la salida de ese item hacia su vecino inmediato. Al estar activa, el
menú permite quitarla y la información de la fila incluye «Crossfade». El
último item tiene deshabilitadas ambas acciones porque no tiene destino.

La selección llama `SongLibraryStore::setCrossfadeNextForSetlist`; el modelo
persiste `transitionToNext.crossfade` junto a `autoPlay`. Para el transporte,
`getCrossfadeNextSong` sólo ofrece el vecino cuando ambos flags están activos.
Los clientes remotos envían `setlist.crossfade` con `{songId, enabled}` al
servidor, sin activar reproducción local.

Abrir el menú o cambiar la duración no carga una canción ni inicia playback.
La reproducción simultánea y sus garantías de ruteo pertenecen al contrato del
motor/transporte; la marca del menú por sí sola no demuestra salida de audio.

El crossfade conserva el procesamiento de tono/tempo y los generadores de click y
guía dinámicos de la canción saliente. Cada canción usa su mezcla, sus destinos
y su tempo; las ganancias complementarias se aplican también a esos buses
virtuales. Pads sigue siendo un bus global único. El tono/tempo de la saliente
queda capturado al comenzar la transición; los controles visibles pertenecen a
la entrante una vez cargada. Loops, saltos pendientes, ensayo y custom arrange
siguen requiriendo salir de esos modos antes de activar una transición. La marca
de setlist se conserva y no habilita por sí sola una ruta incompatible.

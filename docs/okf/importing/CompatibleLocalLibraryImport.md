---
type: Import Contract
title: Compatible Local Library Import
description: Importación de canciones y stems de una biblioteca local compatible hacia proyectos .pwproj portátiles
resource: src/data/PrimeLibrarySource.cpp
---

# Compatible Local Library Import

En macOS, `PrimeLibrarySource` expone únicamente canciones cuyos stems locales existen en el directorio de tracks de la biblioteca compatible instalada. Al seleccionar una canción en el asistente de creación, el flujo debe:

1. Copiar título, artista, BPM, tono y compás al borrador.
2. Crear un stem del borrador por cada track local disponible, conservando id, nombre, categoría y metadatos de audio.
3. Usar cada ruta sólo como fuente de importación. Al guardar, `ProjectStore` empaqueta copias de los stems dentro del `.pwproj`.

El proyecto resultante no depende de la ubicación de la biblioteca externa ni de que la aplicación de origen permanezca instalada.

Configuración presenta esta opción como **Biblioteca compatible**, con el
interruptor **Habilitar importación local**. El adaptador conserva su única
fuente compatible; no se amplía a otras bibliotecas ni formatos. Los nombres
internos de clase, la clave persistida y la ruta del contenedor se conservan
para no romper la integración existente.

La variante opcional `MTCONTROLS_MAC_APP_STORE=ON` no ofrece esta integracion:
el acceso directo al contenedor de la aplicación de origen y el proceso `/usr/bin/sqlite3` no
estan preparados para App Sandbox. El toggle queda deshabilitado y su ayuda
indica importar archivos de audio desde Biblioteca. Ese selector conserva la
importacion de contenido autorizado y guarda copias privadas. El escritorio
habitual mantiene la integracion existente. Ver
[Mac App Store](../release/MacAppStore.md).

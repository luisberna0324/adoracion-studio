---
type: Storage Contract
title: Library Persistence on iOS
description: Conservacion y reubicacion de canciones locales al actualizar builds de iPhone y iPad
resource: src/data/SongLibraryStore.cpp
timestamp: 2026-09-21T00:00:00-03:00
---

# Library Persistence on iOS

Una instalacion de actualizacion de PlayWorship debe conservar el contenedor de
datos existente. El flujo de despliegue nunca debe desinstalar la aplicacion
antes de instalar el nuevo bundle, porque una desinstalacion elimina el sandbox
y no puede recuperarse solamente desde el dispositivo.

iOS puede cambiar el UUID del contenedor de datos al reemplazar un build de
desarrollo aunque preserve su contenido. `songs.json` contiene rutas absolutas
para stems, artwork y proyectos administrados; por eso, al cargar la biblioteca,
`SongLibraryStore` reubica cualquier ruta inexistente que apunte dentro de un
root anterior `MTControls` hacia el mismo path relativo dentro del root actual.

La reparacion se limita a archivos existentes y no vacios que permanecen debajo
de `SongLibraryStore::getRootDirectory()`. Las rutas externas seleccionadas por
el usuario no se reinterpretan. La biblioteca solo elimina una entrada local no
portable despues de intentar esta reubicacion y confirmar que ningun stem
incluido puede reproducirse.

## Validacion

1. Crear o descargar una cancion y registrar la cantidad de entradas y stems.
2. Instalar un bundle nuevo con el mismo `CFBundleIdentifier`, sin desinstalar.
3. Confirmar que `songs.json` y los blobs siguen presentes aunque cambie el UUID
   del contenedor.
4. Abrir la aplicacion y comprobar que las rutas se reescriben al UUID actual,
   que la cancion permanece en Biblioteca y que puede reproducirse.
5. Repetir la instalacion para comprobar que la migracion es idempotente.

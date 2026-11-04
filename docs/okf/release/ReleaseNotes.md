# Notas de release: una fuente, dos vistas

Desde 2.4.0 se revisan dos archivos independientes junto al codigo de la app:

- `release-notes/<version>.json`: contenido editorial, schemaVersion 2.
- `release-status/<version>.json`: disponibilidad verificada, schemaVersion 1.

Una skill reutilizable, `playworship-release-notes`, guia la auditoria de cada
subida. No se crea una skill por version ni otra descripcion en cada sitio.
Comparar los arboles finales y la base anterior de cada plataforma; no deducir
las novedades de titulos de PR. No prometer funciones revertidas ni confundir
una funcion heredada con un cambio introducido en esa version.

## Contratos de entrada

El documento editorial contiene `version`, `status: released`, `publishedAt`,
`platforms`, `title`, `summary`, de uno a tres `highlights` breves y `changes`
agrupados para el detalle. Un borrador no pasa el preflight de publicacion.
`publishedAt` fecha la entrega de GitHub, no la aprobacion de todas las tiendas.
Los campos internos ajenos al contrato no se exportan. Limite: 64 KiB.

La disponibilidad contiene `version`, `verifiedOn` y `distribution`. Los canales
son windows-installer, macos-installer, android-apk, mac-app-store e ios-app-store.
`available`, `pending_review`, `in_review`, `processing` y `unavailable` son distintos.
Puede incluir `build`, `reviewSubmitted` y `releaseType`. Se rechazan versiones
incoherentes, canales duplicados y fechas anteriores al estado ya publicado.
Los estados Apple requieren evidencia fechada de Apple: Actions no convierte
una carga en aprobacion ni consulta automaticamente App Review.

## Salidas generadas

```text
notes.md                       # cuerpo editorial de GitHub Release
release-notes.json             # asset editorial completo de GitHub
release-status.json            # asset de disponibilidad de GitHub
versions/<version>.json        # detalle editorial publico individual
status/<version>.json          # disponibilidad publica mutable
index.json                     # schemaVersion 2: hasta 12 resumenes
latest.json                    # schemaVersion 2: solo el ultimo resumen
```

Cada resumen tiene version, fecha, plataformas objetivo, titulo, descripcion,
highlights y rutas relativas `detailsFile`/`statusFile`. No contiene `changes`
ni estados de tiendas. Los estados no se incrustan en el Markdown editorial.
Los archivos publicos viven en `<prefix>/release-notes/` del bucket existente.
No confundir su `latest.json` con `<prefix>/latest.json`, el manifiesto de
instaladores que conserva su contrato anterior.

## Flujo manual existente de Actions

1. `prepare` valida ambas entradas del SHA seleccionado mediante `git show`,
   antes de los builds publicables. No usa el working tree ni otra rama.
2. Tras construir, el renderer genera Markdown, assets y catalogo desde esa
   revision fijada. GitHub recibe el Markdown y ambos assets JSON.
3. `scripts/publish_release_notes.sh` combina el resumen con el indice existente
   por SemVer. Publicar una version anterior no degrada la ultima version.
4. Publica los archivos individuales antes que los punteros index/latest,
   usando las credenciales y bucket existentes, con cache publico de 60 segundos.

Todo sigue bajo `workflow_dispatch` y `publish_release=true`. No hay triggers
automaticos ni nuevas suites QA en Actions: validar un documento no certifica
el binario. Para tags anteriores a 2.4.0 sin documento se conserva el modo legacy.
No mover tags existentes para incorporar estas modificaciones posteriores.
Una tag con el antiguo borrador no hereda los cambios locales: fallara el nuevo
preflight y requiere una decision explicita sobre su correccion editorial.

## Actualizar solo disponibilidad

Editar `release-status/<version>.json` tras verificar el canal. Con autorizacion
para publicar, usar el mismo workflow manual con:

- `ref`: revision que contiene el estado revisado y este publicador.
- `release_status_only=true`, `status_version=2.4.0`, `publish_release=true`.
- `export_play_signing_key=false`, `google_play_only=false`.

Debe existir una GitHub Release no borrador para esa version. El modo actualiza
solo `status/<version>.json` y el asset `release-status.json`; no toca notas,
index/latest, tags, binarios, manifiesto de descargas ni retencion. No ejecuta
builds ni envia nada a Apple. No usar la matriz nativa para cambiar un estado.

## Consumo ligero y respaldo

La landing integra un enlace "Notas de version" junto a la descarga de cada
plataforma, hacia `https://help.playworship.app/#novedades`. No hay un bloque
independiente, consultas al feed ni resumenes duplicados.
`latest.json` sigue generado y disponible para consumidores que necesiten un
resumen; el sincronizador de la landing queda preparado pero no se usa en su boton.

La ayuda pide solo el indice de hasta 12 resumenes, sin consultar ni mostrar
estados de tienda, builds, filtros o un panel de disponibilidad.
Solo pide `versions/<version>.json` al abrir una tarjeta; conserva una
cache validada de hasta 12 documentos. Los documentos antiguos permanecen en GCS,
aunque salgan del indice reciente. No hay una descarga creciente del historial.

Los sitios validan esquema, version, fechas, rutas y tamanos; usan consultas
anonimas con timeout y snapshots generados si la red falla. La ayuda incluye
por separado el ultimo detalle como asset estatico. El estado fechado sigue como
archivo operativo, fuera de la vista y del bundle del navegador. Su lista
legacy conserva solamente notas anteriores al contrato. No editar a mano los
snapshots ni agregar releases nuevas a esa lista. Un estado viejo no se muestra
bajo el numero de una version nueva. La disponibilidad no habilita pagos ni
modifica los enlaces de descarga del producto.

Las lecturas publicas usan la [JSON API de Cloud Storage](https://docs.cloud.google.com/storage/docs/cross-origin)
sin tokens ni ampliar CORS/permisos del bucket. Los uploads de assets usan
[`gh release upload`](https://cli.github.com/manual/gh_release_upload).
Un error de autenticacion, red o esquema al leer el catalogo previo detiene
la publicacion; no se trata como historial vacio. Solo 404 permite inicializar.

## Comprobar localmente, sin publicar

```sh
python3 scripts/release_notes.py --version 2.4.0 --output-dir output/release-document
python3 tests/test_release_notes.py
python3 tests/test_release_version.py
bash -n scripts/publish_release_notes.sh
# Estado solamente, sin requerir ni generar el documento editorial:
python3 scripts/release_notes.py --version 2.4.0 --status-only --output-dir output/release-status
```

En el checkout de la ayuda:

```sh
npm run sync:releases -- /ruta/checkout/app/output/release-document/index.json
npm run check
```

En el checkout de la landing:

```sh
npm run sync:releases -- /ruta/checkout/app/output/release-document/latest.json
npm run test:releases
npm run build
```

Tras publicar el feed, `npm run sync:releases` sin argumento actualiza cada
snapshot desde GCS. La ayuda valida resumen, detalle y estado antes de sustituir
archivos; la landing valida solo el resumen. La preparacion transpila los
validadores para Node sin ejecutar las suites ni iniciar despliegues.

Esta preparacion local no publica el workflow, feed ni sitios. Push, despliegue
y disparo manual requieren su propia autorizacion. Verificar por separado el
run, assets, JSON publico y cada sitio antes de afirmar que esta operativo.

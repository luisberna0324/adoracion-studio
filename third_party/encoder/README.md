# Codificador de audio opcional de macOS

El escritorio habitual de PlayWorship utiliza FFmpeg como proceso externo para
formatos que no admiten sus lectores integrados y para exportar MP3/M4A. La
conversión integrada a FLAC sigue siendo la primera opción de importación.
La variante Mac App Store y los targets móviles no descargan este ejecutable.
Los contratos funcionales están en
[SongCreation](../../docs/okf/importing/SongCreation.md) y
[SongMixExporter](../../docs/okf/audio-systems/SongMixExporter.md).

El bootstrap selecciona la arquitectura del proceso, comprueba la caché y
descarga automáticamente la variante compatible cuando la necesita. Una copia
ARM anterior no bloquea la recuperación en Intel. Los tamaños y SHA-256 fijados
en `src/data/MacEncoderDownload.h` se verifican antes de instalar.

La distribución Intel `2026-11-06-1` ofrece el
[ejecutable](https://storage.googleapis.com/playworship-assets/encoder/macos/2026-11-06-1/x86_64/ffmpeg),
su [manifiesto](https://storage.googleapis.com/playworship-assets/encoder/macos/2026-11-06-1/x86_64/manifest.json)
y las [fuentes, licencias y receta de compilación](https://storage.googleapis.com/playworship-assets/encoder/macos/2026-11-06-1/x86_64/sources-and-licenses.tar.xz).
La URL ARM anterior permanece disponible. La recuperación automática requiere
la aplicación con el selector nuevo; la versión 2.4.0 instalada previamente
conserva su URL compartida y su validación de caché anterior.

## Compilación Intel controlada

`scripts/build_macos_audio_encoder.sh` prepara un ejecutable `x86_64` con un
mínimo de macOS 12.0. Usa CPU genérica, deshabilita el ensamblador de FFmpeg y
NASM de LAME, y compila ambos proyectos con `-march=x86-64 -mno-avx -mno-avx2`.
No exige AVX2 ni incorpora bibliotecas dinámicas de Homebrew. Las bibliotecas
FFmpeg/LAME se enlazan estáticamente; las bibliotecas del sistema de Apple
permanecen dinámicas. La receta puede ejecutarse en un Mac Intel o compilar el
target Intel desde Apple Silicon con las herramientas de Xcode.

Fuentes fijadas e integridad:

| Proyecto | Versión y fuente | SHA256 del archivo fuente |
| --- | --- | --- |
| FFmpeg | [8.0.1, tar.xz oficial](https://ffmpeg.org/releases/ffmpeg-8.0.1.tar.xz) | `05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41` |
| LAME | [3.100, proyecto oficial en SourceForge](https://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz/download) | `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e` |

El script valida esos hashes antes de extraer o ejecutar `configure`. No usa
URLs `latest`. FFmpeg también publica una
[firma PGP de la fuente](https://ffmpeg.org/releases/ffmpeg-8.0.1.tar.xz.asc);
su [procedimiento oficial de verificación](https://ffmpeg.org/download.html#release-verification)
es un control adicional de autenticidad al revisar las fuentes fijadas.
Los hashes de esta receta identifican sus entradas; no equivalen a afirmar que
se verificó una firma PGP durante cada compilación.

Requiere macOS y Xcode Command Line Tools; no instala dependencias:

```sh
scripts/build_macos_audio_encoder.sh \
  --work-dir /tmp/playworship-encoder-build \
  --output-dir /tmp/playworship-encoder-x86_64
```

Para reutilizar archivos fuente ya descargados, sin acceso de red:

```sh
scripts/build_macos_audio_encoder.sh \
  --source-dir /ruta/a/fuentes \
  --work-dir /tmp/playworship-encoder-build \
  --output-dir /tmp/playworship-encoder-x86_64
```

`--source-dir` debe contener los dos archivos con sus nombres/versiones de la
tabla. Cada ejecución conserva un directorio de compilación nuevo y sus logs.
El destino debe ser nuevo; la receta no borra ni reemplaza paquetes existentes.
No ejecuta el binario resultante ni lo instala, firma o sube a un servicio.
La reproducción de esta receta fija fuentes y flags; la identidad byte a byte
del resultado también depende de la versión del compilador y SDK registrada en
`BUILD.txt`.

## Cobertura del helper

Se deshabilitan los componentes por defecto y se habilita este conjunto:

| Uso | Componentes |
| --- | --- |
| Entrada | WAV/AIFF (PCM y ADPCM), FLAC, MP3, MOV/M4A (AAC y ALAC), AAC, OGG (Vorbis y Opus), ASF/WMA |
| Salida | FLAC, MP3 mediante `libmp3lame`, AAC/M4A/MP4, WAV PCM16/PCM24 |
| Protocolos locales | `file`, `pipe`; sin red |
| Filtros | `aresample`, `aformat`, `anull` |

Habilitar un contenedor no garantiza todas las variantes de códec que puede
alojar. La matriz se limita a los códecs de audio listados; por ejemplo, no
habilita vídeo ni formatos cifrados. Una compilación correcta requiere después
verificar conversiones reales de cada formato de entrada y las salidas de
PlayWorship. Un smoke test con Rosetta no reemplaza la prueba en el Mac Intel
antiguo: el manifiesto de arquitectura y los flags limitan el target, pero no
verifican por sí solos la importación completa en ese equipo.

## Fuentes, licencias y paquete de distribución

Esta receta no activa `--enable-gpl`, `--enable-version3` ni
`--enable-nonfree`. FFmpeg y la biblioteca LAME se distribuyen bajo LGPL 2.1 o
posterior; se conservan los textos que acompañan a las fuentes. Consultar la
[guía de licencias de FFmpeg](https://ffmpeg.org/legal.html) y el archivo
`COPYING` de LAME para los términos completos.

El directorio generado incluye:

- `ffmpeg`: ejecutable Intel.
- `sources/`: ambos archivos fuente originales, sin modificaciones.
- `licenses/`: licencias y avisos de FFmpeg y LAME.
- `build-config/`: receta, flags resueltos, configuración y dependencias Mach-O.
- `BUILD.txt` y `SHA256SUMS`: versiones, procedencia, toolchain e integridad.

Al publicar un helper en el alojamiento de PlayWorship, ofrecer también este
paquete de fuentes, licencias y configuración junto al binario, y enlazarlo
desde la información de terceros/descarga. Conservar las fuentes exactas y
cualquier cambio futuro; una actualización de hash/versión debe producir un
paquete nuevo. La firma y el hash definitivos del ejecutable se deben registrar
después de cualquier firma de distribución, porque firmarlo modifica sus bytes.
No basta con enlazar al proyecto original sin conservar la fuente correspondiente
al artefacto que distribuye PlayWorship.

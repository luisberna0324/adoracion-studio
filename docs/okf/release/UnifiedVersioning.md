# Version unificada 2.4.0

La siguiente entrega comun se prepara como **2.4.0** para Windows x64/x86,
macOS (PKG y variante Store), Android (APK/AAB), iPhone e iPad. Las versiones
ya publicadas conservan sus numeros originales. Preparar esta configuracion
no modifica los instaladores ni las fichas existentes.

## Fuente y verificaciones

`src/AppVersion.h` define `PLAYWORSHIP_RELEASE_VERSION`. CMake, la identidad
nativa, Gradle y `scripts/local_pipeline.py` leen esa misma fuente. CMake y
Gradle rechazan overrides que no coincidan, incluidos caches antiguos de CMake.
Usar un directorio nuevo o configurar explicitamente
`-DMTCONTROLS_APP_VERSION=2.4.0` al regenerar un build antiguo.

En Android, `versionName`, la version de C++ y el nombre del instalador deben
coincidir. `versionCode` sigue siendo independiente: cada APK/AAB Release exige
un `PLAYWORSHIP_VERSION_CODE` explicito, superior a todas las builds distribuidas,
incluidas las de Google Play. Su disponibilidad se comprueba antes de generar.

El workflow manual conserva `release_kind`: para pasar de 2.3.6 a 2.4.0 usar
`minor`. El resolver valida la version del SHA seleccionado antes de iniciar
los builds. Los tags historicos que no contienen AppVersion.h siguen siendo
reproducibles con su numeracion original. Este cambio no ejecuta Actions.

## Preparar iOS y iPadOS

Con las dependencias locales presentes, fijar el equipo de distribucion ya
autorizado y un numero de build libre. Se mantienen bundle ID y app universal:

```sh
cmake -S . -B build/ios-appstore-2.4.0 -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DJUCE_DIR="${JUCE_DIR:?Indicar checkout JUCE}" \
  -DMTCONTROLS_IOS_DEVELOPMENT_TEAM="${PLAYWORSHIP_APPLE_TEAM:?Indicar equipo autorizado}" \
  -DMTCONTROLS_IOS_BUILD_NUMBER="${PLAYWORSHIP_IOS_BUILD:?Indicar build libre}" \
  -DMTCONTROLS_IOS_TARGETED_DEVICE_FAMILY=1,2
```

Este perfil genera MARKETING_VERSION/CFBundleShortVersionString 2.4.0. El entero
CFBundleVersion permanece independiente. Antes de archivar, comprobar los
ajustes efectivos con `xcodebuild -showBuildSettings`. Archive, firma,
exportacion, validacion con `scripts/validate_ios_app_store.sh`, procesamiento
y revision de Apple son pasos posteriores. Ver [AppStore](AppStore.md).

## Preparar macOS Store

```sh
cmake -S . -B build/mac-appstore-2.4.0 -G Xcode \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DJUCE_DIR="${JUCE_DIR:?Indicar checkout JUCE}" \
  -DMTCONTROLS_MAC_APP_STORE=ON \
  -DMTCONTROLS_MAC_DEVELOPMENT_TEAM="${PLAYWORSHIP_APPLE_TEAM:?Indicar equipo autorizado}" \
  -DMTCONTROLS_MAC_BUILD_NUMBER="${PLAYWORSHIP_MAC_BUILD:?Indicar build libre}"
```

Conservar la validacion del perfil sandbox y la firma existentes descritas en
[MacAppStore](MacAppStore.md). El PKG desktop y los presets Windows usan la
misma fuente de version de CMake. La variante Linux tambien la hereda.

## Notas y disponibilidad

`release-notes/2.4.0.json` es un borrador sin fecha de publicacion. No describe
2.4.0 como disponible ni anuncia funciones pendientes de validar. Completar las
notas con el delta real del SHA final antes de publicarlas. La aprobacion de
tiendas puede hacer que cada plataforma quede disponible en fechas distintas.
La ayuda debe marcar disponibilidad por plataforma y conservar el historial.

Comprobaciones de preparacion:

```sh
python3 scripts/release_version.py --expect 2.4.0
python3 tests/test_release_version.py
cd android
./gradlew :app:printReleaseVersion --offline --no-daemon
```

## Evidencia local de preparacion, 2026-10-31

- Cinco pruebas de version pasaron, incluida la ejecucion del resolver real del
  workflow en un repositorio temporal: minor genera 2.4.0, patch incoherente
  se rechaza y un tag historico mantiene su version.
- Xcode genero el proyecto iOS con MARKETING_VERSION 2.4.0, bundle ID existente
  y familia 1,2; tambien el perfil macOS Store con 2.4.0 y sandbox, configurado
  para arm64/x86_64. No se compilaron archives ni se verifico firma.
- Gradle ejecuto printReleaseVersion con 2.4.0 y rechazo version incorrecta,
  contador cero y assembleRelease sin contador explicito, antes de compilar.
- Los metadatos de prueba usaron build 1. Es un fixture local, no una eleccion
  de contador para distribucion. Windows no se compilo en este Mac.

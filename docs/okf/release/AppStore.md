---
type: Release Contract
title: App Store — preflight de PlayWorship para iOS
description: Contrato de compilacion, privacidad, firma y revision para distribuir la app universal de PlayWorship en App Store Connect.
tags: [ios, ipad, iphone, app-store, privacy, release]
timestamp: 2026-09-16T00:00:00Z
---

# App Store — preflight de PlayWorship para iOS

## Contrato del binario

- Bundle ID: `com.multitracks.playworship`.
- App universal para iPhone y iPad (`TARGETED_DEVICE_FAMILY=1,2`).
- Interfaz exclusivamente horizontal. `UIRequiresFullScreen=true` evita que
  iPadOS exija tambien orientaciones verticales para multitarea.
- `CFBundleShortVersionString` usa SemVer desde `MTCONTROLS_APP_VERSION`.
- `CFBundleVersion` es un entero creciente independiente configurado con
  `MTCONTROLS_IOS_BUILD_NUMBER`.
- El build de distribucion usa Xcode 26 o posterior y el SDK iOS 26 o
  posterior.
- `ITSAppUsesNonExemptEncryption=false` declara que la app usa solo cifrado
  estandar exento, como HTTPS y autenticacion.
- El icono App Store es PNG RGB de 1024 x 1024 sin canal alfa.

## Privacidad

`assets/ios/PrivacyInfo.xcprivacy` se copia en la raiz del bundle iOS. Declara:

- Sin tracking ni dominios de tracking.
- APIs de timestamps de archivos para archivos del contenedor y documentos
  seleccionados por el usuario (`C617.1`, `3B52.1`).
- Tiempo de arranque para temporizadores y MIDI/audio (`35F9.1`).
- Espacio en disco para decidir si se puede generar cache de pads (`E174.1`).
- Email, ID de usuario, ID de dispositivo, historial de compra, audio y otro
  contenido del usuario, vinculados a la cuenta y usados para funcionalidad.

Las respuestas de App Privacy en App Store Connect deben coincidir exactamente
con este manifiesto y con el backend desplegado.

## Permisos

- Bluetooth se usa para dispositivos MIDI Bluetooth.
- Red local se usa para servicios compatibles de musica y presentacion en la
  red del usuario.
- Audio en segundo plano se usa para mantener la reproduccion durante una
  presentacion.
- La app no solicita microfono.

## Preflight local

Configurar y compilar un Archive con firma automatica:

```bash
cmake -S . -B build/ios-appstore \
  -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DMTCONTROLS_IOS_TARGETED_DEVICE_FAMILY=1,2 \
  -DMTCONTROLS_IOS_DEVELOPMENT_TEAM=<TEAM_ID> \
  -DMTCONTROLS_IOS_BUILD_NUMBER=<BUILD_NUMBER> \
  -DJUCE_DIR=../third_party/JUCE

xcodebuild \
  -project build/ios-appstore/MTControls.xcodeproj \
  -scheme MTControls_Standalone \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  -archivePath build/ios-appstore/archive/PlayWorship.xcarchive \
  -allowProvisioningUpdates archive
```

Validar el `.app` dentro del Archive:

```bash
scripts/validate_ios_app_store.sh \
  build/ios-appstore/archive/PlayWorship.xcarchive/Products/Applications/PlayWorship.app \
  --allow-development-signing
```

El flag solo admite la firma de desarrollo que puede contener el Archive antes
de exportarlo. Despues de exportar con metodo `app-store-connect`, repetir el
script sobre el `.app` distribuible sin ese flag. La validacion de Apple se
completa desde Xcode Organizer o con `altool` sobre el IPA exportado.

## Requisitos de producto antes de enviar a revision

- Crear el registro de la app en App Store Connect con el Bundle ID exacto.
- Publicar URLs funcionales de politica de privacidad, terminos y soporte.
- Proporcionar eliminacion completa de cuenta desde Configuracion de la app o
  un enlace directo al flujo web autenticado.
- Resolver la regla 4.8: si Google sigue siendo login principal, agregar una
  opcion equivalente como Sign in with Apple. La alternativa es usar
  exclusivamente un sistema propio de cuentas en iOS.
- Resolver compras digitales: agregar productos equivalentes con StoreKit o
  distribuir iOS como app companera sin compras ni llamadas a comprar fuera de
  la app.
- Preparar una cuenta de revision con contenido util, instrucciones de acceso y
  backend de produccion estable.
- Cargar capturas landscape de iPhone y iPad, descripcion, palabras clave,
  categoria Music, clasificacion de edad y notas de revision.
- Validar primero el build con TestFlight en iPhone y iPad fisicos.

## Fuentes Apple

- App Review Guidelines: https://developer.apple.com/app-store/review/guidelines/
- Privacy manifests: https://developer.apple.com/documentation/bundleresources/privacy-manifest-files
- Account deletion: https://developer.apple.com/support/offering-account-deletion-in-your-app
- Upload builds: https://developer.apple.com/help/app-store-connect/manage-builds/upload-builds

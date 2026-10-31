# QA fuera de Actions y releases manuales

La siguiente entrega se prepara con version unificada **2.4.0** en
`src/AppVersion.h`. Usar `release_kind=minor` despues de 2.3.6. El resolver
comprueba que el SemVer calculado coincida con el del SHA seleccionado antes
de generar instaladores. Ver [Version unificada](UnifiedVersioning.md) para
iOS/iPadOS, Mac Store y los contadores de build independientes.

Solo `.github/workflows/ci.yml` permanece activo: genera instaladores de release
mediante `workflow_dispatch`. No hay eventos push, PR, merge, tag, schedule,
workflow_run ni workflows auxiliares. Actions no ejecuta suites de QA,
automáticas o manuales; la generación conserva la validación del paquete firmado. Se retiraron tests.yml, pr-semver.yml y
android-cloudbuild.yml; sus pruebas fuente permanecen intactas.

## Coordinación principal en Codex Cloud

Codex Cloud inicia y coordina QA de app/backend sobre una revisión fijada.
Ejecutar allí todos los tests que sus dependencias permitan y guardar resultados
por SHA. Los targets no disponibles se marcan BLOCKED; complementar Apple,
Windows y hardware en sus entornos y devolver esos logs al informe Cloud.
Ese informe permite decidir si una release esta lista para publicarse. La
generacion manual de instaladores puede usarse para preparar su validacion;
no declara ni certifica QA de las plataformas. Subir codigo o activar releases
requiere aprobacion concreta adicional.

## Pipeline local complementario del mismo SHA

En un worktree limpio, registrar SHA de app y backend. Para el Mac preparado:

```sh
python3 scripts/local_pipeline.py --config ../local-pipeline-mac.json --targets all
```

SemVer y recursos (sin JUCE) también pueden ejecutarse por separado:

```sh
python3 scripts/local_pipeline.py --targets semver,resources
bash scripts/semver.sh --branch feat/nombre --sequence 0 --prerelease --require-supported-branch
cmake -S tests/binary_resources -B build/binary-resources -G Ninja
cmake --build build/binary-resources --parallel 3
ctest --test-dir build/binary-resources --output-on-failure --no-tests=error
```

El pipeline conserva backend typecheck/pruebas/build y PlayWorshipTests completo.
Guardar summary.json, logs, commits, plataformas, resultados y blockers.
Un resultado anterior de otro SHA o un target BLOCKED no acredita QA de release.
Para Windows ejecutar sus presets VS2022 x64/x86 en Windows; para Android Release
se requiere firma autorizada. Probar audio/MIDI, UI, login y hardware pertinentes
fuera de Actions. El PKG local no notarizado y iOS development no acreditan
una distribución certificada. Ver [Testing](../Testing.md) y
[LocalPipeline](../distribution/LocalPipeline.md).

## Codex Cloud

En Linux, SemVer y las regresiones de tests/binary_resources usan los comandos
anteriores directamente. En el checkout backend limpio y con dependencias:

```sh
npm run typecheck
node --import tsx --test tests/*.test.ts
npm run build
```

No subir secretos; desactivar tokens de subida Sentry. El pipeline Mac exige
Darwin y no se usa como evidencia Cloud. Cloud solo acredita pruebas realmente
ejecutadas con logs/SHA. El entorno Cloud revisado no dispone de JUCE/Bungee y
SDKs Apple/Windows completos: no acredita PlayWorshipTests ni builds nativos.
Tests E2E con Firebase requieren sus emuladores y servidor; unit tests no los
sustituyen. Los tests interactivos y LAN mantienen su validación específica.

## Solicitar release

1. Para generar instaladores, seleccionar la revision de app que se quiere
   construir. Antes de publicar, revisar la evidencia de QA y los blockers del
   SHA exacto y las plataformas correspondientes.
2. Tras aprobacion, disparar manualmente ci.yml con ref (preferir SHA completo)
   y release_kind. El resolver fija la revision; no exige qa_sha ni
   qa_confirmed y la generacion no ejecuta ni certifica QA automaticamente.
3. retain_artifacts=true autoriza guardar instaladores en artefactos del run
   GitHub por siete días (almacenamiento facturable según presupuesto).
   publish_release=false genera instaladores sin uploads Sentry/GCS, cleanup,
   manifiesto ni tag/GitHub Release. El modo completo sigue consumiendo la
   matriz de Actions y usa los secretos de firma Android existentes. Solo ejecutarla por solicitud
   concreta de generación de release. Si retain_artifacts y publish_release
   están desactivados, prepare falla antes de compilar: no se gastan matrices
   para generar instaladores sin destino.
4. publish_release=true requiere autorización adicional: incluye uploads
   Sentry/GCS, retención de tres versiones, manifiesto y tag/GitHub Release.
   Todos están protegidos por el opt-in; no hay publicación por eventos.

Un tag explícito conserva su versión; rama/SHA calcula release_kind desde tags
estables. La revisión fijada identifica también símbolos Sentry. No se cancela
una operación de release activa por una ejecución nueva. Subir esta rama o
fusionarla necesita aprobación; nada de esta preparación despacha Actions.

## App Bundle para Google Play

El input booleano `google_play_only` vale `false` por defecto y conserva el
flujo completo anterior. Con `true`, sólo se ejecutan `prepare` y el build de
Android; macOS y Windows se omiten. No se añade otro workflow ni una suite de QA.

Para preparar el AAB de una revisión aprobada:

- `ref`: SHA de la revisión concreta que contiene los cambios aprobados.
- `release_kind=patch`: calcula la siguiente versión desde el último tag estable;
  con `v2.3.5` como último tag, el candidato es `2.3.6`. Un tag explícito conserva
  su propia versión, como en el flujo completo.
- `google_play_only=true`, `retain_artifacts=true`, `publish_release=false`.

`prepare` rechaza `google_play_only=true` con `publish_release=true` o sin
retención del artefacto, antes de resolver versiones y abrir la matriz. Los
secretos Android Release existentes sólo se usan en el runner temporal; no se
exportan al Mac ni se conserva el keystore en los artefactos. El destino es
exclusivamente `dist/playworship-google-play-<versión>-arm64.aab`, retenido en el
run por siete días. El nombre del artefacto incluye versión y SHA.

El build ejecuta `:app:bundleRelease`, usando el número de run como `versionCode`.
Antes de enviarlo a Play Console se debe comprobar que ese código esté libre allí.
Se comprueba firma JAR válida y distinta de Android Debug, estructura AAB,
bibliotecas arm64 y paquete/versión/ausencia de `debuggable` en el manifiesto de
Release que empaqueta Gradle. La revisión del AAB final con `bundletool` confirma
el manifiesto binario y `PAGE_ALIGNMENT_16K`; compilar o firmar no sustituye esa
comprobación ni QA en dispositivos con páginas de 16 KB.

Este modo no genera APK para la web, símbolos Sentry, carga GCS, limpieza de
versiones, manifiesto de descargas, tag ni GitHub Release. El AAB descargado aún
necesita carga y procesamiento en Play Console, validación de la ficha y envío a
revisión: generar el archivo no acredita publicación. Se mantienen las
aprobaciones concretas para subir código, ejecutar el generador y enviar la app.

## Copia cifrada de firma para el alta en Google Play

`export_play_signing_key=false` conserva el comportamiento de generación de
instaladores. Sólo tras autorización concreta de transferencia de la clave a
Google Play, puede activarse este modo manual dentro del mismo `ci.yml`.
No añade workflows, triggers ni suites de QA.

Con `export_play_signing_key=true`, `prepare` y todos los builds/publicaciones
que dependen de él se omiten. Sólo corre `export-play-signing-key`, con token
`contents: read`, desde `main`, con `ref` igual al SHA exacto del workflow
revisado, `google_play_only=true`, `retain_artifacts=true` y
`publish_release=false`. No calcula otra versión ni recompila la app.

El JAR oficial PEPK se descarga por HTTPS y se compara contra el SHA-256
revisado antes de recibir secretos. La clave pública de cifrado descargada de
la consola de esta app queda fijada en `scripts/google-play/` con su hash.
El wrapper Java usa la API pública de PEPK con contraseñas provenientes del
entorno; no pasa secretos por argumentos ni llama al CLI que podría imprimirlos
ante un error. Exige el certificado Release conocido antes de cifrar y valida
el ZIP resultante con su certificado público.

El keystore sólo se decodifica en `$RUNNER_TEMP`, con permisos restringidos, y
se elimina mediante trap y limpieza `always` antes de conservar el artefacto.
Únicamente se guardan ZIP cifrado y reporte público durante un día. No se
suben a Release, web ni GCS. Descargar ese ZIP no prueba la importación: el
propietario completa el cambio de credencial en Play Console y después se
comparan sus huellas de firma y subida. Nunca guardar keystore sin cifrar,
contraseñas ni secretos en el repo, artefactos o logs.

## Activación sin consumo accidental

Estos cambios locales no alteran los workflows remotos vigentes. Antes de subir
la rama o abrir/fusionar PR, el propietario puede deshabilitar temporalmente
Actions del repositorio en Settings → Actions → General. Con aprobación concreta,
subir/fusionar la configuración y verificar que solo ci.yml manual permanezca;
reactivar Actions únicamente cuando se quiera habilitar releases manuales.
No modificar presupuestos ni iniciar runs como parte de esta activación.
Mantener límite de gasto cero es complementario y no sustituye deshabilitar
los workflows existentes si se quiere impedir toda ejecución.

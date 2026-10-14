# QA fuera de Actions y releases manuales

Solo `.github/workflows/ci.yml` permanece activo: genera instaladores de release
mediante `workflow_dispatch`. No hay eventos push, PR, merge, tag, schedule,
workflow_run ni workflows auxiliares. Actions no ejecuta tests ni checks,
automáticos o manuales. Se retiraron tests.yml, pr-semver.yml y
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
   manifiesto ni tag/GitHub Release. La matriz sigue consumiendo Actions y usa
   los secretos de firma Android existentes. Solo ejecutarla por solicitud
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

## Activación sin consumo accidental

Estos cambios locales no alteran los workflows remotos vigentes. Antes de subir
la rama o abrir/fusionar PR, el propietario puede deshabilitar temporalmente
Actions del repositorio en Settings → Actions → General. Con aprobación concreta,
subir/fusionar la configuración y verificar que solo ci.yml manual permanezca;
reactivar Actions únicamente cuando se quiera habilitar releases manuales.
No modificar presupuestos ni iniciar runs como parte de esta activación.
Mantener límite de gasto cero es complementario y no sustituye deshabilitar
los workflows existentes si se quiere impedir toda ejecución.

---
type: UI and Storage Contract
title: Canción demo para aprender
resource: src/DemoSongIntegration.cpp
timestamp: 2026-10-23
---

# Canción demo para aprender

## Acceso

La bienvenida también ofrece **Prueba sin conexión**, con usuario y contraseña
`usuarioplay0324`. Carga la demo directamente en pausa y abre los controles
normales sin consultar el backend ni iniciar el recorrido. No crea una cuenta,
licencia ni permisos de Nube/Teams. El modo local se recuerda en este dispositivo
y se abandona mediante Cerrar sesión. Ver [contrato de acceso](../integrations/Auth.md#acceso-local-con-la-demo-incluida).

Tras entrar con el acceso habitual, la primera Biblioteca vacía ofrece
**Probar demo** o **Más tarde**. La decisión se recuerda en este dispositivo.
El catálogo **Aprender PlayWorship** ofrece siempre **Probar con una canción
demo**, también para usuarios con canciones y para recuperarla tras borrarla.
No se instala por iniciar sesión, abrir el catálogo o saltar la bienvenida.
La demo no concede una cuenta, licencia ni acceso a Cloud; fuera de la prueba
local, se conserva el contrato de autenticación y acceso offline existente.

Elegir la demo requiere modo Local y terminar cualquier carga, guardado,
exportación, transferencia o borrador de automatización en curso. No cambia
automáticamente Local/Cliente/Servidor. Si Ensayo está abierto, se pide cerrarlo.
Los errores se muestran en el catálogo y permiten reintentar.

## Canción incluida

`assets/demo/LuzDeEsperanza.pwproj` contiene **Luz de esperanza — PlayWorship ·
Demo**: instrumental original sintetizado, 90 segundos, 96 BPM, Re mayor,
ocho stems FLAC y seis secciones (Intro, Verso, Coro, Puente, Coro y Final).
No descarga audio, no utiliza canciones personales ni samples de terceros.
`assets/demo/provenance.json` registra procedencia y hashes. El proyecto se
incluye en BinaryData para escritorio, iOS y Android.

Su identidad portable es `7f3e2cd6-ff35-5b0a-a726-1dfdf8ccc66c`; el título
editable no decide si es una demo. La instalación copia el paquete a almacenamiento
administrado antes de abrirlo con ProjectStore. Nunca modifica el recurso del bundle.

## Instalación, persistencia y recuperación

`DemoSongService::ensureInstalled` es explícito e idempotente. Devuelve la demo
existente sin sobrescribir ajustes, metadatos ni pertenencia a setlists. Ante
archivos faltantes en una demo existente, informa el error y pide eliminarla y
recuperarla; no reinicia cambios silenciosamente. Un inventario malformado
impide instalar. Un fallo de extracción/registro limpia la instalación parcial.

Eliminar usa el flujo normal de Biblioteca. No hay reinstalación automática.
Elegir el curso después de borrarla recupera el paquete original; su contenido
no depende de la copia de QA de `output/`. El servicio no modifica sesión,
suscripción, selección ni otras canciones/setlists. El editor carga la canción
en pausa sin añadirla al setlist activo.

La preparación de disco usa un worker, con un overlay modal y bloqueo temporal
de cargas/avance automático. El callback vuelve al message thread y valida
SafePointer, generación de acceso, modo local, cuenta y acceso antes de cargar.
Una respuesta antigua no cambia la sesión ni el overlay de un intento nuevo.
El progreso muestra la fase real de copia, validación o registro. El worker
puede devolver también el `ProjectDocument` preparado: tanto la instalación
nueva como una demo existente lo entregan a la activación para no abrir otra
vez el ZIP en el message thread. La demo existente usa sus recursos locales
validados, sin volver a extraer ni validar los recursos internos del paquete: una
onda empaquetada dañada no puede borrar su caché ni sus ajustes vigentes. Si no
se pueden leer sus ajustes locales, informa el error conservando los archivos.
La demo existente conserva su identidad y
metadatos locales y usa sus ajustes locales actuales, incluso si la fecha del
sidecar coincide con la del paquete. No se reescribe su paquete ni sus ajustes.
La preparación de los transports de audio y la restauración de los controles
siguen siendo síncronas durante la activación; este cambio evita la segunda
apertura de disco, no convierte toda la carga en una tarea asíncrona.
Durante el curso se suspende el
avance automático del setlist, incluso si la demo ya pertenecía a él.

## Recorrido práctico

`demo-song` tiene diez pasos en escritorio/tablet y catorce en teléfono.
Reutiliza los controles y el audio habituales. `DemoLearningProgress` observa
hechos de la demo cargada desde la entrada de cada paso:

- Play, Pausa, reanudar y Stop requieren una revisión nueva de transporte.
  Stop se comprueba contra Inicio, incluso si el usuario lo personalizó.
- Volumen requiere mover un fader; Mute y Solo requieren activar y desactivar
  el mismo canal dentro del paso.
- Loop de sección requiere activar y desactivar su repetición.
- Teléfono añade abrir Ensayo, enfoque y restauración, creación y borrado de
  A–B y cierre de Ensayo. Apagar Repetir conservando A/B no cuenta como borrar.
- Cambiar canción invalida la evidencia. Reanudar no convierte estados previos
  en ejercicios realizados. La guía expone Más/X si hace falta reabrir Ensayo.

No se avanza automáticamente. Intro y cierre son informativos. Los ajustes
manuales permanecen en la demo como en cualquier canción; recuperar el original
requiere borrarla primero. El progreso sigue siendo local y separado del proyecto.
Durante mezcla, loop y enfoque en Ensayo se conserva también acceso a Play para volver a escuchar
si la demo termina; ese control no tapa ni reemplaza el destino del ejercicio.

## Validación

`demo_song`: paquete/audio, instalación, idempotencia, ajustes existentes,
borrado/recuperación, documento preparado con recursos y ajustes actuales,
invariancia de cuenta/setlists y fallos de almacenamiento.
`demo_learning` y `learning_guide`: hechos por paso, transporte, Inicio
personalizado, pares Mute/Solo, loops, Ensayo y contenido por plataforma/idioma.
`learning_overlay`: catálogo, geometría y entrega de gestos a controles reales.

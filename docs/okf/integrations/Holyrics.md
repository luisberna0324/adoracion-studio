---
type: Integration Contract
title: Holyrics
description: Proyeccion de letras de escritorio por API HTTP local y LAN
resource: src/integrations/HolyricsModel.cpp
---

# Holyrics

La vista Letras y su scheduler se comparten con FreeShow. Configuración →
Integraciones permite elegir FreeShow o Holyrics, IP privada/loopback, puerto
(8091 por defecto para Holyrics) y token. Los endpoints se guardan por separado;
el token permanece en memoria salvo que el usuario active Recordar en macOS.
En ese caso se guarda en el llavero nativo, por proveedor/IP/puerto, y se recupera
al iniciar sin conectar automáticamente. La lectura de arranque prohíbe diálogos
del llavero, también en el backend legacy de macOS; si una compilación nueva
requiere permiso de acceso, la app abre con el campo vacío en vez de bloquear
la ventana. Guardar sigue siendo una acción explícita del usuario. Nunca se incluye en JSON, logs, canciones
ni proyectos; otras plataformas siguen usando tokens de sesión. Desactivar
Recordar y guardar elimina la credencial del endpoint seleccionado.
Las tarjetas con logos identifican aplicaciones externas y muestran el proveedor
seleccionado. Elegir un proveedor no conecta hasta Guardar y probar. No requiere Holyrics Plan,
API de internet ni un puente MIDI. Esta primera integración es de escritorio;
no se incorpora la proyección a iOS/Android.

## Compatibilidad

Requiere Holyrics 2.23.0 o posterior: `ShowLyrics.initial_index` no existe en
versiones anteriores. No usar una instalación antigua para seguimiento de
diapositivas. El ZIP alternativo oficial descargado el 2026-11-01 declara
2.20.0 en Holyrics.cfg y su lanzador no respondió en esta Mac; queda pendiente
actualizar/abrir Holyrics y validar la proyección real. Las pruebas del protocolo,
scheduler y panel pasan, pero no sustituyen esa validación.

## Protocolo

Fuente: https://github.com/holyrics/API-Server/blob/main/README-en.md

El API Server de Holyrics se habilita en File → Settings → API Server. El
propietario crea un token con GetSongs, GetLyrics, ShowLyrics, SetF8 y
GetCurrentPresentation y GetVersion. Antes de proyectar se verifica la versión y
se bloquean versiones anteriores o respuestas desconocidas. Las solicitudes usan POST JSON a /api/<acción>?token=…
según la documentación oficial. Se usa la misma conexión de sockets directa,
sin proxy, redirección ni llamadas de red desde el audio callback de FreeShow.
El token sólo se envía al endpoint LAN seleccionado.

GetSongs lista canciones sin proyectar. GetLyrics devuelve diapositivas y su
orden (índices desde 1); el catálogo conserva repeticiones. Las descripciones
forman los grupos del menú y las tarjetas muestran el texto de cada diapositiva.
Los IDs de destino derivan de descripción/texto y la firma valida el orden. Un
cambio de contenido/orden exige revisar la marca, sin retarget silencioso.
ShowLyrics usa id e initial_index desde cero. Ocultar letras envía SetF8 enable=true
(fondo visible). No se implementan pantalla negra ni cierre de presentación.
GetCurrentPresentation confirma canción, número de diapositiva desde 1 y tipo
`default`, o tipo `wallpaper` para ocultar. Un envío aceptado sin coincidencia no
se anuncia como proyección confirmada.

## Persistencia y seguridad temporal

El contenedor existente freeShowCues sigue separado de la letra nativa lyrics.
El modelo incorpora provider en canción y destino. FreeShow antiguo omite este
campo y sigue interpretándose como freeshow. Los documentos con marcas Holyrics
usan versión 2 para que una app anterior rechace el formato en vez de enviar
órdenes al programa incorrecto. Cambiar de proveedor conserva las marcas y las
marca para revisión si pertenecen al otro programa. queueTarget rechaza un
proveedor distinto del conectado, incluyendo ocultar letras.

Se conservan las garantías del scheduler: spans realmente reproducidos, seeks,
loops, expiración y generaciones. Abrir catálogo/seleccionar tarjeta/editar no
proyecta; sólo Probar o seguimiento explícito. La prueba con transporte inyectado
no demuestra proyección física; esa comprobación se registra por separado.

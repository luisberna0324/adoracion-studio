# Auditoria editorial de PlayWorship 2.4.1

## Alcance

Preparacion local solicitada el 6 de noviembre de 2026; version 2.4.1 confirmada
por el propietario. La solicitud posterior de publicar autoriza subir estas
notas, pero no iniciar nuevos builds nativos ni envios a tiendas.

- Base publicada: v2.4.0, `04b7915da710bc1911082ccfe778c243a1484612`.
- Fuente candidata: main, `46686d72387c1d37fa72c223f1524d8b58aa0f61`.
- La revision contiene las PR 50 y 51, incluyendo deteccion de secciones,
  Holyrics, crossfade, interfaz adaptable y preparacion del versionado 2.4.1.
- La rama local `codex/holyrics-lyrics-integration` permanece intacta. Se preparo
  este cambio en el worktree `release-notes-2.4.1`, sobre la fuente candidata.

El documento 2.4.1 ya existia en main con `status: released` y una fecha de
publicacion prevista. La preparacion inicial conservo esos campos sin usarlos
como prueba de publicacion. Tras existir la release, la fecha se corrige al
publishedAt real de GitHub y la disponibilidad se verifica por separado.
Al iniciar la auditoria, GitHub no tenia Release 2.4.1; el run 42313766244 estaba
en progreso sobre la fuente candidata. No se despacho, reinicio ni altero ese
run. Sus artefactos y la aprobacion de tiendas requieren verificacion separada.

Al comprobar la publicacion a las 23:55 UTC del 6 de noviembre, los jobs Windows
x64 y x86 del mismo run habian fallado en Build (Windows); Android habia
terminado y macOS continuaba. GitHub aun no tenia Release 2.4.1 y el manifiesto
publico de instaladores seguia en 2.4.0. No crear una tag anticipada ni publicar
el historial como una release disponible sin resolver ese estado. Un avance
marcado En preparacion requiere una decision editorial aparte.

Comprobacion posterior del 7 de noviembre de 2026: el run 42318990581 termino
correctamente sobre `66669fcd4e4ca5644bb0c3010aceee86adf71d84`, despues de los
ajustes de compilacion Windows de las PR 52 y 53. La tag v2.4.1 conserva ese
SHA y GitHub publico su release a las 00:54:25 UTC. Los enlaces versionados de
Windows x64, macOS y Android respondieron HTTP 200. Solo esos tres canales se
marcan disponibles; no se infiere aprobacion o publicacion de tiendas Apple.
No se reinicio ningun build para esta actualizacion editorial.

## Evidencia y limites editoriales

| Grupo | Contrato y codigo contrastados | Redaccion y limite |
| --- | --- | --- |
| Generacion automatica de secciones | `docs/okf/ui/SectionDetection.md`, `src/ui/panels/SectionDetectionPanel.cpp`, `src/PluginEditor.cpp`, `src/analysis/GuideSectionAlignment.cpp`, `src/analysis/GuideRepetitionMatcher.h` | Identifica anuncios de la guia y genera propuestas. La revision y la confirmacion son explicitas: no se promete generacion infalible ni aplicacion sin consentimiento. |
| Procesamiento local | `cmake/WhisperGuide.cmake`, `src/analysis/WhisperGuideTranscriber.cpp`, `src/analysis/GuideModelProvisioner.cpp` | El modelo requiere descarga autorizada; la inferencia usa audio local. El proveedor movil esta desactivado por defecto. Se limita el anuncio a escritorio de descarga directa. No confundir el fallback de Apple con MusicUnderstanding nativo validado. |
| Crossfade | `docs/okf/ui/SongCrossfade.md`, `docs/okf/transport/Transport.md`, `src/PluginProcessor.cpp`, `src/audio/RetainedVirtualBuses.h`, `src/ui/panels/SongListPanel.cpp` | Duraciones de 1/2/3/5/8/10 s, mezcla y tono/tempo independientes, clic y guia incluidos. Pads es global. Excluye loops, saltos, Ensayo y custom arrange al iniciar la transicion. No promete un fundido de duracion completa si no queda suficiente audio saliente. |
| Holyrics | `docs/okf/integrations/Holyrics.md`, `src/integrations/FreeShowClient.cpp`, `src/integrations/HolyricsModel.cpp`, `src/data/IntegrationTokenStore.cpp` | Escritorio y LAN, version minima 2.23.0 comprobada por el cliente, destinos por proveedor. Recordar tokens en llavero solo en macOS. Los proyectos con destinos Holyrics requieren formato compatible. No prometer proyeccion desde iOS/Android ni anunciar ProPresenter/EasyWorship/WorshipTools. |
| Densidad de interfaz | `docs/okf/ui/DesktopLayout.md`, `src/PluginEditor.cpp`, `src/ui/panels/ChannelGridPanel.cpp`, `src/ui/panels/SettingsPanel.cpp` | Automatico/Compacto/Normal/Grande en escritorio; no se alteran los controles tactiles moviles ni la mezcla al redimensionar. |
| Importacion | `docs/okf/importing/SongCreation.md`, `src/data/SongLibraryStore.cpp`, `src/data/MacEncoderCompatibility.h`, `src/data/SongData.cpp` | Conversor integrado primero y fallback compatible en Intel. Categoria CUERDAS musical, no guia. No prometer compatibilidad universal con todos los formatos ni borrar archivos previos al fallar una conversion. |

Los BPM decimales y los faders vinculados ya estaban incluidos desde 2.3.6 y
se conservaban en 2.4.0; no se presentan de nuevo como novedades de 2.4.1.
El estado de procesamiento/revision de Apple y los numeros de build permanecen
fuera del texto editorial y de la vista publica de ayuda.

## Entrega y comprobacion

La unica fuente editorial es `release-notes/2.4.1.json`. El Markdown de lectura,
el resumen, el indice y los documentos por version se generan con:

```sh
python3 scripts/release_notes.py --version 2.4.1 --output-dir output/release-document-2.4.1
python3 scripts/release_version.py --expect 2.4.1
python3 tests/test_release_notes.py
```

El Markdown generado no es una segunda fuente para editar. La publicacion
autorizada actualiza el feed y el respaldo generado de la ayuda. La landing
conserva sus enlaces a ese historial, sin reintroducir un bloque editorial.
La auditoria inspecciona contratos y codigo; las comprobaciones editoriales no
certifican audio real, reconocimiento con guias privadas, Holyrics fisico, QA
movil ni el resultado de los builds de Actions o de la revision de Apple.

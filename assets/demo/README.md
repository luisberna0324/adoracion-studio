# Luz de esperanza

Demo instrumental original incluida para aprender PlayWorship sin descargar
audio. El paquete contiene ocho stems FLAC mono de 24 kHz, una portada, forma
de onda y seis secciones: Intro, Verso, Coro, Puente, Coro y Final. Duración:
90 segundos; tempo: 96 BPM; tonalidad: Re mayor; compás: 4/4.

El audio fue sintetizado por
`output/app-store-preparation/demo/generate_demo.py` para preparar las capturas
del App Store. No contiene samples, grabaciones de terceros ni canciones de
la Biblioteca del usuario. La portada es el icono de PlayWorship del repositorio.
`provenance.json` conserva características, hashes y comprobaciones del origen,
sin rutas locales de herramientas.

`LuzDeEsperanza.pwproj` se empaquetó con el manifiesto original y los mismos
bytes de audio, portada y forma de onda, usando fechas ZIP fijas. El paquete
de trabajo usado para QA contenía cambios de mezcla y una indicación añadida;
esos cambios no forman parte de esta demo. `provenance.json` registra ambos
hashes y la reconstrucción. Su identidad de proyecto es
`7f3e2cd6-ff35-5b0a-a726-1dfdf8ccc66c`. Se incluye mediante
`MTControlsAssets`; la app copia sus bytes a almacenamiento privado antes de
registrarlo. El recurso empaquetado nunca se usa como proyecto editable.

`DemoSongService::ensureInstalled` se ejecuta por petición del usuario. Si la
demo ya está disponible conserva sus ajustes. Después de eliminarla de la
Biblioteca, volver a instalarla recupera el original. No agrega canciones a
setlists, no activa una lista y no reproduce audio. El usuario conserva las
acciones habituales para agregarla a sus listas y eliminarla.

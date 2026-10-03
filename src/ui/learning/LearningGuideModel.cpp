#include "LearningGuideModel.h"
#include "../../data/ApplicationDataPaths.h"

#include <algorithm>

namespace playworship::learning {
namespace {
constexpr juce::int64 maximumFileBytes = 64 * 1024;
constexpr size_t maximumLessons = 128;
constexpr int maximumIdLength = 96;

bool validId(const juce::String &id) {
  return id.isNotEmpty() && id.length() <= maximumIdLength &&
         id.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.");
}

bool containsStep(const Lesson &lesson, const juce::String &id) {
  return std::any_of(lesson.steps.begin(), lesson.steps.end(),
                     [&id](const Step &step) { return step.id == id; });
}
} // namespace

bool evaluateStepCondition(const StepCondition condition, const StepFacts &facts) {
  switch (condition) {
  case StepCondition::None: return true;
  case StepCondition::SettingsOpen: return facts.settingsOpen;
  case StepCondition::RemotePageOpen: return facts.settingsOpen && facts.remotePageOpen;
  case StepCondition::ServerModeActive: return facts.serverModeActive;
  case StepCondition::ServerListening: return facts.serverModeActive && facts.serverListening;
  case StepCondition::ServerHasClient:
    return facts.serverModeActive && facts.serverListening && facts.serverHasClient;
  case StepCondition::ClientModeActive: return facts.clientModeActive;
  case StepCondition::RemoteConnected: return facts.clientModeActive && facts.remoteConnected;
  case StepCondition::SettingsClosed: return !facts.settingsOpen;
  case StepCondition::SongLoaded: return facts.songLoaded;
  case StepCondition::RemotePlayApplied:
    return facts.clientModeActive && facts.remoteConnected && facts.songLoaded && facts.remotePlayApplied;
  case StepCondition::RemoteStopApplied:
    return facts.clientModeActive && facts.remoteConnected && facts.songLoaded && facts.remoteStopApplied;
  }
  return false;
}

RemotePrerequisite remoteStepPrerequisite(const juce::String &lessonId,
                                         const Step &step, const StepFacts &facts) {
  const bool playerStep = step.condition == StepCondition::SongLoaded ||
      step.condition == StepCondition::RemotePlayApplied ||
      step.condition == StepCondition::RemoteStopApplied || step.anchor == Anchor::RemoteIndicator;
  const bool client = lessonId == "remote-client";
  const bool server = lessonId == "remote-server";
  const bool needsClient = client && (playerStep ||
      step.condition == StepCondition::RemoteConnected || step.condition == StepCondition::SettingsClosed);
  const bool needsServer = server && (playerStep || step.anchor == Anchor::RemoteAlias ||
      step.condition == StepCondition::ServerListening ||
      step.condition == StepCondition::ServerHasClient || step.condition == StepCondition::SettingsClosed);
  if (!needsClient && !needsServer) return RemotePrerequisite::None;

  const auto navigateToRemote = [&facts](const RemotePrerequisite atRemotePage) {
    if (!facts.settingsOpen) return RemotePrerequisite::OpenSettings;
    if (!facts.remotePageOpen) return RemotePrerequisite::OpenRemotePage;
    return atRemotePage;
  };
  if (needsClient && !facts.clientModeActive)
    return navigateToRemote(RemotePrerequisite::ChooseClient);
  if (needsServer && !facts.serverModeActive)
    return navigateToRemote(RemotePrerequisite::ChooseServer);
  if (needsClient && !facts.remoteConnected)
    return navigateToRemote(RemotePrerequisite::WaitClient);
  if (needsServer && !facts.serverListening)
    return navigateToRemote(RemotePrerequisite::WaitServer);
  if (playerStep && facts.settingsOpen) return RemotePrerequisite::CloseSettings;
  return RemotePrerequisite::None;
}

std::vector<Lesson> makeLessons(const bool phone, const bool english) {
  const auto text = [english](const char *spanish, const char *translated) {
    return juce::String::fromUTF8(english ? translated : spanish);
  };
  const auto step = [&text](const char *id, const char *titleEs, const char *titleEn,
                            const char *descriptionEs, const char *descriptionEn,
                            const char *taskEs, const char *taskEn, const Anchor anchor,
                            const bool needsSong,
                            const StepCondition condition = StepCondition::None) {
    return Step{juce::String(id), text(titleEs, titleEn),
                text(descriptionEs, descriptionEn), text(taskEs, taskEn),
                anchor, needsSong, condition};
  };

  std::vector<Lesson> lessons;
  lessons.push_back({
      "first-steps", text("Primeros pasos", "First steps"),
      text("Conoce la canción, la onda y los controles de reproducción.",
           "Find your song, waveform and playback controls."),
      {
          step("welcome", "Tu espacio de reproducción", "Your playback space",
               "La barra superior controla la reproducción. La onda muestra el tiempo y el mixer reúne los instrumentos. Puedes salir de esta guía y retomarla después.",
               "The top bar controls playback. The waveform shows the timeline and the mixer brings your instruments together. You can leave this guide and resume it later.",
               "Ubica la barra, la onda y los instrumentos.", "Find the top bar, waveform and instruments.", Anchor::None, false),
          step("choose-song", "Elige una canción", "Choose a song",
               "Abre la lista de canciones o la Biblioteca para elegir una canción disponible. Un setlist reúne las canciones que vas a usar.",
               "Open the song list or Library to choose an available song. A setlist groups the songs you plan to use.",
               "Carga una canción de tu Biblioteca o setlist.", "Load a song from your Library or setlist.", Anchor::SongSelection, false),
          step("play-pause", "Reproduce y pausa", "Play and pause",
               "Play inicia la canción. El mismo control permite pausarla conservando la posición. Al volver a pulsarlo, continúa desde ese punto.",
               "Play starts the song. The same control pauses it while keeping its position. Press it again to continue from there.",
               "Prueba Play y luego Pausa.", "Try Play, then Pause.", Anchor::PlayPause, true),
          step("waveform", "Recorre la onda", "Explore the waveform",
               phone
                   ? "La onda representa la canción. Deslízala horizontalmente para recorrerla y usa dos dedos para ampliar o reducir la vista. El cursor indica la posición de reproducción."
                   : "La onda representa la canción. Amplía o desplaza la vista para ubicar un momento. La regla musical y el cursor te ayudan a seguir la reproducción.",
               phone
                   ? "The waveform represents the song. Drag horizontally to browse it and pinch to zoom in or out. The cursor shows the playback position."
                   : "The waveform represents the song. Zoom or move the view to find a moment. The musical ruler and cursor help you follow playback.",
               "Localiza un momento de la canción.", "Find a moment in the song.", Anchor::Waveform, true),
          step("stop", "Detén la reproducción", "Stop playback",
               "Stop detiene la canción y vuelve al Inicio configurado. Si hay un loop A–B de Ensayo activo, vuelve a A.",
               "Stop stops the song and returns to its configured Start. With an active Practice A–B loop, it returns to A.",
               "Pulsa Stop y observa el cursor.", "Press Stop and watch the cursor.", Anchor::Stop, true),
      }});

  lessons.push_back({
      "mix-click", text("Mezcla, clic y tono", "Mix, click and key"),
      text("Ajusta los instrumentos y adapta la canción a tu interpretación.",
           "Balance your instruments and adapt the song to your performance."),
      {
          step("instrument-level", "El volumen de cada instrumento", "Each instrument's level",
               "Cada canal tiene su propio fader. El volumen cambia su nivel en la mezcla y el VU muestra la señal de ese canal.",
               "Each channel has its own fader. Volume changes its level in the mix and the VU meter shows its signal.",
               "Mueve suavemente el fader de un instrumento.", "Gently move one instrument's fader.", Anchor::Mixer, true),
          step("mute-solo", "Mute y Solo", "Mute and Solo",
               "Mute silencia un canal. Solo permite escucharlo de forma aislada dentro de la mezcla. Desactívalos al terminar si quieres recuperar la mezcla completa.",
               "Mute silences a channel. Solo lets you hear it in isolation within the mix. Turn them off afterwards to restore the full mix.",
               "Prueba Mute o Solo y vuelve a desactivarlo.", "Try Mute or Solo, then turn it off again.", Anchor::Mixer, true),
          step("metronome", "Mantén el pulso", "Keep the beat",
               "El metrónomo genera el clic siguiendo el tiempo musical de la canción. Este botón activa o silencia el clic; al tocar los BPM puedes revisar sus divisiones y compás.",
               "The metronome generates a click following the song's musical timeline. This button enables or silences the click; tap BPM to review its divisions and meter.",
               "Prueba el interruptor del clic y vuelve a dejarlo como estaba.", "Try the click switch, then return it to its previous state.", Anchor::Metronome, true),
          step("tempo", "Cambia la velocidad", "Change the speed",
               "Tempo ajusta los BPM de reproducción sin cambiar el tono. El valor original sirve como referencia para volver a la velocidad de la canción.",
               "Tempo adjusts playback BPM without changing pitch. The original value is your reference for returning to the song's speed.",
               "Revisa el tempo original antes de ajustarlo.", "Check the original tempo before adjusting it.", Anchor::Tempo, true),
          step("key", "Adapta la tonalidad", "Adapt the key",
               "Tono transpone la canción por semitonos. Usa el tono original como referencia al ajustar la canción a la voz o al instrumento.",
               "Key transposes the song in semitones. Use the original key as your reference when adapting it to a voice or instrument.",
               "Abre Tono y localiza la tonalidad original.", "Open Key and find the original key.", Anchor::Key, true),
          step("pads", "Un fondo con Pads", "A background with Pads",
               "Pads ofrece un fondo sostenido con su propio encendido, nota y volumen. Puede sonar independientemente de la reproducción de la canción.",
               "Pads provides a sustained background with its own power, note and volume controls. It can sound independently of song playback.",
               "Abre Pads y revisa nota, encendido y volumen.", "Open Pads and check its note, power and volume.", Anchor::Pads, false),
      }});

  Lesson practice{
      "practice-sections", text(phone ? "Ensaya por instrumento" : "Secciones y automatización",
                                phone ? "Practice by instrument" : "Sections and automation"),
      text(phone ? "Recorre la onda y repite el fragmento que necesitas practicar."
                 : "Organiza la canción y prepara cambios de mezcla por sección.",
           phone ? "Explore the waveform and repeat the passage you need to practice."
                 : "Arrange the song and prepare mix changes for each section."),
      {}};
  if (phone) {
    practice.steps = {
        step("open-practice", "Abre Ensayo", "Open Practice",
             "El menú Más de la barra superior abre Ensayo en vertical. Elige un instrumento desde el nombre sobre la tarjeta de onda. En remoto, la onda individual no está disponible y el audio sigue saliendo del servidor.",
             "The top bar's More menu opens Practice in portrait. Choose an instrument using the name above its waveform card. In remote mode, individual waveforms are unavailable and audio still comes from the server.",
             "Abre Más y entra en Ensayo.", "Open More and enter Practice.", Anchor::More, true),
        step("practice-navigation", "Muévete por la canción", "Move through the song",
             "Arrastra la onda hacia la derecha para retroceder y a la izquierda para avanzar. El audio se pausa durante el movimiento y continúa al asentarse si ya estaba reproduciendo. Un doble toque en la mitad izquierda retrocede un compás; en la derecha, avanza uno. Pellizca para ampliar la vista.",
             "Drag the waveform right to move back and left to move forward. Audio pauses during movement and resumes when it settles if it was already playing. Double-tap the left half to go back one bar or the right half to go forward one. Pinch to zoom.",
             "Prueba un arrastre corto o un doble toque.", "Try a short drag or a double-tap.", Anchor::Waveform, true),
        step("practice-listen", "Enfoca tu instrumento", "Focus your instrument",
             "Un swipe hacia arriba en la tarjeta enfoca el instrumento seleccionado. Hacia abajo vuelve a la mezcla normal; otro swipe hacia abajo reduce ese instrumento. Clic, guía y Pads conservan su nivel. Restaurar mezcla recupera la escucha normal.",
             "Swipe up on the card to focus the selected instrument. Swipe down to restore the normal mix; another swipe down reduces that instrument. Click, guide and Pads keep their levels. Restore mix returns to normal listening.",
             "Enfoca un instrumento y restaura la mezcla.", "Focus an instrument, then restore the mix.", Anchor::Waveform, true),
        step("practice-ab", "Repite una región A–B", "Repeat an A–B region",
             "A marca el inicio y B cierra la región y activa su repetición. Ambas marcas encajan al pulso más cercano. Repetir apaga el loop conservando las marcas; la papelera borra la región. Es temporal y se libera al cerrar Ensayo si todavía pertenece a esa vista.",
             "A marks the start and B closes the region and enables repetition. Both snap to the nearest beat. Repeat turns looping off while keeping the marks; the trash button clears the region. It is temporary and is released when Practice closes if that view still owns it.",
             "Marca A, avanza, marca B y prueba Borrar región.", "Mark A, move forward, mark B and try Clear region.", Anchor::PracticeLoop, true),
    };
  } else {
    practice.steps = {
        step("section-edit", "Edita las secciones", "Edit sections",
             "Activa la edición de la onda y elige una sección para ver sus acciones. Si no hay secciones, marca un punto en la regla y usa + para agregar una.",
             "Enable waveform editing and select a section to see its actions. If there are no sections, mark a point on the ruler and use + to add one.",
             "Entra en edición y selecciona una sección.", "Enter editing and select a section.", Anchor::Waveform, true),
        step("section-copy", "Copia y mueve con referencia", "Copy and move with a reference",
             "Copiar una sección conserva su audio de origen. Puedes mover la copia sin cambiar la sección original. Deshacer permite revertir una edición confirmada.",
             "Copying a section preserves its source audio. You can move the copy without changing the original section. Undo lets you revert a confirmed edit.",
             "Revisa Copiar y Deshacer en la edición de secciones.", "Find Copy and Undo in section editing.", Anchor::Waveform, true),
        step("section-automation", "Automatiza una sección", "Automate a section",
             "Automatizar abre un borrador y pausa la reproducción. La barra flotante queda sobre esa sección, fijada por su ID. Ajusta volumen, ganancia o mute de instrumentos, y nota, encendido o nivel de Pads. El indicador A y el borde pulsante señalan qué se está editando.",
             "Automate opens a draft and pauses playback. The floating bar remains over that section, fixed to its ID. Adjust instrument volume, gain or mute, and Pads note, power or level. The A indicator and pulsing border show what is being edited.",
             "Localiza Automatizar sobre una sección.", "Find Automate on a section.", Anchor::Waveform, true),
        step("section-automation-save", "Guarda o descarta el borrador", "Save or discard the draft",
             "Listo guarda tus cambios y Cancelar descarta el borrador. Más permite Borrar automatización o Volver a la sección. Se guardan valores, no curvas. Master, Clic, Guía y ruteo quedan manuales; Solo de instrumentos se bloquea durante el borrador.",
             "Done saves your changes and Cancel discards the draft. More offers Remove automation or Return to section. Values are saved, not curves. Master, Click, Guide and routing stay manual; instrument Solo is disabled during a draft.",
             "Identifica Listo, Cancelar y Más en la barra flotante.", "Identify Done, Cancel and More on the floating bar.", Anchor::Waveform, true),
    };
  }
  lessons.push_back(std::move(practice));

  lessons.push_back({
      "remote-server", text("Preparar un servidor", "Prepare a server"),
      text("Comparte una canción y sus controles con otros equipos de tu red.",
           "Share a song and its controls with other devices on your network."),
      {
          step("server-prepare", "El equipo que reproduce el audio", "The device that plays audio",
               "El servidor reproduce la canción y comparte su estado, onda y controles con varios clientes. Para practicar necesitas otro equipo con PlayWorship en la misma red local. Los cambios de los clientes afectan al audio del servidor.",
               "The server plays the song and shares its state, waveform and controls with multiple clients. To practice, you need another device with PlayWorship on the same local network. Client changes affect the server's audio.",
               "Prepara el segundo equipo y confirma que comparten la red.", "Prepare the second device and confirm they share the network.", Anchor::None, false),
          step("server-open-settings", "Abre Configuración", "Open Settings",
               "Vamos a activar el servidor usando los controles habituales. Puedes pausar esta guía y retomarla desde este paso cuando tengas el segundo equipo listo.",
               "We will enable the server using the usual controls. You can pause this guide and resume at this step when the second device is ready.",
               "Abre Configuración desde la barra superior.", "Open Settings from the top bar.", Anchor::Settings, false, StepCondition::SettingsOpen),
          step("server-open-remote", "Entra en Control remoto", "Open Remote control",
               "En móvil y tablet, Control remoto está en Audio y conexiones. En el escritorio ancho, sus controles están en General. La guía continúa dentro de Configuración.",
               "On phone and tablet, Remote control is under Audio and connections. On a wide desktop, its controls are in General. The guide continues inside Settings.",
               "Abre Control remoto; en escritorio, entra en General.", "Open Remote control; on desktop, open General.", Anchor::RemoteSettings, false, StepCondition::RemotePageOpen),
          step("server-enable", "Activa Servidor", "Enable Server",
               "Local es el funcionamiento habitual. Servidor mantiene la reproducción en este equipo y anuncia su presencia en la red. Cliente se usa en el equipo desde el que quieres controlar la canción.",
               "Local is the usual mode. Server keeps playback on this device and announces it on the network. Client is used on the device you want to control the song from.",
               "Selecciona Servidor en Local · Cliente · Servidor.", "Select Server in Local · Client · Server.", Anchor::RemoteMode, false, StepCondition::ServerModeActive),
          step("server-name", "Reconoce el nombre del equipo", "Recognize the device name",
               "Los clientes encontrarán el servidor por el nombre de este dispositivo. Puedes escribir un alias para distinguirlo, por ejemplo, Escenario. Si dejas el alias vacío se usa el nombre del equipo; un iPhone o iPad puede mostrar un nombre genérico.",
               "Clients will find the server by this device's name. You can enter an alias to identify it, such as Stage. Leaving the alias empty uses the device name; an iPhone or iPad may display a generic name.",
               "Revisa el nombre o escribe un alias y confirma el campo.", "Check the name or enter an alias and confirm the field.", Anchor::RemoteAlias, false),
          step("server-listening", "Comprueba que está disponible", "Check that it is available",
               "El estado confirma si el servidor está escuchando. En iPhone y iPad, permite Red local cuando iOS lo solicite. Si aparece un error, resuélvelo antes de continuar; cambiar de página no apaga el servidor.",
               "Status confirms whether the server is listening. On iPhone and iPad, allow Local Network access when iOS asks. If an error appears, resolve it before continuing; changing pages does not turn the server off.",
               "Espera a que el estado confirme que el servidor está listo.", "Wait for status to confirm that the server is ready.", Anchor::RemoteStatus, false, StepCondition::ServerListening),
          step("server-connect-client", "Conecta el segundo equipo", "Connect the second device",
               "En el otro equipo abre Configuración → Control remoto y selecciona Cliente. Con un único servidor se conecta automáticamente; si aparecen varios, elige este equipo por su nombre o alias. En iPhone y iPad concede el permiso de Red local.",
               "On the other device, open Settings → Remote control and select Client. A single server connects automatically; if several appear, choose this device by its name or alias. On iPhone and iPad, grant Local Network access.",
               "Conecta un Cliente y espera a que aparezca en el estado.", "Connect a Client and wait for it to appear in status.", Anchor::RemoteStatus, false, StepCondition::ServerHasClient),
          step("server-close-settings", "Vuelve al reproductor", "Return to the player",
               "La conexión permanece activa cuando cierras Configuración. El servidor conserva la misma onda, mixer y controles de reproducción.",
               "The connection stays active when you close Settings. The server keeps the same waveform, mixer and playback controls.",
               "Cierra Configuración con su botón X.", "Close Settings using its X button.", Anchor::SettingsClose, false, StepCondition::SettingsClosed),
          step("server-load-song", "Comparte una canción", "Share a song",
               "Si ya hay una canción cargada, está disponible para el cliente. Si no, elige una desde Biblioteca o tu setlist. El cliente recibirá sus datos, canales y onda; el audio seguirá saliendo de este servidor.",
               "If a song is already loaded, it is available to the client. Otherwise, choose one from the Library or your setlist. The client receives its details, channels and waveform; audio continues to come from this server.",
               "Carga una canción o confirma la que ya está seleccionada.", "Load a song or confirm the one already selected.", Anchor::SongSelection, false, StepCondition::SongLoaded),
          step("server-ready", "Servidor preparado", "Server prepared",
               "El otro equipo ya puede usar Play, Stop y los controles de mezcla sobre esta canción. Los cambios se confirman en el servidor y se comparten con los clientes. Para enseñar el uso del segundo equipo, abre allí la guía Conectarse como cliente.",
               "The other device can now use Play, Stop and mix controls on this song. Changes are confirmed on the server and shared with clients. To learn how to use the second device, open Connect as a client there.",
               "Comprueba en el cliente la canción, la onda y sus canales.", "Check the song, waveform and channels on the client.", Anchor::Waveform, true),
      }});

  lessons.push_back({
      "remote-client", text("Conectarse como cliente", "Connect as a client"),
      text("Conecta con un servidor y prueba la reproducción desde este equipo.",
           "Connect to a server and try playback from this device."),
      {
          step("client-prepare", "Prepara ambos equipos", "Prepare both devices",
               "Necesitas un equipo con PlayWorship en modo Servidor, dentro de la misma red local. Este cliente recibirá la canción, los canales y la onda. El sonido sale del servidor: las acciones que practiques aquí afectan a su reproducción.",
               "You need a device with PlayWorship in Server mode on the same local network. This client receives the song, channels and waveform. Audio comes from the server: the actions you practice here affect its playback.",
               "Confirma la red y prepara el servidor con una canción.", "Confirm the network and prepare the server with a song.", Anchor::None, false),
          step("client-open-settings", "Abre Configuración", "Open Settings",
               "Vamos a establecer la conexión dentro de Configuración. Si todavía no tienes un servidor disponible, pausa la guía aquí y retómala cuando esté preparado.",
               "We will establish the connection inside Settings. If you do not have a server available yet, pause the guide here and resume when it is prepared.",
               "Abre Configuración desde la barra superior.", "Open Settings from the top bar.", Anchor::Settings, false, StepCondition::SettingsOpen),
          step("client-open-remote", "Entra en Control remoto", "Open Remote control",
               "En móvil y tablet, busca Control remoto en Audio y conexiones. En el escritorio ancho está en General. Esta página reúne el modo, la lista de servidores y el estado de conexión.",
               "On phone and tablet, find Remote control under Audio and connections. On a wide desktop it is in General. This page contains the mode, server list and connection status.",
               "Abre Control remoto; en escritorio, entra en General.", "Open Remote control; on desktop, open General.", Anchor::RemoteSettings, false, StepCondition::RemotePageOpen),
          step("client-enable", "Activa Cliente", "Enable Client",
               "Cliente pausa la reproducción local y recibe el estado del servidor. La app conserva la distribución de sus controles. En iPhone y iPad permite Red local cuando iOS lo solicite para poder encontrar equipos.",
               "Client pauses local playback and receives the server's state. The app keeps the layout of its controls. On iPhone and iPad, allow Local Network access when iOS asks so devices can be found.",
               "Selecciona Cliente y concede Red local si se solicita.", "Select Client and allow Local Network access if requested.", Anchor::RemoteMode, false, StepCondition::ClientModeActive),
          step("client-connect", "Encuentra tu servidor", "Find your server",
               "Con un único servidor descubierto la conexión es automática. Si hay varios, abre la lista y elige el nombre o alias del equipo preparado. Espera al estado completo: encontrar el nombre o abrir la conexión no significa que los controles estén listos.",
               "With a single discovered server, connection is automatic. If several are found, open the list and choose the prepared device's name or alias. Wait for the full state: finding the name or opening the connection does not mean controls are ready.",
               "Elige tu servidor si hace falta y espera la conexión.", "Choose your server if needed and wait for the connection.", Anchor::RemoteServers, false, StepCondition::RemoteConnected),
          step("client-status", "Confirma la conexión", "Confirm the connection",
               "El estado debe confirmar que se recibió la información del servidor. Si no se conecta, comprueba que el otro equipo siga en Servidor, que ambos usen la misma red y que el permiso de Red local esté activo.",
               "Status must confirm that the server's information has been received. If it does not connect, check that the other device is still in Server mode, both use the same network and Local Network access is allowed.",
               "Revisa el servidor seleccionado y su estado conectado.", "Check the selected server and its connected status.", Anchor::RemoteStatus, false, StepCondition::RemoteConnected),
          step("client-close-settings", "Vuelve a la canción", "Return to the song",
               "Al cerrar Configuración sigues conectado. La onda y los canales utilizan los datos del servidor; sus controles funcionan en los mismos lugares de siempre.",
               "Closing Settings keeps you connected. The waveform and channels use the server's data; their controls work in their usual positions.",
               "Cierra Configuración con su botón X.", "Close Settings using its X button.", Anchor::SettingsClose, false, StepCondition::SettingsClosed),
          step("client-load-song", "Selecciona la canción remota", "Select the remote song",
               "La lista muestra las canciones y setlists compartidos por el servidor. Si ya hay una canción cargada, puedes usarla; si no, elige una disponible. La selección se aplica al servidor y se muestra en los clientes.",
               "The list shows the songs and setlists shared by the server. If a song is already loaded, you can use it; otherwise choose an available one. Selection is applied to the server and shown on clients.",
               "Carga una canción compartida o confirma la ya cargada.", "Load a shared song or confirm the one already loaded.", Anchor::SongSelection, false, StepCondition::SongLoaded),
          step("client-indicator", "Ubica el indicador REMOTO", "Find the REMOTE indicator",
               "La etiqueta REMOTO en la esquina superior derecha de la onda confirma que la información del servidor está disponible. La canción, los canales y los VU usan ese estado. Este cliente controla la sesión y no reproduce los stems del servidor.",
               "The REMOTE label in the waveform's upper-right corner confirms that the server's information is available. The song, channels and VU meters use that state. This client controls the session and does not play the server's stems.",
               "Ubica REMOTO junto a la onda de la canción.", "Find REMOTE beside the song's waveform.", Anchor::RemoteIndicator, true, StepCondition::RemoteConnected),
          step("client-play", "Inicia desde el cliente", "Start from the client",
               "Play envía una acción al servidor. El cursor y los VU reflejan su respuesta; escucha el audio en el equipo servidor. La guía habilita Continuar cuando el servidor confirma tu acción y el estado de reproducción.",
               "Play sends an action to the server. The cursor and VU meters reflect its response; listen for audio on the server device. The guide enables Continue when the server confirms your action and playback state.",
               "Pulsa Play; si ya está reproduciendo, pausa y vuelve a Play.", "Press Play; if already playing, pause and press Play again.", Anchor::PlayPause, true, StepCondition::RemotePlayApplied),
          step("client-stop", "Detén desde el cliente", "Stop from the client",
               "Stop detiene la reproducción del servidor y devuelve el cursor al Inicio configurado, o a A si hay un loop A–B de Ensayo activo. Espera la confirmación para comprobar que la acción se aplicó.",
               "Stop stops playback on the server and returns the cursor to the configured Start, or to A with an active Practice A–B loop. Wait for confirmation to check that the action was applied.",
               "Pulsa Stop y espera a que se confirme la detención.", "Press Stop and wait for the stop to be confirmed.", Anchor::Stop, true, StepCondition::RemoteStopApplied),
          step("client-ready", "Control remoto preparado", "Remote control prepared",
               "Ya conectaste este cliente, recibiste una canción y probaste Play y Stop con confirmación del servidor. También puedes ajustar mezcla, clic, tono, tempo y Pads desde los controles habituales. Para volver a tu canción local, elige Local en Control remoto; se recupera pausada.",
               "You connected this client, received a song and tried Play and Stop with server confirmation. You can also adjust mix, click, key, tempo and Pads from the usual controls. To return to your local song, choose Local in Remote control; it is restored paused.",
               "Termina la guía cuando hayas comprobado ambos controles.", "Finish the guide once you have checked both controls.", Anchor::None, false),
      }});
  return lessons;
}

ProgressStore::ProgressStore(juce::File requestedFile) : file(std::move(requestedFile)) {
  load();
}

juce::File ProgressStore::defaultFile() {
  return ApplicationDataPaths::applicationDataDirectory()
      .getChildFile("MTControls").getChildFile("learning_progress.json");
}

LessonProgress ProgressStore::get(const juce::String &lessonId) const {
  const auto found = progress.find(lessonId);
  return found != progress.end() ? found->second : LessonProgress{};
}

int ProgressStore::resumeIndex(const Lesson &lesson) const {
  const auto saved = get(lesson.id);
  const auto found = std::find_if(lesson.steps.begin(), lesson.steps.end(),
                                [&saved](const Step &step) { return step.id == saved.stepId; });
  return found == lesson.steps.end() ? 0 : static_cast<int>(found - lesson.steps.begin());
}

bool ProgressStore::hasProgress(const Lesson &lesson) const {
  const auto found = progress.find(lesson.id);
  return found != progress.end() && containsStep(lesson, found->second.stepId);
}

juce::Result ProgressStore::record(const Lesson &lesson, const int stepIndex,
                                  const bool completed) {
  if (!validId(lesson.id) || stepIndex < 0 || stepIndex >= static_cast<int>(lesson.steps.size()) ||
      !validId(lesson.steps[static_cast<size_t>(stepIndex)].id))
    return juce::Result::fail("Invalid learning lesson or step.");
  const auto previous = progress.find(lesson.id);
  const auto existed = previous != progress.end();
  if (!existed && progress.size() >= maximumLessons)
    return juce::Result::fail("Too many saved learning lessons.");
  const LessonProgress saved = existed ? previous->second : LessonProgress{};
  const LessonProgress next{lesson.steps[static_cast<size_t>(stepIndex)].id, completed};
  if (existed && saved.stepId == next.stepId && saved.completed == next.completed)
    return juce::Result::ok();
  progress[lesson.id] = next;
  const auto result = save();
  if (result.failed()) {
    if (existed)
      progress[lesson.id] = saved;
    else
      progress.erase(lesson.id);
  }
  return result;
}

juce::Result ProgressStore::restart(const Lesson &lesson) {
  return record(lesson, 0, false);
}

void ProgressStore::load() {
  progress.clear();
  if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > maximumFileBytes)
    return;
  const auto parsed = juce::JSON::parse(file);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr)
    return;
  const auto version = root->getProperty("version");
  if (!version.isInt() || static_cast<int>(version) != 1)
    return;
  const auto lessons = root->getProperty("lessons");
  const auto *entries = lessons.getDynamicObject();
  if (entries == nullptr)
    return;
  const auto &properties = entries->getProperties();
  for (int index = 0; index < properties.size() &&
                      progress.size() < maximumLessons; ++index) {
    const auto id = properties.getName(index).toString();
    const auto *entry = properties.getValueAt(index).getDynamicObject();
    if (!validId(id) || entry == nullptr)
      continue;
    const auto stepId = entry->getProperty("stepId");
    const auto completed = entry->getProperty("completed");
    if (!stepId.isString() || !validId(stepId.toString()) || !completed.isBool())
      continue;
    progress.emplace(id, LessonProgress{stepId.toString(), static_cast<bool>(completed)});
  }
}

juce::Result ProgressStore::save() const {
  const auto directoryResult = file.getParentDirectory().createDirectory();
  if (directoryResult.failed())
    return juce::Result::fail("Could not create learning progress directory: " +
                              directoryResult.getErrorMessage());
  auto *entries = new juce::DynamicObject();
  for (const auto &entry : progress) {
    auto *value = new juce::DynamicObject();
    value->setProperty("stepId", entry.second.stepId);
    value->setProperty("completed", entry.second.completed);
    entries->setProperty(entry.first, juce::var(value));
  }
  auto *root = new juce::DynamicObject();
  root->setProperty("version", 1);
  root->setProperty("lessons", juce::var(entries));
  const auto json = juce::JSON::toString(juce::var(root), true);
  const auto byteCount = static_cast<size_t>(json.getNumBytesAsUTF8());
  if (byteCount > static_cast<size_t>(maximumFileBytes))
    return juce::Result::fail("Learning progress is too large to save.");
  juce::TemporaryFile temporary(file, juce::TemporaryFile::useHiddenFile);
  {
    juce::FileOutputStream output(temporary.getFile());
    if (!output.openedOk() || !output.write(json.toRawUTF8(), byteCount))
      return juce::Result::fail("Could not write learning progress.");
    output.flush();
    if (output.getStatus().failed() ||
        temporary.getFile().getSize() != static_cast<juce::int64>(byteCount))
      return juce::Result::fail("Could not finish writing learning progress.");
  }
  if (!temporary.overwriteTargetFileWithTemporary())
    return juce::Result::fail("Could not replace learning progress.");
  return juce::Result::ok();
}
} // namespace playworship::learning

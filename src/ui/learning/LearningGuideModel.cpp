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

std::vector<Lesson> makeLessons(const bool phone, const bool english) {
  const auto text = [english](const char *spanish, const char *translated) {
    return juce::String::fromUTF8(english ? translated : spanish);
  };
  const auto step = [&text](const char *id, const char *titleEs, const char *titleEn,
                            const char *descriptionEs, const char *descriptionEn,
                            const char *taskEs, const char *taskEn, const Anchor anchor,
                            const bool needsSong) {
    return Step{juce::String(id), text(titleEs, titleEn),
                text(descriptionEs, descriptionEn), text(taskEs, taskEn),
                anchor, needsSong};
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
      "settings-remote", text("Configuración y control remoto", "Settings and remote control"),
      text("Prepara tus salidas y conoce los modos de conexión entre equipos.",
           "Prepare your outputs and learn how devices connect."),
      {
          step("settings", "Encuentra la configuración", "Find Settings",
               "Configuración agrupa las opciones de la aplicación. Apariencia e idioma personaliza la interfaz; las ayudas junto a cada opción explican su función.",
               "Settings groups the application's options. Appearance and language personalizes the interface; help beside each option explains its purpose.",
               "Abre Configuración y revisa sus categorías.", "Open Settings and review its categories.", Anchor::Settings, false),
          step("routing", "Prepara las salidas", "Prepare your outputs",
               "Ruteo asigna los instrumentos, el clic y la guía a las salidas disponibles de tu dispositivo de audio. Revisa esas asignaciones al cambiar de dispositivo. El hardware se configura en el equipo que reproduce el audio.",
               "Routing assigns instruments, click and guide to the available outputs of your audio device. Review the assignments when changing devices. Hardware is configured on the device playing the audio.",
               "Entra en Ruteo y revisa las salidas disponibles.", "Open Routing and check the available outputs.", Anchor::Settings, false),
          step("remote-modes", "Local, Cliente o Servidor", "Local, Client or Server",
               "Local es el funcionamiento habitual. Servidor reproduce el audio y comparte el estado con varios clientes. Cliente recibe canciones, controles y onda, y envía acciones al servidor; no reproduce sus stems. La interfaz de reproducción conserva su distribución.",
               "Local is the normal mode. Server plays the audio and shares state with multiple clients. Client receives songs, controls and waveform, and sends actions to the server; it does not play the server's stems. The playback interface keeps its layout.",
               "Abre Control remoto y revisa los tres modos.", "Open Remote control and review the three modes.", Anchor::Settings, false),
          step("remote-connect", "Conecta dentro de tu red", "Connect on your network",
               "Los equipos deben estar en la misma red local. En Cliente, un único servidor descubierto se conecta automáticamente; si hay varios, elige uno en Configuración. En iPhone y iPad, permite el acceso a Red local cuando el sistema lo solicite.",
               "Devices must be on the same local network. In Client mode, a single discovered server connects automatically; with several servers, choose one in Settings. On iPhone and iPad, allow Local Network access when the system asks.",
               "Si tienes otro equipo como Servidor, elige Cliente y revisa la conexión.", "If another device is running as Server, choose Client and check the connection.", Anchor::Settings, false),
          step("remote-reconnect", "Reconoce el estado remoto", "Recognize remote status",
               "REMOTO indica que llegó el estado del servidor. Si se pierde la conexión, aparece RECONECTANDO: se conserva la vista, la posición se congela y los VU bajan a cero. Los controles esperan el nuevo estado antes de enviar acciones otra vez. La reproducción del servidor continúa.",
               "REMOTE means the server's state has arrived. If the connection drops, RECONNECTING appears: the view stays, position freezes and VU meters fall to zero. Controls wait for fresh state before sending actions again. Server playback continues.",
               "En modo Cliente, ubica el indicador remoto en la esquina de la onda.", "In Client mode, find the remote indicator in the waveform's corner.", Anchor::Waveform, false),
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

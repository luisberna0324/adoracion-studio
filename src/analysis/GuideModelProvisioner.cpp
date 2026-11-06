#include "GuideModelProvisioner.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <thread>

namespace playworship::analysis {
namespace {
constexpr juce::int64 maximumModelBytes = 256 * 1024 * 1024;

class HttpsModelSource final : public IGuideModelDownloadSource {
public:
  std::unique_ptr<juce::InputStream> open(const GuideModelAsset &asset,
      const CancellationCheck &cancelled, juce::String &error) override {
    if (cancelled()) return {};
    int status = 0;
    auto stream = juce::URL(asset.url).createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(15000).withNumRedirectsToFollow(5)
            .withStatusCode(&status).withHttpRequestCmd("GET")
            .withProgressCallback([cancelled](int, int) { return !cancelled(); }));
    if (!stream || status < 200 || status >= 300) {
      error = juce::String::fromUTF8("No se pudo descargar el modelo de voz. Comprueba la conexión y vuelve a intentar.");
      return {};
    }
    return stream;
  }
};

class CancelableHashStream final : public juce::InputStream {
public:
  CancelableHashStream(juce::FileInputStream &s, const CancellationCheck &c)
      : stream(s), cancelled(c) {}
  juce::int64 getTotalLength() override { return stream.getTotalLength(); }
  juce::int64 getPosition() override { return stream.getPosition(); }
  bool setPosition(juce::int64 p) override { return stream.setPosition(p); }
  bool isExhausted() override { return cancelled() || stream.isExhausted(); }
  int read(void *data, int bytes) override {
    return cancelled() ? 0 : stream.read(data, std::min(bytes, 64 * 1024));
  }
private:
  juce::FileInputStream &stream;
  const CancellationCheck &cancelled;
};

bool verifiedModel(const juce::File &file, const GuideModelAsset &asset,
                   const CancellationCheck &cancelled) {
  if (!file.existsAsFile() || file.getSize() != asset.sizeBytes) return false;
  auto input = file.createInputStream();
  if (!input) return false;
  CancelableHashStream stream(*input, cancelled);
  const auto digest = juce::SHA256(stream).toHexString();
  return !cancelled() && input->getStatus().wasOk() &&
         input->getPosition() == asset.sizeBytes && digest == asset.sha256;
}

void prepare(const juce::File &destination, const GuideModelAsset &asset,
             const std::shared_ptr<IGuideModelDownloadSource> &source,
             const CancellationCheck &cancelled,
             const GuideTranscriptionProgress &progress,
             GuideModelPreparationResult &result) {
  const auto report = [&](double fraction) { if (progress && !cancelled()) progress(fraction); };
  if (cancelled()) return;
  if (destination == juce::File{} || asset.sizeBytes <= 0 || asset.sizeBytes > maximumModelBytes ||
      asset.sha256.length() != 64 || !asset.sha256.containsOnly("0123456789abcdef") ||
      !asset.url.startsWith("https://")) {
    result.error = juce::String::fromUTF8("La preparación del modelo de voz no tiene una configuración válida."); return;
  }
  report(0);
  if (destination.exists()) {
    if (verifiedModel(destination, asset, cancelled)) {
      result.modelFile = destination; result.alreadyPresent = true; report(1); return;
    }
    if (!cancelled()) result.error = juce::String::fromUTF8("El archivo de modelo existente no es válido. Elige otro archivo o una ubicación libre.");
    return;
  }
  if (!destination.getParentDirectory().createDirectory()) {
    result.error = juce::String::fromUTF8("No se puede crear la carpeta del modelo de voz."); return;
  }
  juce::TemporaryFile temporary(destination);
  {
    auto output = temporary.getFile().createOutputStream();
    if (!output || output->getStatus().failed()) {
      result.error = juce::String::fromUTF8("No se puede guardar el modelo de voz."); return;
    }
    auto input = source->open(asset, cancelled, result.error);
    if (!input || cancelled()) {
      if (result.error.isEmpty() && !cancelled()) result.error = juce::String::fromUTF8("No se pudo iniciar la descarga del modelo de voz.");
      return;
    }
    if (input->getTotalLength() > asset.sizeBytes) {
      result.error = juce::String::fromUTF8("La descarga no corresponde al modelo de voz esperado."); return;
    }
    std::array<char, 64 * 1024> block{};
    juce::int64 received = 0;
    while (!input->isExhausted()) {
      if (cancelled()) return;
      const auto count = input->read(block.data(), int(block.size()));
      if (cancelled()) return;
      if (count <= 0) break;
      received += count;
      if (received > asset.sizeBytes || !output->write(block.data(), size_t(count))) {
        result.error = juce::String::fromUTF8("La descarga del modelo está incompleta o no se puede guardar."); return;
      }
      report(0.9 * double(received) / double(asset.sizeBytes));
    }
    output->flush();
    if (received != asset.sizeBytes || output->getStatus().failed()) {
      result.error = juce::String::fromUTF8("La descarga del modelo de voz quedó incompleta. Vuelve a intentar."); return;
    }
  }
  report(0.95);
  if (!verifiedModel(temporary.getFile(), asset, cancelled)) {
    if (!cancelled()) result.error = juce::String::fromUTF8("No se pudo verificar el modelo de voz descargado. Vuelve a intentar.");
    return;
  }
  if (cancelled()) return;
  if (destination.exists()) {
    if (!verifiedModel(destination, asset, cancelled)) {
      if (!cancelled()) result.error = juce::String::fromUTF8("La ubicación del modelo cambió durante la descarga.");
      return;
    }
    result.alreadyPresent = true;
  } else if (!temporary.overwriteTargetFileWithTemporary()) {
    result.error = juce::String::fromUTF8("No se puede completar el guardado del modelo de voz."); return;
  }
  if (cancelled()) return;
  result.modelFile = destination;
  report(1);
}
} // namespace

GuideModelAsset defaultWhisperGuideModelAsset() {
  return {"https://huggingface.co/ggerganov/whisper.cpp/resolve/f281eb45af861ab5e5297d23694b7d46e090c02c/ggml-base-q5_1.bin",
          "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898", 59707625};
}

std::shared_ptr<GuideModelPreparationTask> prepareWhisperGuideModel(
    juce::File destination, GuideTranscriptionProgress progress,
    std::function<void(GuideModelPreparationResult)> completion,
    std::shared_ptr<IGuideModelDownloadSource> source, GuideModelAsset asset) {
  auto task = std::make_shared<GuideModelPreparationTask>();
  const auto state = task->cancelled;
  if (!source) source = std::make_shared<HttpsModelSource>();
  std::thread([destination, progress = std::move(progress), completion = std::move(completion),
               source = std::move(source), asset = std::move(asset), state] {
    GuideModelPreparationResult result;
    const auto cancelled = [state] { return state->load(); };
    try { prepare(destination, asset, source, cancelled, progress, result); }
    catch (...) { result.error = juce::String::fromUTF8("No se pudo preparar el modelo de voz. Vuelve a intentar."); }
    result.cancelled = cancelled();
    if (result.cancelled || result.error.isNotEmpty()) result.modelFile = juce::File{};
    if (completion) completion(std::move(result));
  }).detach();
  return task;
}

} // namespace playworship::analysis

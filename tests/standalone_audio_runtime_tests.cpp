#include "audio/StandaloneAudioRuntime.h"
#include "PluginProcessor.h"
#include "IsolatedTestRoot.h"
#include "data/AssetBootstrapManager.h"
#include "ui/NativeEditorBoundsConstrainer.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/components/ChannelStripLookAndFeel.h"
#include "ui/components/ChannelStripCard.h"
#include "data/ProjectStore.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>

namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TestDevice final : public juce::AudioIODevice {
public:
  explicit TestDevice(int count) : AudioIODevice("Test output", "Test audio"), channels(count) {}
  juce::StringArray getOutputChannelNames() override {
    juce::StringArray result;
    for (int i = 0; i < channels; ++i) result.add("Out " + juce::String(i + 1));
    return result;
  }
  juce::StringArray getInputChannelNames() override { return {"Input 1", "Input 2"}; }
  juce::Array<double> getAvailableSampleRates() override { return {48000.0}; }
  juce::Array<int> getAvailableBufferSizes() override { return {64}; }
  int getDefaultBufferSize() override { return 64; }
  juce::String open(const juce::BigInteger &in, const juce::BigInteger &out, double, int) override {
    expect(in.isZero(), "input channels are never opened, including during restore");
    inputs = in;
    juce::BigInteger available;
    available.setRange(0, channels, true);
    outputs = out & available;
    opened = true;
    return {};
  }
  void close() override { stop(); opened = false; }
  bool isOpen() override { return opened; }
  void start(juce::AudioIODeviceCallback *next) override {
    stop(); callback = next;
    if (callback != nullptr) callback->audioDeviceAboutToStart(this);
  }
  void stop() override {
    if (auto *old = std::exchange(callback, nullptr)) old->audioDeviceStopped();
  }
  bool isPlaying() override { return callback != nullptr; }
  juce::String getLastError() override { return {}; }
  int getCurrentBufferSizeSamples() override { return 64; }
  double getCurrentSampleRate() override { return 48000; }
  int getCurrentBitDepth() override { return 32; }
  juce::BigInteger getActiveOutputChannels() const override { return outputs; }
  juce::BigInteger getActiveInputChannels() const override { return inputs; }
  int getOutputLatencyInSamples() override { return 0; }
  int getInputLatencyInSamples() override { return 0; }
  void render(juce::AudioBuffer<float> &buffer) {
    expect(callback != nullptr, "audio callback attached");
    callback->audioDeviceIOCallbackWithContext(nullptr, 0, buffer.getArrayOfWritePointers(),
                                               buffer.getNumChannels(), buffer.getNumSamples(), {});
  }
private:
  int channels;
  bool opened = false;
  juce::BigInteger inputs, outputs;
  juce::AudioIODeviceCallback *callback = nullptr;
};

class TestDeviceType final : public juce::AudioIODeviceType {
public:
  explicit TestDeviceType(int count) : AudioIODeviceType("Test audio"), channels(count) {}
  void scanForDevices() override {}
  juce::StringArray getDeviceNames(bool) const override { return {"Test output"}; }
  int getDefaultDeviceIndex(bool) const override { return 0; }
  int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
  bool hasSeparateInputsAndOutputs() const override { return false; }
  juce::AudioIODevice *createDevice(const juce::String&, const juce::String&) override {
    return new TestDevice(channels);
  }
private:
  int channels;
};

class TestEditor final : public juce::AudioProcessorEditor {
public:
  explicit TestEditor(juce::AudioProcessor &processor) : AudioProcessorEditor(processor) {}
};

class TestProcessor final : public juce::AudioProcessor {
public:
  TestProcessor() : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), false)
                                     .withOutput("Output", juce::AudioChannelSet::discreteChannels(64), true)) {}
  const juce::String getName() const override { return "Test processor"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}
  void prepareToPlay(double, int) override { ++prepares; }
  void releaseResources() override { ++releases; }
  bool isBusesLayoutSupported(const BusesLayout &layout) const override {
    return layout.getMainInputChannelSet().isDisabled() &&
           layout.getMainOutputChannelSet().size() > 0 &&
           layout.getMainOutputChannelSet().size() <= 64;
  }
  void processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer&) override {
    ++blocks;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
      for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        buffer.setSample(ch, sample, static_cast<float>(ch + 1) / 64.0f);
  }
  bool hasEditor() const override { return true; }
  juce::AudioProcessorEditor *createEditor() override {
    return new TestEditor(*this);
  }
  void getStateInformation(juce::MemoryBlock &dest) override { dest.replaceAll(&state, sizeof(state)); }
  void setStateInformation(const void *source, int size) override {
    if (checkStateRestore) checkStateRestore();
    if (size == sizeof(state)) std::memcpy(&state, source, sizeof(state));
  }
  int state = 0, prepares = 0, releases = 0, blocks = 0;
  std::function<void()> checkStateRestore;
};

void testDynamicWindowLimits() {
  TestProcessor processor;
  juce::DocumentWindow window("Test window", juce::Colours::black, 0, false);
  std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
  NativeEditorBoundsConstrainer constrainer(window, processor);
  editor->setResizable(true, false);
  auto check = [&](int width, int height) {
    editor->setResizeLimits(width, height, 32000, 32000);
    expect(constrainer.getWrappedConstrainer()->getMinimumWidth() == width,
           "window observes current editor width limit");
    expect(constrainer.getWrappedConstrainer()->getMinimumHeight() == height,
           "window observes current editor height limit");
  };
  check(480, 500); // Welcome/login.
  check(940, 600); // Main workspace.
  check(480, 500); // Logout returns to welcome.
  processor.editorBeingDeleted(editor.get());
}

void testRuntime(int outputCount) {
  juce::PropertySet settings;
  int originalState = 12345;
  settings.setValue("filterState", juce::MemoryBlock(&originalState, sizeof(originalState)).toBase64Encoding());
  auto setup = juce::parseXML("<DEVICESETUP deviceType=\"Test audio\" audioOutputDeviceName=\"Test output\" audioInputDeviceName=\"Test output\" audioDeviceRate=\"48000\" audioDeviceBufferSize=\"64\" audioDeviceInChans=\"11\"/>");
  settings.setValue("audioSetup", setup.get());
  StandaloneAudioDeviceBridge bridge;
  bridge.getAudioDeviceManager().addAudioDeviceType(std::make_unique<TestDeviceType>(outputCount));
  TestProcessor processor;
  processor.checkStateRestore = [&] {
    expect(bridge.getAvailableOutputChannelCount() == outputCount,
           "state restoration sees device outputs before normalizing saved routes");
  };
  StandaloneAudioRuntime runtime(processor, bridge, &settings);
  expect(runtime.start(64).isEmpty(), "runtime starts with saved test device");
  expect(processor.state == originalState, "legacy filterState restored");
  auto *device = dynamic_cast<TestDevice*>(bridge.getAudioDeviceManager().getCurrentAudioDevice());
  expect(device != nullptr, "saved audio device selected");
  expect(device->getActiveInputChannels().isZero(), "legacy input channels are disabled");
  expect(device->getActiveOutputChannels().countNumberOfSetBits() == outputCount,
         "all available stage outputs opened");
  expect(processor.getTotalNumOutputChannels() == outputCount, "processor layout follows physical output count");
  juce::AudioBuffer<float> output(outputCount, 64);
  output.clear();
  device->render(output);
  expect(processor.blocks == 1, "exactly one processing callback");
  for (int ch = 0; ch < outputCount; ++ch)
    expect(std::abs(output.getSample(ch, 10) - static_cast<float>(ch + 1) / 64.0f) < 0.00001f,
           "each output reaches its corresponding device channel");
  const auto prepares = processor.prepares;
  expect(runtime.start(64).isEmpty() && processor.prepares == prepares, "repeated start is idempotent");
  processor.state = 67890;
  runtime.saveState();
  expect(settings.getXmlValue("audioSetup") != nullptr, "audioSetup persists");
  runtime.stop();
  runtime.stop();
  expect(bridge.getAudioDeviceManager().getCurrentAudioDevice() == nullptr, "stop closes audio device");
  expect(processor.releases > 0, "stop releases processor before destruction");
  processor.state = 0;
  expect(runtime.start(64).isEmpty(), "runtime restarts");
  expect(processor.state == 67890, "saved processor state round trips");
  runtime.stop();
}

void testRealProcessorRuntime(int outputCount) {
  expect(isolatedTestRoot().isDirectory(), "real processor storage is isolated");
  auto processor = std::make_unique<MTControlsAudioProcessor>();
  auto &bridge = processor->getAudioDeviceBridge();
  auto &manager = bridge.getAudioDeviceManager();
  manager.addAudioDeviceType(std::make_unique<TestDeviceType>(outputCount));

  // Restore a stereo route at the far end of the real device. Starting with
  // the bridge's initial two-channel width must not clamp this saved route.
  StemOutputRoutingState routes;
  routes.resetToDefaults(1);
  routes.setStemOutputChannel(0, outputCount - 1);
  routes.setStemOutputWidth(0, 2);
  auto state = processor->getValueTreeState().copyState();
  state.appendChild(routes.toValueTree(), nullptr);
  juce::MemoryBlock savedState;
  const auto stateXml = state.createXml();
  juce::AudioProcessor::copyXmlToBinary(*stateXml, savedState);
  juce::PropertySet settings;
  settings.setValue("filterState", savedState.toBase64Encoding());
  const auto setupXml = juce::parseXML(
      "<DEVICESETUP deviceType=\"Test audio\" audioOutputDeviceName=\"Test output\" "
      "audioInputDeviceName=\"Test output\" audioDeviceRate=\"48000\" "
      "audioDeviceBufferSize=\"64\" audioDeviceInChans=\"11\"/>");
  settings.setValue("audioSetup", setupXml.get());

  StandaloneAudioRuntime runtime(*processor, bridge, &settings);
  expect(runtime.start(64).isEmpty(), "real processor starts on saved device");
  std::cout << "Real processor: physical outputs=" << outputCount
            << ", processor outputs=" << processor->getTotalNumOutputChannels()
            << ", processor inputs=" << processor->getTotalNumInputChannels() << '\n';
  const auto checkChannels = [&](int expectedOutputs) {
    const auto *device = manager.getCurrentAudioDevice();
    expect(device != nullptr, "real processor has an open device");
    expect(device->getActiveInputChannels().isZero(), "real device has no microphone input");
    expect(device->getActiveOutputChannels().countNumberOfSetBits() == expectedOutputs,
           "device exposes the requested active outputs");
    expect(processor->getTotalNumOutputChannels() == expectedOutputs,
           "real processor layout follows device outputs");
    expect(processor->getTotalNumInputChannels() == 0,
           "real processor has no logical input channels");
    expect(bridge.getAvailableOutputChannelCount() == expectedOutputs,
           "real processor routing sees the active device width");
  };
  const auto checkSavedRoute = [&] {
    expect(processor->getStemOutputChannel(0) == outputCount - 1 &&
               processor->getStemOutputWidth(0) == 2,
           "real processor preserves the saved final stereo output pair");
  };
  checkChannels(outputCount);
  checkSavedRoute();
  expect(runtime.start(64).isEmpty(), "real processor repeated start succeeds");
  checkChannels(outputCount);
  runtime.saveState();
  runtime.stop();
  runtime.stop();
  expect(manager.getCurrentAudioDevice() == nullptr, "real processor stop closes device");
  expect(runtime.start(64).isEmpty(), "real processor restarts with persisted state");
  checkChannels(outputCount);
  checkSavedRoute();

  // AudioProcessorPlayer must renegotiate on a live output-selection change,
  // and prepareToPlay must normalize routing against the new width.
  juce::AudioDeviceManager::AudioDeviceSetup setup;
  manager.getAudioDeviceSetup(setup);
  setup.useDefaultOutputChannels = false;
  setup.outputChannels.clear();
  setup.outputChannels.setRange(0, 2, true);
  expect(manager.setAudioDeviceSetup(setup, true).isEmpty(), "switch to stereo outputs succeeds");
  checkChannels(2);
  expect(processor->getStemOutputChannel(0) == 1 && processor->getStemOutputWidth(0) == 2,
         "real processor clamps the saved stereo route to the smaller device width");
  setup.outputChannels.setRange(0, outputCount, true);
  expect(manager.setAudioDeviceSetup(setup, true).isEmpty(), "restore all outputs succeeds");
  checkChannels(outputCount);

  processor->setMetronomeEnabled(false);
  processor->setPadsEnabled(false);
  juce::AudioBuffer<float> output(outputCount, 64);
  for (int channel = 0; channel < outputCount; ++channel)
    for (int sample = 0; sample < output.getNumSamples(); ++sample)
      output.setSample(channel, sample, 1.0f);
  auto *device = dynamic_cast<TestDevice *>(manager.getCurrentAudioDevice());
  expect(device != nullptr, "real processor retains the simulated device");
  device->render(output);
  expect(output.getMagnitude(0, output.getNumSamples()) == 0.0f,
         "real runtime leaves no residual signal when no source is playing");
  runtime.stop();
}

void writeRoutingFixtureWav(const juce::File &file, int frames, float level) {
  expect(file.isAChildOf(isolatedTestRoot()), "routing WAV fixtures stay isolated");
  expect(file.getParentDirectory().createDirectory().wasOk(), "routing fixture directory opens");
  expect(file.deleteFile(), "routing fixture replaces any old WAV");
  auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "routing fixture WAV writer opens");
  juce::AudioBuffer<float> block(1, 4096);
  for (int sample = 0; sample < block.getNumSamples(); ++sample)
    block.setSample(0, sample, level);
  while (frames > 0) {
    const int count = juce::jmin(frames, block.getNumSamples());
    expect(writer->writeFromAudioSampleBuffer(block, 0, count), "routing fixture WAV writes");
    frames -= count;
  }
}

std::vector<CategoryRoutingDefaults> monoBusDefaults(int clickChannel, int guideChannel) {
  std::vector<CategoryRoutingDefaults> defaults(kTrackCategories.size(), {1, 2});
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    const juce::String category = kTrackCategories[i];
    if (category == "Click") defaults[i] = {clickChannel, 1};
    if (category == "Cues") defaults[i] = {guideChannel, 1};
  }
  return defaults;
}

// Exercise the real AudioProcessorPlayer path: a direct processBlock call would
// miss host-generated silence when a loader suspends the processor.
void testRealProcessorSongCrossfade() {
  const auto sourceA = isolatedTestRoot().getChildFile("crossfade-outgoing.wav");
  const auto sourceB = isolatedTestRoot().getChildFile("crossfade-incoming.wav");
  writeRoutingFixtureWav(sourceA, 48000 * 20, 0.20f);
  writeRoutingFixtureWav(sourceB, 48000 * 20, 0.35f);
  auto processor = std::make_unique<MTControlsAudioProcessor>();
  auto &p = *processor;
  auto &bridge = p.getAudioDeviceBridge();
  auto &manager = bridge.getAudioDeviceManager();
  manager.addAudioDeviceType(std::make_unique<TestDeviceType>(2));
  juce::PropertySet settings;
  const auto setup = juce::parseXML(
      "<DEVICESETUP deviceType=\"Test audio\" audioOutputDeviceName=\"Test output\" "
      "audioDeviceRate=\"48000\" audioDeviceBufferSize=\"64\"/>");
  settings.setValue("audioSetup", setup.get());
  StandaloneAudioRuntime runtime(p, bridge, &settings);
  expect(runtime.start(64).isEmpty(), "crossfade real runtime starts");
  auto *device = dynamic_cast<TestDevice *>(manager.getCurrentAudioDevice());
  expect(device != nullptr, "crossfade renders through the actual standalone audio callback");
  const auto configure = [&](int channel) {
    p.setPadsEnabled(false);
    p.setMetronomeEnabled(false);
    p.setMetronomeMuted(true);
    auto guide = p.getDynamicGuideSettings(); guide.enabled = false;
    p.setDynamicGuideSettings(guide); p.setDynamicGuideMuted(true);
    p.setMasterVolume(1.0f); p.setMasterGain(1.0f); p.setMasterMute(false);
    p.setStemVolume(0, 1.0f); p.setStemGain(0, 1.0f);
    p.setStemMute(0, false); p.setStemSolo(0, false);
    p.setStemOutputChannel(0, channel); p.setStemOutputWidth(0, 1);
    p.setStemClickGuideMask({false}); p.setStemGuideMask({false});
    p.setSongFadeOutSeconds(0.0);
  };
  expect(p.loadSongStems({sourceA.getFullPathName()}, "crossfade-a", "Outgoing", "C", 120.0),
         "crossfade loads its actual outgoing source");
  configure(1); p.setMasterVolume(0.7f); p.setMasterGain(0.8f); p.playSong();

  struct Observation { float left = 0, right = 0; int phase = 0; };
  constexpr size_t maxObservations = 12000;
  std::vector<Observation> observations(maxObservations);
  std::atomic<size_t> count{0};
  std::atomic<int> phase{0};
  std::atomic<bool> renderRunning{true};
  std::atomic<bool> renderFailed{false};
  std::thread render([&] {
    try {
      juce::AudioBuffer<float> output(2, 64);
      while (renderRunning.load(std::memory_order_acquire)) {
        output.clear();
        const int currentPhase = phase.load(std::memory_order_acquire);
        device->render(output);
        const size_t index = count.load(std::memory_order_relaxed);
        if (index < observations.size()) {
          observations[index] = {output.getMagnitude(0, 0, 64), output.getMagnitude(1, 0, 64), currentPhase};
          count.store(index + 1, std::memory_order_release);
        }
        juce::Thread::sleep(1);
      }
    } catch (...) { renderFailed.store(true, std::memory_order_release); }
  });
  struct JoinRender {
    std::atomic<bool> &running; std::thread &thread;
    ~JoinRender() { running.store(false, std::memory_order_release); if (thread.joinable()) thread.join(); }
  } join{renderRunning, render};
  const auto wait = [&](const std::function<bool()> &condition, const char *message) {
    for (int i = 0; i < 5000 && !condition() && !renderFailed.load(std::memory_order_acquire); ++i)
      juce::Thread::sleep(1);
    expect(!renderFailed.load(std::memory_order_acquire) && condition(), message);
  };
  const auto blocks = [&](size_t amount) {
    const auto before = count.load(std::memory_order_acquire);
    wait([&] { return count.load(std::memory_order_acquire) >= before + amount; }, "crossfade audio callback keeps advancing");
  };
  blocks(250);
  expect(p.requestSongCrossfade(1.0), "dry original-tempo outgoing song accepts crossfade");
  phase.store(1, std::memory_order_release);
  expect(p.loadSongStems({sourceB.getFullPathName()}, "crossfade-b", "Incoming", "C", 120.0, {}, true),
         "incoming deck prepares while outgoing real host callbacks continue");
  configure(2);
  phase.store(2, std::memory_order_release);
  p.finishPendingSongTransition();
  wait([&] { return !p.isSongCrossfadeActive(); }, "crossfade finishes within its bounded duration");
  phase.store(3, std::memory_order_release); blocks(100);
  const size_t firstCount = count.load(std::memory_order_acquire);
  int holdingBlocks = 0, holdingSilent = 0, overlapBlocks = 0, incomingOnlyBlocks = 0;
  float minHoldingLevel = 1.0f, maxHoldingLevel = 0.0f;
  for (size_t index = 0; index < firstCount; ++index) {
    const auto &sample = observations[index];
    if (sample.phase == 1) {
      ++holdingBlocks;
      minHoldingLevel = juce::jmin(minHoldingLevel, sample.left);
      maxHoldingLevel = juce::jmax(maxHoldingLevel, sample.left);
      if (sample.left < 0.01f) ++holdingSilent;
      expect(sample.right < 0.00001f, "preparing incoming song does not leak into the outgoing route");
    }
    if (sample.phase == 2 && sample.left > 0.01f && sample.right > 0.01f) ++overlapBlocks;
    if (sample.phase == 3 && sample.left < 0.00001f && sample.right > 0.1f) ++incomingOnlyBlocks;
  }
  std::cout << "Crossfade actual callbacks: hold=" << holdingBlocks << ", silent=" << holdingSilent
            << ", both=" << overlapBlocks << ", incoming=" << incomingOnlyBlocks
            << ", outgoing range=" << minHoldingLevel << ".." << maxHoldingLevel << '\n';
  expect(holdingBlocks > 0 && holdingSilent == 0,
         "outgoing source stays audible throughout load: no host-suspension or handoff gap");
  expect(minHoldingLevel > 0.10f && maxHoldingLevel < 0.125f,
         "outgoing deck preserves its own master gain without adopting or doubling the incoming master");
  expect(overlapBlocks > 50, "two real songs overlap on their independent mono routes during the fade");
  expect(incomingOnlyBlocks > 50, "finished crossfade leaves only the incoming source and its own route");

  // Repeated transitions must not leave a fading old deck after Pause/Stop or
  // after an explicit cancellation. Each new destination has a distinct route.
  for (const int action : {0, 1, 2}) {
    phase.store(0, std::memory_order_release);
    if (!p.isSongPlaying()) p.playSong();
    blocks(100);
    expect(p.requestSongCrossfade(1.0), "repeated dry crossfade can be armed");
    const bool toA = (action % 2) == 0;
    expect(p.loadSongStems({(toA ? sourceA : sourceB).getFullPathName()},
                          toA ? "crossfade-a" : "crossfade-b", "Cancellation destination", "C", 120.0, {}, true),
           "cancellation fixture prepares its next source");
    configure(toA ? 1 : 2); p.finishPendingSongTransition(); blocks(30);
    if (action == 0) p.pauseSong();
    else if (action == 1) p.stopSong();
    else p.cancelSongCrossfade();
    wait([&] { return !p.isSongCrossfadeActive(); }, "Pause, Stop and explicit cancel retire the outgoing deck");
    const auto checkStart = count.load(std::memory_order_acquire); blocks(40);
    const auto checkEnd = count.load(std::memory_order_acquire);
    for (size_t index = checkStart + 2; index < checkEnd; ++index) {
      const auto &sample = observations[index];
      if (action < 2)
        expect(sample.left < 0.00001f && sample.right < 0.00001f, "paused/stopped transition leaves no outgoing tail");
      else
        expect((toA ? sample.right : sample.left) < 0.00001f,
               "explicit cancel keeps only the current incoming route");
    }
  }
  renderRunning.store(false, std::memory_order_release); render.join();
  expect(!renderFailed.load(), "crossfade host callback completes without an exception");
  runtime.stop();
}

void writeCrossfadeTone(const juce::File &file, double frequency) {
  expect(file.isAChildOf(isolatedTestRoot()), "crossfade tone fixture is isolated");
  file.deleteFile();
  auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(24));
  expect(writer != nullptr, "crossfade tone fixture writer opens");
  juce::AudioBuffer<float> block(1, 4096);
  constexpr double pi = 3.14159265358979323846;
  for (int frame = 0; frame < 48000 * 40;) {
    const int count = juce::jmin(4096, 48000 * 40 - frame);
    for (int i = 0; i < count; ++i) {
      const double t = static_cast<double>(frame + i) / 48000.0;
      // The carrier detects pitch; its slow amplitude envelope independently
      // detects source-time scaling. A dry retained-deck fallback fails both.
      block.setSample(0, i, static_cast<float>((0.20 + 0.12 * std::cos(2.0 * pi * 6.0 * t))
                                              * std::sin(2.0 * pi * frequency * t)));
    }
    expect(writer->writeFromAudioSampleBuffer(block, 0, count), "crossfade tone fixture writes");
    frame += count;
  }
}

double crossingFrequency(const std::vector<float> &samples) {
  if (samples.size() < 100) return 0.0;
  int crossings = 0;
  for (size_t i = 1; i < samples.size(); ++i)
    if (samples[i - 1] <= 0.0f && samples[i] > 0.0f) ++crossings;
  return static_cast<double>(crossings) * 48000.0 / static_cast<double>(samples.size());
}

double envelopeFrequency(const std::vector<float> &samples) {
  constexpr int window = 480; // 10 ms RMS rejects carrier phase.
  const int count = static_cast<int>(samples.size()) / window;
  if (count < 60) return 0.0;
  std::vector<double> levels(static_cast<size_t>(count));
  double mean = 0;
  for (int block = 0; block < count; ++block) {
    double squared = 0;
    for (int i = 0; i < window; ++i) {
      const double sample = samples[static_cast<size_t>(block * window + i)];
      squared += sample * sample;
    }
    levels[static_cast<size_t>(block)] = std::sqrt(squared / window);
    mean += levels[static_cast<size_t>(block)];
  }
  mean /= count;
  double bestFrequency = 0, bestPower = 0;
  constexpr double pi = 3.14159265358979323846;
  for (int candidate = 30; candidate <= 110; ++candidate) {
    const double frequency = candidate / 10.0;
    double real = 0, imaginary = 0;
    for (int i = 0; i < count; ++i) {
      const double hann = 0.5 - 0.5 * std::cos(2.0 * pi * i / (count - 1));
      const double value = (levels[static_cast<size_t>(i)] - mean) * hann;
      const double angle = 2.0 * pi * frequency * i / 100.0;
      real += value * std::cos(angle); imaginary += value * std::sin(angle);
    }
    const double power = real * real + imaginary * imaginary;
    if (power > bestPower) { bestPower = power; bestFrequency = frequency; }
  }
  return bestFrequency;
}

void testProcessedSongCrossfade(int channels, double outgoingTempo) {
  const auto sourceA = isolatedTestRoot().getChildFile("crossfade-processed-a.wav");
  const auto sourceB = isolatedTestRoot().getChildFile("crossfade-processed-b.wav");
  writeCrossfadeTone(sourceA, 110.0); writeCrossfadeTone(sourceB, 330.0);
  auto processor = std::make_unique<MTControlsAudioProcessor>(); auto &p = *processor;
  auto &bridge = p.getAudioDeviceBridge(); auto &manager = bridge.getAudioDeviceManager();
  manager.addAudioDeviceType(std::make_unique<TestDeviceType>(channels));
  juce::PropertySet settings;
  const auto setup = juce::parseXML("<DEVICESETUP deviceType=\"Test audio\" audioOutputDeviceName=\"Test output\" audioDeviceRate=\"48000\" audioDeviceBufferSize=\"64\"/>");
  settings.setValue("audioSetup", setup.get());
  StandaloneAudioRuntime runtime(p, bridge, &settings);
  expect(runtime.start(64).isEmpty(), "processed crossfade real runtime starts");
  auto *device = dynamic_cast<TestDevice *>(manager.getCurrentAudioDevice());
  expect(device != nullptr, "processed crossfade uses the actual host callback");
  const auto configure = [&](int channel, int pitch, double tempo) {
    p.setPadsEnabled(false); p.setMetronomeEnabled(false); p.setMetronomeMuted(true);
    auto guide = p.getDynamicGuideSettings(); guide.enabled = false; p.setDynamicGuideSettings(guide); p.setDynamicGuideMuted(true);
    p.setMasterVolume(1); p.setMasterGain(1); p.setMasterMute(false);
    p.setStemVolume(0, 1); p.setStemGain(0, 1); p.setStemMute(0, false); p.setStemSolo(0, false);
    p.setStemOutputChannel(0, channel); p.setStemOutputWidth(0, 1);
    p.setStemClickGuideMask({false}); p.setStemGuideMask({false}); p.setStemPitchShiftMask({true});
    p.setPitchSemiTones(pitch); p.setPlaybackTempoBpm(tempo); p.setSongFadeOutSeconds(0);
  };
  expect(p.loadSongStems({sourceA.getFullPathName()}, "processed-a", "Processed outgoing", "C", 120), "processed outgoing loads");
  configure(1, 12, outgoingTempo); p.playSong();
  struct Block { std::array<float, 64> outgoing{}, incoming{}; std::array<float, 8> peaks{}; int phase = 0; };
  std::vector<Block> captured(15000);
  std::atomic<size_t> count{0}; std::atomic<int> phase{0}; std::atomic<bool> running{true}, failed{false};
  std::thread render([&] {
    try {
      juce::AudioBuffer<float> output(channels, 64);
      while (running.load(std::memory_order_acquire)) {
        const int currentPhase = phase.load(std::memory_order_acquire); output.clear(); device->render(output);
        const auto index = count.load(std::memory_order_relaxed);
        if (index < captured.size()) {
          auto &block = captured[index]; block.phase = currentPhase;
          std::copy_n(output.getReadPointer(0), 64, block.outgoing.begin());
          std::copy_n(output.getReadPointer(channels - 1), 64, block.incoming.begin());
          for (int channel = 0; channel < channels; ++channel) block.peaks[static_cast<size_t>(channel)] = output.getMagnitude(channel, 0, 64);
          count.store(index + 1, std::memory_order_release);
        }
        juce::Thread::sleep(1);
      }
    } catch (...) { failed.store(true, std::memory_order_release); }
  });
  struct Guard { std::atomic<bool>& running; std::thread& thread; ~Guard() { running.store(false); if(thread.joinable()) thread.join(); } } guard{running,render};
  const auto wait = [&](const std::function<bool()>& condition) {
    for(int i=0;i<10000 && !condition() && !failed.load();++i) juce::Thread::sleep(1);
    expect(!failed.load() && condition(), "processed crossfade callback advances within deadline");
  };
  const auto blocks = [&](size_t n) { const auto before=count.load(); wait([&]{return count.load()>=before+n;}); };
  blocks(1000);
  expect(p.requestSongCrossfade(1.0), "pitched and stretched outgoing playback accepts crossfade");
  phase.store(1);
  bool held = false;
  expect(p.loadSongStems({sourceB.getFullPathName()}, "processed-b", "Processed incoming", "G", 120,
      [&](double progress) {
        if (!held && progress >= 0.90) {
          held = true; phase.store(10); blocks(900); phase.store(1);
        }
      }, true), "processed incoming prepares while outgoing DSP remains live");
  configure(channels, -5, 132);
  phase.store(2); p.finishPendingSongTransition();
  wait([&]{return !p.isSongCrossfadeActive();});
  phase.store(3); blocks(500);
  const auto published=count.load(std::memory_order_acquire);
  std::vector<float> heldAudio, overlapAudio, incomingAudio;
  int heldSilent=0, heldCount=0, overlap=0, finalLeak=0, overlapSilent=0;
  for(size_t i=0;i<published;++i) {
    const auto &block=captured[i];
    if(block.phase==10) {
      ++heldCount; if(block.peaks[0]<0.002f) ++heldSilent;
      heldAudio.insert(heldAudio.end(),block.outgoing.begin(),block.outgoing.end());
      expect(block.peaks[static_cast<size_t>(channels-1)]<0.00001f,"incoming processed route stays silent until configured");
    }
    if(block.phase==2 && block.peaks[0]<0.002f && block.peaks[static_cast<size_t>(channels-1)]<0.002f) ++overlapSilent;
    if(block.phase==2 && block.peaks[0]>0.002f && block.peaks[static_cast<size_t>(channels-1)]>0.002f) {
      ++overlap;
      // Analyze the first half, while the fade has useful signal-to-noise.
      if(overlap<350) overlapAudio.insert(overlapAudio.end(),block.outgoing.begin(),block.outgoing.end());
    }
    if(block.phase==3) {
      if(block.peaks[0]>0.00001f) ++finalLeak;
      incomingAudio.insert(incomingAudio.end(),block.incoming.begin(),block.incoming.end());
    }
    for(int channel=1;channel<channels-1;++channel)
      expect(block.peaks[static_cast<size_t>(channel)]<0.00001f,"processed crossfade never leaks into unused physical outputs");
  }
  const double heldPitch=crossingFrequency(heldAudio), overlapPitch=crossingFrequency(overlapAudio);
  const double heldTempo=envelopeFrequency(heldAudio), incomingPitch=crossingFrequency(incomingAudio);
  std::cout<<"Processed crossfade "<<channels<<" outputs tempo="<<outgoingTempo<<": held="<<heldCount
      <<", silent="<<heldSilent<<", overlap="<<overlap<<", held Hz="<<heldPitch
      <<", overlap Hz="<<overlapPitch<<", envelope Hz="<<heldTempo<<", incoming Hz="<<incomingPitch<<'\n';
  expect(heldCount>=850 && heldSilent==0,"processed outgoing remains audible through a prolonged loader hold");
  expect(std::abs(heldPitch-220.0)<10.0 && std::abs(overlapPitch-220.0)<12.0,"outgoing octave shift and DSP history survive capture and overlap instead of dry fallback");
  expect(std::abs(heldTempo-6.0*outgoingTempo/120.0)<0.65,"outgoing faster/slower tempo survives ownership transfer");
  expect(overlap>150 && finalLeak==0,"processed songs overlap then retire the outgoing route completely");
  expect(overlapSilent==0,"cold incoming pitch/tempo latency cannot produce a silent block while retiring the outgoing deck");
  expect(std::abs(incomingPitch-330.0*std::pow(2.0,-5.0/12.0))<12.0,"incoming song keeps its independent pitch while old DSP fades");
  running.store(false); render.join(); runtime.stop();
}

void testGeneratedBusSongCrossfade() {
  juce::String error;
  expect(AssetBootstrapManager::ensureGuidesPackReady(&error), "crossfade guide pack is available locally");
  for(const auto *language:{"en","es"}) {
    const auto directory=AssetBootstrapManager::getGuidesLanguageSectionsDirectory(language);
    for(const auto &old:directory.findChildFiles(juce::File::findFiles,false,"1.*")) old.deleteFile();
    // Keep an actual spoken-cue voice alive across capture, then schedule new
    // cues beyond capture. A restarted/omitted guide cannot satisfy both.
    writeRoutingFixtureWav(directory.getChildFile("1.wav"),48000,0.22f);
  }
  const auto source=isolatedTestRoot().getChildFile("crossfade-virtual-silence.wav");
  writeRoutingFixtureWav(source,48000*30,0);
  auto processor=std::make_unique<MTControlsAudioProcessor>(); auto &p=*processor;
  auto &bridge=p.getAudioDeviceBridge(); auto &manager=bridge.getAudioDeviceManager();
  manager.addAudioDeviceType(std::make_unique<TestDeviceType>(8));
  juce::PropertySet settings;
  const auto setup=juce::parseXML("<DEVICESETUP deviceType=\"Test audio\" audioOutputDeviceName=\"Test output\" audioDeviceRate=\"48000\" audioDeviceBufferSize=\"64\"/>");
  settings.setValue("audioSetup",setup.get()); StandaloneAudioRuntime runtime(p,bridge,&settings);
  expect(runtime.start(64).isEmpty(),"generated crossfade runtime starts");
  auto *device=dynamic_cast<TestDevice*>(manager.getCurrentAudioDevice()); expect(device,"generated crossfade has actual host callback");
  const auto configure=[&](bool incoming) {
    p.setPadsEnabled(false); p.setMasterVolume(1);p.setMasterGain(1);p.setMasterMute(false);
    p.setStemVolume(0,1);p.setStemGain(0,1);p.setStemMute(0,false);p.setStemSolo(0,false);
    p.setStemOutputChannel(0,incoming?2:1);p.setStemOutputWidth(0,1);
    p.setStemClickGuideMask({false});p.setStemGuideMask({false});p.setStemPitchShiftMask({true});p.setSongFadeOutSeconds(0);
    p.setPitchSemiTones(3);p.setPlaybackTempoBpm(incoming?180:120);
    p.clearMetronomeClickSamples();p.setMetronomeClickStemIndex(-1);
    p.setMetronomeEnabled(true);p.setMetronomeMuted(false);p.setMetronomeSolo(false);
    p.setMetronomeVolume(1);p.setMetronomeGain(1);p.setClickDivision(1);p.setClickDivisionEnabled(true);
    p.setMetronomeOutputRouting(incoming?5:3,1);
    auto guide=p.getDynamicGuideSettings();guide.enabled=true;p.setDynamicGuideSettings(guide);
    p.setDynamicGuideMuted(false);p.setDynamicGuideSolo(false);p.setDynamicGuideVolume(1);p.setDynamicGuideGain(1);
    p.setDynamicGuideOutputRouting(incoming?6:4,1);
    SongArrangementSnapshot arrangement;
    SongArrangementPartSnapshot part;part.id="crossfade-section";part.name="Verse";part.endSec=30;arrangement.parts.push_back(part);
    const std::vector<double> positions = incoming ? std::vector<double>{0.1}
        : std::vector<double>{0.3,0.9,1.5,2.1,2.7,3.3,3.9};
    for(const double position:positions) {
      SongArrangementCueSnapshot cue;cue.id="count-"+juce::String(position);cue.name="1";cue.partId=part.id;cue.positionSec=cue.offsetSec=position;
      arrangement.cues.push_back(cue);
    }
    p.setSongArrangement(arrangement,false);
    expect(p.hasDynamicGuideAssets(),"crossfade generated guide uses decoded real WAV assets");
  };
  expect(p.loadSongStems({source.getFullPathName()},"virtual-a","Virtual outgoing","C",120),"virtual outgoing loads");configure(false);p.playSong();
  struct Block {std::array<float,8> peaks{};int phase=0;};std::vector<Block> capture(12000);
  std::atomic<size_t> count{0};std::atomic<int> phase{0};std::atomic<bool> running{true},failed{false};
  std::thread render([&]{try{juce::AudioBuffer<float> output(8,64);while(running.load()) {
    const int currentPhase=phase.load();output.clear();device->render(output);const auto index=count.load();
    if(index<capture.size()){auto &block=capture[index];block.phase=currentPhase;for(int c=0;c<8;++c)block.peaks[(size_t)c]=output.getMagnitude(c,0,64);count.store(index+1,std::memory_order_release);}juce::Thread::sleep(1);
  }}catch(...){failed.store(true);}});
  struct Guard {std::atomic<bool>& running;std::thread& thread;~Guard(){running.store(false);if(thread.joinable())thread.join();}}guard{running,render};
  const auto wait=[&](const std::function<bool()>& condition){for(int i=0;i<10000&&!condition()&&!failed.load();++i)juce::Thread::sleep(1);expect(!failed.load()&&condition(),"generated crossfade callback advances");};
  const auto blocks=[&](size_t n){const auto before=count.load();wait([&]{return count.load()>=before+n;});};
  blocks(600);
  expect(p.requestSongCrossfade(1.5),"audible dynamic click and guide accept crossfade");
  bool held=false;phase.store(1);
  expect(p.loadSongStems({source.getFullPathName()},"virtual-b","Virtual incoming","G",120,[&](double progress){if(!held&&progress>=0.90){held=true;phase.store(10);blocks(900);phase.store(1);}},true),"generated outgoing buses continue through incoming preparation");
  configure(true);phase.store(2);p.finishPendingSongTransition();wait([&]{return !p.isSongCrossfadeActive();});phase.store(3);blocks(400);
  const auto published=count.load(std::memory_order_acquire);int holdGuide=0,holdCount=0,overlapGuide=0,newGuide=0,newClick=0,oldClick=0;
  bool oldClickOn=false,newClickOn=false;
  std::vector<float> incomingGuideLevels, incomingClickPeaks;
  float incomingClickPeak=0;
  std::vector<size_t> oldClickStarts,newClickStarts;
  bool oldTimelineClickOn=false,newTimelineClickOn=false;
  for(size_t i=0;i<published;++i){const auto &b=capture[i];
    if(b.phase==10||b.phase==2) {
      const bool oldOn=b.peaks[2]>0.002f,newOn=b.peaks[4]>0.002f;
      if(oldOn&&!oldTimelineClickOn)oldClickStarts.push_back(i);
      if(newOn&&!newTimelineClickOn)newClickStarts.push_back(i);
      oldTimelineClickOn=oldOn;newTimelineClickOn=newOn;
    }
    if(b.phase==10){++holdCount;if(b.peaks[3]>0.01f)++holdGuide;expect(b.peaks[4]<0.00001f&&b.peaks[5]<0.00001f,"incoming virtual buses do not sound during loader hold");}
    if(b.phase==2){if(b.peaks[3]>0.001f)++overlapGuide;if(b.peaks[5]>0.001f)++newGuide;
      if(b.peaks[5]>0.002f) incomingGuideLevels.push_back(b.peaks[5]);
      const bool oldOn=b.peaks[2]>0.002f,newOn=b.peaks[4]>0.002f;
      if(oldOn&&!oldClickOn)++oldClick;
      if(newOn&&!newClickOn)++newClick;
      if(newOn)incomingClickPeak=juce::jmax(incomingClickPeak,b.peaks[4]);
      else if(newClickOn){incomingClickPeaks.push_back(incomingClickPeak);incomingClickPeak=0;}
      oldClickOn=oldOn;newClickOn=newOn;}
    if(b.phase==3)expect(b.peaks[2]<0.00001f&&b.peaks[3]<0.00001f,"old virtual click and guide fully retire after crossfade");
    expect(b.peaks[6]<0.00001f&&b.peaks[7]<0.00001f,"virtual mono buses never leak into unassigned outputs");
  }
  std::cout<<"Generated crossfade: hold guide="<<holdGuide<<'/'<<holdCount<<", overlap old/new guide="<<overlapGuide<<'/'<<newGuide<<", old/new clicks="<<oldClick<<'/'<<newClick<<'\n';
  expect(holdCount>=850&&holdGuide>750,"in-flight and future outgoing guide phrases remain audible through prolonged load");
  expect(overlapGuide>200&&newGuide>200,"both independently scheduled guide buses render during overlap");
  expect(oldClick>=2&&newClick>=3,"old and new metronomes retain independent tempos during overlap");
  const auto medianPeriod=[](const std::vector<size_t>& starts) {
    std::vector<size_t> periods;
    // The first observed pulse can already be in flight at the observation
    // boundary, so only compare complete subsequent pulse intervals.
    for(size_t i=2;i<starts.size();++i)periods.push_back(starts[i]-starts[i-1]);
    if(periods.empty())return 0.0;
    std::sort(periods.begin(),periods.end());return static_cast<double>(periods[periods.size()/2]);
  };
  expect(std::abs(medianPeriod(oldClickStarts)-375.0)<6.0
      &&std::abs(medianPeriod(newClickStarts)-250.0)<6.0,
      "outgoing 120 BPM and incoming 180 BPM clocks keep their own sample-accurate pulse periods");
  if(incomingClickPeak>0)incomingClickPeaks.push_back(incomingClickPeak);
  expect(incomingGuideLevels.size()>200&&incomingClickPeaks.size()>=3,
      "incoming virtual buses provide enough samples to verify their fade envelope");
  const auto averageEdge=[&](bool last){double sum=0;for(size_t i=0;i<30;++i)
      sum+=incomingGuideLevels[last?incomingGuideLevels.size()-1-i:i];return sum/30;};
  expect(averageEdge(true)>averageEdge(false)*1.6,
      "incoming generated guide follows the crossfade envelope instead of entering at full volume");
  expect(incomingClickPeaks.back()>incomingClickPeaks.front()*1.4f,
      "incoming generated click follows the crossfade envelope instead of entering at full volume");
  running.store(false);render.join();runtime.stop();
}

void testAdvancedSongCrossfade() {
  testProcessedSongCrossfade(2,180.0);
  testProcessedSongCrossfade(8,90.0);
  testGeneratedBusSongCrossfade();
}

void testRealProcessorMonoClickAndGuide() {
  // Bootstrap only the bundled local pack, then replace one actual guide WAV
  // in isolated storage. The processor's public settings API decodes it; no
  // guide-engine scratch buffer or synthetic processor stands in for output.
  juce::String guideError;
  expect(AssetBootstrapManager::ensureGuidesPackReady(&guideError),
         "bundled guide pack is available without network access");
  for (const auto *language : {"en", "es"}) {
    const auto directory = AssetBootstrapManager::getGuidesLanguageSectionsDirectory(language);
    // Avoid an equal-priority bundled "1.mp3" winning resolution over WAV.
    for (const auto &old : directory.findChildFiles(juce::File::findFiles, false, "1.*"))
      expect(old.deleteFile(), "isolated count sample is replaced by the mono WAV");
    writeRoutingFixtureWav(directory.getChildFile("1.wav"), 4800, 0.25f);
  }
  const auto source = isolatedTestRoot().getChildFile("mono-bus-silent-source.wav");
  writeRoutingFixtureWav(source, 48000 * 4, 0.0f);

  auto processor = std::make_unique<MTControlsAudioProcessor>();
  auto &p = *processor;
  auto &bridge = p.getAudioDeviceBridge();
  auto &manager = bridge.getAudioDeviceManager();
  manager.addAudioDeviceType(std::make_unique<TestDeviceType>(2));
  juce::PropertySet settings;
  const auto setup = juce::parseXML(
      "<DEVICESETUP deviceType=\"Test audio\" audioOutputDeviceName=\"Test output\" "
      "audioDeviceRate=\"48000\" audioDeviceBufferSize=\"64\"/>");
  settings.setValue("audioSetup", setup.get());
  StandaloneAudioRuntime runtime(p, bridge, &settings);
  expect(runtime.start(64).isEmpty(), "real mono-bus runtime starts on two physical outputs");
  p.setPadsEnabled(false);
  p.setMasterVolume(1.0f);
  p.setMasterGain(1.0f);
  p.setMasterMute(false);
  p.clearMetronomeClickSamples(); // Exercise the actual dynamic synth click.
  p.setMetronomeVolume(1.0f);
  p.setMetronomeGain(1.0f);
  p.setMetronomeMuted(false);
  p.setMetronomeSolo(false);
  p.setClickDivision(1);
  p.setClickDivisionEnabled(true);
  p.setDynamicGuideVolume(1.0f);
  p.setDynamicGuideGain(1.0f);
  p.setDynamicGuideMuted(false);
  p.setDynamicGuideSolo(false);

  ImportedSongData song;
  song.id = "mono-bus-routing";
  song.title = "Mono bus routing";
  ImportedStemData stem;
  stem.category = "Other";
  stem.displayName = "Silent instrument";
  stem.storedPath = source.getFullPathName();
  song.stems.push_back(stem);
  SongArrangementPartSnapshot verse;
  verse.id = "verse"; verse.name = "Verse"; verse.endSec = 4.0;
  SongArrangementCueSnapshot cue;
  cue.id = "one"; cue.name = "1"; cue.partId = verse.id;
  cue.positionSec = cue.offsetSec = 0.25;
  SongArrangementSnapshot arrangement;
  arrangement.parts.push_back(verse);
  arrangement.cues.push_back(cue);

  // No matching click/guide stem: current device defaults must replace old
  // stereo song snapshots on both virtual buses independently.
  p.setMetronomeOutputRouting(1, 2);
  p.setDynamicGuideOutputRouting(1, 2);
  p.applySongOutputRoutingDefaults({}, monoBusDefaults(1, 2));
  expect(p.getMetronomeOutputChannel() == 1 && p.getMetronomeOutputWidth() == 1 &&
             p.getDynamicGuideOutputChannel() == 2 && p.getDynamicGuideOutputWidth() == 1,
         "device mono defaults override stereo snapshots without corresponding stems");

  for (bool alteredTime : {false, true}) {
    for (bool guide : {false, true}) {
      for (int destination : {1, 2}) {
        p.pauseSong();
        expect(p.loadSongStems({source.getFullPathName()}, song.id, song.title, "C", 120.0),
               "mono-bus fixture loads a real silent stem");
        p.setSongFadeOutSeconds(0.0);
        p.setSongArrangement(arrangement, false);
        p.setStemClickGuideMask({false});
        p.setStemGuideMask({false});
        p.setMetronomeClickStemIndex(-1);
        auto guideSettings = p.getDynamicGuideSettings();
        guideSettings.enabled = guide;
        p.setDynamicGuideSettings(guideSettings);
        if (guide) expect(p.hasDynamicGuideAssets(), "real guide WAV assets are decoded");
        p.setMetronomeEnabled(!guide);
        p.setMetronomeOutputRouting(1, 2);
        p.setDynamicGuideOutputRouting(1, 2);
        p.applySongOutputRoutingDefaults(song, monoBusDefaults(destination, destination));
        p.setPitchSemiTones(alteredTime ? 3 : 0);
        p.setPlaybackTempoBpm(alteredTime ? 108.0 : 120.0);
        p.playSong();

        auto *device = dynamic_cast<TestDevice *>(manager.getCurrentAudioDevice());
        expect(device != nullptr, "mono buses use the actual runtime device callback");
        std::array<float, 2> peaks{};
        juce::AudioBuffer<float> output(2, 64);
        for (int callback = 0; callback < 750; ++callback) {
          // A stale/unwritten channel must fail too, not look like silence.
          for (int ch = 0; ch < 2; ++ch)
            for (int sample = 0; sample < output.getNumSamples(); ++sample)
              output.setSample(ch, sample, 1.0f);
          device->render(output);
          for (int ch = 0; ch < 2; ++ch)
            peaks[static_cast<size_t>(ch)] = juce::jmax(
                peaks[static_cast<size_t>(ch)], output.getMagnitude(ch, 0, output.getNumSamples()));
          if ((callback % 32) == 0) juce::Thread::sleep(1);
        }
        std::cout << (guide ? "Guide WAV" : "Dynamic click") << " mono Out " << destination
                  << (alteredTime ? " with tempo/pitch" : " at original tempo/pitch")
                  << ": L=" << peaks[0] << ", R=" << peaks[1] << '\n';
        expect(peaks[static_cast<size_t>(destination - 1)] > 0.01f,
               "real mono bus reaches the selected physical channel");
        expect(peaks[static_cast<size_t>(2 - destination)] == 0.0f,
               "real mono bus never leaks to the opposite physical channel");
      }
    }
  }

  // Legacy/imported sources may be classified Other while their names still
  // identify click/guide. The same device policy must reach their inherited
  // virtual buses, and the resulting stem routes must survive runtime restore.
  p.pauseSong();
  expect(p.loadSongStems({source.getFullPathName(), source.getFullPathName()},
                        song.id, song.title, "C", 120.0), "linked click/guide stems load");
  song.stems[0].displayName = "Click";
  song.stems.push_back(song.stems[0]);
  song.stems[1].displayName = "Guide";
  p.setStemClickGuideMask({true, true});
  p.setStemGuideMask({false, true});
  p.setMetronomeClickStemIndex(0);
  p.setStemOutputWidth(0, 2);
  p.setStemOutputWidth(1, 2);
  p.applySongOutputRoutingDefaults(song, monoBusDefaults(1, 2));
  const auto checkInheritedRoutes = [&] {
    expect(p.getStemOutputChannel(0) == 1 && p.getStemOutputWidth(0) == 1 &&
               p.getStemOutputChannel(1) == 2 && p.getStemOutputWidth(1) == 1,
           "Other stems named Click/Guide use their mono category routes");
    expect(p.getMetronomeOutputChannel() == 1 && p.getMetronomeOutputWidth() == 1 &&
               p.getDynamicGuideOutputChannel() == 2 && p.getDynamicGuideOutputWidth() == 1,
           "dynamic buses inherit their linked stem's mono route");
  };
  checkInheritedRoutes();
  runtime.saveState();
  runtime.stop();
  p.setStemOutputWidth(0, 2);
  p.setStemOutputWidth(1, 2);
  expect(runtime.start(64).isEmpty(), "mono routes restart through persisted standalone state");
  checkInheritedRoutes();

  // A saved stereo default must remain playable after narrowing the same
  // device to one active channel. The UI cannot offer a stereo destination.
  juce::AudioDeviceManager::AudioDeviceSetup monoSetup;
  manager.getAudioDeviceSetup(monoSetup);
  monoSetup.useDefaultOutputChannels = false;
  monoSetup.outputChannels.clear();
  monoSetup.outputChannels.setBit(0);
  expect(manager.setAudioDeviceSetup(monoSetup, true).isEmpty(),
         "real mono-bus runtime accepts one active output");
  expect(p.getTotalNumOutputChannels() == 1 && bridge.getAvailableOutputChannelCount() == 1,
         "one-channel output selection reaches the real processor");
  p.applySongOutputRoutingDefaults(song,
      std::vector<CategoryRoutingDefaults>(kTrackCategories.size(), {1, 2}));
  expect(p.getStemOutputWidth(0) == 1 && p.getStemOutputWidth(1) == 1 &&
             p.getMetronomeOutputWidth() == 1 && p.getDynamicGuideOutputWidth() == 1,
         "stereo defaults fold to mono when only one callback channel is active");
  runtime.stop();
}

juce::Slider *findClickGainKnob(juce::Component &component) {
  if (component.getComponentID() == "clickDynGain")
    return dynamic_cast<juce::Slider *>(&component);
  for (auto *child : component.getChildren())
    if (auto *knob = findClickGainKnob(*child)) return knob;
  return nullptr;
}

void endClickGainGesture(juce::Slider &knob) {
  const auto position = knob.getLocalBounds().toFloat().getCentre();
  const auto now = juce::Time::getCurrentTime();
  knob.mouseUp({juce::Desktop::getInstance().getMainMouseSource(), position,
      juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &knob, &knob,
      now, position, now, 1, true});
}

void testClickDynGainKnobPortableRoundTrip() {
  ProjectDocument document;
  document.song.id = "click-gain-ui-project";
  document.song.projectId = document.song.id;
  document.song.title = "Click gain UI project";
  document.song.blankSong = true;
  document.song.bpm = 120.0;
  document.song.timeSignature = "4/4";
  // An old project may use both independent controls. Merely displaying the
  // combined knob must neither normalise that saved pair nor alter its sound.
  document.settings.clickDynMix.volume = 0.4f;
  document.settings.clickDynMix.gain = 2.5f;
  const auto archive = isolatedTestRoot().getChildFile("click-gain-ui.pwproj");
  juce::String error;
  expect(ProjectStore::save(document, archive, &error), "click gain fixture saves");
  ProjectDocument reopened;
  expect(ProjectStore::open(archive, reopened, &error) &&
             SongLibraryStore::registerProjectSong(reopened.song),
         "click gain fixture opens through the portable library");
  const auto setlist = SongLibraryStore::createSetlist("Click gain UI setlist");
  expect(SongLibraryStore::addSongToSetlist(setlist, reopened.song.id) &&
             SongLibraryStore::setActiveSetlistId(setlist),
         "click gain fixture is the active setlist item");

  auto processor = std::make_unique<MTControlsAudioProcessor>();
  processor->setMetronomeVolume(reopened.settings.clickDynMix.volume);
  processor->setMetronomeGain(reopened.settings.clickDynMix.gain);
  ChannelStripLookAndFeel lookAndFeel;
  ChannelGridPanel grid(lookAndFeel, *processor);
  grid.setSize(1200, 700);
  grid.setSong(reopened.song);
  auto *knob = findClickGainKnob(grid);
  expect(knob != nullptr, "the real mixer exposes its existing click knob");
  expect(juce::approximatelyEqual(knob->getValue(), 1.0) &&
             juce::approximatelyEqual(processor->getMetronomeVolume(), 0.4f) &&
             juce::approximatelyEqual(processor->getMetronomeGain(), 2.5f),
         "opening an old mix displays its effective level without changing its stored controls");

  // Exercise the assembled UI callback and actual drag-end persistence path,
  // not a duplicate of the mapping formula or a direct processor setter.
  knob->setValue(2.5, juce::sendNotificationSync);
  expect(juce::approximatelyEqual(processor->getMetronomeVolume(), 1.0f) &&
             juce::approximatelyEqual(processor->getMetronomeGain(), 2.5f),
         "the same knob can boost the real click bus above its previous maximum");
  endClickGainGesture(*knob);
  grid.flushPendingPersistenceNow();
  expect(ProjectStore::savePendingPortableChanges(archive, &error) &&
             ProjectStore::open(archive, reopened, &error),
         "boosted click state consolidates and reopens as a portable project");
  expect(juce::approximatelyEqual(reopened.settings.clickDynMix.volume, 1.0f) &&
             juce::approximatelyEqual(reopened.settings.clickDynMix.gain, 2.5f),
         "drag end stores gain separately, without clipping or applying boost twice on reload");

  knob->setValue(0.6, juce::sendNotificationSync);
  endClickGainGesture(*knob);
  grid.flushPendingPersistenceNow();
  expect(ProjectStore::savePendingPortableChanges(archive, &error) &&
             ProjectStore::open(archive, reopened, &error) &&
             juce::approximatelyEqual(reopened.settings.clickDynMix.volume, 0.6f) &&
             juce::approximatelyEqual(reopened.settings.clickDynMix.gain, 1.0f),
         "lowering the same knob removes prior boost and persists normal attenuation");
  processor->setMetronomeVolume(reopened.settings.clickDynMix.volume);
  processor->setMetronomeGain(reopened.settings.clickDynMix.gain);
  grid.setSong(reopened.song);
  knob = findClickGainKnob(grid);
  expect(knob != nullptr && std::abs(knob->getValue() - 0.6) < 1.0e-6,
         "rebuilt mixer reads the portable click level back into the same knob");
}

juce::Component *findMixerControl(juce::Component &component,
                                  const juce::String &identifier) {
  if (component.getComponentID() == identifier) return &component;
  for (auto *child : component.getChildren())
    if (auto *match = findMixerControl(*child, identifier)) return match;
  return nullptr;
}

ChannelStripCard *findMixerStrip(juce::Component &component,
                                const juce::String &name) {
  if (auto *strip = dynamic_cast<ChannelStripCard *>(&component))
    if (strip->getChannelName() == name) return strip;
  for (auto *child : component.getChildren())
    if (auto *match = findMixerStrip(*child, name)) return match;
  return nullptr;
}

void collectMixerStrips(juce::Component &component,
                         std::vector<ChannelStripCard *> &strips) {
  if (auto *strip = dynamic_cast<ChannelStripCard *>(&component))
    strips.push_back(strip);
  for (auto *child : component.getChildren())
    collectMixerStrips(*child, strips);
}

void snapshotMixerLayout(ChannelGridPanel &grid, const juce::String &name) {
  const auto output = juce::SystemStats::getEnvironmentVariable(
      "PLAYWORSHIP_LAYOUT_SNAPSHOTS", {});
  if (output.isEmpty()) return;
  const juce::File folder(output);
  expect(folder.createDirectory().wasOk(), "mixer preview directory opens");
  auto stream = folder.getChildFile(name + ".png").createOutputStream();
  expect(stream != nullptr, "mixer preview stream opens");
  stream->setPosition(0);
  stream->truncate();
  expect(juce::PNGImageFormat{}.writeImageToStream(
             grid.createComponentSnapshot(grid.getLocalBounds()), *stream),
         "real mixer component snapshot writes");
}

void testDesktopMixerDensityLayouts() {
#if !JUCE_IOS && !JUCE_ANDROID
  const auto source = isolatedTestRoot().getChildFile("mixer-density-source.wav");
  writeRoutingFixtureWav(source, 2048, 0.1f);
  ImportedSongData song;
  song.id = "mixer-density-layout";
  song.title = "Mixer density layout";
  song.bpm = 120.0;
  song.timeSignature = "4/4";
  juce::StringArray paths, ids;
  for (int i = 0; i < 12; ++i) {
    ImportedStemData stem;
    stem.stemId = "density-stem-" + juce::String(i);
    stem.displayName = "Instrument " + juce::String(i + 1);
    stem.category = "Keys";
    stem.fileName = source.getFileName();
    stem.sourcePath = stem.storedPath = source.getFullPathName();
    stem.sampleRate = 48000.0;
    stem.channels = 1;
    stem.durationSec = 2048.0 / 48000.0;
    song.stems.push_back(stem);
    paths.add(stem.storedPath);
    ids.add(stem.stemId);
  }
  auto processor = std::make_unique<MTControlsAudioProcessor>();
  expect(processor->loadSongStems(paths, song.id, song.title, "C", song.bpm),
         "real processor loads twelve stems for desktop layout");
  processor->setSectionAutomationStemIds(ids);
  for (int i = 0; i < 12; ++i)
    processor->setStemVolume(i, 0.1f + static_cast<float>(i) * 0.05f);
  processor->setMasterVolume(0.73f);
  ChannelStripLookAndFeel lookAndFeel;
  ChannelGridPanel grid(lookAndFeel, *processor);
  grid.setSize(1100, 640);
  grid.setSong(song);
  std::vector<ChannelStripCard *> strips;
  collectMixerStrips(grid, strips);
  expect(strips.size() == 15,
         "layout fixture includes every stem, Master, Click Dyn and Guide Dyn");
  auto *master = findMixerStrip(grid, "MASTER");
  expect(master != nullptr, "density fixture exposes the real Master strip");
  auto *link = dynamic_cast<juce::Button *>(
      findMixerControl(*master, "linkInstrumentFaders"));
  expect(link != nullptr && link->isEnabled() && static_cast<bool>(link->onClick),
         "desktop layout fixture exposes the real link action");
  link->setToggleState(true, juce::dontSendNotification);
  link->onClick();
  expect(link->getToggleState(), "density test begins with linked faders active");
  juce::Viewport *viewport = nullptr;
  for (auto *child : grid.getChildren())
    if (auto *candidate = dynamic_cast<juce::Viewport *>(child)) viewport = candidate;
  expect(viewport != nullptr && viewport->getViewedComponent() != nullptr,
         "real mixer retains its scrolling viewport");

  const auto expectMixUnchanged = [&] {
    expect(link->getToggleState() &&
               findMixerStrip(grid, "MASTER") == master &&
               std::abs(processor->getMasterVolume() - 0.73f) < 1.0e-6f,
           "resizing retains the same mixer, linking and Master volume");
    for (int i = 0; i < 12; ++i)
      expect(std::abs(processor->getStemVolume(i) -
                      (0.1f + static_cast<float>(i) * 0.05f)) < 1.0e-6f,
             "resizing does not replace or scale musical volume levels");
  };
  const auto expectControlsInside = [&] {
    auto *content = viewport->getViewedComponent();
    for (auto *strip : strips) {
      auto *slot = strip->getParentComponent();
      expect(content->getLocalBounds().contains(slot->getBounds()),
             "every channel belongs entirely to the scrollable content");
      expect(slot->getLocalBounds().contains(strip->getBounds()),
             "each fader strip remains inside its cell");
      for (const auto *id : {"muteButton", "soloButton"}) {
        auto *button = findMixerControl(*strip, id);
        if (button != nullptr && button->isVisible())
          expect(strip->getLocalBounds().contains(button->getBounds()),
                 "Solo and Mute remain completely inside their strip");
      }
    }
    expectMixUnchanged();
  };

  grid.setResponsiveProfile(ResponsiveLayoutProfile::Standard);
  const juce::Point<int> normalSize(master->getWidth(), master->getHeight());
  snapshotMixerLayout(grid, "mixer-normal-1100x640");
  grid.setResponsiveProfile(ResponsiveLayoutProfile::Compact);
  expect(master->getWidth() < normalSize.x && master->getHeight() < normalSize.y,
         "Compact decreases both dimensions when height permits the normal profile");
  snapshotMixerLayout(grid, "mixer-compact-1100x640");
  expectControlsInside();

  // At these sizes two rows can fit at a readable scale. A width-only scale
  // used to leave the bottom row partially outside the visible viewport.
  for (const auto size : {juce::Point<int>(900, 450), {760, 450}, {1100, 470}}) {
    grid.setSize(size.x, size.y);
    viewport->setViewPosition(0, 0);
    expectControlsInside();
    for (auto *strip : strips)
      expect(viewport->getViewArea().contains(strip->getParentComponent()->getBounds()),
             "height-aware density shows every row when the minimum readable scale fits");
    snapshotMixerLayout(grid, "mixer-compact-" + juce::String(size.x) + "x" + juce::String(size.y));
  }

  grid.setSize(420, 300);
  expectControlsInside();
  auto *content = viewport->getViewedComponent();
  expect(content->getHeight() > viewport->getViewHeight(),
         "short narrow windows preserve scroll rather than hiding or shrinking channels below the minimum");
  auto *last = strips.back()->getParentComponent();
  viewport->setViewPosition(0, content->getHeight());
  expect(viewport->getViewArea().contains(last->getBounds()),
         "scrolling reaches the complete final row and its bottom controls");
  expectMixUnchanged();
  grid.setSize(1100, 640);
  viewport->setViewPosition(0, 0);
  expectControlsInside();
#endif
}

void beginLinkedFaderGesture(juce::Slider &fader) {
  if (fader.onDragStart) fader.onDragStart();
}

void testLinkedFaderControlLayouts() {
  ChannelStripLookAndFeel lookAndFeel;
  for (const bool compact : {false, true}) {
    for (const float scale : {0.82f, 1.0f, 1.4f}) {
      ChannelStripCard master("MASTER", lookAndFeel, [] {
        return std::pair<float, float>{0.0f, 0.0f};
      }, compact);
      master.setSoloVisible(false);
      master.setMuteAlignedToFader(true);
      master.setLayoutScale(scale);
      master.setSize(juce::roundToInt((compact ? 72.0f : 90.0f) * scale),
                     juce::roundToInt((compact ? 276.0f : 244.0f) * scale));
      auto *mute = dynamic_cast<juce::Button *>(findMixerControl(master, "muteButton"));
      auto *link = dynamic_cast<juce::Button *>(findMixerControl(master, "linkInstrumentFaders"));
      expect(mute != nullptr && link != nullptr, "Master layout owns its mute and link controls");
      master.setFaderLinkButtonVisible(true);
      ChannelStripCard reference("INSTRUMENT", lookAndFeel, [] {
        return std::pair<float, float>{0.0f, 0.0f};
      }, compact);
      reference.setLayoutScale(scale);
      reference.setSize(master.getWidth(), master.getHeight());
      auto *referenceSolo = dynamic_cast<juce::Button *>(findMixerControl(reference, "soloButton"));
      auto *referenceMute = dynamic_cast<juce::Button *>(findMixerControl(reference, "muteButton"));
      expect(referenceSolo != nullptr && referenceMute != nullptr &&
                 !referenceSolo->getBounds().isEmpty() &&
                 mute->getBounds() == referenceSolo->getBounds() &&
                 link->getBounds() == referenceMute->getBounds(),
             "Master mute/link matches the ordinary Solo/Mute row width, spacing and centring at every supported scale");
      expect(!link->getBounds().isEmpty() && link->getWidth() >= 12 &&
                 master.getLocalBounds().contains(link->getBounds()) &&
                 master.getLocalBounds().contains(mute->getBounds()) &&
                 !link->getBounds().intersects(mute->getBounds()),
             "compact and standard Master layouts keep both buttons inside and separate at every supported scale");
    }
  }
}

void testLinkedInstrumentFadersPortableRoundTrip() {
  testLinkedFaderControlLayouts();
  const auto source = isolatedTestRoot().getChildFile("linked-faders-source.flac");
  {
    juce::FlacAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = source.createOutputStream();
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(16));
    expect(writer != nullptr, "linked mixer fixture uses a local lossless source");
    juce::AudioBuffer<float> audio(1, 2048);
    audio.clear(); audio.setSample(0, 32, 0.25f);
    expect(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
           "linked mixer source audio writes");
  }
  ProjectDocument document;
  document.song.id = "linked-faders-ui-project";
  document.song.projectId = document.song.id;
  document.song.title = "Linked instrument faders";
  document.song.bpm = 120.0;
  document.song.timeSignature = "4/4";
  const std::array<const char *, 5> names{{"Click", "Guide", "Drums", "Bass", "Piano"}};
  const std::array<const char *, 5> categories{{"Click", "Cues", "Drums", "Bass", "Keys"}};
  const std::array<float, 5> initialVolumes{{0.48f, 0.35f, 0.6f, 0.3f, 0.0f}};
  for (size_t i = 0; i < names.size(); ++i) {
    ImportedStemData stem;
    stem.stemId = "linked-" + juce::String(names[i]).toLowerCase();
    stem.displayName = names[i]; stem.category = categories[i];
    stem.fileName = source.getFileName();
    stem.sourcePath = stem.storedPath = source.getFullPathName();
    stem.sampleRate = 48000; stem.channels = 1; stem.durationSec = 2048.0 / 48000.0;
    document.song.stems.push_back(stem);
    StemMixSnapshot mix;
    mix.stemId = stem.stemId; mix.volume = initialVolumes[i];
    document.settings.mix.stems.push_back(mix);
  }
  document.settings.mix.master.volume = 0.73f;
  document.settings.clickDynMix.volume = 0.51f;
  document.settings.dynamicGuideMix.volume = 0.33f;
  const auto archive = isolatedTestRoot().getChildFile("linked-faders-ui.pwproj");
  juce::String error;
  expect(ProjectStore::save(document, archive, &error), "linked mixer project saves");
  ProjectDocument reopened;
  expect(ProjectStore::open(archive, reopened, &error) &&
             SongLibraryStore::registerProjectSong(reopened.song),
         "linked mixer project registers through the portable library");
  const auto setlist = SongLibraryStore::createSetlist("Linked faders UI setlist");
  expect(SongLibraryStore::addSongToSetlist(setlist, reopened.song.id) &&
             SongLibraryStore::setActiveSetlistId(setlist),
         "linked mixer project is the active setlist item");

  auto processor = std::make_unique<MTControlsAudioProcessor>();
  juce::StringArray paths;
  for (const auto &stem : reopened.song.stems) paths.add(stem.storedPath);
  expect(processor->loadSongStems(paths, reopened.song.id, reopened.song.title, "C", 120.0),
         "real processor loads the linked instruments");
  juce::StringArray stemIds;
  for (const auto &stem : reopened.song.stems) stemIds.add(stem.stemId);
  processor->setSectionAutomationStemIds(stemIds);
  processor->setStemClickGuideMask({true, true, false, false, false});
  processor->setStemGuideMask({false, true, false, false, false});
  for (int i = 0; i < static_cast<int>(initialVolumes.size()); ++i)
    processor->setStemVolume(i, initialVolumes[static_cast<size_t>(i)]);
  processor->setMasterVolume(0.73f);
  processor->setMetronomeVolume(0.51f);
  processor->setDynamicGuideVolume(0.33f);
  processor->setPadsVolume(0.42f);
  ChannelStripLookAndFeel lookAndFeel;
  ChannelGridPanel grid(lookAndFeel, *processor);
  grid.setSize(1200, 700);
  grid.setSong(reopened.song);
  auto *master = findMixerStrip(grid, "MASTER");
  auto *drumStrip = findMixerStrip(grid, "DRUMS");
  auto *bassStrip = findMixerStrip(grid, "BASS");
  auto *pianoStrip = findMixerStrip(grid, "PIANO");
  auto *guideStrip = findMixerStrip(grid, "GUIDE");
  expect(master != nullptr && drumStrip != nullptr && bassStrip != nullptr && pianoStrip != nullptr && guideStrip != nullptr,
         "real mixer exposes all named linked fixture strips");
  auto *link = dynamic_cast<juce::Button *>(findMixerControl(*master, "linkInstrumentFaders"));
  auto *drums = dynamic_cast<juce::Slider *>(findMixerControl(*drumStrip, "volumeFader"));
  auto *bass = dynamic_cast<juce::Slider *>(findMixerControl(*bassStrip, "volumeFader"));
  auto *piano = dynamic_cast<juce::Slider *>(findMixerControl(*pianoStrip, "volumeFader"));
  auto *guide = dynamic_cast<juce::Slider *>(findMixerControl(*guideStrip, "volumeFader"));
  auto *mute = dynamic_cast<juce::Button *>(findMixerControl(*master, "muteButton"));
  expect(link != nullptr && drums != nullptr && bass != nullptr && piano != nullptr && guide != nullptr && mute != nullptr,
         "link toggle and existing native faders are available in their real strips");
  expect(link->isVisible() && link->isEnabled() && !link->getToggleState(),
         "instrument linking is an explicit initially inactive mixer mode");
  expect(!link->getBounds().intersects(mute->getBounds()) &&
             master->getLocalBounds().contains(link->getBounds()),
         "link control occupies the free space beside Master mute without overlap");
  const auto close = [](float actual, float expected) { return std::abs(actual - expected) < 1.0e-5f; };
  const auto expectFixedBuses = [&] {
    expect(close(processor->getMasterVolume(), 0.73f) &&
               close(processor->getMetronomeVolume(), 0.51f) &&
               close(processor->getDynamicGuideVolume(), 0.33f) &&
               close(processor->getPadsVolume(), 0.42f) &&
               close(processor->getStemVolume(0), 0.48f) &&
               close(processor->getStemVolume(1), 0.35f),
           "group gestures leave Master, dynamic buses, Pads, click and guide stems untouched");
  };
  const auto toggle = [&](bool enabled) {
    link->setToggleState(enabled, juce::dontSendNotification);
    expect(static_cast<bool>(link->onClick), "link toggle has an assembled mixer callback");
    link->onClick();
  };
  toggle(true);
  expect(link->getToggleState() && close(processor->getStemVolume(2), 0.6f) &&
             close(processor->getStemVolume(3), 0.3f) && close(processor->getStemVolume(4), 0.0f),
         "activating the group changes no instrument level");
  expectFixedBuses();
  expect(drums->getDescription().isNotEmpty() && bass->getDescription().isNotEmpty() &&
             piano->getDescription().isNotEmpty(),
         "every eligible instrument is linked, including an instrument initially at zero");
  beginLinkedFaderGesture(*drums);
  drums->setValue(0.3, juce::sendNotificationSync);
  endClickGainGesture(*drums);
  expect(close(processor->getStemVolume(2), 0.3f) && close(processor->getStemVolume(3), 0.3f) &&
             close(processor->getStemVolume(4), 0.3f),
         "the first linked movement sets every instrument to the source fader's absolute level");
  // A second finger on an excluded guide strip must remain independent, and
  // ending that finger must not release the first finger's musical gesture.
  beginLinkedFaderGesture(*drums);
  drums->setValue(0.24, juce::sendNotificationSync);
  beginLinkedFaderGesture(*guide);
  guide->setValue(0.2, juce::sendNotificationSync);
  endClickGainGesture(*guide);
  expect(close(processor->getStemVolume(1), 0.2f) &&
             close(processor->getStemVolume(2), 0.24f) && close(processor->getStemVolume(3), 0.24f) &&
             close(processor->getStemVolume(4), 0.24f),
         "an excluded guide remains independently adjustable during a linked instrument drag");
  drums->setValue(0.3, juce::sendNotificationSync);
  endClickGainGesture(*drums);
  expect(close(processor->getStemVolume(1), 0.2f) &&
             close(processor->getStemVolume(2), 0.3f) && close(processor->getStemVolume(3), 0.3f) &&
             close(processor->getStemVolume(4), 0.3f),
         "releasing the independent guide preserves the continuing instrument group's gesture");
  beginLinkedFaderGesture(*guide);
  guide->setValue(0.35, juce::sendNotificationSync);
  endClickGainGesture(*guide);
  expectFixedBuses();
  // A second leader with no drag-start callback exercises wheel/keyboard entry.
  bass->setValue(0.2, juce::sendNotificationSync);
  endClickGainGesture(*bass);
  expect(close(processor->getStemVolume(2), 0.2f) && close(processor->getStemVolume(3), 0.2f) &&
             close(processor->getStemVolume(4), 0.2f),
         "another illuminated instrument can equalize the group without a pointer drag-start");
  toggle(false);
  beginLinkedFaderGesture(*drums);
  drums->setValue(1.0, juce::sendNotificationSync);
  endClickGainGesture(*drums);
  toggle(true);
  expect(close(processor->getStemVolume(2), 1.0f) && close(processor->getStemVolume(3), 0.2f) &&
             close(processor->getStemVolume(4), 0.2f),
         "reactivating linking preserves unequal starting levels until the next movement");
  beginLinkedFaderGesture(*bass);
  bass->setValue(0.8, juce::sendNotificationSync);
  endClickGainGesture(*bass);
  expect(close(processor->getStemVolume(2), 0.8f) && close(processor->getStemVolume(3), 0.8f) &&
             close(processor->getStemVolume(4), 0.8f) &&
             std::abs(bass->getValue() - 0.8) < 1.0e-5,
         "an instrument initially at the top does not limit another source fader or retain its old level");
  beginLinkedFaderGesture(*piano);
  piano->setValue(1.0, juce::sendNotificationSync);
  endClickGainGesture(*piano);
  expect(close(processor->getStemVolume(2), 1.0f) && close(processor->getStemVolume(3), 1.0f) &&
             close(processor->getStemVolume(4), 1.0f),
         "moving any linked instrument to the top places the entire group at the top");
  beginLinkedFaderGesture(*drums);
  drums->setValue(0.0, juce::sendNotificationSync);
  endClickGainGesture(*drums);
  expect(close(processor->getStemVolume(2), 0.0f) && close(processor->getStemVolume(3), 0.0f) &&
             close(processor->getStemVolume(4), 0.0f),
         "moving one linked instrument fully down places every instrument at zero");
  beginLinkedFaderGesture(*bass);
  bass->setValue(0.5, juce::sendNotificationSync);
  endClickGainGesture(*bass);
  expect(close(processor->getStemVolume(2), 0.5f) && close(processor->getStemVolume(3), 0.5f) &&
             close(processor->getStemVolume(4), 0.5f),
         "raising a linked fader from silence to the middle places every instrument at the same middle level");
  beginLinkedFaderGesture(*piano);
  piano->setValue(0.25, juce::sendNotificationSync);
  endClickGainGesture(*piano);
  expect(close(processor->getStemVolume(2), 0.25f) && close(processor->getStemVolume(3), 0.25f) &&
             close(processor->getStemVolume(4), 0.25f),
         "the originally silent instrument can lead the entire group at an absolute level");
  beginLinkedFaderGesture(*piano);
  piano->setValue(0.5, juce::sendNotificationSync);
  endClickGainGesture(*piano);
  expect(close(processor->getStemVolume(2), 0.5f) && close(processor->getStemVolume(3), 0.5f) &&
             close(processor->getStemVolume(4), 0.5f),
         "repeated linked gestures retain equal levels rather than restoring the original proportions");
  expectFixedBuses();
  grid.flushPendingPersistenceNow();
  expect(ProjectStore::savePendingPortableChanges(archive, &error) &&
             ProjectStore::open(archive, reopened, &error),
         "all linked changes consolidate into and reopen from the portable project");
  for (const auto &expected : std::array<std::pair<const char *, float>, 3>{{
           {"linked-drums", 0.5f}, {"linked-bass", 0.5f}, {"linked-piano", 0.5f}}}) {
    const auto found = std::find_if(reopened.settings.mix.stems.begin(), reopened.settings.mix.stems.end(),
        [&](const StemMixSnapshot &mix) { return mix.stemId == expected.first; });
    expect(found != reopened.settings.mix.stems.end() && close(found->volume, expected.second),
           "portable persistence includes every affected linked instrument, not just the dragged fader");
  }
  expect(close(reopened.settings.mix.master.volume, 0.73f) &&
             close(reopened.settings.clickDynMix.volume, 0.51f) &&
             close(reopened.settings.dynamicGuideMix.volume, 0.33f),
         "portable group changes do not rewrite the independent buses");
  toggle(false);
  expect(drums->getDescription().isEmpty() && bass->getDescription().isEmpty() &&
             piano->getDescription().isEmpty(),
         "deactivating linking removes every fader's linked indication");
  beginLinkedFaderGesture(*drums);
  drums->setValue(0.2, juce::sendNotificationSync);
  endClickGainGesture(*drums);
  expect(close(processor->getStemVolume(2), 0.2f) && close(processor->getStemVolume(3), 0.5f) &&
             close(processor->getStemVolume(4), 0.5f),
         "deactivating linking restores ordinary independent fader editing");
  toggle(true);
  grid.setMidiLearnActive(true);
  expect(!link->getToggleState() && !link->isEnabled(),
         "entering MIDI Learn disarms linking without reinterpreting mapped Master controls");
  grid.setMidiLearnActive(false);
  toggle(true);
  SongArrangementPartSnapshot verse;
  verse.id = "linked-verse"; verse.name = "Verse";
  verse.startSec = 0.0; verse.endSec = 0.04;
  SongArrangementSnapshot arrangement; arrangement.parts.push_back(verse);
  processor->setSongArrangement(arrangement, false);
  expect(processor->beginSectionAutomationEditing(verse.id),
         "open the real section automation draft for the loaded linked instruments");
  const auto awaitUnlinkedButton = [&](const bool expectedEnabled, const char *message) {
    const auto started = juce::Time::getMillisecondCounter();
    const auto ready = [&] { return !link->getToggleState() && link->isEnabled() == expectedEnabled; };
    while (!ready() && juce::Time::getMillisecondCounter() - started < 1000) {
      // This console fixture has no long-running native app event loop. Deliver
      // due JUCE timers while awaiting the observable UI state, not a fixed delay.
      juce::Timer::callPendingTimersSynchronously();
      if (!ready()) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    if (!ready())
      std::cerr << "Linked fader automation fixture: editing="
                << processor->isSectionAutomationEditing() << ", toggle="
                << link->getToggleState() << ", enabled=" << link->isEnabled()
                << ", expectedEnabled=" << expectedEnabled << '\n';
    expect(ready(), message);
  };
  awaitUnlinkedButton(false,
      "entering section automation editing disarms linking before multi-instrument draft changes");
  expect(processor->finishSectionAutomationEditing(false), "discard the isolated automation draft");
  awaitUnlinkedButton(true,
      "leaving automation editing reenables linking without silently reactivating it");
  toggle(true);
  auto nextSong = reopened.song; nextSong.id = "linked-next-song";
  grid.setSong(nextSong);
  master = findMixerStrip(grid, "MASTER");
  link = dynamic_cast<juce::Button *>(findMixerControl(*master, "linkInstrumentFaders"));
  expect(link != nullptr && !link->getToggleState(),
         "switching songs always leaves the next mixer in ordinary unlinked mode");
  expectFixedBuses();
}

void testRealProcessorLayoutLimits() {
  auto processor = std::make_unique<MTControlsAudioProcessor>();
  auto layout = processor->getBusesLayout();
  for (const auto input : {juce::AudioChannelSet::disabled(),
                           juce::AudioChannelSet::mono(),
                           juce::AudioChannelSet::stereo()}) {
    layout.inputBuses.set(0, input);
    for (int count : {1, 2, 8, 64}) {
      layout.outputBuses.set(0, juce::AudioChannelSet::discreteChannels(count));
      expect(processor->checkBusesLayoutSupported(layout),
             "real processor accepts disabled, mono or stereo inputs with 1..64 outputs");
    }
    for (int count : {0, 65}) {
      layout.outputBuses.set(0, juce::AudioChannelSet::discreteChannels(count));
      expect(!processor->checkBusesLayoutSupported(layout), "real processor rejects output counts outside 1..64");
    }
  }
  layout.inputBuses.set(0, juce::AudioChannelSet::discreteChannels(3));
  layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
  expect(!processor->checkBusesLayoutSupported(layout), "real processor still rejects multichannel inputs");
}
} // namespace

int main(int argc, char **argv) {
  juce::ScopedJuceInitialiser_GUI initialiseJuce;
  try {
    if (argc == 2 && juce::String(argv[1]) == "--song-crossfade-only") {
      testRealProcessorSongCrossfade();
      testAdvancedSongCrossfade();
      std::cout << "Song crossfade: actual host callback overlap, mono routes and cancellation passed\n";
      return 0;
    }
    if (argc == 2 && juce::String(argv[1]) == "--mixer-layout-only") {
      testDesktopMixerDensityLayouts();
      std::cout << "Desktop mixer density: real rows, complete controls, scroll and retained mix passed\n";
      return 0;
    }
    if (argc == 2 && juce::String(argv[1]) == "--click-gain-only") {
      testClickDynGainKnobPortableRoundTrip();
      std::cout << "Click Dyn gain: mixer UI and portable-project round trip passed\n";
      return 0;
    }
    if (argc == 2 && juce::String(argv[1]) == "--linked-faders-only") {
      testLinkedInstrumentFadersPortableRoundTrip();
      std::cout << "Linked instrument faders: mixer UI, equal levels and portable persistence passed\n";
      return 0;
    }
    testRealProcessorRuntime(8);
    testRealProcessorRuntime(2);
    testRealProcessorRuntime(64);
    testRealProcessorMonoClickAndGuide();
    testRealProcessorSongCrossfade();
    testAdvancedSongCrossfade();
    testRealProcessorLayoutLimits();
    testClickDynGainKnobPortableRoundTrip();
    testLinkedInstrumentFadersPortableRoundTrip();
    testDesktopMixerDensityLayouts();
    testRuntime(2);
    testRuntime(8);
    testRuntime(64);
    testDynamicWindowLimits();
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Standalone audio runtime: real processor layouts, routing, state and lifecycle passed\n";
}

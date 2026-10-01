#include "audio/StandaloneAudioRuntime.h"
#include "ui/NativeEditorBoundsConstrainer.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

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
} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI initialiseJuce;
  try {
    testRuntime(2);
    testRuntime(8);
    testRuntime(64);
    testDynamicWindowLimits();
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Standalone audio runtime: stereo, stage outputs, state and lifecycle passed\n";
}

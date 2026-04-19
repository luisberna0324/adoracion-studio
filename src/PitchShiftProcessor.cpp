#include "PitchShiftProcessor.h"

#include <SoundTouch.h>
#include <algorithm>
#include <cmath>

#if MTCONTROLS_HAS_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace {
constexpr double kMinTempoRatio = 0.5;
constexpr double kMaxTempoRatio = 2.0;

float semitonesToPitchScale(const int semitones) {
  return std::pow(2.0f, static_cast<float>(semitones) / 12.0f);
}
} // namespace

class PitchShiftBackend {
public:
  virtual ~PitchShiftBackend() = default;

  virtual void prepare(double sampleRate, int samplesPerBlock, int numChannels,
                       int pitchSemiTones, double tempoRatio) = 0;
  virtual void reset(double sampleRate, int samplesPerBlock, int numChannels,
                     int pitchSemiTones, double tempoRatio) = 0;
  virtual void setPitchSemiTones(int semitones) = 0;
  virtual void setTempoRatio(double ratio) = 0;
  virtual void process(juce::AudioBuffer<float> &buffer) = 0;
  virtual int getLatencySamples() const noexcept = 0;
  virtual bool isPrepared() const noexcept = 0;
};

class SoundTouchBackend final : public PitchShiftBackend {
public:
  void prepare(double sampleRate, int samplesPerBlock, int numChannels,
               int pitchSemiTones, double tempoRatio) override {
    channels = numChannels;
    prepared = (sampleRate > 0.0 && channels > 0);

    interleavedInput.clear();
    receiveBuffer.clear();
    outputFifo.clear();
    outputReadPos = 0;

    if (!prepared) {
      latencySamples = 0;
      return;
    }

    if (channels > SOUNDTOUCH_MAX_CHANNELS) {
      prepared = false;
      latencySamples = 0;
      return;
    }

    soundtouch.setSampleRate(static_cast<unsigned int>(sampleRate));
    soundtouch.setChannels(static_cast<unsigned int>(channels));
    soundtouch.setPitchSemiTones(static_cast<float>(pitchSemiTones));
    soundtouch.setTempo(static_cast<float>(tempoRatio));
    soundtouch.setRate(1.0f);
    soundtouch.clear();

    if (samplesPerBlock > 0 && channels > 0) {
      interleavedInput.resize(static_cast<size_t>(samplesPerBlock * channels));
      receiveBuffer.resize(static_cast<size_t>(samplesPerBlock * channels * 2));
    }

    primeLatency();
  }

  void reset(double sampleRate, int samplesPerBlock, int numChannels,
             int pitchSemiTones, double tempoRatio) override {
    prepare(sampleRate, samplesPerBlock, numChannels, pitchSemiTones,
            tempoRatio);
  }

  void setPitchSemiTones(int semitones) override {
    if (!prepared)
      return;
    soundtouch.setPitchSemiTones(static_cast<float>(semitones));
    soundtouch.clear();
    outputFifo.clear();
    outputReadPos = 0;
    primeLatency();
  }

  void setTempoRatio(double ratio) override {
    if (!prepared)
      return;
    soundtouch.setTempo(static_cast<float>(ratio));
    soundtouch.clear();
    outputFifo.clear();
    outputReadPos = 0;
    primeLatency();
  }

  void process(juce::AudioBuffer<float> &buffer) override {
    if (!prepared || channels <= 0)
      return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
      return;

    const size_t requiredInputSize = static_cast<size_t>(numSamples * channels);
    if (interleavedInput.size() < requiredInputSize)
      interleavedInput.resize(requiredInputSize);

    for (int sample = 0; sample < numSamples; ++sample) {
      const size_t base = static_cast<size_t>(sample * channels);
      for (int ch = 0; ch < channels; ++ch)
        interleavedInput[base + static_cast<size_t>(ch)] =
            buffer.getSample(ch, sample);
    }

    soundtouch.putSamples(interleavedInput.data(), numSamples);

    const int framesNeeded = numSamples;
    while (static_cast<int>((outputFifo.size() - outputReadPos) /
                            static_cast<size_t>(channels)) <
           framesNeeded) {
      const int maxFrames =
          std::max(framesNeeded,
                   static_cast<int>(receiveBuffer.size() /
                                    static_cast<size_t>(channels)));
      if (receiveBuffer.size() < static_cast<size_t>(maxFrames * channels)) {
        receiveBuffer.resize(static_cast<size_t>(maxFrames * channels));
      }

      const int received = soundtouch.receiveSamples(receiveBuffer.data(), maxFrames);
      if (received <= 0)
        break;

      const size_t appendCount = static_cast<size_t>(received * channels);
      outputFifo.insert(outputFifo.end(), receiveBuffer.begin(),
                        receiveBuffer.begin() + appendCount);
    }

    const int availableFrames = static_cast<int>((outputFifo.size() - outputReadPos) /
                                                 static_cast<size_t>(channels));

    if (availableFrames < framesNeeded) {
      const size_t padCount =
          static_cast<size_t>((framesNeeded - availableFrames) * channels);
      outputFifo.insert(outputFifo.end(), padCount, 0.0f);
    }

    for (int sample = 0; sample < numSamples; ++sample) {
      const size_t base = outputReadPos + static_cast<size_t>(sample * channels);
      for (int ch = 0; ch < channels; ++ch) {
        buffer.setSample(ch, sample, outputFifo[base + static_cast<size_t>(ch)]);
      }
    }

    outputReadPos += requiredInputSize;
    if (outputReadPos > outputFifo.size() / 2) {
      outputFifo.erase(outputFifo.begin(),
                       outputFifo.begin() + static_cast<std::ptrdiff_t>(outputReadPos));
      outputReadPos = 0;
    }
  }

  int getLatencySamples() const noexcept override { return latencySamples; }
  bool isPrepared() const noexcept override { return prepared; }

private:
  void primeLatency() {
    latencySamples = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
    if (latencySamples < 0)
      latencySamples = 0;

    outputFifo.clear();
    outputReadPos = 0;
    if (latencySamples > 0 && channels > 0) {
      outputFifo.resize(static_cast<size_t>(latencySamples * channels), 0.0f);
    }
  }

  soundtouch::SoundTouch soundtouch;
  std::vector<float> interleavedInput;
  std::vector<float> receiveBuffer;
  std::vector<float> outputFifo;
  size_t outputReadPos = 0;
  int channels = 0;
  int latencySamples = 0;
  bool prepared = false;
};

#if MTCONTROLS_HAS_RUBBERBAND
class RubberBandBackend final : public PitchShiftBackend {
public:
  void prepare(double sampleRate, int samplesPerBlock, int numChannels,
               int pitchSemiTones, double tempoRatio) override {
    sampleRateHz = sampleRate;
    channels = numChannels;
    blockSize = juce::jmax(1, samplesPerBlock);
    prepared = (sampleRateHz > 0.0 && channels > 0);

    outputReadPos = 0;
    outputFifo.clear();
    planarInput.clear();
    planarOutput.clear();
    channelPointers.clear();

    if (!prepared) {
      latencySamples = 0;
      stretcher.reset();
      return;
    }

    using RB = rubberband::RubberBandStretcher;
    const auto options = RB::OptionProcessRealTime |
                         RB::OptionEngineFiner |
                         RB::OptionPitchHighQuality |
                         RB::OptionWindowShort |
                         RB::OptionThreadingNever;

    stretcher = std::make_unique<RB>(sampleRateHz, channels, options, 1.0,
                                     semitonesToPitchScale(pitchSemiTones));

    stretcher->setMaxProcessSize(static_cast<size_t>(blockSize));
    setTempoRatio(tempoRatio);
    setPitchSemiTones(pitchSemiTones);

    planarInput.resize(static_cast<size_t>(channels));
    planarOutput.resize(static_cast<size_t>(channels));
    channelPointers.resize(static_cast<size_t>(channels));
    for (int ch = 0; ch < channels; ++ch) {
      planarInput[static_cast<size_t>(ch)].resize(static_cast<size_t>(blockSize));
      planarOutput[static_cast<size_t>(ch)].resize(static_cast<size_t>(blockSize * 2));
      channelPointers[static_cast<size_t>(ch)] = nullptr;
    }

    latencySamples = juce::jmax(0, static_cast<int>(stretcher->getLatency()));
    if (latencySamples > 0) {
      outputFifo.resize(static_cast<size_t>(latencySamples * channels), 0.0f);
    }
  }

  void reset(double sampleRate, int samplesPerBlock, int numChannels,
             int pitchSemiTones, double tempoRatio) override {
    prepare(sampleRate, samplesPerBlock, numChannels, pitchSemiTones,
            tempoRatio);
  }

  void setPitchSemiTones(int semitones) override {
    if (stretcher == nullptr)
      return;
    stretcher->setPitchScale(semitonesToPitchScale(semitones));
  }

  void setTempoRatio(double ratio) override {
    if (stretcher == nullptr)
      return;
    // RubberBand's time ratio is duration scaling, while app tempoRatio is speed.
    const double safeRatio = juce::jlimit(kMinTempoRatio, kMaxTempoRatio, ratio);
    stretcher->setTimeRatio(1.0 / safeRatio);
  }

  void process(juce::AudioBuffer<float> &buffer) override {
    if (!prepared || stretcher == nullptr)
      return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
      return;

    latencySamples = juce::jmax(0, static_cast<int>(stretcher->getLatency()));

    if (numSamples > blockSize) {
      blockSize = numSamples;
      for (int ch = 0; ch < channels; ++ch) {
        planarInput[static_cast<size_t>(ch)].resize(static_cast<size_t>(blockSize));
        planarOutput[static_cast<size_t>(ch)].resize(static_cast<size_t>(blockSize * 2));
      }
      stretcher->setMaxProcessSize(static_cast<size_t>(blockSize));
    }

    for (int ch = 0; ch < channels; ++ch) {
      auto &in = planarInput[static_cast<size_t>(ch)];
      std::copy_n(buffer.getReadPointer(ch), numSamples, in.begin());
      channelPointers[static_cast<size_t>(ch)] = in.data();
    }

    stretcher->process(channelPointers.data(), static_cast<size_t>(numSamples), false);

    const int framesNeeded = numSamples;
    while (static_cast<int>((outputFifo.size() - outputReadPos) /
                            static_cast<size_t>(channels)) <
           framesNeeded) {
      const size_t available = stretcher->available();
      if (available == 0)
        break;

      const int retrieveFrames = juce::jmin<int>(
          static_cast<int>(available),
          juce::jmax(framesNeeded, static_cast<int>(planarOutput[0].size())));
      for (int ch = 0; ch < channels; ++ch)
        channelPointers[static_cast<size_t>(ch)] =
            planarOutput[static_cast<size_t>(ch)].data();

      const int received = static_cast<int>(
          stretcher->retrieve(channelPointers.data(), static_cast<size_t>(retrieveFrames)));
      if (received <= 0)
        break;

      const size_t oldSize = outputFifo.size();
      outputFifo.resize(oldSize + static_cast<size_t>(received * channels));
      for (int frame = 0; frame < received; ++frame) {
        const size_t frameBase = oldSize + static_cast<size_t>(frame * channels);
        for (int ch = 0; ch < channels; ++ch) {
          outputFifo[frameBase + static_cast<size_t>(ch)] =
              planarOutput[static_cast<size_t>(ch)][static_cast<size_t>(frame)];
        }
      }
    }

    const int availableFrames = static_cast<int>((outputFifo.size() - outputReadPos) /
                                                 static_cast<size_t>(channels));
    if (availableFrames < framesNeeded) {
      outputFifo.resize(outputFifo.size() +
                            static_cast<size_t>((framesNeeded - availableFrames) *
                                                channels),
                        0.0f);
    }

    for (int sample = 0; sample < numSamples; ++sample) {
      const size_t base = outputReadPos + static_cast<size_t>(sample * channels);
      for (int ch = 0; ch < channels; ++ch)
        buffer.setSample(ch, sample, outputFifo[base + static_cast<size_t>(ch)]);
    }

    outputReadPos += static_cast<size_t>(numSamples * channels);
    if (outputReadPos > outputFifo.size() / 2) {
      outputFifo.erase(outputFifo.begin(),
                       outputFifo.begin() + static_cast<std::ptrdiff_t>(outputReadPos));
      outputReadPos = 0;
    }
  }

  int getLatencySamples() const noexcept override { return latencySamples; }
  bool isPrepared() const noexcept override { return prepared; }

private:
  std::unique_ptr<rubberband::RubberBandStretcher> stretcher;
  std::vector<std::vector<float>> planarInput;
  std::vector<std::vector<float>> planarOutput;
  std::vector<float *> channelPointers;
  std::vector<float> outputFifo;
  size_t outputReadPos = 0;
  int channels = 0;
  int blockSize = 0;
  int latencySamples = 0;
  double sampleRateHz = 0.0;
  bool prepared = false;
};
#endif

void PitchShiftProcessor::prepare(double sampleRate, int samplesPerBlock,
                                  int numChannels) {
  currentSampleRate = sampleRate;
  currentBlockSize = juce::jmax(0, samplesPerBlock);
  currentChannels = numChannels;
  prepared = (currentSampleRate > 0.0 && currentChannels > 0);

#if MTCONTROLS_HAS_RUBBERBAND && MTCONTROLS_TIMEPITCH_BACKEND_RUBBERBAND
  if (backend == nullptr)
    backend = std::make_unique<RubberBandBackend>();
#else
  if (backend == nullptr)
    backend = std::make_unique<SoundTouchBackend>();
#endif

  backend->prepare(currentSampleRate, currentBlockSize, currentChannels,
                   pitchSemiTones, tempoRatio);
  primeLatency();
}

void PitchShiftProcessor::reset() {
  if (backend == nullptr)
    return;
  backend->reset(currentSampleRate, currentBlockSize, currentChannels,
                 pitchSemiTones,
                 tempoRatio);
  primeLatency();
}

void PitchShiftProcessor::setPitchSemiTones(int semitones) {
  if (pitchSemiTones == semitones)
    return;

  pitchSemiTones = semitones;
  if (backend != nullptr)
    backend->setPitchSemiTones(pitchSemiTones);
  primeLatency();
}

void PitchShiftProcessor::setTempoRatio(double ratio) {
  const double normalized = juce::jlimit(kMinTempoRatio, kMaxTempoRatio, ratio);
  if (juce::approximatelyEqual(tempoRatio, normalized))
    return;

  tempoRatio = normalized;
  if (backend != nullptr)
    backend->setTempoRatio(tempoRatio);
  primeLatency();
}

int PitchShiftProcessor::getPitchSemiTones() const noexcept {
  return pitchSemiTones;
}

double PitchShiftProcessor::getTempoRatio() const noexcept { return tempoRatio; }

int PitchShiftProcessor::getLatencySamples() const noexcept {
  return latencySamples;
}

int PitchShiftProcessor::getNumChannels() const noexcept { return currentChannels; }

bool PitchShiftProcessor::isPrepared() const noexcept {
  return prepared && backend != nullptr && backend->isPrepared();
}

void PitchShiftProcessor::process(juce::AudioBuffer<float> &buffer) {
  if (!isPrepared() || currentChannels <= 0)
    return;
  if (pitchSemiTones == 0 && juce::approximatelyEqual(tempoRatio, 1.0))
    return;

  if (buffer.getNumChannels() != currentChannels) {
    prepare(currentSampleRate, buffer.getNumSamples(), buffer.getNumChannels());
  }

  backend->process(buffer);
}

void PitchShiftProcessor::primeLatency() {
  latencySamples = backend != nullptr ? backend->getLatencySamples() : 0;
  if (latencySamples < 0)
    latencySamples = 0;
}

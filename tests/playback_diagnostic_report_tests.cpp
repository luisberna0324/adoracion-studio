#include "../src/data/PlaybackDiagnosticReport.h"
#include "../src/audio/SignalIntegrityMetrics.h"
#include "IsolatedTestRoot.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static void expect(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
  const auto folder = isolatedTestRoot().getChildFile("diagnostics");
  expect(folder.createDirectory().wasOk(), "create isolated test folder");
  const auto csv = folder.getChildFile("source.csv");
  expect(csv.replaceWithText("wall_ms,value\n1,5\n100000,2\n150000,3\n160000,secret\n170000,1,2\n200001,4\n199999,9"), "write fixture");
  const auto window = PlaybackDiagnosticReport::readWindow(csv, 200000);
  expect(window.rows == 2, "filter old/future/malformed/private/unfinished rows");
  expect(window.firstMs == 100000 && window.lastMs == 150000, "report actual coverage");
  expect(!window.csv.contains("secret") && !window.csv.contains("199999"), "exclude private and incomplete rows");
  AudioDiagnostics::ReportFiles files;
  files.files[0] = csv;
  auto* metadata = new juce::DynamicObject();
  metadata->setProperty("accountUid", "test-account");
  metadata->setProperty("deviceModel", "test-device");
  const auto report = PlaybackDiagnosticReport::create(files, juce::var(metadata), 200000, folder);
  expect(report.error.isEmpty() && report.file.existsAsFile(), "create ZIP");
  juce::ZipFile archive(report.file);
  const int manifestIndex = archive.getIndexOfFileName("report.json");
  expect(manifestIndex >= 0 && archive.getIndexOfFileName("audio.csv") >= 0, "contains structured manifest and CSV");
  std::unique_ptr<juce::InputStream> manifestStream(archive.createStreamForEntry(manifestIndex));
  auto manifest = juce::JSON::parse(manifestStream->readEntireStreamAsString());
  expect((int)manifest["schemaVersion"] == 2 && !(bool)manifest["includesAudio"], "versioned metadata without audio");
  expect(manifest["coverage"][0]["rows"] == juce::var(2), "archive records coverage");

  // Limit reads even if the existing logger has run for hours.
  juce::MemoryOutputStream large;
  large << "wall_ms,value\n";
  for (int i = 0; i < 200000; ++i) large << "100000,0.125\n";
  csv.replaceWithText(large.toUTF8());
  const auto bounded = PlaybackDiagnosticReport::readWindow(csv, 200000);
  expect(bounded.byteLimited && bounded.rows > 0 && bounded.rows < 200000, "bounded export marks truncated coverage");

  {
    AudioDiagnostics diagnostics;
    diagnostics.startBackgroundLogger(folder.getChildFile("logs"));
    AudioDiagnostics::DiagEvent event;
    event.wallMs = juce::Time::currentTimeMillis();
    event.sourceGeneration = 42;
    diagnostics.pushEvent(event);
    AudioDiagnostics::DeviceEvent device;
    device.wallMs = event.wallMs;
    device.nativeXruns = -1;
    device.sourceGeneration = 42;
    diagnostics.pushDeviceEvent(device);
    AudioDiagnostics::DistortionEvent distortion;
    distortion.wallMs = event.wallMs;
    distortion.sourceGeneration = 42;
    distortion.markerId = 7;
    distortion.sourceKind = 2;
    distortion.outputPeak = 1.1f;
    distortion.overSamples = 3;
    diagnostics.pushDistortionEvent(distortion);
    AudioDiagnostics::MarkerEvent marker;
    marker.wallMs = event.wallMs;
    marker.monotonicMs = juce::Time::getMillisecondCounterHiRes();
    marker.sourceGeneration = 42;
    marker.markerId = 7;
    diagnostics.pushMarkerEvent(marker);
    diagnostics.stopBackgroundLogger();
    const auto exported = diagnostics.getReportFiles();
    const auto blocks = exported.files[0].loadFileAsString();
    const auto devices = exported.files[3].loadFileAsString();
    const auto distortionRows = exported.files[4].loadFileAsString();
    const auto markerRows = exported.files[5].loadFileAsString();
    expect(blocks.contains("source_generation") && blocks.contains(",42\n"), "callback generation survives export");
    expect(devices.contains("native_xruns") && devices.contains(",-1,"), "unsupported XRuns remain distinguishable from zero");
    expect(distortionRows.contains("boundary_delta") &&
               distortionRows.contains(",7,2,-1,"),
           "signal-integrity metrics survive export");
    expect(markerRows.contains("marker_id") && markerRows.contains(",7,-1,"),
           "audible marker survives export");
  }

  {
    juce::AudioBuffer<float> signal(1, 4);
    signal.setSample(0, 0, 0.0f);
    signal.setSample(0, 1, 0.5f);
    signal.setSample(0, 2, 1.1f);
    signal.setSample(0, 3, -0.8f);
    playworship::audio::SignalIntegrityState state;
    const auto first = playworship::audio::measureSignalIntegrity(
        signal, 0, signal.getNumSamples(), 1.0f, state);
    expect(first.outputPeak > 1.09f && first.overSamples == 1 &&
               first.nearClipSamples == 1 && first.largeDeltaSamples >= 2,
           "detect clipping and large discontinuities");

    signal.clear();
    signal.setSample(0, 0, 1.0f);
    const auto second = playworship::audio::measureSignalIntegrity(
        signal, 0, signal.getNumSamples(), 1.0f, state);
    expect(second.boundaryDelta > 1.79f && second.suspicious(),
           "detect discontinuity at callback boundary");

    signal.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
    const auto invalid = playworship::audio::measureSignalIntegrity(
        signal, 0, 1, 1.0f, state);
    expect(invalid.nonFiniteSamples == 1 && invalid.suspicious(),
           "detect non-finite samples");
  }
  folder.deleteRecursively();
  std::cout << "Playback diagnostic report tests passed\n";
}

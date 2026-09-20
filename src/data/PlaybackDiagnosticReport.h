#pragma once

#include "../AudioDiagnostics.h"

// File I/O and compression: call exclusively on a background thread. The
// allowlisted numeric CSVs contain no paths, song names, audio or auth tokens.
class PlaybackDiagnosticReport {
public:
  struct Result {
    juce::File file;
    juce::String error;
  };

  struct Window {
    juce::String csv;
    int rows = 0;
    bool byteLimited = false;
    double firstMs = 0, lastMs = 0;
  };

  static Window readWindow(const juce::File& file, const double endMs) {
    Window result;
    auto input = file.createInputStream();
    if (!input) return result;
    const auto header = input->readNextLine();
    if (!header.startsWith("wall_ms,")) return result;
    const auto dataStart = input->getPosition();
    const auto length = input->getTotalLength();
    constexpr juce::int64 byteLimit = 2 * 1024 * 1024;
    const auto readStart = juce::jmax(dataStart, length - byteLimit);
    result.byteLimited = readStart > dataStart;
    input->setPosition(readStart);
    juce::MemoryBlock bytes;
    input->readIntoMemoryBlock(bytes, static_cast<ssize_t>(length - readStart));
    auto text = bytes.toString();
    if (readStart > dataStart) text = text.fromFirstOccurrenceOf("\n", false, false);
    // A running logger may be in the middle of a row. Never export that tail.
    const int finalNewline = text.lastIndexOfChar('\n');
    if (finalNewline < 0) return result;
    text = text.substring(0, finalNewline);
    juce::MemoryOutputStream filtered;
    filtered << header << "\n";
    const int columns = juce::StringArray::fromTokens(header, ",", "").size();
    for (auto line : juce::StringArray::fromLines(text)) {
      line = line.trim();
      if (line.isEmpty() || line.containsOnly("0123456789.,-+eE") == false) continue;
      const auto fields = juce::StringArray::fromTokens(line, ",", "");
      if (fields.size() != columns) continue;
      const double time = fields[0].getDoubleValue();
      if (time < endMs - 120000.0 || time > endMs) continue;
      if (result.rows == 0) result.firstMs = time;
      result.lastMs = time;
      ++result.rows;
      filtered << line << "\n";
    }
    result.csv = filtered.toUTF8();
    return result;
  }

  static Result create(const AudioDiagnostics::ReportFiles& source,
                       juce::var metadata, const double endMs,
                       const juce::File& directory) {
    Result result;
    auto* manifest = metadata.getDynamicObject();
    if (!manifest) { result.error = "Invalid report metadata"; return result; }
    const auto id = juce::Uuid().toString();
    manifest->setProperty("schemaVersion", 2);
    manifest->setProperty("reportId", id);
    manifest->setProperty("capturedAtMs", endMs);
    manifest->setProperty("requestedWindowMs", 120000);
    manifest->setProperty("includesAudio", false);
    manifest->setProperty("coverageNote", "Only the current audio-engine log session is included. Logs may lag by 500 ms. Cumulative counters must be compared within the same source/engine generation. Native XRuns -1 means unsupported.");
    auto* drops = new juce::DynamicObject();
    drops->setProperty("blocks", static_cast<juce::int64>(source.droppedBlocks));
    drops->setProperty("stems", static_cast<juce::int64>(source.droppedStems));
    drops->setProperty("gaps", static_cast<juce::int64>(source.droppedGaps));
    drops->setProperty("devices", static_cast<juce::int64>(source.droppedDevices));
    drops->setProperty("distortion", static_cast<juce::int64>(source.droppedDistortion));
    drops->setProperty("markers", static_cast<juce::int64>(source.droppedMarkers));
    manifest->setProperty("droppedEventsLifetime", juce::var(drops));
    const std::array<const char*, 6> names{
        "audio.csv", "stems.csv", "gaps.csv", "device.csv",
        "distortion.csv", "markers.csv"};
    juce::Array<juce::var> coverage;
    juce::ZipFile::Builder zip;
    const auto addText = [&zip](const juce::String& name, const juce::String& text) {
      zip.addEntry(new juce::MemoryInputStream(text.toRawUTF8(), text.getNumBytesAsUTF8(), true),
                   6, name, juce::Time::getCurrentTime());
    };
    int totalRows = 0;
    for (size_t i = 0; i < names.size(); ++i) {
      const auto window = readWindow(source.files[i], endMs);
      auto* item = new juce::DynamicObject();
      item->setProperty("file", names[i]);
      item->setProperty("rows", window.rows);
      item->setProperty("firstMs", window.firstMs);
      item->setProperty("lastMs", window.lastMs);
      item->setProperty("byteLimited", window.byteLimited);
      coverage.add(juce::var(item));
      totalRows += window.rows;
      if (window.csv.isNotEmpty()) addText(names[i], window.csv);
    }
    if (totalRows == 0) {
      result.error = "No recent audio diagnostics. Play a song and try again.";
      return result;
    }
    manifest->setProperty("coverage", coverage);
    addText("report.json", juce::JSON::toString(metadata, false));
    addText("README.txt", "PlayWorship playback diagnostics\nreport.json: device, app version, account ID, context and capture coverage.\naudio.csv: audio callback timing and cumulative streaming loss.\nstems.csv: per-track timing and cumulative lock/streaming misses.\ngaps.csv: detected gaps between callbacks.\ndevice.csv: native output XRuns, configuration and playback context (1 Hz).\ndistortion.csv: numeric signal-integrity metrics for stems, master and final output.\nmarkers.csv: optional internal marker events.\nSource kind: 0=stem, 1=master after gain, 2=final device output.\nNo songs, microphone recordings, credentials or file paths are included.\n");
    if (directory.createDirectory().failed()) { result.error = "Cannot create report directory"; return result; }
    // Retain share-sheet files long enough for receiving apps to copy them.
    // Cleanup is restricted to reports generated by this feature.
    for (const auto& old : directory.findChildFiles(juce::File::findFiles, false, "PlayWorship-diagnostic-*.zip"))
      if (old.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days(7))
        old.deleteFile();
    result.file = directory.getChildFile("PlayWorship-diagnostic-" + id + ".zip");
    auto output = result.file.createOutputStream();
    if (!output || !zip.writeToStream(*output, nullptr)) result.error = "Cannot write diagnostic ZIP";
    if (output) {
      output->flush();
      if (output->getStatus().failed()) result.error = "Cannot save diagnostic ZIP";
    }
    output.reset();
    if (result.error.isNotEmpty()) { result.file.deleteFile(); result.file = juce::File{}; }
    return result;
  }
};

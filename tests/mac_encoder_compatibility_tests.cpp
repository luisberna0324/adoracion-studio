#include "data/MacEncoderCompatibility.h"
#include "data/MacEncoderDownload.h"
#include "IsolatedTestRoot.h"

#include <juce_cryptography/juce_cryptography.h>
#if JUCE_MAC
#include "data/AppSettings.h"
#include "data/AssetBootstrapManager.h"
#endif

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Architecture = mac_encoder::Architecture;
using Bytes = std::vector<std::uint8_t>;

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void put32(Bytes &bytes, size_t offset, std::uint32_t value, bool littleEndian) {
  for (int i = 0; i < 4; ++i)
    bytes[offset + size_t(littleEndian ? i : 3 - i)] = std::uint8_t(value >> (8 * i));
}

void put64(Bytes &bytes, size_t offset, std::uint64_t value, bool littleEndian) {
  for (int i = 0; i < 8; ++i)
    bytes[offset + size_t(littleEndian ? i : 7 - i)] = std::uint8_t(value >> (8 * i));
}

Bytes thin(Architecture architecture, bool littleEndian = true) {
  Bytes bytes(64);
  put32(bytes, 0, 0xfeedfacfu, littleEndian);
  put32(bytes, 4, architecture == Architecture::intel64 ? 0x01000007u : 0x0100000cu, littleEndian);
  put32(bytes, 8, architecture == Architecture::intel64 ? 3u : 0u, littleEndian);
  put32(bytes, 12, 2, littleEndian);
  return bytes;
}

Bytes universal(bool wide, bool littleEndian) {
  Bytes bytes(320);
  put32(bytes, 0, wide ? 0xcafebabfu : 0xcafebabeu, littleEndian);
  put32(bytes, 4, 2, littleEndian);
  for (int i = 0; i < 2; ++i) {
    const auto architecture = i == 0 ? Architecture::intel64 : Architecture::appleSilicon;
    const auto executable = thin(architecture);
    const size_t entry = 8 + size_t(i) * (wide ? 32u : 20u);
    const size_t offset = 128 + size_t(i) * 128;
    put32(bytes, entry, i == 0 ? 0x01000007u : 0x0100000cu, littleEndian);
    if (wide) {
      put64(bytes, entry + 8, offset, littleEndian);
      put64(bytes, entry + 16, executable.size(), littleEndian);
    } else {
      put32(bytes, entry + 8, std::uint32_t(offset), littleEndian);
      put32(bytes, entry + 12, std::uint32_t(executable.size()), littleEndian);
    }
    std::copy(executable.begin(), executable.end(), bytes.begin() + std::ptrdiff_t(offset));
  }
  return bytes;
}

bool compatible(const Bytes &bytes, Architecture architecture) {
  juce::MemoryInputStream stream(bytes.data(), bytes.size(), false);
  return mac_encoder::supportsArchitecture(stream, architecture);
}

void headerContracts() {
  for (const auto architecture : {Architecture::intel64, Architecture::appleSilicon}) {
    const auto other = architecture == Architecture::intel64
                           ? Architecture::appleSilicon : Architecture::intel64;
    for (const bool littleEndian : {true, false}) {
      const auto binary = thin(architecture, littleEndian);
      expect(compatible(binary, architecture), "native thin Mach-O architecture is accepted");
      expect(!compatible(binary, other), "thin binary from the other architecture is rejected");
    }
  }
  for (const bool wide : {false, true}) {
    for (const bool littleEndian : {false, true}) {
      const auto binary = universal(wide, littleEndian);
      expect(compatible(binary, Architecture::intel64), "universal Intel slice is recognized");
      expect(compatible(binary, Architecture::appleSilicon), "universal Apple Silicon slice is recognized");
    }
  }
  auto binary = thin(Architecture::intel64);
  put32(binary, 12, 6, true);
  expect(!compatible(binary, Architecture::intel64), "a dylib cannot be selected as an executable encoder");
  binary = thin(Architecture::intel64);
  put32(binary, 20, 1024, true);
  expect(!compatible(binary, Architecture::intel64), "truncated load commands are rejected");
  binary = thin(Architecture::intel64);
  binary.resize(31);
  expect(!compatible(binary, Architecture::intel64), "truncated thin headers are rejected");
  expect(!compatible(Bytes(64, 'x'), Architecture::intel64), "non-Mach-O content is rejected");
  binary = universal(false, false);
  put32(binary, 4, 0, false);
  expect(!compatible(binary, Architecture::intel64), "an empty universal table is rejected");
  put32(binary, 4, 65, false);
  expect(!compatible(binary, Architecture::intel64), "unbounded architecture tables are rejected");
  binary = universal(false, false);
  binary.resize(35);
  expect(!compatible(binary, Architecture::intel64), "truncated universal tables are rejected");
  binary = universal(true, false);
  put64(binary, 16, UINT64_MAX, false);
  expect(!compatible(binary, Architecture::intel64), "overflowing slice offsets are rejected");
  binary = universal(false, false);
  put32(binary, 20, 1000, false);
  expect(!compatible(binary, Architecture::intel64), "slices must be inside the downloaded file");
  binary = universal(false, false);
  put32(binary, 132, 0x0100000cu, true);
  expect(!compatible(binary, Architecture::intel64), "slice architecture must match its table entry");
  binary = universal(false, false);
  put32(binary, 4, 1, false);
  expect(!compatible(binary, Architecture::appleSilicon), "an Intel-only universal file is rejected on Apple Silicon");
}

void write(const juce::File &file, const Bytes &bytes) {
  expect(file.replaceWithData(bytes.data(), bytes.size()), "write isolated executable-header fixture");
}

void selectionAndInstallation() {
  const auto root = isolatedTestRoot();
  expect(root.createDirectory().wasOk(), "isolated encoder directory exists");
  const auto managed = root.getChildFile("managed-encoder");
  const auto bundled = root.getChildFile("bundled-encoder");
  write(managed, thin(Architecture::appleSilicon));
  write(bundled, thin(Architecture::intel64));
  expect(managed.setExecutePermission(true) && bundled.setExecutePermission(true),
         "compatible selection fixtures have explicit execution permission");
  expect(mac_encoder::firstCompatible({managed, bundled}, Architecture::intel64) == bundled,
         "an incompatible managed encoder cannot hide a compatible bundled copy");
  expect(mac_encoder::firstCompatible({managed, bundled}, Architecture::appleSilicon) == managed,
         "a compatible managed encoder retains precedence");
  expect(mac_encoder::firstCompatible({managed}, Architecture::intel64) == juce::File{},
         "an installed Apple Silicon encoder cannot become available on Intel");

#if !JUCE_WINDOWS
  write(managed, thin(Architecture::intel64));
  expect(managed.setExecutePermission(false) && !mac_encoder::hasExecuteAccess(managed),
         "the cached native Mach-O fixture has no execution permission");
  expect(mac_encoder::firstCompatible({managed}, Architecture::intel64) == juce::File{},
         "a native cached encoder without execution permission cannot become ready");
  expect(mac_encoder::firstCompatible({managed, bundled}, Architecture::intel64) == bundled,
         "a non-executable cache cannot hide a compatible executable bundled encoder");
#endif

  const auto temporary = root.getChildFile("encoder.download");
  juce::String error;
  write(temporary, thin(Architecture::appleSilicon));
  expect(!mac_encoder::installCompatibleDownload(temporary, bundled, Architecture::intel64, error) &&
             error.isNotEmpty() && !temporary.existsAsFile() &&
             mac_encoder::supportsArchitecture(bundled, Architecture::intel64),
         "a wrong-architecture download is discarded while the existing Intel encoder is preserved");
  write(temporary, Bytes(7, 'x'));
  expect(!mac_encoder::installCompatibleDownload(temporary, bundled, Architecture::intel64, error) &&
             !temporary.existsAsFile() && mac_encoder::supportsArchitecture(bundled, Architecture::intel64),
         "a truncated download leaves the existing encoder usable");
  write(temporary, universal(false, false));
  expect(mac_encoder::installCompatibleDownload(temporary, bundled, Architecture::intel64, error) &&
             !temporary.existsAsFile() && mac_encoder::supportsArchitecture(bundled, Architecture::appleSilicon),
         "a verified universal encoder replaces the installed copy through the temporary file");

  const auto directory = root.getChildFile("directory-destination");
  expect(directory.createDirectory().wasOk(), "create an invalid installation destination");
  const auto retained = directory.getChildFile("retained.txt");
  expect(retained.replaceWithText("preserve"), "create destination preservation fixture");
  write(temporary, thin(Architecture::intel64));
  expect(!mac_encoder::installCompatibleDownload(temporary, directory, Architecture::intel64, error) &&
             retained.loadFileAsString() == "preserve" && !temporary.existsAsFile(),
         "failed installation preserves the prior destination and removes its temporary download");
}

void downloadAssetSelection() {
  const auto intel = mac_encoder::downloadAsset(Architecture::intel64);
  const auto arm = mac_encoder::downloadAsset(Architecture::appleSilicon);
  expect(intel.architecture == Architecture::intel64 &&
             juce::String(intel.relativePath) ==
                 "/encoder/macos/2026-11-06-1/x86_64/ffmpeg" &&
             juce::String(intel.version) == "2026-11-06-1",
         "Intel selects its versioned Intel download instead of the Apple Silicon endpoint");
  expect(arm.architecture == Architecture::appleSilicon &&
             juce::String(arm.relativePath) == "/encoder/macos/2026-04-18-1/ffmpeg" &&
             juce::String(arm.version) == "2026-04-18-1",
         "Apple Silicon retains its existing versioned download endpoint");
  expect(intel.sizeBytes > 0 && juce::String(intel.sha256).length() == 64 &&
             arm.sizeBytes > 0 && juce::String(arm.sha256).length() == 64,
         "both native downloads declare an exact size and SHA256 pin");
}

void verifiedDownloadInstallation() {
  const auto root = isolatedTestRoot().getChildFile("verified-downloads");
  expect(root.createDirectory().wasOk(), "create isolated pinned-download fixtures");
  const auto current = root.getChildFile("ffmpeg");
  const auto temporary = root.getChildFile("ffmpeg.download");
  const auto intel = thin(Architecture::intel64);
  const auto arm = thin(Architecture::appleSilicon);
  const auto intelHash = juce::SHA256(intel.data(), intel.size()).toHexString();
  const auto armHash = juce::SHA256(arm.data(), arm.size()).toHexString();
  const mac_encoder::DownloadAsset asset{Architecture::intel64, "fixture-v1",
      "/isolated/intel/ffmpeg", juce::int64(intel.size()), intelHash.toRawUTF8()};
  juce::String error;
  write(current, arm);
  expect(current.setExecutePermission(true),
         "the incompatible cached fixture is executable before architecture repair");
  const auto expectCurrentPreserved = [&] {
    expect(!temporary.exists() && mac_encoder::supportsArchitecture(current, Architecture::appleSilicon) &&
               juce::SHA256(current).toHexString() == armHash && error.isNotEmpty(),
           "a rejected download removes only its temporary and preserves the cached encoder");
  };

  auto corrupted = intel;
  corrupted.back() ^= 0xff;
  write(temporary, corrupted);
  expect(!mac_encoder::installVerifiedDownload(temporary, current, asset, error),
         "a same-size download with an incorrect SHA256 is rejected");
  expectCurrentPreserved();

  auto shortened = intel;
  shortened.pop_back();
  write(temporary, shortened);
  expect(!mac_encoder::installVerifiedDownload(temporary, current, asset, error),
         "a short download cannot replace the cached encoder");
  expectCurrentPreserved();

  auto oversized = intel;
  oversized.push_back(0);
  write(temporary, oversized);
  expect(!mac_encoder::installVerifiedDownload(temporary, current, asset, error),
         "a download larger than its descriptor is rejected");
  expectCurrentPreserved();

  auto otherArchitecture = asset;
  otherArchitecture.sha256 = armHash.toRawUTF8();
  write(temporary, arm);
  expect(!mac_encoder::installVerifiedDownload(temporary, current, otherArchitecture, error),
         "a correctly pinned Apple Silicon payload cannot be installed as an Intel encoder");
  expectCurrentPreserved();

  expect(!mac_encoder::installVerifiedDownload(current, current, asset, error) &&
             juce::SHA256(current).toHexString() == armHash,
         "validation failure cannot remove a destination reused as its own temporary");

  expect(mac_encoder::firstCompatible({current}, Architecture::intel64) == juce::File{},
         "a cached Apple Silicon download is unavailable on Intel before repair");
  write(temporary, intel);
  error = "previous error";
  expect(mac_encoder::verifyDownload(temporary, asset, error) && error.isEmpty(),
         "a matching size, SHA256 and native Mach-O header verify together");
  expect(mac_encoder::installVerifiedDownload(temporary, current, asset, error) &&
             error.isEmpty() && !temporary.exists() &&
             mac_encoder::firstCompatible({current}, Architecture::intel64) == current &&
             juce::SHA256(current).toHexString() == intelHash,
         "a verified Intel download repairs the incompatible cached Apple Silicon encoder");
}

#if JUCE_MAC
void automaticDownloadSmoke() {
  const auto architecture = mac_encoder::currentArchitecture();
  const auto other = architecture == Architecture::intel64
                         ? Architecture::appleSilicon : Architecture::intel64;
  const auto asset = mac_encoder::downloadAsset(architecture);
  const auto managed = AssetBootstrapManager::getEncoderBinaryPath();
  expect(managed.isAChildOf(isolatedTestRoot()),
         "the live bootstrap smoke uses exclusively isolated app storage");
  write(managed, thin(other));
  expect(managed.setExecutePermission(true),
         "the smoke cache is unavailable because of architecture, not permissions");
  expect(AssetBootstrapManager::getAvailableEncoderBinary() == juce::File{},
         "the incompatible cached encoder is unavailable before automatic repair");

  juce::String error;
  std::vector<double> progress;
  std::vector<juce::String> phases;
  bool downloadingObserved = false;
  const bool ready = AssetBootstrapManager::ensureEncoderReady(
      &error, [&](double value, const juce::String &phase) {
        expect(value >= 0.0 && value <= 1.0,
               "real bootstrap progress stays within its documented range");
        expect(progress.empty() || value >= progress.back(),
               "real bootstrap progress remains monotonic");
        if (phase == "Downloading encoder") {
          downloadingObserved = true;
          expect(value < 1.0 &&
                     AppSettings::getAssetsBootstrapState().encoderStatus == "Downloading",
                 "the download phase remains incomplete until native installation succeeds");
        }
        progress.push_back(value);
        phases.push_back(phase);
      });
  if (!ready)
    throw std::runtime_error("Automatic encoder download failed: " + error.toStdString());
  expect(error.isEmpty() && downloadingObserved && !progress.empty() &&
             progress.back() == 1.0 && phases.back() == "Encoder ready",
         "automatic repair reaches native encoder ready after actual download progress");
  const auto state = AppSettings::getAssetsBootstrapState();
  expect(state.encoderStatus == "Ready" && state.encoderVersion == asset.version &&
             state.lastError.isEmpty(),
         "the ready state persists the descriptor version and clears earlier failures");
  expect(mac_encoder::verifyDownload(managed, asset, error),
         "the downloaded managed encoder matches its native architecture, size and SHA256 pin");

  juce::StringArray command;
  command.add(managed.getFullPathName());
  command.add("-version");
  juce::ChildProcess process;
  expect(process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr),
         "the actual downloaded native encoder can be launched");
  if (!process.waitForProcessToFinish(15000)) {
    process.kill();
    throw std::runtime_error("The downloaded encoder version probe timed out");
  }
  expect(process.getExitCode() == 0 &&
             process.readAllProcessOutput().startsWith("ffmpeg version"),
         "the downloaded native encoder executes successfully and identifies itself");

  const auto modified = managed.getLastModificationTime();
  progress.clear();
  phases.clear();
  expect(AssetBootstrapManager::ensureEncoderReady(
             &error, [&](double value, const juce::String &phase) {
               progress.push_back(value);
               phases.push_back(phase);
             }) && error.isEmpty() && progress == std::vector<double>{1.0} &&
             phases == std::vector<juce::String>{"Encoder ready"} &&
             managed.getLastModificationTime() == modified,
         "a second bootstrap reuses the repaired managed encoder without downloading again");
  std::cout << "PASS: automatic bootstrap architecture="
            << (architecture == Architecture::intel64 ? "x86_64" : "arm64")
            << " asset=" << asset.relativePath
            << " state=" << state.encoderStatus
            << " version=" << state.encoderVersion
            << " native_execution=passed cache_reuse=passed\n";
}
#endif
} // namespace

int main(int argc, char **argv) {
  try {
#if JUCE_MAC
    if (argc == 2 && juce::String::fromUTF8(argv[1]) == "--download-encoder") {
      automaticDownloadSmoke();
      return 0;
    }
#endif
    expect(argc == 1, "Usage: MacEncoderTests [--download-encoder]");
    headerContracts();
    selectionAndInstallation();
    downloadAssetSelection();
    verifiedDownloadInstallation();
    std::cout << "PASS: Mac encoder architecture, pinned download selection, cache repair and atomic installation contracts\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#if !JUCE_WINDOWS
#include <unistd.h>
#endif

namespace mac_encoder {

enum class Architecture { intel64, appleSilicon };

namespace detail {
inline std::uint32_t read32(const std::uint8_t *bytes, bool littleEndian) {
  std::uint32_t result = 0;
  for (int i = 0; i < 4; ++i)
    result = (result << 8) | bytes[littleEndian ? 3 - i : i];
  return result;
}

inline std::uint64_t read64(const std::uint8_t *bytes, bool littleEndian) {
  std::uint64_t result = 0;
  for (int i = 0; i < 8; ++i)
    result = (result << 8) | bytes[littleEndian ? 7 - i : i];
  return result;
}

inline bool readAt(juce::InputStream &stream, std::uint64_t position,
                   void *bytes, int count) {
  return stream.setPosition(static_cast<juce::int64>(position)) &&
         stream.read(bytes, count) == count;
}

inline std::uint32_t cpuType(Architecture architecture) {
  return architecture == Architecture::intel64 ? 0x01000007u : 0x0100000cu;
}

inline bool thinExecutable(juce::InputStream &stream, std::uint64_t offset,
                           std::uint64_t length, Architecture architecture) {
  std::array<std::uint8_t, 32> header{};
  if (length < header.size() || !readAt(stream, offset, header.data(), int(header.size())))
    return false;
  const auto magic = read32(header.data(), false);
  if (magic != 0xcffaedfeu && magic != 0xfeedfacfu)
    return false;
  const bool littleEndian = magic == 0xcffaedfeu;
  const auto commandBytes = read32(header.data() + 20, littleEndian);
  return read32(header.data() + 4, littleEndian) == cpuType(architecture) &&
         read32(header.data() + 12, littleEndian) == 2u &&
         commandBytes <= length - header.size() &&
         read32(header.data() + 16, littleEndian) <= commandBytes / 8;
}
} // namespace detail

// Inspect architecture headers without launching a subprocess. This does not
// certify the encoder's instruction set, signature or dynamic dependencies.
inline bool supportsArchitecture(juce::InputStream &stream, Architecture architecture) {
  const auto streamLength = stream.getTotalLength();
  if (streamLength < 32)
    return false;
  const auto length = static_cast<std::uint64_t>(streamLength);
  std::array<std::uint8_t, 8> header{};
  if (!detail::readAt(stream, 0, header.data(), int(header.size())))
    return false;
  const auto magic = detail::read32(header.data(), false);
  if (magic == 0xcffaedfeu || magic == 0xfeedfacfu)
    return detail::thinExecutable(stream, 0, length, architecture);
  const bool wide = magic == 0xcafebabfu || magic == 0xbfbafecau;
  const bool littleEndian = magic == 0xbebafecau || magic == 0xbfbafecau;
  if (!wide && magic != 0xcafebabeu && magic != 0xbebafecau)
    return false;
  const auto count = detail::read32(header.data() + 4, littleEndian);
  const std::uint64_t entrySize = wide ? 32u : 20u;
  const auto tableEnd = 8u + std::uint64_t(count) * entrySize;
  if (count == 0 || count > 64 || tableEnd > length)
    return false;

  bool found = false;
  for (std::uint32_t i = 0; i < count; ++i) {
    std::array<std::uint8_t, 32> entry{};
    if (!detail::readAt(stream, 8u + std::uint64_t(i) * entrySize,
                        entry.data(), int(entrySize)))
      return false;
    const auto offset = wide ? detail::read64(entry.data() + 8, littleEndian)
                             : detail::read32(entry.data() + 8, littleEndian);
    const auto sliceLength = wide ? detail::read64(entry.data() + 16, littleEndian)
                                  : detail::read32(entry.data() + 12, littleEndian);
    if (offset < tableEnd || offset > length || sliceLength < 32 ||
        sliceLength > length - offset)
      return false;
    if (detail::read32(entry.data(), littleEndian) == detail::cpuType(architecture)) {
      if (!detail::thinExecutable(stream, offset, sliceLength, architecture))
        return false;
      found = true;
    }
  }
  return found;
}

inline bool supportsArchitecture(const juce::File &file, Architecture architecture) {
  auto stream = file.createInputStream();
  return stream != nullptr && supportsArchitecture(*stream, architecture);
}

inline bool hasExecuteAccess(const juce::File &file) {
#if JUCE_WINDOWS
  return file.existsAsFile();
#else
  return file.existsAsFile() &&
         ::access(file.getFullPathName().toRawUTF8(), X_OK) == 0;
#endif
}

inline juce::File firstCompatible(std::initializer_list<juce::File> candidates,
                                  Architecture architecture) {
  for (const auto &candidate : candidates)
    if (hasExecuteAccess(candidate) && supportsArchitecture(candidate, architecture))
      return candidate;
  return {};
}

inline bool installCompatibleDownload(const juce::File &temporary,
                                      const juce::File &destination,
                                      Architecture architecture,
                                      juce::String &error) {
  if (temporary == destination) {
    error = "The encoder download must use a separate temporary file.";
    return false;
  }
  if (!supportsArchitecture(temporary, architecture)) {
    error = "The downloaded audio encoder is incompatible with this Mac's architecture.";
    temporary.deleteFile();
    return false;
  }
  bool installed = false;
  if (temporary.setExecutePermission(true)) {
#if JUCE_MAC
    // JUCE's replacement can fall back to copying over the destination after
    // rename fails. Keep the existing encoder intact on every rename error.
    installed = std::rename(temporary.getFullPathName().toRawUTF8(),
                            destination.getFullPathName().toRawUTF8()) == 0;
#else
    installed = temporary.replaceFileIn(destination);
#endif
  }
  if (!installed) {
    error = "Couldn't install the downloaded audio encoder.";
    temporary.deleteFile();
    return false;
  }
  return true;
}

#if JUCE_MAC
inline Architecture currentArchitecture() {
#if JUCE_ARM
  return Architecture::appleSilicon;
#else
  return Architecture::intel64;
#endif
}
#endif

} // namespace mac_encoder

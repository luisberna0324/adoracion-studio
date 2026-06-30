#include "UpdateCrypto.h"

#include <array>

namespace PlayWorshipUpdate {
namespace {

constexpr std::array<uint32_t, 64> k = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

uint32_t rotr(const uint32_t value, const uint32_t bits) noexcept {
  return (value >> bits) | (value << (32u - bits));
}

class Sha256 {
public:
  void update(const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    totalBytes += static_cast<uint64_t>(size);
    while (size > 0) {
      const auto take = juce::jmin<size_t>(size, block.size() - blockUsed);
      std::memcpy(block.data() + blockUsed, bytes, take);
      blockUsed += take;
      bytes += take;
      size -= take;
      if (blockUsed == block.size()) {
        processBlock(block.data());
        blockUsed = 0;
      }
    }
  }

  juce::String finalHex() {
    const uint64_t totalBits = totalBytes * 8u;
    uint8_t marker = 0x80u;
    update(&marker, 1);

    uint8_t zero = 0;
    while (blockUsed != 56)
      update(&zero, 1);

    std::array<uint8_t, 8> length{};
    for (int i = 7; i >= 0; --i)
      length[static_cast<size_t>(7 - i)] =
          static_cast<uint8_t>((totalBits >> (i * 8)) & 0xffu);
    update(length.data(), length.size());

    std::array<uint8_t, 32> digest{};
    for (size_t i = 0; i < state.size(); ++i) {
      digest[i * 4 + 0] = static_cast<uint8_t>((state[i] >> 24) & 0xffu);
      digest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xffu);
      digest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xffu);
      digest[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xffu);
    }
    return juce::String::toHexString(digest.data(),
                                     static_cast<int>(digest.size()), 0)
        .toLowerCase();
  }

private:
  void processBlock(const uint8_t *chunk) {
    std::array<uint32_t, 64> w{};
    for (size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(chunk[i * 4 + 0]) << 24) |
             (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
             static_cast<uint32_t>(chunk[i * 4 + 3]);
    }
    for (size_t i = 16; i < 64; ++i) {
      const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    auto a = state[0], b = state[1], c = state[2], d = state[3];
    auto e = state[4], f = state[5], g = state[6], h = state[7];
    for (size_t i = 0; i < 64; ++i) {
      const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const auto ch = (e & f) ^ (~e & g);
      const auto temp1 = h + s1 + ch + k[i] + w[i];
      const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const auto maj = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }

  std::array<uint32_t, 8> state = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                   0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                   0x1f83d9abu, 0x5be0cd19u};
  std::array<uint8_t, 64> block{};
  size_t blockUsed = 0;
  uint64_t totalBytes = 0;
};

} // namespace

juce::String sha256ForFile(const juce::File &file) {
  auto input = file.createInputStream();
  if (input == nullptr)
    return {};

  Sha256 sha;
  juce::HeapBlock<char> buffer(1024 * 256);
  for (;;) {
    const int read = input->read(buffer.getData(), 1024 * 256);
    if (read <= 0)
      break;
    sha.update(buffer.getData(), static_cast<size_t>(read));
  }
  return sha.finalHex();
}

bool constantTimeEqualsIgnoreCase(const juce::String &a, const juce::String &b) {
  const auto lhs = a.trim().toLowerCase();
  const auto rhs = b.trim().toLowerCase();
  if (lhs.length() != rhs.length())
    return false;

  int diff = 0;
  for (int i = 0; i < lhs.length(); ++i)
    diff |= lhs[i] ^ rhs[i];
  return diff == 0;
}

} // namespace PlayWorshipUpdate

#pragma once
#include <cstdint>
#include <string>
// UI intent only: never retains Apple proof, email, token or account lookup results.
class AppleLinkFlow {
public:
  enum class Stage { idle, offer, authenticate, confirm, linking };
  Stage stage = Stage::idle;
  uint64_t generation = 0;
  int64_t expiresAt = 0;
  std::string uid;
  void cancel() { ++generation; stage = Stage::idle; expiresAt = 0; uid.clear(); }
  void begin(int64_t now) { cancel(); stage = Stage::offer; expiresAt = now + 300000; }
  bool valid(int64_t now) const { return stage != Stage::idle && now < expiresAt; }
  bool choose(int64_t now) { if (!valid(now) || stage != Stage::offer) return false; ++generation; stage = Stage::authenticate; return true; }
  bool authenticated(const std::string& account, int64_t now) {
    if (!valid(now) || stage != Stage::authenticate || account.empty()) return false;
    ++generation; uid = account; stage = Stage::confirm; return true;
  }
  bool confirm(const std::string& account, int64_t now) {
    if (!valid(now) || stage != Stage::confirm || uid != account) return false;
    ++generation; stage = Stage::linking; return true;
  }
};

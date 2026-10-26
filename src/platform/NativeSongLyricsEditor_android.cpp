#include "NativeSongLyricsEditor.h"

#if JUCE_ANDROID
#include "NativeSongLyricsEditing.h"
#include <jni.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/native/juce_JNIHelpers_android.h>
#include <atomic>
#include <map>
#include <mutex>

namespace NativeSongLyricsEditor {
namespace {
struct Presentation {
  juce::GlobalRef activity;
  juce::Component::SafePointer<juce::Component> owner;
  Save save;
  std::function<void()> close;
  std::atomic<bool> cancelled{false}, saving{false};
  explicit Presentation(juce::Component &component) : owner(&component) {}
};
std::mutex presentationsMutex;
std::map<jlong, std::shared_ptr<Presentation>> presentations;
std::atomic<jlong> nextID{1};

bool readString(JNIEnv *env, jstring value, juce::String &out) {
  if (value == nullptr) return false;
  const auto size = env->GetStringLength(value);
  const auto *chars = env->GetStringChars(value, nullptr);
  if (chars == nullptr) return false;
  bool valid = true;
  for (jsize i = 0; i < size; ++i) {
    if (chars[i] == 0 || (chars[i] >= 0xdc00 && chars[i] <= 0xdfff)) { valid = false; break; }
    if (chars[i] >= 0xd800 && chars[i] <= 0xdbff) {
      if (++i >= size || chars[i] < 0xdc00 || chars[i] > 0xdfff) { valid = false; break; }
    }
  }
  if (valid) {
    const auto *first = reinterpret_cast<const juce::CharPointer_UTF16::CharType *>(chars);
    out = juce::String(juce::CharPointer_UTF16(first), juce::CharPointer_UTF16(first + size));
  }
  env->ReleaseStringChars(value, chars);
  return valid;
}
jstring writeString(JNIEnv *env, const juce::String &value) {
  return env->NewString(reinterpret_cast<const jchar *>(value.toUTF16().getAddress()),
                        static_cast<jsize>(songLyricsUtf16Length(value)));
}
std::shared_ptr<Presentation> lookup(jlong id) {
  std::lock_guard<std::mutex> lock(presentationsMutex);
  const auto found = presentations.find(id);
  return found != presentations.end() ? found->second : nullptr;
}
bool callActivity(const std::shared_ptr<Presentation> &state, const char *name,
                  const char *signature, jlong id, const juce::String *first = nullptr,
                  const juce::String *second = nullptr, bool success = false) {
  auto *env = juce::getEnv();
  if (state->activity.get() == nullptr) return false;
  auto cls = env->GetObjectClass(state->activity.get());
  if (cls == nullptr) return false;
  auto method = env->GetMethodID(cls, name, signature);
  if (method == nullptr) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    return false;
  }
  jstring a = first != nullptr ? writeString(env, *first) : nullptr;
  jstring b = second != nullptr ? writeString(env, *second) : nullptr;
  if (second != nullptr) env->CallVoidMethod(state->activity.get(), method, id, a, b);
  else if (first != nullptr) env->CallVoidMethod(state->activity.get(), method, id, static_cast<jboolean>(success), a);
  else env->CallVoidMethod(state->activity.get(), method, id);
  const bool ok = !env->ExceptionCheck();
  if (!ok) env->ExceptionClear();
  if (a != nullptr) env->DeleteLocalRef(a);
  if (b != nullptr) env->DeleteLocalRef(b);
  env->DeleteLocalRef(cls);
  return ok;
}
void cancelPresentation(jlong id, const std::shared_ptr<Presentation> &state) {
  if (state->cancelled.exchange(true)) return;
  {
    std::lock_guard<std::mutex> lock(presentationsMutex);
    presentations.erase(id);
  }
  callActivity(state, "dismissNativeLyricsEditor", "(J)V", id);
}
class AndroidLyricsHandle final : public Handle {
public:
  AndroidLyricsHandle(jlong value, std::shared_ptr<Presentation> presentation)
      : id(value), state(std::move(presentation)) {}
  ~AndroidLyricsHandle() override { cancel(); }
  void cancel() override { if (state) cancelPresentation(id, state); }
private:
  jlong id;
  std::shared_ptr<Presentation> state;
};
juce::var transformError() {
  auto *result = new juce::DynamicObject;
  result->setProperty("error", "Invalid lyrics document or edit");
  return juce::var(result);
}
bool integerProperty(const juce::var &value, const char *name, int &out) {
  const auto property = value.getProperty(name, {});
  if (!(property.isInt() || property.isInt64())) return false;
  const auto number = static_cast<juce::int64>(property);
  if (number < 0 || number > kSongLyricsMaxUtf16Length) return false;
  out = static_cast<int>(number);
  return true;
}
} // namespace

bool isAvailable() { return true; }
std::unique_ptr<Handle> present(juce::Component &owner, Options options, Save save,
                               std::function<void()> close) {
  if (!save || (options.document && !isValidSongLyricsDocument(*options.document))) return {};
  auto activity = juce::getMainActivity();
  if (activity.get() == nullptr) return {};
  auto state = std::make_shared<Presentation>(owner);
  state->activity = juce::GlobalRef(activity);
  state->save = std::move(save); state->close = std::move(close);
  const jlong id = nextID.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(presentationsMutex);
    presentations.emplace(id, state);
  }
  const auto config = juce::JSON::toString(presentationConfig(options), true);
  const auto document = juce::JSON::toString(songLyricsToVar(options.document.value_or(SongLyricsDocument{})), true);
  if (!callActivity(state, "showNativeLyricsEditor", "(JLjava/lang/String;Ljava/lang/String;)V", id, &config, &document)) {
    cancelPresentation(id, state);
    return {};
  }
  return std::make_unique<AndroidLyricsHandle>(id, std::move(state));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_rmsl_juce_NativeSongLyricsEditor_nativeIsActive(JNIEnv *, jclass, jlong id) {
  const auto state = lookup(id);
  return state && !state->cancelled.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_rmsl_juce_NativeSongLyricsEditor_nativeTransform(JNIEnv *env, jclass,
                                                       jstring encoded, jstring operation) {
  juce::String documentJSON, operationJSON;
  auto error = [&] { return writeString(env, juce::JSON::toString(transformError(), true)); };
  if (!readString(env, encoded, documentJSON) || !readString(env, operation, operationJSON)) return error();
  auto document = songLyricsFromVar(juce::JSON::parse(documentJSON));
  const auto op = juce::JSON::parse(operationJSON);
  if (!document || op.getDynamicObject() == nullptr) return error();
  const auto action = op.getProperty("action", {}).toString();
  bool valid = false;
  int start = 0, length = 0;
  if (action == "labels") valid = true;
  else if (action == "setText" && op.getProperty("text", {}).isString())
    valid = NativeSongLyricsEditing::setText(*document, op.getProperty("text", {}).toString());
  else if (integerProperty(op, "start", start) && integerProperty(op, "length", length)) {
    if (action == "replace" && op.getProperty("text", {}).isString())
      valid = NativeSongLyricsEditing::replaceText(*document, start, length, op.getProperty("text", {}).toString());
    else if (action == "section") {
      const auto paragraphs = NativeSongLyricsEditing::enclosingParagraphs(document->text, start, length);
      valid = paragraphs.valid() && applySongLyricsSection(*document, paragraphs.start, paragraphs.length,
                                                           op.getProperty("kind", {}).toString());
    } else if (action == "clear") valid = clearSongLyricsSections(*document, start, length);
  }
  if (!valid) return error();
  auto *result = new juce::DynamicObject;
  result->setProperty("document", songLyricsToVar(*document));
  auto labels = sectionLabels(*document);
  std::map<juce::String, bool> completeBlocks;
  const auto completion = NativeSongLyricsEditing::completeParagraphsForSections(*document);
  for (size_t i = 0; i < document->sections.size(); ++i)
    completeBlocks.emplace(document->sections[i].id, completion[i]);
  if (auto *entries = labels.getArray()) {
    for (auto &entry : *entries) {
      auto *label = entry.getDynamicObject();
      if (label == nullptr) continue;
      const auto found = completeBlocks.find(label->getProperty("id").toString());
      label->setProperty("completeBlock", found != completeBlocks.end() && found->second);
    }
  }
  result->setProperty("labels", labels);
  return writeString(env, juce::JSON::toString(juce::var(result), true));
}

extern "C" JNIEXPORT void JNICALL
Java_com_rmsl_juce_NativeSongLyricsEditor_nativeSave(JNIEnv *env, jclass, jlong id, jstring encoded) {
  const auto state = lookup(id);
  if (!state || state->cancelled.load() || state->saving.exchange(true)) return;
  juce::String json;
  const auto document = readString(env, encoded, json) ? songLyricsFromVar(juce::JSON::parse(json)) : std::nullopt;
  if (!document) {
    state->saving.store(false);
    const juce::String error("Invalid lyrics document");
    callActivity(state, "completeNativeLyricsSave", "(JZLjava/lang/String;)V", id, &error, nullptr, false);
    return;
  }
  juce::MessageManager::callAsync([state, id, document] {
    if (state->cancelled.load() || state->owner == nullptr) return;
    auto completed = std::make_shared<std::atomic<bool>>(false);
    auto completion = [state, id, completed](bool success, juce::String error) {
      if (completed->exchange(true)) return;
      juce::MessageManager::callAsync([state, id, success, error = std::move(error)] {
        if (state->cancelled.load() || state->owner == nullptr) return;
        state->saving.store(false);
        callActivity(state, "completeNativeLyricsSave", "(JZLjava/lang/String;)V", id, &error, nullptr, success);
      });
    };
    try {
      state->save(hasSongLyrics(*document) ? document : std::nullopt, completion);
    } catch (...) {
      completion(false, "Couldn't save the lyrics. Please try again.");
    }
  });
}

extern "C" JNIEXPORT void JNICALL
Java_com_rmsl_juce_NativeSongLyricsEditor_nativeClose(JNIEnv *, jclass, jlong id) {
  const auto state = lookup(id);
  if (!state) return;
  juce::MessageManager::callAsync([state, id] {
    if (state->cancelled.exchange(true)) return;
    {
      std::lock_guard<std::mutex> lock(presentationsMutex);
      presentations.erase(id);
    }
    if (state->owner != nullptr && state->close) state->close();
  });
}
} // namespace NativeSongLyricsEditor
#endif

#include "NativeAuthSession.h"

#if JUCE_ANDROID
#include <mutex>
#include <jni.h>
#include <android/log.h>
#endif

namespace NativeAuthSession {

#if JUCE_ANDROID
static std::mutex callbackMutex;
static Completion pendingCompletion;

extern "C" JNIEXPORT void JNICALL
Java_com_rmsl_juce_JuceActivity_onAuthCallback(JNIEnv* env, jobject,
                                                jstring callbackUrl) {
  const char* chars = env->GetStringUTFChars(callbackUrl, nullptr);
  const juce::String url(chars != nullptr ? chars : "");
  if (chars != nullptr) env->ReleaseStringUTFChars(callbackUrl, chars);
  Completion completion;
  { std::lock_guard<std::mutex> lock(callbackMutex); completion = std::move(pendingCompletion); }
  if (completion)
    juce::MessageManager::callAsync([completion = std::move(completion), url]() mutable { completion(url, {}); });
}
#endif

bool start(const juce::String &loginUrl, const juce::String &, Completion completion) {
#if JUCE_ANDROID
  { std::lock_guard<std::mutex> lock(callbackMutex); pendingCompletion = std::move(completion); }
  return juce::URL(loginUrl).launchInDefaultBrowser();
#else
  juce::ignoreUnused(loginUrl, completion);
  return false;
#endif
}

void cancel() {
#if JUCE_ANDROID
  std::lock_guard<std::mutex> lock(callbackMutex); pendingCompletion = {};
#endif
}

} // namespace NativeAuthSession

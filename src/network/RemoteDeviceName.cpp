#include "RemoteDeviceName.h"

#if JUCE_ANDROID
#include <jni.h>
#include <juce_core/native/juce_JNIHelpers_android.h>
#endif

namespace playworship::remote {

juce::String getRemoteDeviceName() {
#if JUCE_ANDROID
  auto activity = juce::getCurrentActivity();
  if (activity.get() != nullptr) {
    auto *env = juce::getEnv();
    auto activityClass = env->GetObjectClass(activity.get());
    if (activityClass != nullptr) {
      const auto method = env->GetMethodID(activityClass, "getRemoteDeviceName",
                                           "()Ljava/lang/String;");
      if (method != nullptr) {
        auto javaName = static_cast<jstring>(
            env->CallObjectMethod(activity.get(), method));
        if (!env->ExceptionCheck() && javaName != nullptr) {
          const char *utf8 = env->GetStringUTFChars(javaName, nullptr);
          const juce::String name(utf8 != nullptr ? utf8 : "");
          if (utf8 != nullptr)
            env->ReleaseStringUTFChars(javaName, utf8);
          if (env->ExceptionCheck())
            env->ExceptionClear();
          if (name.trim().isNotEmpty()) {
            env->DeleteLocalRef(javaName);
            env->DeleteLocalRef(activityClass);
            return name;
          }
        }
        if (env->ExceptionCheck())
          env->ExceptionClear();
        if (javaName != nullptr)
          env->DeleteLocalRef(javaName);
      } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
      }
      env->DeleteLocalRef(activityClass);
    }
  }
#endif
  auto name = juce::SystemStats::getComputerName().trim();
  if (name.isEmpty() || name.equalsIgnoreCase("localhost") ||
      name.startsWithIgnoreCase("localhost.")) {
#if JUCE_ANDROID
    return "Android";
#elif JUCE_WINDOWS
    return "Windows PC";
#else
    return "Computer";
#endif
  }
  return name;
}

} // namespace playworship::remote

#pragma once

#include <functional>

class SettingsPanel;

namespace NativeIOSSettings {
bool show(SettingsPanel &panel, std::function<void()> onClose);
void dismiss();
void refresh();
}

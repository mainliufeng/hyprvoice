#pragma once
#include <map>
#include <string>

namespace hv {
inline std::string SceneLabel(const std::string &scene) {
  static const std::map<std::string, std::string> labels = {
      {"raw", "听写"},
      {"correct", "纠错"},
      {"format", "整理"},
      {"translate", "翻译"}};
  auto found = labels.find(scene);
  return found == labels.end() ? scene : found->second;
}

inline std::string RecordingHint(bool command_mode, bool pressed) {
  // Bindings belong to the compositor, so their physical keys are unknown.
  if (command_mode)
    return "松开指令快捷键结束";
  return pressed ? "松开录音快捷键结束" : "再按录音快捷键结束";
}
} // namespace hv

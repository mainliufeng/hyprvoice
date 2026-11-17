#pragma once
#include <sherpa-onnx/c-api/c-api.h>
#include <stdexcept>
#include <string_view>

namespace hv {
class SherpaRuntimeMismatch : public std::runtime_error {
public:
  SherpaRuntimeMismatch()
      : std::runtime_error(
            "本地语音库版本已变化，请重新构建并安装 HyprVoice 后重启") {}
};
// Empty when the loaded library does not expose a version string.
inline std::string_view SherpaRuntimeVersion() {
  const char *version = SherpaOnnxGetVersionStr();
  return version ? std::string_view(version) : std::string_view();
}
inline bool SherpaRuntimeCompatible() {
  return SherpaRuntimeVersion() == HYPRVOICE_SHERPA_VERSION;
}
inline void RequireSherpaRuntime() {
  if (!SherpaRuntimeCompatible())
    throw SherpaRuntimeMismatch();
}
} // namespace hv

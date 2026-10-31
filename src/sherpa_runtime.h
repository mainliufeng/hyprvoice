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
inline void RequireSherpaRuntime() {
  const char *version = SherpaOnnxGetVersionStr();
  if (!version || std::string_view(version) != HYPRVOICE_SHERPA_VERSION)
    throw SherpaRuntimeMismatch();
}
} // namespace hv

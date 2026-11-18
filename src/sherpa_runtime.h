#pragma once
#include <sherpa-onnx/c-api/c-api.h>
#include <stdexcept>
#include <string>
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
// Why an installed binary can stop working after a system upgrade; empty when
// the build-time and loaded library versions agree. Shared so `--version`, the
// diagnostics text and the startup refusal name the same cause and fix.
inline std::string SherpaVersionNote(std::string_view build,
                                     std::string_view runtime) {
  if (build == runtime)
    return {};
  return "语音库：与构建时不一致（构建 " +
         std::string(build.empty() ? "未知" : build) + "，当前 " +
         std::string(runtime.empty() ? "unknown" : runtime) +
         "）；请重新构建并安装后重启，只重启程序不会修复";
}
inline bool SherpaRuntimeCompatible() {
  return SherpaRuntimeVersion() == HYPRVOICE_SHERPA_VERSION;
}
inline void RequireSherpaRuntime() {
  if (!SherpaRuntimeCompatible())
    throw SherpaRuntimeMismatch();
}
} // namespace hv

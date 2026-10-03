#include "config.h"
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
namespace hv {
std::string ExpandPath(std::string s) {
  if (s.starts_with("~/")) {
    const char *home = std::getenv("HOME");
    if (!home)
      throw std::runtime_error("HOME is unset");
    s.replace(0, 1, home);
  }
  return s;
}
std::filesystem::path ConfigPath() {
  if (auto p = std::getenv("HYPRVOICE_CONFIG"))
    return p;
  if (auto p = std::getenv("XDG_CONFIG_HOME"))
    return std::filesystem::path(p) / "hyprvoice/config.json";
  return ExpandPath("~/.config/hyprvoice/config.json");
}
std::filesystem::path RuntimePath() {
  auto p = std::getenv("XDG_RUNTIME_DIR");
  if (!p || !*p)
    throw std::runtime_error("XDG_RUNTIME_DIR is required");
  return std::filesystem::path(p) / "hyprvoice";
}
std::string ReadFile(const std::filesystem::path &path, size_t limit) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot read " + path.string());
  std::string result;
  char buf[4096];
  while (f.read(buf, sizeof(buf)) || f.gcount()) {
    result.append(buf, f.gcount());
    if (result.size() > limit)
      throw std::runtime_error("File exceeds size limit: " + path.string());
  }
  if (!f.eof())
    throw std::runtime_error("Read failed: " + path.string());
  return result;
}
Json DefaultConfig() {
  return {{"streaming_model",
           "~/.local/share/vinput/models/sherpa-onnx/"
           "x-asr-960ms-streaming-zipformer-transducer-zh-en-punct-int8"},
          {"offline_model", "~/.local/share/vinput/models/sherpa-onnx/"
                            "x-asr-zipformer-transducer-zh-en-punct-int8"},
          {"vad_model", "~/.local/share/hyprvoice/silero_vad.onnx"},
          {"hotwords", ""},
          {"audio_source", ""},
          {"gain", 1.0},
          {"max_recording_seconds", 180},
          {"auto_commit", true},
          {"scene", "raw"},
          {"clipboard_restore", false},
          {"terminal_classes",
           {"kitty", "Alacritty", "org.wezfurlong.wezterm",
            "com.mitchellh.ghostty", "foot"}},
          {"llm",
           {{"base_url", "https://api.openai.com/v1"},
            {"model", ""},
            {"api_key_env", "HYPRVOICE_API_KEY"},
            {"timeout_seconds", 15},
            {"allow_http", false}}},
          {"prompts",
           {{"correct", "纠正转录错误并补充标点。保留原意、否定、数字、专有名词"
                        "，不添加事实。只返回修改后的文字。"},
            {"format", "整理口述文字，去除无意义重复，适当分段。保留所有事实、"
                       "数字和否定。只返回整理后的文字。"},
            {"translate",
             "将输入翻译成英文，保留事实、数字与专有名词。只返回译文。"}}}};
}
Config::Config(const std::filesystem::path &p) : data(DefaultConfig()) {
  if (!std::filesystem::exists(p))
    throw std::runtime_error("Config missing: " + p.string() +
                             "; run hyprvoice init");
  data.merge_patch(Json::parse(ReadFile(p)));
  if (data.value("gain", 1.0) <= 0 || data.value("gain", 1.0) > 8)
    throw std::runtime_error("gain must be in (0,8]");
  int limit = data.value("max_recording_seconds", 180);
  if (limit < 1 || limit > 600)
    throw std::runtime_error("max_recording_seconds must be 1..600");
  auto scene = data.value("scene", std::string("raw"));
  if (scene != "raw" && !data.at("prompts").contains(scene))
    throw std::runtime_error("Unknown scene: " + scene);
}
std::string Config::path(const char *key) const {
  return ExpandPath(data.at(key).get<std::string>());
}
} // namespace hv

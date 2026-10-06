#include "config.h"
#include <cstdlib>
#include <fcntl.h>
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
  return {{"asr", {{"backend", "x-asr"}}},
          {"fun",
           {{"worker", "~/.local/libexec/hyprvoice/fun-worker"},
            {"model_dir", "~/.local/share/hyprvoice/models/fun-asr-nano"},
            {"threads", 8},
            {"timeout_seconds", 120}}},
          {"streaming_model",
           "~/.local/share/hyprvoice/models/"
           "x-asr-960ms-streaming-zipformer-transducer-zh-en-punct-int8"},
          {"offline_model", "~/.local/share/hyprvoice/models/"
                            "x-asr-zipformer-transducer-zh-en-punct-int8"},
          {"vad_model", "~/.local/share/hyprvoice/silero_vad.onnx"},
          {"hotwords", ""},
          {"audio_source", ""},
          {"gain", 1.0},
          {"max_recording_seconds", 180},
          {"auto_commit", true},
          {"scene", "raw"},
          {"clipboard_restore", false},
          {"context", {{"enabled", false}, {"max_chars", 1024}}},
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
           {{"correct", "忠实纠错：只纠正有依据的识别错词并补充标点。保留本次"
                        "措辞、语序、口头重复和语气，不主动删句、改写或总结。"
                        "保留数字、金额、日期、小数、版本号、否定、条件、引用、"
                        "不确定性及专有名词的正确名称，中英混说不擅自翻译。"
                        "上下文不足时保留原词，不猜测或添加事实。只返回本次"
                        "纠错后的文字。"},
            {"format", "主动整理：可去除无意义口头重复，调整语序、标点和分段，"
                       "让口述更清晰；有意义的强调、引用原话不能删改。保留所有"
                       "事实、数字、金额、日期、小数、版本号、否定、条件、"
                       "不确定性及专有名词的正确名称，中英混说不擅自翻译。"
                       "不改变立场，不推断结论，不添加事实；上下文不足时"
                       "保留原词。只返回本次整理后的文字。"},
            {"translate",
             "将输入翻译成英文，保留事实、数字与专有名词。只返回译文。"}}}};
}
Config::Config(const std::filesystem::path &p) : data(DefaultConfig()) {
  if (!std::filesystem::exists(p))
    throw std::runtime_error("Config missing: " + p.string() +
                             "; run hyprvoice init");
  data.merge_patch(Json::parse(ReadFile(p)));
  auto backend = data.at("asr").value("backend", std::string());
  if (backend != "x-asr" && backend != "fun")
    throw std::runtime_error("asr.backend must be x-asr or fun");
  int threads = data.at("fun").value("threads", 8);
  if (threads < 1 || threads > 64)
    throw std::runtime_error("fun.threads must be 1..64");
  int timeout = data.at("fun").value("timeout_seconds", 120);
  if (timeout < 5 || timeout > 600)
    throw std::runtime_error("fun.timeout_seconds must be 5..600");
  if (data.value("gain", 1.0) <= 0 || data.value("gain", 1.0) > 8)
    throw std::runtime_error("gain must be in (0,8]");
  int limit = data.value("max_recording_seconds", 180);
  if (limit < 1 || limit > 600)
    throw std::runtime_error("max_recording_seconds must be 1..600");
  int context_limit = data.at("context").value("max_chars", 1024);
  if (context_limit < 1 || context_limit > 2048)
    throw std::runtime_error("context.max_chars must be 1..2048");
  if (!data.contains("prompts") || !data.at("prompts").is_object())
    throw std::runtime_error("prompts must be an object of scene prompts");
  for (auto &[name, prompt] : data.at("prompts").items()) {
    if (name.empty() || name.find('\0') != std::string::npos)
      throw std::runtime_error(
          "Prompt scene names must be non-empty and contain no NUL");
    if (!prompt.is_string())
      throw std::runtime_error("Prompt values must be strings");
  }
  if (data.contains("scene") && !data.at("scene").is_string())
    throw std::runtime_error("scene must be a string");
  auto scene = data.value("scene", std::string("raw"));
  if (scene.empty() || scene.find('\0') != std::string::npos)
    throw std::runtime_error("scene must be non-empty and contain no NUL");
  if (scene != "raw" && !data.at("prompts").contains(scene))
    throw std::runtime_error("Unknown scene: " + scene);
}
std::string Config::path(const char *key) const {
  return ExpandPath(data.at(key).get<std::string>());
}
void SaveBackend(const std::string &backend) {
  auto path = ConfigPath();
  auto data = Json::parse(ReadFile(path));
  data["asr"]["backend"] = backend;
  auto content = data.dump(2) + '\n';
  auto temporary = path.string() + ".XXXXXX";
  int fd = mkstemp(temporary.data());
  if (fd < 0)
    throw std::runtime_error("Cannot save recognition backend");
  try {
    size_t pos = 0;
    while (pos < content.size()) {
      auto n = write(fd, content.data() + pos, content.size() - pos);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error("Cannot save recognition backend");
      pos += n;
    }
    if (fsync(fd) < 0 || rename(temporary.c_str(), path.c_str()) < 0)
      throw std::runtime_error("Cannot save recognition backend");
    close(fd);
  } catch (...) {
    close(fd);
    unlink(temporary.c_str());
    throw;
  }
}
} // namespace hv

#include "diagnostics.h"
#include "process.h"
#include <cstdlib>
#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
namespace hv {
Json LocalDiagnostics(const Config &config, bool recognizer_loaded) {
  const auto &data = config.data;
  Json result = {{"network_tested", false},
                 {"audio_tested", false},
                 {"recognizer_loaded", recognizer_loaded},
                 {"backend", data.at("asr").at("backend")}};
  Json dependencies = Json::object();
  for (const char *program : {"hyprctl", "wl-copy", "wl-paste", "pw-dump"}) {
    auto found = g_find_program_in_path(program);
    dependencies[program] = found != nullptr;
    g_free(found);
  }
  result["dependencies"] = dependencies;
  bool assets = true;
  try {
    if (data.at("asr").at("backend") == "fun") {
      auto worker = ExpandPath(data.at("fun").at("worker"));
      assets = access(worker.c_str(), X_OK) == 0;
      auto models =
          std::filesystem::path(ExpandPath(data.at("fun").at("model_dir")));
      for (const char *name :
           {"funasr-encoder-f16.gguf", "qwen3-0.6b-q8_0.gguf", "fsmn-vad.gguf"})
        assets &= std::filesystem::is_regular_file(models / name);
    } else {
      for (auto key : {"streaming_model", "offline_model"}) {
        auto root = std::filesystem::path(config.path(key));
        auto names =
            std::string(key) == "streaming_model"
                ? std::vector<std::string>{"encoder.int8.onnx", "decoder.onnx",
                                           "joiner.int8.onnx", "tokens.txt",
                                           "bpe.vocab"}
                : std::vector<std::string>{"encoder-epoch-99-avg-1.int8.onnx",
                                           "decoder-epoch-99-avg-1.onnx",
                                           "joiner-epoch-99-avg-1.int8.onnx",
                                           "tokens.txt", "bpe.vocab"};
        for (const auto &name : names)
          assets &= std::filesystem::is_regular_file(root / name);
      }
      assets &= std::filesystem::is_regular_file(config.path("vad_model"));
      auto hotwords = config.path("hotwords");
      assets &= hotwords.empty() || std::filesystem::is_regular_file(hotwords);
    }
  } catch (...) {
    assets = false;
  }
  result["assets_present"] = assets;
  bool text_configured = false;
  try {
    const auto &llm = data.at("llm");
    auto env = llm.at("api_key_env").get<std::string>();
    auto model = llm.at("model").get<std::string>();
    auto url = llm.at("base_url").get<std::string>();
    auto key = std::getenv(env.c_str());
    text_configured =
        !model.empty() && key && *key &&
        (url.starts_with("https://") ||
         (llm.value("allow_http", false) && url.starts_with("http://")));
  } catch (...) {
  }
  result["text_configured"] = text_configured;
  bool current = false;
  try {
    current = ReadFile(config.source_path) == config.source_text;
  } catch (...) {
  }
  result["config_current"] = current;
  struct stat status{};
  bool private_runtime = false;
  try {
    auto path = RuntimePath();
    private_runtime = lstat(path.c_str(), &status) == 0 &&
                      S_ISDIR(status.st_mode) && status.st_uid == getuid() &&
                      (status.st_mode & 077) == 0;
  } catch (...) {
  }
  result["private_runtime"] = private_runtime;
  auto source = data.at("audio_source").get<std::string>();
  result["configured_source"] = source;
  result["audio_graph_readable"] = false;
  result["configured_source_present"] = false;
  result["source_count"] = 0;
  if (dependencies.at("pw-dump") == true) {
    try {
      auto reply = Run({"pw-dump"}, "", 1200);
      if (reply.code == 0) {
        auto graph = Json::parse(reply.out);
        if (graph.is_array()) {
          int sources = 0;
          bool found = false;
          for (const auto &node : graph) {
            if (!node.contains("info") || !node.at("info").contains("props"))
              continue;
            const auto &props = node.at("info").at("props");
            auto media = props.value("media.class", std::string());
            if (!media.starts_with("Audio/Source"))
              continue;
            ++sources;
            found |= !source.empty() &&
                     props.value("node.name", std::string()) == source;
          }
          result["audio_graph_readable"] = true;
          result["configured_source_present"] = found;
          result["source_count"] = sources;
        }
      }
    } catch (...) {
    }
  }
  return result;
}
std::string DiagnosticText(const Json &r) {
  std::string out = r.at("recognizer_loaded") == true
                        ? "本地识别：曾完成当前后端初始化；未做本次识别测试"
                        : "本地识别：当前后端未就绪";
  out += r.at("assets_present") == true
             ? "\n模型文件／执行程序：已发现（不代表识别成功）"
             : "\n模型文件／执行程序：缺少或不可访问";
  out += r.at("text_configured") == true ? "\n文本服务：配置齐全，未联网验证"
                                         : "\n文本服务：配置不完整，未联网验证";
  auto source = r.at("configured_source").get<std::string>();
  out +=
      "\n录音来源：" + (source.empty() ? "系统默认（实际路由未验证）" : source);
  if (r.at("audio_graph_readable") != true)
    out += "\n音源图：未读取（连接或权限未确认）";
  else if (source.empty())
    out += "\n音源图：发现 " + std::to_string(r.at("source_count").get<int>()) +
           " 个来源；尚未试麦";
  else
    out += r.at("configured_source_present") == true
               ? "\n音源图：发现配置来源；尚未试麦"
               : "\n音源图：未发现配置来源；尚未试麦";
  bool all = true;
  for (const auto &value : r.at("dependencies"))
    all &= value == true;
  out += all ? "\n桌面依赖：已发现" : "\n桌面依赖：有缺项";
  out += r.at("private_runtime") == true ? "\n控制目录：当前用户私有"
                                         : "\n控制目录：权限未确认";
  out += r.at("config_current") == true
             ? "\n配置文件：与当前已加载版本一致"
             : "\n配置文件：已变化或不可访问；保存会拒绝覆盖";
  return out;
}
} // namespace hv

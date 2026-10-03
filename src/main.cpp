#include "app.h"
#include "process.h"
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
  using namespace hv;
  try {
    if (argc < 2 || std::string(argv[1]) == "--help") {
      std::cout
          << "hyprvoice init | serve | doctor | transcribe WAV | replay "
             "MANIFEST\n"
          << "hyprvoice start | stop | toggle | press | release | command | "
             "cancel | commit | raw | status | scene NAME | quit\n"
          << "HYPRVOICE_CONFIG overrides ~/.config/hyprvoice/config.json\n";
      return 0;
    }
    std::string cmd = argv[1];
    if (cmd == "init") {
      auto p = ConfigPath();
      if (std::filesystem::exists(p))
        throw std::runtime_error(
            "Config already exists; refusing to overwrite " + p.string());
      std::filesystem::create_directories(p.parent_path());
      std::ofstream f(p);
      f << DefaultConfig().dump(2) << '\n';
      if (!f)
        throw std::runtime_error("Cannot write config");
      std::cout << p << '\n';
      return 0;
    }
    if (cmd == "doctor") {
      Config c(ConfigPath());
      Json report = {{"config", ConfigPath().string()}};
      bool ok = true;
      for (auto key : {"streaming_model", "offline_model"}) {
        auto directory = std::filesystem::path(c.path(key));
        auto names =
            std::string(key) == "streaming_model"
                ? std::vector<std::string>{"encoder.int8.onnx", "decoder.onnx",
                                           "joiner.int8.onnx", "tokens.txt",
                                           "bpe.vocab"}
                : std::vector<std::string>{"encoder-epoch-99-avg-1.int8.onnx",
                                           "decoder-epoch-99-avg-1.onnx",
                                           "joiner-epoch-99-avg-1.int8.onnx",
                                           "tokens.txt", "bpe.vocab"};
        Json missing = Json::array();
        for (auto &name : names)
          if (!std::filesystem::is_regular_file(directory / name))
            missing.push_back(name);
        report[key] = {{"path", directory.string()},
                       {"missing_files", missing}};
        ok &= missing.empty();
      }
      for (auto key : {"vad_model", "hotwords"}) {
        auto path = c.path(key);
        bool exists = std::string(key) == "hotwords" && path.empty()
                          ? true
                          : std::filesystem::is_regular_file(path);
        report[key] = {{"path", path}, {"exists", exists}};
        ok &= exists;
      }
      for (auto program : {"hyprctl", "wl-copy", "wl-paste"}) {
        auto found = g_find_program_in_path(program);
        report[program] = found ? found : "missing";
        ok &= found != nullptr;
        g_free(found);
      }
      report["hyprland"] = Json::parse(Checked({"hyprctl", "version", "-j"}));
      std::cout << report.dump(2) << '\n';
      return ok ? 0 : 1;
    }
    if (cmd == "serve") {
      App app{Config(ConfigPath())};
      app.run();
      return 0;
    }
    if (cmd == "transcribe" || cmd == "replay") {
      if (argc != 3)
        throw std::runtime_error("Expected WAV or JSONL manifest path");
      Config c(ConfigPath());
      Asr asr(c);
      auto transcribe = [&](const std::string &path) {
        auto pcm = ReadWave(path);
        auto begin = g_get_monotonic_time();
        asr.begin();
        for (size_t i = 0; i < pcm.size(); i += 320)
          asr.push(
              std::span(pcm).subspan(i, std::min<size_t>(320, pcm.size() - i)));
        auto end = g_get_monotonic_time();
        auto result = asr.finish();
        return Json{{"text", result.text},
                    {"streaming", result.streaming},
                    {"speech", result.speech},
                    {"finish_ms", (g_get_monotonic_time() - end) / 1000.0},
                    {"total_ms", (g_get_monotonic_time() - begin) / 1000.0}};
      };
      if (cmd == "transcribe")
        std::cout << transcribe(argv[2]).dump() << '\n';
      else {
        std::ifstream f(argv[2]);
        if (!f)
          throw std::runtime_error("Cannot read manifest");
        std::string line;
        bool failed = false;
        while (std::getline(f, line)) {
          if (line.empty())
            continue;
          auto item = Json::parse(line);
          Json result;
          try {
            std::filesystem::path path =
                item.contains("wav") ? item.at("wav").get<std::string>()
                                     : item.at("audio").get<std::string>();
            if (path.is_relative())
              path = std::filesystem::path(argv[2]).parent_path() / path;
            result = transcribe(path.string());
          } catch (const std::exception &e) {
            result = {{"error", e.what()}};
            failed = true;
          }
          result["id"] = item.at("id");
          std::cout << result.dump() << std::endl;
        }
        if (failed)
          return 1;
      }
      return 0;
    }
    auto result = SendCommand(cmd, argc > 2 ? argv[2] : "");
    std::cout << result.dump(2) << '\n';
    return result.value("ok", false) ? 0 : 1;
  } catch (const std::exception &e) {
    std::cerr << "hyprvoice: " << e.what() << '\n';
    return 1;
  }
}

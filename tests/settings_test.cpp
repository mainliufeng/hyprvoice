#include "config.h"
#include "diagnostics.h"
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace hv;
static int checks = 0;
static void check(bool condition, const char *name) {
  if (!condition)
    throw std::runtime_error(name);
  ++checks;
}
int main() {
  auto pattern =
      (std::filesystem::temp_directory_path() / "hv-settings-XXXXXX").string();
  if (!mkdtemp(pattern.data()))
    return 1;
  std::filesystem::path root(pattern), path = root / "config.json";
  try {
    Json initial = {{"asr", {{"backend", "fun"}}},
                    {"scene", "raw"},
                    {"prompts", {{"custom <b>&", "Public synthetic prompt"}}},
                    {"context", {{"enabled", false}, {"max_chars", 123}}},
                    {"llm",
                     {{"legacy_key", "synthetic-private-canary"},
                      {"model", "private-model-canary"},
                      {"base_url", "https://private-url-canary.invalid"},
                      {"api_key_env", "HV_DIAGNOSTIC_TEST_KEY"}}},
                    {"private_other", "synthetic-private-canary"},
                    {"audio_source", "qa-source"},
                    {"fun",
                     {{"worker", (root / "worker").string()},
                      {"model_dir", root.string()}}}};
    std::ofstream(path) << initial.dump();
    Config config(path);
    auto original = config.source_text;
    auto reject = [&](const Json &patch) {
      bool failed = false;
      try {
        SaveSettings(path, original, patch);
      } catch (...) {
        failed = true;
      }
      check(failed && ReadFile(path) == original,
            "rejected patch changed valid config");
    };
    for (const Json &patch :
         std::vector<Json>{Json::array(),
                           Json::object(),
                           {{"auto_commit", 1}},
                           {{"context", {{"enabled", "yes"}}}},
                           {{"context", {{"max_chars", 2048}}}},
                           {{"asr", {{"backend", "bad"}}}},
                           {{"asr", {{"backend", "fun"}, {"threads", 3}}}},
                           {{"scene", "missing"}},
                           {{"scene", nullptr}},
                           {{"llm", {{"api_key_env", "other"}}}},
                           {{"audio_source", "other"}},
                           {{"prompts", {{"new", "prompt"}}}}})
      reject(patch);
    std::atomic<bool> cancel = true;
    bool cancelled = false;
    try {
      SaveSettings(path, original, {{"scene", "correct"}}, &cancel);
    } catch (...) {
      cancelled = true;
    }
    check(cancelled && ReadFile(path) == original, "cancel changed config");
    int lock = open((path.string() + ".settings.lock").c_str(), O_RDWR);
    check(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0,
          "cannot hold test lock");
    reject({{"scene", "correct"}});
    close(lock);
    auto external = original + "\n";
    std::ofstream(path) << external;
    bool conflict = false;
    try {
      SaveSettings(path, original, {{"scene", "correct"}});
    } catch (...) {
      conflict = true;
    }
    check(conflict && ReadFile(path) == external,
          "external edit was overwritten");
    std::ofstream(path) << original;
    chmod(root.c_str(), 0500);
    reject({{"scene", "correct"}});
    chmod(root.c_str(), 0700);
    auto saved = SaveSettings(path, original,
                              {{"scene", "custom <b>&"},
                               {"auto_commit", false},
                               {"context", {{"enabled", true}}},
                               {"asr", {{"backend", "fun"}}}});
    auto document = Json::parse(saved);
    check(Config(path).data.at("scene") == "custom <b>&",
          "custom scene save failed");
    check(document.at("llm") == initial.at("llm") &&
              document.at("private_other") == initial.at("private_other"),
          "unexposed values changed");
    check(document.at("context").at("max_chars") == 123 &&
              document.at("audio_source") == "qa-source",
          "context range or audio route changed");
    struct stat info{};
    stat(path.c_str(), &info);
    check((info.st_mode & 077) == 0, "saved config is not private");
    auto target = root / "target.json";
    std::filesystem::rename(path, target);
    std::filesystem::create_symlink(target, path);
    bool symlink = false;
    try {
      SaveSettings(path, saved, {{"scene", "raw"}});
    } catch (...) {
      symlink = true;
    }
    check(symlink && ReadFile(target) == saved, "symlink config was replaced");
    std::filesystem::remove(path);
    std::filesystem::rename(target, path);
    std::ofstream(root / "worker") << "#!/bin/sh\nexit 1\n";
    chmod((root / "worker").c_str(), 0700);
    for (const char *name :
         {"funasr-encoder-f16.gguf", "qwen3-0.6b-q8_0.gguf", "fsmn-vad.gguf"})
      std::ofstream(root / name);
    // This local helper provides only graph metadata; no audio or network API.
    std::ofstream(root / "pw-dump")
        << "#!/bin/sh\nprintf '%s' "
           "'[{\"info\":{\"props\":{\"media.class\":\"Audio/Source/"
           "Virtual\",\"node.name\":\"qa-source\",\"private\":\"synthetic-"
           "private-canary\"}}}]'\n";
    chmod((root / "pw-dump").c_str(), 0700);
    auto old_path = std::string(getenv("PATH") ? getenv("PATH") : "");
    setenv("PATH", (root.string() + ":" + old_path).c_str(), 1);
    setenv("HV_DIAGNOSTIC_TEST_KEY", "synthetic-private-canary", 1);
    auto diagnostic = LocalDiagnostics(Config(path), false);
    check(diagnostic.at("assets_present") == true &&
              diagnostic.at("recognizer_loaded") == false,
          "files were treated as successful model initialization");
    check(diagnostic.at("configured_source_present") == true &&
              diagnostic.at("audio_tested") == false &&
              diagnostic.at("network_tested") == false,
          "unperformed tests reported successful");
    check(diagnostic.at("text_configured") == true,
          "text config completeness wrong");
    auto exposed = diagnostic.dump() + DiagnosticText(diagnostic);
    check(exposed.find("canary") == std::string::npos &&
              exposed.find("HV_DIAGNOSTIC_TEST_KEY") == std::string::npos,
          "diagnostics leaked private metadata");
    auto stale = Config(path);
    std::ofstream(path) << saved << '\n';
    check(LocalDiagnostics(stale, false).at("config_current") == false,
          "stale file not detected");
    setenv("PATH", old_path.c_str(), 1);
    unsetenv("HV_DIAGNOSTIC_TEST_KEY");
    for (const auto &entry : std::filesystem::directory_iterator(root))
      check(!entry.path().filename().string().starts_with("config.json.") ||
                entry.path().filename() == "config.json.settings.lock",
            "temporary config leaked");
    std::filesystem::remove_all(root);
    std::cout << checks
              << " settings transaction and diagnostic checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::filesystem::remove_all(root);
    std::cerr << e.what() << '\n';
    return 1;
  }
}

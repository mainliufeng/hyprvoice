#include "config.h"
#include "ui_text.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

struct Fixture {
  std::filesystem::path directory;
  Fixture() {
    auto pattern =
        (std::filesystem::temp_directory_path() / "hyprvoice-ui-config-XXXXXX")
            .string();
    if (!mkdtemp(pattern.data()))
      throw std::runtime_error("Cannot create test config directory");
    directory = pattern;
  }
  ~Fixture() { std::filesystem::remove_all(directory); }
  hv::Config load(const hv::Json &patch) {
    auto path = directory / "config.json";
    std::ofstream(path) << patch.dump();
    return hv::Config(path);
  }
  void reject(const hv::Json &patch, const char *expected) {
    try {
      load(patch);
    } catch (const std::runtime_error &error) {
      check(std::string(error.what()).find(expected) != std::string::npos,
            "invalid config did not report the expected reason");
      return;
    }
    throw std::runtime_error("invalid scene config was accepted");
  }
};

int main() {
  try {
    Fixture fixture;
    check(fixture.load(hv::Json::object()).data.at("scene") == "raw",
          "default scene changed");
    for (auto [scene, label] : {std::pair{"raw", "听写"},
                                {"correct", "纠错"},
                                {"format", "整理"},
                                {"translate", "翻译"}}) {
      fixture.load({{"scene", scene}});
      check(hv::SceneLabel(scene) == label, "built-in scene label changed");
    }
    // Prompt keys are extensible; names must reach the UI without a fixed map
    // lookup, markup interpretation or ASCII-only assumptions.
    for (const auto &name : {std::string("polish"), std::string("写作整理"),
                             std::string("写作 <b>& 整理")}) {
      auto config =
          fixture.load({{"scene", name}, {"prompts", {{name, "Test prompt"}}}});
      check(config.data.at("scene") == name, "custom scene was not loaded");
      check(hv::SceneLabel(name) == name, "custom scene label lost its name");
    }
    fixture.reject({{"scene", "unknown"}}, "Unknown scene");
    fixture.reject({{"scene", "correct"}, {"prompts", {{"correct", nullptr}}}},
                   "Unknown scene");
    fixture.reject({{"scene", 42}}, "scene must be a string");
    fixture.reject({{"scene", ""}}, "scene must be non-empty");
    fixture.reject({{"scene", std::string("a\0b", 3)}}, "NUL");
    fixture.reject({{"prompts", nullptr}}, "prompts must be an object");
    fixture.reject({{"prompts", hv::Json::array()}},
                   "prompts must be an object");
    fixture.reject({{"prompts", "bad"}}, "prompts must be an object");
    fixture.reject({{"prompts", {{"polish", 42}}}},
                   "Prompt values must be strings");
    fixture.reject({{"prompts", {{"", "Test prompt"}}}}, "Prompt scene names");
    fixture.reject({{"prompts", {{std::string("a\0b", 3), "Test prompt"}}}},
                   "Prompt scene names");
    // JSON merge-patch deletion can leave raw-only configurations usable.
    fixture.load({{"prompts",
                   {{"correct", nullptr},
                    {"format", nullptr},
                    {"translate", nullptr}}}});
    for (bool command_mode : {false, true}) {
      for (bool held : {false, true}) {
        auto hint = hv::RecordingHint(command_mode, held);
        check(hint.find("F8") == std::string::npos &&
                  hint.find("F9") == std::string::npos,
              "hint assumes a physical key binding");
        check(hint.find(command_mode ? "指令快捷键" : "录音快捷键") !=
                  std::string::npos,
              "hint does not describe the shortcut role");
        check(hint.find(command_mode || held ? "松开" : "再按") !=
                  std::string::npos,
              "hint does not describe the correct release/toggle action");
      }
    }
    std::cout << "UI text and scene configuration checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

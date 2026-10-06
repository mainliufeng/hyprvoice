#pragma once
#include <atomic>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
namespace hv {
using Json = nlohmann::json;
std::string ExpandPath(std::string path);
std::filesystem::path ConfigPath();
std::filesystem::path RuntimePath();
Json DefaultConfig();
void ValidateConfig(const Json &data);
void ValidateSettingsPatch(const Json &patch);
// Only the four settings exposed by the UI may be patched. Concurrent edits,
// invalid candidates and cancellation leave the original file untouched.
std::string SaveSettings(const std::filesystem::path &path,
                         const std::string &expected, const Json &patch,
                         const std::atomic<bool> *cancel = nullptr);
void SaveBackend(const std::string &backend);
struct Config {
  Json data;
  std::filesystem::path source_path;
  std::string source_text;
  explicit Config(const std::filesystem::path &path);
  std::string path(const char *key) const;
};
std::string ReadFile(const std::filesystem::path &path,
                     size_t limit = 1024 * 1024);
} // namespace hv

#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
namespace hv {
using Json = nlohmann::json;
std::string ExpandPath(std::string path);
std::filesystem::path ConfigPath();
std::filesystem::path RuntimePath();
Json DefaultConfig();
void SaveBackend(const std::string &backend);
struct Config {
  Json data;
  explicit Config(const std::filesystem::path &path);
  std::string path(const char *key) const;
};
std::string ReadFile(const std::filesystem::path &path,
                     size_t limit = 1024 * 1024);
} // namespace hv

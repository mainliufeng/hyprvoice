#include "launcher_env.h"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace hv {
namespace {
constexpr std::string_view whitespace = " \t\r";

void SkipWhitespace(std::string_view &text) {
  auto first = text.find_first_not_of(whitespace);
  text.remove_prefix(first == text.npos ? text.size() : first);
}

std::string_view TrimRight(std::string_view value) {
  const auto last = value.find_last_not_of(whitespace);
  return last == value.npos ? std::string_view{} : value.substr(0, last + 1);
}

char Take(std::string_view &text) {
  auto c = text.front();
  text.remove_prefix(1);
  return c;
}

bool ReadValue(std::string_view &text, std::string &value) {
  SkipWhitespace(text);
  while (!text.empty() && (text.front() == '\'' || text.front() == '"')) {
    const char quote = Take(text);
    while (!text.empty() && text.front() != quote) {
      auto c = Take(text);
      if (quote == '"' && c == '\\' && !text.empty()) {
        const char escaped = text.front();
        if (escaped == '\n') {
          Take(text);
          continue;
        }
        if (std::string_view("\"\\$`").find(escaped) != text.npos)
          c = Take(text);
      }
      value += c;
    }
    if (text.empty())
      return false;
    Take(text);
    SkipWhitespace(text);
  }
  auto keep = value.size();
  while (!text.empty() && text.front() != '\n') {
    const auto c = Take(text);
    if (c == '\\') {
      if (text.empty())
        return false;
      const auto escaped = Take(text);
      if (escaped != '\n')
        value += escaped;
      keep = value.size();
    } else {
      value += c;
      if (whitespace.find(c) == text.npos)
        keep = value.size();
    }
  }
  value.resize(keep);
  if (!text.empty())
    Take(text);
  return true;
}

std::string FindAssignment(std::string_view text, std::string_view name) {
  std::string result;
  while (!text.empty()) {
    SkipWhitespace(text);
    if (text.empty())
      break;
    const auto newline = text.find('\n');
    const auto line = text.substr(0, newline);
    const auto equals = line.find('=');
    if (text.front() == '#' || text.front() == ';' || equals == text.npos) {
      text.remove_prefix(newline == text.npos ? text.size() : newline + 1);
      continue;
    }
    const auto key = TrimRight(line.substr(0, equals));
    text.remove_prefix(equals + 1);
    std::string value;
    if (!ReadValue(text, value))
      return {};
    if (key == name)
      result = std::move(value);
  }
  // An API credential is used in an HTTP header and must fit on one line.
  if (result.find_first_of("\r\n") != result.npos)
    return {};
  return result;
}
} // namespace

std::string TextCredential(const Config &config) {
  const auto name = config.data.at("llm").at("api_key_env").get<std::string>();
  if (const auto *value = std::getenv(name.c_str()))
    return value;
  if (name.empty())
    return {};
  const auto path = config.source_path.parent_path() / "llm.env";
  const int fd =
      open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
    return {};
  struct stat info{};
  if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) ||
      info.st_uid != getuid() || (info.st_mode & 077) != 0) {
    close(fd);
    return {};
  }
  // The extra byte detects an oversized file, including one changed mid-read.
  std::array<char, 65537> bytes;
  size_t size = 0;
  while (size < bytes.size()) {
    const auto n = read(fd, bytes.data() + size, bytes.size() - size);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      close(fd);
      const std::string_view text(bytes.data(), size);
      if (n < 0 || text.find('\0') != text.npos)
        return {};
      return FindAssignment(text, name);
    }
    size += n;
  }
  close(fd);
  return {};
}
} // namespace hv

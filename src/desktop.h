#pragma once
#include "config.h"
#include <gio/gio.h>
#include <optional>
namespace hv {
struct Target {
  std::string address, app, stable;
  int pid = 0;
};
class Desktop {
public:
  explicit Desktop(const Config &config) : config_(config) {}
  ~Desktop();
  Target target();
  bool matches(const Target &target);
  Json context(const Target &target);
  std::string selection(const Target &target);
  void paste(const Target &target, const std::string &text,
             const std::string &selection = "");
  std::optional<std::string> clipboard();

private:
  const Config &config_;
  GSubprocess *owner_ = nullptr;
  void setClipboard(const std::string &text);
  void shortcut(const Target &target, const std::string &key);
  void requireTarget(const Target &target);
  bool terminal(const Target &target);
};
} // namespace hv

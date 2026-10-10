#pragma once
#include "config.h"
#include <gio/gio.h>
#include <optional>
#include <stdexcept>
#include <unordered_map>
namespace hv {
struct PasteUncertain : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct Target {
  std::string address, app, stable;
  int pid = 0;
  std::string seat, route_token;
  std::string kind = "application";
};
class Desktop {
public:
  explicit Desktop(const Config &config) : config_(config) {}
  ~Desktop();
  Target target();
  Target rebind(const Target &target);
  bool matches(const Target &target);
  Json context(const Target &target);
  Json inputTarget(const Target &target);
  std::string selection(const Target &target);
  void paste(const Target &target, const std::string &text, const Json &guard,
             bool window_only = false);
  void copy(const std::string &text, const std::string &seat = "") {
    setClipboard(text, seat);
  }
  std::optional<std::string> clipboard();

private:
  const Config &config_;
  std::unordered_map<std::string, GSubprocess *> owners_;
  std::optional<bool> lua_dispatch_;
  std::optional<bool> seat_input_;
  bool local_editor_ = false;
  std::optional<std::string> clipboard(const std::string &seat);
  void setClipboard(const std::string &text, const std::string &seat = "");
  void shortcut(const Target &target, const std::string &key);
  void requireTarget(const Target &target);
  bool terminal(const Target &target);
};
} // namespace hv

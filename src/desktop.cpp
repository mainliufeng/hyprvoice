#include "desktop.h"
#include "process.h"
#include <chrono>
#include <regex>
#include <stdexcept>
#include <thread>
namespace hv {
Desktop::~Desktop() {
  if (owner_) {
    g_subprocess_force_exit(owner_);
    g_subprocess_wait(owner_, nullptr, nullptr);
    g_object_unref(owner_);
  }
}
Target Desktop::target() {
  auto j = Json::parse(Checked({"hyprctl", "activewindow", "-j"}));
  Target t{j.value("address", std::string()), j.value("class", std::string()),
           j.value("stableId", std::string()), j.value("pid", 0)};
  if (t.pid <= 0 || !std::regex_match(t.address, std::regex("0x[0-9a-fA-F]+")))
    throw std::runtime_error("No active application window");
  return t;
}
bool Desktop::matches(const Target &t) {
  try {
    auto now = target();
    return now.address == t.address && now.pid == t.pid &&
           now.stable == t.stable;
  } catch (...) {
    return false;
  }
}
void Desktop::requireTarget(const Target &t) {
  if (!matches(t))
    throw std::runtime_error("目标窗口已变化；结果已保留，请回到原窗口后提交");
}
bool Desktop::terminal(const Target &t) {
  for (auto &c : config_.data.at("terminal_classes"))
    if (c.get<std::string>() == t.app)
      return true;
  return false;
}
void Desktop::shortcut(const Target &t, const std::string &key) {
  requireTarget(t);
  std::string mods = terminal(t) ? "CTRL SHIFT" : "CTRL";
  Checked({"hyprctl", "dispatch", "sendshortcut",
           mods + ", " + key + ", address:" + t.address});
}
std::optional<std::string> Desktop::clipboard() {
  auto r = Run({"wl-paste", "--no-newline", "--type", "text"}, "", 800);
  if (r.code)
    return std::nullopt;
  if (!g_utf8_validate(r.out.data(), r.out.size(), nullptr) ||
      r.out.find('\0') != std::string::npos)
    return std::nullopt;
  return r.out;
}
void Desktop::setClipboard(const std::string &text) {
  if (text.size() > 1024 * 1024 ||
      !g_utf8_validate(text.data(), text.size(), nullptr))
    throw std::runtime_error("Clipboard text is invalid or too large");
  GError *error = nullptr;
  const char *argv[] = {"wl-copy", "--foreground", "--type",
                        "text/plain;charset=utf-8", nullptr};
  auto next = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE, &error);
  if (!next) {
    std::string msg = error->message;
    g_error_free(error);
    throw std::runtime_error(msg);
  }
  auto in = g_subprocess_get_stdin_pipe(next);
  gsize written = 0;
  bool ok = g_output_stream_write_all(in, text.data(), text.size(), &written,
                                      nullptr, &error) &&
            g_output_stream_close(in, nullptr, &error);
  if (!ok) {
    g_subprocess_force_exit(next);
    g_subprocess_wait(next, nullptr, nullptr);
    g_object_unref(next);
    std::string msg = error ? error->message : "Clipboard write failed";
    if (error)
      g_error_free(error);
    throw std::runtime_error(msg);
  }
  // Round-trip readiness before sending a paste shortcut. Never use a fixed
  // sleep as evidence that the application has received the clipboard.
  bool ready = false;
  for (int i = 0; i < 10; ++i) {
    auto value = clipboard();
    if (value && *value == text) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!ready) {
    g_subprocess_force_exit(next);
    g_subprocess_wait(next, nullptr, nullptr);
    g_object_unref(next);
    throw std::runtime_error("Clipboard ownership could not be established");
  }
  if (owner_) {
    g_subprocess_force_exit(owner_);
    g_subprocess_wait(owner_, nullptr, nullptr);
    g_object_unref(owner_);
  }
  owner_ = next;
}
std::string Desktop::selection(const Target &t) {
  if (terminal(t))
    throw std::runtime_error(
        "终端选区不代表可替换的编辑区域；请在编辑器中使用指令模式");
  requireTarget(t);
  auto previous = clipboard();
  auto uuid = g_uuid_string_random();
  std::string marker = "hyprvoice-selection-" + std::string(uuid);
  g_free(uuid);
  setClipboard(marker);
  auto restore = [&] {
    auto value = clipboard();
    if (previous && value && (*value == marker))
      setClipboard(*previous);
  };
  try {
    shortcut(t, "C");
    std::optional<std::string> selected;
    auto until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
    while (std::chrono::steady_clock::now() < until) {
      requireTarget(t);
      auto value = clipboard();
      if (value && *value != marker) {
        selected = value;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    if (!selected || selected->empty()) {
      restore();
      throw std::runtime_error("未读到选中文字；请先选择文字再触发指令模式");
    }
    // Copy replaced our marker. Restore plain text only while no other
    // application/user operation has replaced that selection clipboard.
    auto value = clipboard();
    if (previous && value && *value == *selected)
      setClipboard(*previous);
    requireTarget(t);
    return *selected;
  } catch (...) {
    restore();
    throw;
  }
}
void Desktop::paste(const Target &t, const std::string &text,
                    const std::string &selected) {
  if (text.empty())
    throw std::runtime_error("Nothing to commit");
  requireTarget(t);
  if (!selected.empty() && selection(t) != selected)
    throw std::runtime_error("选区内容已变化；未替换，结果已保留");
  auto previous = clipboard();
  setClipboard(text);
  shortcut(t, "V");
  // Paste has no application acknowledgement. Default: leave output on the
  // clipboard. Optional delayed restore is explicitly best-effort.
  if (config_.data.value("clipboard_restore", false) && previous) {
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    auto value = clipboard();
    if (value && *value == text)
      setClipboard(*previous);
  }
}
} // namespace hv

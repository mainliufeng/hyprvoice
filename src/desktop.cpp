#include "desktop.h"
#include "context.h"
#include "process.h"
#include <algorithm>
#include <chrono>
#include <regex>
#include <stdexcept>
#include <thread>
namespace hv {
Desktop::~Desktop() {
  for (const auto &[seat, owner] : owners_) {
    g_subprocess_force_exit(owner);
    g_subprocess_wait(owner, nullptr, nullptr);
    g_object_unref(owner);
  }
}
Target Desktop::target() {
  if (!seat_input_) {
    auto reply = Run({"hyprctl", "-j", "seat", "capabilities"});
    auto caps = Json::parse(reply.out, nullptr, false);
    bool supported = false;
    if (!reply.code && caps.is_object()) {
      auto features = caps.value("features", Json::array());
      supported = std::find(features.begin(), features.end(),
                            "human-input-target-v1") != features.end();
    }
    seat_input_ = supported;
  }
  Json route;
  if (*seat_input_) {
    route = Json::parse(Checked({"hyprctl", "-j", "seat", "input-target"}));
    if (!route.value("allowed", false))
      throw std::runtime_error(route.value(
          "reason", std::string("当前桌面不可输入；请先接管并聚焦应用输入框")));
  }
  auto j = *seat_input_
               ? route.at("window")
               : Json::parse(Checked({"hyprctl", "activewindow", "-j"}));
  Target t{j.value("address", std::string()),
           j.value("class", std::string()),
           j.value("stableId", std::string()),
           j.value("pid", 0),
           *seat_input_ ? route.at("seatName").get<std::string>() : "",
           *seat_input_ ? route.at("token").get<std::string>() : ""};
  if (t.pid <= 0 || !std::regex_match(t.address, std::regex("0x[0-9a-fA-F]+")))
    throw std::runtime_error("No active application window");
  return t;
}
Target Desktop::rebind(const Target &previous) {
  auto current = target();
  const auto identity = [](const std::string &token) {
    return token.substr(0, token.find(':'));
  };
  if (current.address != previous.address || current.pid != previous.pid ||
      current.stable != previous.stable || current.seat != previous.seat ||
      identity(current.route_token) != identity(previous.route_token))
    throw std::runtime_error("目标窗口或桌面已变化；请回到原输入位置后确认");
  return current;
}
bool Desktop::matches(const Target &t) {
  try {
    auto now = target();
    return now.address == t.address && now.pid == t.pid &&
           now.stable == t.stable && now.seat == t.seat &&
           now.route_token == t.route_token;
  } catch (...) {
    return false;
  }
}
Json Desktop::context(const Target &t) {
  requireTarget(t);
  auto reply =
      Run({"/proc/self/exe", "read-context", std::to_string(t.pid),
           std::to_string(config_.data.at("context").value("max_chars", 1024))},
          "", 1500);
  requireTarget(t);
  if (reply.code)
    return {{"available", false}, {"protected", false}, {"text", ""}};
  return Json::parse(reply.out);
}
Json Desktop::inputTarget(const Target &t) {
  requireTarget(t);
  auto reply =
      Run({"/proc/self/exe", "read-target", std::to_string(t.pid)}, "", 1500);
  requireTarget(t);
  if (reply.code)
    return {{"available", false}, {"reliable", false}, {"protected", false}};
  auto guard = Json::parse(reply.out);
  guard["terminal_window"] = terminal(t);
  return guard;
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
  if (!lua_dispatch_) {
    auto caps = Run({"hyprctl", "-j", "seat", "capabilities"});
    bool lua = false;
    if (!caps.code) {
      auto value = Json::parse(caps.out, nullptr, false);
      if (value.is_object())
        lua = value.value("dialect", std::string()) == "lua";
    }
    lua_dispatch_ = lua;
  }
  std::vector<std::string> args{"hyprctl"};
  if (!t.route_token.empty()) {
    // hyprctl serializes command arguments into one IPC string.
    std::replace(mods.begin(), mods.end(), ' ', '+');
    args.insert(args.end(),
                {"seat", "input-shortcut", t.route_token, mods, key});
  } else if (*lua_dispatch_) {
    args.push_back("dispatch");
    args.push_back("hl.dsp.send_shortcut({mods=" + Json(mods).dump() +
                   ",key=" + Json(key).dump() +
                   ",window=" + Json("address:" + t.address).dump() + "})");
  } else {
    args.push_back("dispatch");
    args.push_back("sendshortcut");
    args.push_back(mods + ", " + key + ", address:" + t.address);
  }
  // Capability negotiation is read-only. Failures before this final check
  // have sent no key and must keep a normally retryable pending result.
  requireTarget(t);
  try {
    auto reply = Checked(args);
    if (reply.find_first_not_of(" \t\r\n") == std::string::npos ||
        !reply.starts_with("ok"))
      throw std::runtime_error("Shortcut dispatcher did not acknowledge");
  } catch (const std::exception &) {
    if (key == "V")
      throw PasteUncertain("粘贴发送状态未知；请检查输入框，避免重复输入");
    throw;
  }
}
std::optional<std::string> Desktop::clipboard() { return clipboard(""); }
std::optional<std::string> Desktop::clipboard(const std::string &seat) {
  std::vector<std::string> args{"wl-paste", "--no-newline", "--type", "text"};
  if (!seat.empty())
    args.insert(args.end(), {"--seat", seat});
  auto r = Run(args, "", 800);
  if (r.code)
    return std::nullopt;
  if (!g_utf8_validate(r.out.data(), r.out.size(), nullptr) ||
      r.out.find('\0') != std::string::npos)
    return std::nullopt;
  return r.out;
}
void Desktop::setClipboard(const std::string &text, const std::string &seat) {
  if (text.size() > 1024 * 1024 ||
      !g_utf8_validate(text.data(), text.size(), nullptr))
    throw std::runtime_error("Clipboard text is invalid or too large");
  GError *error = nullptr;
  std::vector<std::string> args{"wl-copy", "--foreground", "--type",
                                "text/plain;charset=utf-8"};
  if (!seat.empty())
    args.insert(args.end(), {"--seat", seat});
  std::vector<const char *> argv;
  for (const auto &arg : args)
    argv.push_back(arg.c_str());
  argv.push_back(nullptr);
  auto next =
      g_subprocess_newv(argv.data(), G_SUBPROCESS_FLAGS_STDIN_PIPE, &error);
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
    auto value = clipboard(seat);
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
  auto &owner = owners_[seat.empty() ? "Hyprland" : seat];
  if (owner) {
    g_subprocess_force_exit(owner);
    g_subprocess_wait(owner, nullptr, nullptr);
    g_object_unref(owner);
  }
  owner = next;
}
std::string Desktop::selection(const Target &t) {
  if (terminal(t))
    throw std::runtime_error(
        "终端选区不代表可替换的编辑区域；请在编辑器中使用指令模式");
  requireTarget(t);
  auto guard = inputTarget(t);
  if (guard.value("protected", false))
    throw std::runtime_error("密码输入框不能复制选区或使用文本修改指令");
  if (!guard.value("reliable", false))
    throw std::runtime_error(
        "无法核验编辑控件；未复制选区，请在支持的编辑器中使用指令模式");
  auto ranges = guard.value("selections", Json::array());
  if (ranges.size() != 1 || ranges[0][0] == ranges[0][1])
    throw std::runtime_error("请先选择一段非空文字再触发指令模式");
  auto previous = clipboard(t.seat);
  auto uuid = g_uuid_string_random();
  std::string marker = "hyprvoice-selection-" + std::string(uuid);
  g_free(uuid);
  setClipboard(marker, t.seat);
  auto restore = [&] {
    auto value = clipboard(t.seat);
    if (previous && value && (*value == marker))
      setClipboard(*previous, t.seat);
  };
  try {
    if (!EquivalentInputTarget(guard, inputTarget(t)))
      throw std::runtime_error("编辑位置已变化；未复制选区");
    shortcut(t, "C");
    std::optional<std::string> selected;
    auto until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
    while (std::chrono::steady_clock::now() < until) {
      requireTarget(t);
      auto value = clipboard(t.seat);
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
    char *digest = g_compute_checksum_for_string(
        G_CHECKSUM_SHA256, selected->data(), selected->size());
    bool exact = digest && guard.at("selection_digests").size() == 1 &&
                 guard.at("selection_digests")[0] == digest;
    g_free(digest);
    if (!exact)
      throw std::runtime_error("复制文字与已核验选区不符；未发送模型请求");
    // Copy replaced our marker. Restore plain text only while no other
    // application/user operation has replaced that selection clipboard.
    auto value = clipboard(t.seat);
    if (previous && value && *value == *selected)
      setClipboard(*previous, t.seat);
    requireTarget(t);
    if (!EquivalentInputTarget(guard, inputTarget(t)))
      throw std::runtime_error("编辑位置已变化；未使用选区内容");
    return *selected;
  } catch (...) {
    restore();
    throw;
  }
}
void Desktop::paste(const Target &t, const std::string &text, const Json &guard,
                    bool window_only) {
  if (text.empty())
    throw std::runtime_error("Nothing to commit");
  requireTarget(t);
  auto verify = [&] {
    auto current = inputTarget(t);
    if (current.value("protected", false))
      throw std::runtime_error("密码输入框不能接收本次结果；结果已保留");
    const bool window_input =
        window_only && WindowOnlyInputTarget(guard) &&
        (WindowOnlyInputTarget(current) ||
         (current.value("reliable", false) &&
          current.value("selections", Json::array()).empty()));
    if (!window_input && !EquivalentInputTarget(guard, current))
      throw std::runtime_error(
          "编辑控件、光标、选区或文字已变化，或无法核验；未发送，结果已保留");
  };
  verify();
  auto previous = clipboard(t.seat);
  setClipboard(text, t.seat);
  // Clipboard readiness may take time. Verify again immediately before send.
  verify();
  shortcut(t, "V");
  // Paste has no application acknowledgement. Default: leave output on the
  // clipboard. Optional delayed restore is explicitly best-effort.
  if (config_.data.value("clipboard_restore", false) && previous) {
    try {
      std::this_thread::sleep_for(std::chrono::milliseconds(700));
      auto value = clipboard(t.seat);
      if (value && *value == text)
        setClipboard(*previous, t.seat);
    } catch (const std::exception &) {
      // The paste request already went out. A best-effort restore failure
      // must not leave this result available for a second insertion.
    }
  }
}
} // namespace hv

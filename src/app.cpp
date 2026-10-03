#include "app.h"
#include "process.h"
#include "rewrite.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <glib-unix.h>
#include <gtk4-layer-shell.h>
#include <iostream>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
namespace hv {
static sockaddr_un Address(const std::string &path) {
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  if (path.size() >= sizeof(a.sun_path))
    throw std::runtime_error("Socket path too long");
  std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
  return a;
}
static std::string Receive(int fd) {
  std::string out;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (out.size() < 65536 && std::chrono::steady_clock::now() < deadline) {
    pollfd p{fd, POLLIN, 0};
    if (poll(&p, 1, 100) <= 0)
      continue;
    char buf[4096];
    auto n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;
    out.append(buf, n);
    if (out.find('\n') != std::string::npos)
      return out.substr(0, out.find('\n'));
  }
  throw std::runtime_error("Incomplete or oversized IPC message");
}
Json SendCommand(const std::string &command, const std::string &arg) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    throw std::runtime_error("Cannot create control socket");
  try {
    auto addr = Address((RuntimePath() / "control.sock").string());
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
      throw std::runtime_error(
          "Hyprvoice is not running; start hyprvoice serve");
    auto message = Json{{"command", command}, {"arg", arg}}.dump() + "\n";
    if (send(fd, message.data(), message.size(), MSG_NOSIGNAL) !=
        static_cast<ssize_t>(message.size()))
      throw std::runtime_error("Control request failed");
    auto reply = Json::parse(Receive(fd));
    close(fd);
    return reply;
  } catch (...) {
    close(fd);
    throw;
  }
}
App::App(Config c) : config_(std::move(c)), desktop_(config_) {
  scene_ = config_.data.value("scene", std::string("raw"));
  state_ = {{"phase", "loading"},
            {"scene", scene_},
            {"text", "正在加载本地语音模型…"},
            {"raw", ""},
            {"error", ""},
            {"command_mode", false},
            {"seconds", 0}};
}
App::~App() {
  cancel_ = true;
  stop_ = true;
  if (worker_.joinable())
    worker_.join();
  if (focus_socket_ >= 0)
    close(focus_socket_);
  if (socket_ >= 0) {
    close(socket_);
    unlink((RuntimePath() / "control.sock").c_str());
  }
  if (lock_ >= 0)
    close(lock_);
  if (loop_)
    g_main_loop_unref(loop_);
  if (window_)
    gtk_window_destroy(GTK_WINDOW(window_));
}
void App::update(const Json &patch) {
  std::lock_guard lock(mutex_);
  state_.update(patch);
}
Json App::snapshot() {
  std::lock_guard lock(mutex_);
  return state_;
}
void App::bindSocket() {
  auto dir = RuntimePath();
  std::filesystem::create_directories(dir);
  struct stat st{};
  if (lstat(dir.c_str(), &st) < 0 || !S_ISDIR(st.st_mode) ||
      st.st_uid != getuid())
    throw std::runtime_error("Unsafe runtime directory");
  chmod(dir.c_str(), 0700);
  lock_ = open((dir / "lock").c_str(),
               O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock_ < 0 || flock(lock_, LOCK_EX | LOCK_NB) < 0)
    throw std::runtime_error("Another hyprvoice instance is running");
  socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  auto address = Address((dir / "control.sock").string());
  unlink(address.sun_path);
  if (bind(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
          0 ||
      listen(socket_, 8) < 0)
    throw std::runtime_error("Cannot bind control socket");
  chmod(address.sun_path, 0600);
  const char *signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (!signature)
    throw std::runtime_error("Run from a Hyprland session");
  auto focusAddress =
      Address((std::filesystem::path(std::getenv("XDG_RUNTIME_DIR")) / "hypr" /
               signature / ".socket2.sock")
                  .string());
  focus_socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (connect(focus_socket_, reinterpret_cast<sockaddr *>(&focusAddress),
              sizeof(focusAddress)) < 0)
    throw std::runtime_error("Cannot monitor Hyprland focus events");
  fcntl(focus_socket_, F_SETFL, O_NONBLOCK);
}
void App::focusEvents() {
  char buf[4096];
  for (;;) {
    auto n = recv(focus_socket_, buf, sizeof(buf), MSG_DONTWAIT);
    if (n <= 0) {
      if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
        focus_changed_ = true;
      break;
    }
    focus_events_.append(buf, n);
  }
  size_t p;
  while ((p = focus_events_.find('\n')) != std::string::npos) {
    auto line = focus_events_.substr(0, p);
    focus_events_.erase(0, p + 1);
    if (!delivered_ && line.starts_with("activewindowv2>>")) {
      auto address = line.substr(16);
      if ("0x" + address != target_.address)
        focus_changed_ = true;
    }
  }
  if (focus_events_.size() > 65536)
    focus_events_.clear();
}
void App::start(bool cmd) {
  if (busy_)
    throw std::runtime_error("正在处理上一段语音，请稍候");
  if (!asr_)
    throw std::runtime_error("本地模型不可用，请检查配置并重启");
  if (snapshot().value("phase", std::string()) == "ready")
    throw std::runtime_error("请先提交或取消上一段结果");
  target_ = desktop_.target();
  selected_ = cmd ? desktop_.selection(target_) : "";
  focusEvents();
  focus_changed_ = false;
  delivered_ = false;
  stop_ = false;
  cancel_ = false;
  level_ = 0;
  busy_ = true;
  auto scene = scene_, selected = selected_, history = history_;
  update({{"phase", "starting"},
          {"text", "正在连接麦克风…"},
          {"raw", ""},
          {"error", ""},
          {"command_mode", cmd},
          {"seconds", 0}});
  if (worker_.joinable())
    worker_.join();
  worker_ = std::thread([this, scene, selected, history, cmd] {
    try {
      asr_->begin();
      audio_.start(config_.data.value("audio_source", std::string()));
      update({{"phase", "recording"}, {"text", "请开始说话…"}});
      auto start = std::chrono::steady_clock::now();
      size_t count = 0;
      size_t maximum = config_.data.value("max_recording_seconds", 180) * 16000;
      auto consume = [&](std::vector<float> pcm) {
        if (pcm.empty())
          return;
        count += pcm.size();
        float peak = 0;
        double gain = config_.data.value("gain", 1.0);
        for (auto &s : pcm) {
          s = std::clamp(static_cast<float>(s * gain), -1.0f, 1.0f);
          peak = std::max(peak, std::abs(s));
        }
        level_ = peak;
        auto text = asr_->push(pcm);
        if (!text.empty())
          update({{"text", text}});
      };
      while (!stop_ && !cancel_) {
        consume(audio_.take());
        auto error = audio_.error();
        if (!error.empty())
          throw std::runtime_error(error);
        auto elapsed = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - start)
                           .count();
        update({{"seconds", elapsed}});
        if (count >= maximum ||
            elapsed >= config_.data.value("max_recording_seconds", 180)) {
          stop_ = true;
          update({{"error", "已到录音时长上限，正在处理完整结果"}});
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      audio_.stop();
      consume(audio_.take());
      if (cancel_) {
        asr_->cancel();
        update({{"phase", "idle"}, {"text", ""}, {"raw", ""}, {"error", ""}});
      } else {
        update({{"phase", "finalizing"}, {"text", "正在精修…"}});
        auto result = asr_->finish();
        if (cancel_)
          update({{"phase", "idle"}, {"text", ""}, {"raw", ""}});
        else if (result.text.empty())
          update(
              {{"phase", "idle"}, {"text", "未检测到可识别语音"}, {"raw", ""}});
        else {
          std::string output = result.text, error;
          update({{"raw", result.text}, {"text", result.text}});
          if (cmd || scene != "raw") {
            update({{"phase", "rewriting"}});
            try {
              output = Rewrite(config_, result.text, scene, selected, history,
                               cancel_);
            } catch (const std::exception &e) {
              error = e.what();
              if (cmd)
                output.clear();
            }
          }
          if (cancel_)
            update({{"phase", "idle"}, {"text", ""}, {"raw", ""}});
          else
            update({{"phase", cmd && output.empty() ? "error" : "ready"},
                    {"text", output},
                    {"error", error}});
        }
      }
    } catch (const std::exception &e) {
      audio_.stop();
      asr_->cancel();
      update({{"phase", cancel_ ? "idle" : "error"},
              {"text", ""},
              {"error", cancel_ ? "" : e.what()}});
    }
    busy_ = false;
  });
}
void App::commit(bool raw) {
  if (busy_)
    throw std::runtime_error("结果尚未完成");
  auto state = snapshot();
  if (state.at("phase") != "ready")
    throw std::runtime_error("没有待提交的结果");
  if (raw && state.at("command_mode").get<bool>())
    throw std::runtime_error("指令模式的识别原文是指令，不能用于替换选区");
  auto text = state.at(raw ? "raw" : "text").get<std::string>();
  desktop_.paste(target_, text, selected_);
  delivered_ = true;
  history_ += text + '\n';
  if (history_.size() > 16000)
    history_.erase(0, history_.size() - 16000);
  // Trim history on a UTF-8 boundary after applying the bounded byte limit.
  while (!history_.empty() &&
         (static_cast<unsigned char>(history_.front()) & 0xc0) == 0x80)
    history_.erase(0, 1);
  update({{"phase", "idle"}, {"text", "已发送粘贴请求"}, {"error", ""}});
}
Json App::command(const std::string &cmd, const std::string &arg) {
  try {
    if (cmd == "status")
      return {{"ok", true}, {"state", snapshot()}};
    if (cmd == "start")
      start(false);
    else if (cmd == "command")
      start(true);
    else if (cmd == "stop")
      stop_ = true;
    else if (cmd == "toggle") {
      if (busy_ && (snapshot()["phase"] == "recording" ||
                    snapshot()["phase"] == "starting"))
        stop_ = true;
      else
        start(false);
    } else if (cmd == "press") {
      if (!pressed_) {
        pressed_ = true;
        pressed_at_ = g_get_monotonic_time();
        if (busy_ && (snapshot()["phase"] == "recording" ||
                      snapshot()["phase"] == "starting")) {
          stop_ = true;
          pressed_ = false;
        } else
          start(false);
      }
    } else if (cmd == "release") {
      if (pressed_ && g_get_monotonic_time() - pressed_at_ >= 250000)
        stop_ = true;
      pressed_ = false;
    } else if (cmd == "cancel") {
      pressed_ = false;
      cancel_ = true;
      stop_ = true;
      delivered_ = true;
      if (!busy_)
        update({{"phase", "idle"}, {"text", ""}, {"raw", ""}, {"error", ""}});
      else
        update({{"phase", "cancelling"}});
    } else if (cmd == "commit")
      commit(false);
    else if (cmd == "raw")
      commit(true);
    else if (cmd == "scene") {
      if (arg != "raw" && !config_.data.at("prompts").contains(arg))
        throw std::runtime_error("Unknown scene");
      if (busy_)
        throw std::runtime_error("请在录音结束后切换场景");
      scene_ = arg;
      update({{"scene", scene_}});
    } else if (cmd == "quit") {
      cancel_ = true;
      stop_ = true;
      g_main_loop_quit(loop_);
    } else
      throw std::runtime_error("Unknown command: " + cmd);
    return {{"ok", true}, {"state", snapshot()}};
  } catch (const std::exception &e) {
    if (cmd == "press")
      pressed_ = false;
    update({{"error", e.what()}});
    return {{"ok", false}, {"error", e.what()}};
  }
}
void App::sockets() {
  for (int i = 0; i < 8; ++i) {
    int fd = accept4(socket_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0)
      break;
    Json result;
    try {
      ucred cred{};
      socklen_t n = sizeof(cred);
      if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &n) < 0 ||
          cred.uid != getuid())
        throw std::runtime_error("Unauthorized control client");
      auto j = Json::parse(Receive(fd));
      result = command(j.at("command"), j.value("arg", std::string()));
    } catch (const std::exception &e) {
      result = {{"ok", false}, {"error", e.what()}};
    }
    auto reply = result.dump() + "\n";
    send(fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    close(fd);
  }
}
void App::ui() {
  sockets();
  focusEvents();
  auto state = snapshot();
  std::string phase = state.at("phase");
  if (!busy_ && phase == "ready" && !delivered_ &&
      config_.data.value("auto_commit", true) &&
      !state.at("command_mode").get<bool>()) {
    if (!focus_changed_) {
      auto reply = command("commit");
      if (!reply.value("ok", false))
        focus_changed_ = true;
    } else
      update({{"error", "录音期间窗口已变化；请回到原窗口后点击提交"}});
    state = snapshot();
    phase = state.at("phase");
  }
  static const std::map<std::string, std::string> names = {
      {"loading", "加载模型"},    {"idle", "就绪"},
      {"starting", "连接麦克风"}, {"recording", "录音中"},
      {"finalizing", "精修中"},   {"rewriting", "整理中"},
      {"ready", "待提交"},        {"error", "处理失败"},
      {"cancelling", "取消中"}};
  std::string heading = "● " + names.at(phase) + "  ·  " +
                        (state.at("command_mode").get<bool>()
                             ? std::string("选区修改")
                             : state.at("scene").get<std::string>());
  if (phase == "recording")
    heading +=
        "  ·  " +
        std::to_string(static_cast<int>(state.at("seconds").get<double>())) +
        "s";
  gtk_label_set_text(GTK_LABEL(title_), heading.c_str());
  auto text = state.at("text").get<std::string>();
  if (text.size() > 2400) {
    size_t end = 2400;
    while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
      --end;
    text = text.substr(0, end) + "…";
  }
  gtk_label_set_text(GTK_LABEL(text_), text.c_str());
  auto error = state.at("error").get<std::string>();
  std::string hint = error;
  if (hint.empty())
    hint = state.at("command_mode").get<bool>()
               ? "确认修改结果后提交 · 原文选区会再次核对 · 取消保留原文"
               : "按住说话 · 轻按切换录音 · 原文和整理结果均可提交";
  gtk_label_set_text(GTK_LABEL(hint_), hint.c_str());
  gtk_level_bar_set_value(GTK_LEVEL_BAR(meter_), level_);
  gtk_widget_set_visible(meter_, phase == "recording");
  gtk_widget_set_sensitive(
      raw_button_, phase == "ready" && !state.at("command_mode").get<bool>());
  gtk_widget_set_visible(window_, phase != "idle" || !error.empty());
}
void App::run() {
  gtk_init();
  if (!gtk_layer_is_supported())
    throw std::runtime_error("A Wayland layer-shell compositor is required");
  bindSocket();
  window_ = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(window_), "Hyprvoice");
  gtk_layer_init_for_window(GTK_WINDOW(window_));
  gtk_layer_set_namespace(GTK_WINDOW(window_), "hyprvoice");
  gtk_layer_set_layer(GTK_WINDOW(window_), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_),
                              GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, true);
  gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, 28);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window_), 0);
  auto css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      css, "window { background: #17212b; color: #e9f0f5; border: 1px solid "
           "#42576a; border-radius: 18px; } label { font-size: 15px; } "
           ".heading { color: #71dac6; font-weight: 700; } .hint { color: "
           "#acbdc9; font-size: 12px; } button { border-radius: 10px; padding: "
           "7px 14px; } levelbar block.filled { background: #71dac6; }");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);
  auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 18);
  gtk_widget_set_margin_bottom(box, 18);
  gtk_widget_set_margin_start(box, 22);
  gtk_widget_set_margin_end(box, 22);
  gtk_widget_set_size_request(box, 600, -1);
  gtk_window_set_child(GTK_WINDOW(window_), box);
  title_ = gtk_label_new("");
  gtk_widget_add_css_class(title_, "heading");
  gtk_label_set_xalign(GTK_LABEL(title_), 0);
  gtk_box_append(GTK_BOX(box), title_);
  meter_ = gtk_level_bar_new_for_interval(0, 1);
  gtk_box_append(GTK_BOX(box), meter_);
  text_ = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(text_), true);
  gtk_label_set_xalign(GTK_LABEL(text_), 0);
  gtk_label_set_max_width_chars(GTK_LABEL(text_), 60);
  gtk_label_set_lines(GTK_LABEL(text_), 8);
  gtk_label_set_ellipsize(GTK_LABEL(text_), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(box), text_);
  auto row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append(GTK_BOX(box), row);
  for (auto [label, cmd] : std::vector<std::pair<const char *, const char *>>{
           {"提交结果", "commit"},
           {"提交原文", "raw"},
           {"停止录音", "stop"},
           {"取消", "cancel"}}) {
    auto button = gtk_button_new_with_label(label);
    g_object_set_data_full(G_OBJECT(button), "command", g_strdup(cmd), g_free);
    g_signal_connect(button, "clicked",
                     G_CALLBACK(+[](GtkButton *b, gpointer data) {
                       auto self = static_cast<App *>(data);
                       self->command(static_cast<char *>(
                           g_object_get_data(G_OBJECT(b), "command")));
                     }),
                     this);
    gtk_box_append(GTK_BOX(row), button);
    if (std::string(cmd) == "raw")
      raw_button_ = button;
  }
  hint_ = gtk_label_new("");
  gtk_widget_add_css_class(hint_, "hint");
  gtk_label_set_wrap(GTK_LABEL(hint_), true);
  gtk_label_set_xalign(GTK_LABEL(hint_), 0);
  gtk_label_set_max_width_chars(GTK_LABEL(hint_), 65);
  gtk_box_append(GTK_BOX(box), hint_);
  loop_ = g_main_loop_new(nullptr, false);
  busy_ = true;
  worker_ = std::thread([this] {
    try {
      asr_ = std::make_unique<Asr>(config_);
      update({{"phase", "idle"}, {"text", ""}});
    } catch (const std::exception &e) {
      update({{"phase", "error"}, {"text", ""}, {"error", e.what()}});
    }
    busy_ = false;
  });
  auto timer = g_timeout_add(
      80,
      +[](gpointer data) -> gboolean {
        try {
          static_cast<App *>(data)->ui();
        } catch (const std::exception &e) {
          std::cerr << e.what() << '\n';
        }
        return G_SOURCE_CONTINUE;
      },
      this);
  auto onSignal = +[](gpointer data) -> gboolean {
    auto self = static_cast<App *>(data);
    self->cancel_ = true;
    self->stop_ = true;
    g_main_loop_quit(self->loop_);
    return G_SOURCE_CONTINUE;
  };
  auto sigint = g_unix_signal_add(SIGINT, onSignal, this);
  auto sigterm = g_unix_signal_add(SIGTERM, onSignal, this);
  g_main_loop_run(loop_);
  g_source_remove(timer);
  g_source_remove(sigint);
  g_source_remove(sigterm);
}
} // namespace hv

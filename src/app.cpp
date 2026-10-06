#include "app.h"
#include "overlay_style.h"
#include "process.h"
#include "rewrite.h"
#include "ui_text.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <glib-unix.h>
#include <gtk4-layer-shell.h>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <sstream>
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
            {"backend", config_.data.at("asr").at("backend")},
            {"scene", scene_},
            {"text", "正在加载本地语音模型…"},
            {"raw", ""},
            {"error", ""},
            {"command_mode", false},
            {"seconds", 0},
            {"context", ""},
            {"context_note", ""}};
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
void App::updateLocked(const Json &patch) {
  state_.update(patch);
  if (patch.contains("phase") && patch.at("phase") == "idle") {
    state_["context"] = "";
    state_["context_note"] = "";
    rewrite_request_.reset();
    manual_confirmation_ = false;
  }
}
void App::update(const Json &patch) {
  std::lock_guard lock(mutex_);
  updateLocked(patch);
}
void App::updateSession(uint64_t version, const Json &patch) {
  std::lock_guard lock(mutex_);
  if (version == session_version_ && !cancel_)
    updateLocked(patch);
}
bool App::rememberRewrite(uint64_t version, const TextRequest &request) {
  std::lock_guard lock(mutex_);
  if (version != session_version_ || cancel_)
    return false;
  rewrite_request_ = request;
  return true;
}
void App::finishSession(uint64_t version) {
  std::lock_guard lock(mutex_);
  if (version != session_version_ || cancel_)
    updateLocked({{"phase", "idle"}, {"text", ""}, {"raw", ""}, {"error", ""}});
  busy_ = false;
}
Json App::snapshot() {
  std::lock_guard lock(mutex_);
  auto state = state_;
  state["busy"] = busy_.load();
  state["retry_available"] = rewrite_request_.has_value() && !delivered_;
  state["manual_confirmation"] = manual_confirmation_;
  return state;
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
  if (snapshot().value("phase", std::string()) == "ready" ||
      snapshot().value("retry_available", false))
    throw std::runtime_error("请先提交或取消上一段结果");
  auto next_target = desktop_.target();
  auto next_selected = cmd ? desktop_.selection(next_target) : "";
  target_ = next_target;
  selected_ = next_selected;
  focusEvents();
  focus_changed_ = false;
  delivered_ = false;
  stop_ = false;
  uint64_t version;
  {
    std::lock_guard lock(mutex_);
    version = ++session_version_;
    rewrite_request_.reset();
    manual_confirmation_ = false;
    cancel_ = false;
  }
  level_ = 0;
  recording_text_.clear();
  busy_ = true;
  auto scene = scene_, selected = selected_;
  auto target = target_;
  bool contextual = config_.data.at("context").value("enabled", false);
  update({{"phase", "starting"},
          {"text", "正在连接麦克风…"},
          {"raw", ""},
          {"error", ""},
          {"command_mode", cmd},
          {"seconds", 0},
          {"context", ""},
          {"context_note", contextual ? "读取当前输入框前文…" : ""}});
  if (worker_.joinable())
    worker_.join();
  worker_ = std::thread([this, scene, selected, target, contextual, cmd,
                         version] {
    try {
      // Capture before the bounded accessibility query so a slow application
      // cannot discard the beginning of a held-key utterance.
      audio_.start(config_.data.value("audio_source", std::string()));
      auto start = std::chrono::steady_clock::now();
      asr_->begin(&cancel_);
      updateSession(version, {{"phase", "recording"}, {"text", ""}});
      std::string history;
      bool protected_field = false;
      if (contextual) {
        auto context = desktop_.context(target);
        protected_field = context.value("protected", false);
        history = context.value("text", std::string());
        std::string note = protected_field ? "密码输入框 · 不读取或发送前文"
                           : !context.value("available", false)
                               ? "未读到输入框前文"
                           : history.empty() ? "当前输入框没有前文"
                                             : "参考光标前文";
        updateSession(version, {{"context", history}, {"context_note", note}});
      }
      if (protected_field && cmd)
        throw std::runtime_error("密码输入框不能使用文本修改指令");
      if (cancel_)
        throw std::runtime_error("已取消");
      size_t count = 0;
      auto diagnostic_at = start;
      size_t diagnostic_samples = 0;
      double diagnostic_energy = 0;
      float diagnostic_peak = 0;
      size_t preview_chars = 0;
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
          diagnostic_energy += static_cast<double>(s) * s;
        }
        level_ = peak;
        diagnostic_samples += pcm.size();
        diagnostic_peak = std::max(diagnostic_peak, peak);
        auto text = asr_->push(pcm);
        preview_chars = g_utf8_strlen(text.c_str(), -1);
        if (!text.empty())
          updateSession(version, {{"text", text}});
      };
      while (!stop_ && !cancel_) {
        consume(audio_.take());
        auto error = audio_.error();
        if (!error.empty())
          throw std::runtime_error(error);
        auto elapsed = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - start)
                           .count();
        updateSession(version, {{"seconds", elapsed}});
        auto now = std::chrono::steady_clock::now();
        if (now - diagnostic_at >= std::chrono::seconds(2)) {
          std::cerr << "Capture diagnostic: elapsed_s=" << elapsed
                    << " captured_s=" << count / 16000.0 << " rms="
                    << (diagnostic_samples
                            ? std::sqrt(diagnostic_energy / diagnostic_samples)
                            : 0)
                    << " peak=" << diagnostic_peak
                    << " preview_chars=" << preview_chars << '\n';
          diagnostic_at = now;
          diagnostic_samples = 0;
          diagnostic_energy = 0;
          diagnostic_peak = 0;
        }
        if (count >= maximum ||
            elapsed >= config_.data.value("max_recording_seconds", 180)) {
          stop_ = true;
          updateSession(version,
                        {{"error", "已到录音时长上限，正在处理完整结果"}});
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      audio_.stop();
      consume(audio_.take());
      std::cerr << "Capture diagnostic: stopped captured_s=" << count / 16000.0
                << " preview_chars=" << preview_chars << '\n';
      if (cancel_) {
        asr_->cancel();
        updateSession(
            version,
            {{"phase", "idle"}, {"text", ""}, {"raw", ""}, {"error", ""}});
      } else {
        updateSession(version, {{"phase", "finalizing"}, {"text", ""}});
        auto result = asr_->finish(&cancel_);
        if (cancel_)
          updateSession(version,
                        {{"phase", "idle"}, {"text", ""}, {"raw", ""}});
        else if (result.text.empty())
          update(
              {{"phase", "idle"}, {"text", "未检测到可识别语音"}, {"raw", ""}});
        else {
          TextResult output{result.text, result.warning};
          updateSession(version, {{"raw", result.text}, {"text", result.text}});
          if (!protected_field && (cmd || scene != "raw" || !history.empty())) {
            TextRequest request{
                result.text,    scene == "raw" ? "correct" : scene,
                selected,       history,
                result.warning, cmd};
            if (rememberRewrite(version, request)) {
              updateSession(version, {{"phase", "rewriting"}});
              output = ProcessText(config_, request, cancel_);
            }
          }
          updateSession(
              version,
              {{"phase", cmd && output.text.empty() ? "error" : "ready"},
               {"text", output.text},
               {"error", output.error}});
        }
      }
    } catch (const std::exception &e) {
      audio_.stop();
      asr_->cancel();
      updateSession(version, {{"phase", cancel_ ? "idle" : "error"},
                              {"text", ""},
                              {"error", cancel_ ? "" : e.what()}});
    }
    finishSession(version);
  });
}
void App::retry() {
  TextRequest request;
  uint64_t version;
  {
    std::lock_guard lock(mutex_);
    if (busy_)
      throw std::runtime_error("正在处理本次文字，请稍候");
    auto phase = state_.at("phase").get<std::string>();
    if (delivered_ || !rewrite_request_ ||
        (phase != "ready" && phase != "error"))
      throw std::runtime_error("没有可重试的本次文字，请先录音");
    request = *rewrite_request_;
    version = session_version_;
    manual_confirmation_ = true;
    cancel_ = false;
    busy_ = true;
    updateLocked({{"phase", "retrying"}, {"error", ""}});
  }
  if (worker_.joinable())
    worker_.join();
  worker_ = std::thread([this, request, version] {
    auto output = ProcessText(config_, request, cancel_);
    updateSession(
        version,
        {{"phase",
          request.command_mode && output.text.empty() ? "error" : "ready"},
         {"text", output.text},
         {"error", output.error}});
    finishSession(version);
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
  {
    std::lock_guard lock(mutex_);
    ++session_version_;
    rewrite_request_.reset();
  }
  update({{"phase", "idle"},
          {"text", "已发送粘贴请求"},
          {"error", ""},
          {"context", ""},
          {"context_note", ""}});
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
      std::lock_guard lock(mutex_);
      cancel_ = true;
      stop_ = true;
      delivered_ = true;
      ++session_version_;
      rewrite_request_.reset();
      updateLocked({{"phase", busy_ ? "cancelling" : "idle"},
                    {"text", ""},
                    {"raw", ""},
                    {"error", ""},
                    {"context", ""},
                    {"context_note", ""}});
    } else if (cmd == "retry")
      retry();
    else if (cmd == "commit")
      commit(false);
    else if (cmd == "raw")
      commit(true);
    else if (cmd == "backend") {
      if (arg != "fun" && arg != "x-asr")
        throw std::runtime_error("Backend must be fun or x-asr");
      auto phase = snapshot().at("phase").get<std::string>();
      if (busy_ || phase == "ready" ||
          snapshot().value("retry_available", false))
        throw std::runtime_error(
            "请先结束、提交或取消当前录音，再切换识别后端");
      if (config_.data.at("asr").at("backend") != arg) {
        auto next = config_;
        next.data["asr"]["backend"] = arg;
        if (worker_.joinable())
          worker_.join();
        busy_ = true;
        update({{"phase", "loading"},
                {"text", "正在切换本地识别模型…"},
                {"raw", ""},
                {"error", ""},
                {"seconds", 0}});
        worker_ = std::thread([this, next, arg] {
          try {
            auto recognizer = std::make_unique<Asr>(next);
            SaveBackend(arg); // Persist only after the real model is usable.
            asr_ = std::move(recognizer);
            config_.data["asr"]["backend"] = arg;
            update({{"phase", "idle"}, {"backend", arg}, {"text", ""}});
          } catch (const std::exception &e) {
            update({{"phase", "error"}, {"error", e.what()}, {"text", ""}});
          }
          busy_ = false;
        });
      }
    } else if (cmd == "scene") {
      if (arg != "raw" && !config_.data.at("prompts").contains(arg))
        throw std::runtime_error("Unknown scene");
      if (busy_ || snapshot().value("retry_available", false))
        throw std::runtime_error("请先提交或取消本次结果，再切换场景");
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
    // Rejected concurrent actions must not overwrite the active result/error.
    if (!busy_)
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
      !state.value("manual_confirmation", false) &&
      config_.data.value("auto_commit", true) &&
      !state.at("command_mode").get<bool>()) {
    if (!focus_changed_ && state.at("error").get<std::string>().empty()) {
      auto reply = command("commit");
      if (!reply.value("ok", false))
        focus_changed_ = true;
    } else if (focus_changed_)
      update({{"error", "录音期间窗口已变化；请回到原窗口后点击提交"}});
    state = snapshot();
    phase = state.at("phase");
  }
  static const std::map<std::string, std::string> names = {
      {"loading", "正在准备"},
      {"idle", "就绪"},
      {"starting", "连接麦克风"},
      {"recording", "正在听"},
      {"finalizing", "正在识别"},
      {"rewriting", "正在整理"},
      {"retrying", "正在重试文字处理"},
      {"ready", "待确认"},
      {"error", "需要处理"},
      {"cancelling", "取消中"}};
  const bool recording = phase == "recording", ready = phase == "ready";
  const bool command_mode = state.at("command_mode").get<bool>();
  const bool processing = phase == "loading" || phase == "starting" ||
                          phase == "finalizing" || phase == "rewriting" ||
                          phase == "retrying" || phase == "cancelling";
  const auto scene = state.at("scene").get<std::string>();
  const auto mode =
      command_mode ? std::string("修改选中文字") : SceneLabel(scene);
  gtk_label_set_text(
      GTK_LABEL(title_),
      (names.at(phase) + (scene == "raw" && !command_mode ? "" : " · " + mode))
          .c_str());
  int seconds = static_cast<int>(state.at("seconds").get<double>());
  std::ostringstream elapsed;
  elapsed << std::setfill('0') << std::setw(2) << seconds / 60 << ':'
          << std::setw(2) << seconds % 60;
  gtk_label_set_text(GTK_LABEL(duration_), elapsed.str().c_str());
  gtk_widget_set_visible(duration_, recording);
  gtk_spinner_set_spinning(GTK_SPINNER(spinner_), processing);
  gtk_widget_set_visible(spinner_, processing);
  auto text = state.at("text").get<std::string>();
  if (phase == "starting" || phase == "idle" || phase == "loading")
    recording_text_.clear();
  if (recording && !text.empty() && state.at("backend") == "x-asr")
    recording_text_ = text;
  if (phase == "finalizing")
    text = recording_text_;
  if (phase == "loading" || phase == "starting")
    text.clear();
  auto previous = state.value("context", std::string());
  auto context_note = state.value("context_note", std::string());
  // Show a small tail of the exact context used; never interpret it as markup.
  if (g_utf8_strlen(previous.c_str(), -1) > 96)
    previous =
        "…" + std::string(g_utf8_offset_to_pointer(
                  previous.c_str(), g_utf8_strlen(previous.c_str(), -1) - 96));
  auto context_display =
      (previous.empty() ? context_note : "参考前文  " + previous);
  gtk_label_set_text(GTK_LABEL(context_), context_display.c_str());
  gtk_widget_set_visible(context_, !context_note.empty());
  const bool placeholder = text.empty();
  if (placeholder) {
    if (recording)
      text = state.at("backend") == "fun" ? "开始说话，结束后会显示文字…"
                                          : "说点什么…";
    else if (phase == "loading" || phase == "starting")
      text = "正在准备语音输入…";
    else if (phase == "error")
      text = state.value("retry_available", false)
                 ? "可重试本次文字处理，或取消。"
                 : "这次没有完成，请检查配置后重新录音。";
    else
      text = "正在识别这段语音…";
  }
  if (placeholder)
    gtk_widget_add_css_class(text_, "placeholder");
  else
    gtk_widget_remove_css_class(text_, "placeholder");
  if (text != display_text_ || phase != display_phase_) {
    gtk_label_set_text(GTK_LABEL(text_), text.c_str());
    // Recording follows the newest words; confirmation starts at the beginning.
    // Keep the complete transcript accessible rather than ellipsizing it.
    auto adjustment =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll_));
    if (recording) {
      g_timeout_add_full(
          G_PRIORITY_DEFAULT, 32,
          +[](gpointer data) -> gboolean {
            auto a = GTK_ADJUSTMENT(data);
            gtk_adjustment_set_value(
                a, std::max(0.0, gtk_adjustment_get_upper(a) -
                                     gtk_adjustment_get_page_size(a)));
            return G_SOURCE_REMOVE;
          },
          g_object_ref(adjustment), g_object_unref);
    } else if (ready)
      gtk_adjustment_set_value(adjustment, 0);
    display_text_ = text;
    display_phase_ = phase;
  }
  auto error = state.at("error").get<std::string>();
  std::string hint;
  if (recording)
    hint = RecordingHint(command_mode, pressed_);
  else if (phase == "retrying")
    hint = "复用本次原文和前文，无需重新录音";
  else if (ready)
    hint = state.value("manual_confirmation", false)
               ? (command_mode ? "重试结果需确认后替换选中文字"
                               : "重试结果需确认后插入")
           : command_mode ? "确认后替换选中文字"
                          : "检查后插入文字";
  else
    hint = phase == "error" ? "本次内容不会自动输入"
           : (config_.data.value("auto_commit", true) && !command_mode &&
              error.empty() && !focus_changed_)
               ? "完成后自动输入"
               : "完成后可检查并插入";
  gtk_label_set_text(GTK_LABEL(hint_), hint.c_str());
  auto warning = error;
  if (error.starts_with("语音检测未确认讲话"))
    warning = "语音检测未确认，请检查文字后再插入。";
  else if (error.starts_with("文本处理"))
    warning = command_mode ? "文字处理未完成，选中的原文已保留。"
                           : "文字处理未完成，已保留识别原文供确认。";
  gtk_label_set_text(GTK_LABEL(warning_), warning.c_str());
  gtk_widget_set_tooltip_text(warning_,
                              error.empty() ? nullptr : error.c_str());
  gtk_widget_set_visible(warning_, !error.empty());
  if (recording) {
    std::move(meter_history_.begin() + 1, meter_history_.end(),
              meter_history_.begin());
    meter_history_.back() = std::clamp(std::sqrt(level_.load()), 0.0f, 1.0f);
    gtk_widget_queue_draw(meter_);
  } else
    meter_history_.fill(0);
  gtk_widget_set_visible(meter_, recording);
  gtk_widget_set_visible(commit_button_, ready);
  gtk_widget_set_sensitive(commit_button_, !busy_);
  gtk_widget_set_visible(
      retry_button_, state.value("retry_available", false) &&
                         (ready || phase == "error" || phase == "retrying"));
  gtk_widget_set_sensitive(retry_button_, !busy_);
  gtk_button_set_label(GTK_BUTTON(retry_button_),
                       phase == "retrying" ? "重试中…" : "重试文字处理");
  gtk_widget_set_tooltip_text(
      retry_button_, "复用本次原转录、场景、选区和前文；结果需手动确认");
  gtk_button_set_label(GTK_BUTTON(commit_button_),
                       command_mode ? "确认修改" : "插入文字");
  gtk_widget_set_visible(
      raw_button_, ready && !command_mode &&
                       (state.at("text") != state.at("raw") || !error.empty()));
  gtk_widget_set_sensitive(raw_button_, !busy_);
  gtk_widget_set_visible(stop_button_, recording);
  gtk_widget_set_tooltip_text(cancel_button_,
                              phase == "error" ? "关闭" : "取消本次语音");
  gtk_widget_set_visible(window_, phase != "idle" || !error.empty());
}
void App::run() {
  gtk_init();
  if (!gtk_layer_is_supported())
    throw std::runtime_error("A Wayland layer-shell compositor is required");
  bindSocket();
  window_ = gtk_window_new();
  gtk_widget_add_css_class(window_, "hyprvoice");
  gtk_window_set_title(GTK_WINDOW(window_), "Hyprvoice");
  gtk_window_set_decorated(GTK_WINDOW(window_), false);
  gtk_window_set_resizable(GTK_WINDOW(window_), false);
  gtk_layer_init_for_window(GTK_WINDOW(window_));
  gtk_layer_set_namespace(GTK_WINDOW(window_), "hyprvoice");
  gtk_layer_set_layer(GTK_WINDOW(window_), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_),
                              GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, true);
  gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, 28);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window_), 0);
  auto css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(css, OverlayStyle);
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);
  auto outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_top(outer, 24);
  gtk_widget_set_margin_bottom(outer, 36);
  gtk_widget_set_margin_start(outer, 28);
  gtk_widget_set_margin_end(outer, 28);
  gtk_window_set_child(GTK_WINDOW(window_), outer);
  auto panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(panel, "voice-panel");
  gtk_box_append(GTK_BOX(outer), panel);
  auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_top(box, 18);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 22);
  gtk_widget_set_margin_end(box, 22);
  gtk_widget_set_size_request(box, 420, -1);
  gtk_box_append(GTK_BOX(panel), box);
  auto header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append(GTK_BOX(box), header);
  icon_ = gtk_image_new_from_icon_name("audio-input-microphone-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon_), 20);
  gtk_widget_add_css_class(icon_, "voice-icon");
  gtk_box_append(GTK_BOX(header), icon_);
  auto identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  mode_ = gtk_label_new("语音输入");
  gtk_widget_add_css_class(mode_, "voice-heading");
  gtk_label_set_xalign(GTK_LABEL(mode_), 0);
  gtk_box_append(GTK_BOX(identity), mode_);
  title_ = gtk_label_new("");
  gtk_widget_add_css_class(title_, "voice-status");
  gtk_label_set_xalign(GTK_LABEL(title_), 0);
  gtk_box_append(GTK_BOX(identity), title_);
  gtk_box_append(GTK_BOX(header), identity);
  auto space = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(space, true);
  gtk_box_append(GTK_BOX(header), space);
  meter_ = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(meter_), 84);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(meter_), 24);
  gtk_drawing_area_set_draw_func(
      GTK_DRAWING_AREA(meter_),
      +[](GtkDrawingArea *, cairo_t *cr, int width, int height, gpointer data) {
        auto self = static_cast<App *>(data);
        cairo_set_source_rgb(cr, 0.39, 0.40, 0.44);
        cairo_set_line_width(cr, 2);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        for (size_t i = 0; i < self->meter_history_.size(); ++i) {
          double x = (i + 0.5) * width / self->meter_history_.size();
          double h = 1 + self->meter_history_[i] * (height - 4);
          cairo_move_to(cr, x, (height - h) / 2);
          cairo_line_to(cr, x, (height + h) / 2);
          cairo_stroke(cr);
        }
      },
      this, nullptr);
  gtk_widget_set_tooltip_text(meter_, "最近收到的麦克风音量");
  gtk_box_append(GTK_BOX(header), meter_);
  duration_ = gtk_label_new("");
  gtk_widget_add_css_class(duration_, "voice-time");
  gtk_box_append(GTK_BOX(header), duration_);
  spinner_ = gtk_spinner_new();
  gtk_widget_add_css_class(spinner_, "voice-spinner");
  gtk_box_append(GTK_BOX(header), spinner_);
  cancel_button_ = gtk_button_new_from_icon_name("window-close-symbolic");
  gtk_widget_add_css_class(cancel_button_, "close");
  gtk_accessible_update_property(GTK_ACCESSIBLE(cancel_button_),
                                 GTK_ACCESSIBLE_PROPERTY_LABEL, "取消本次语音",
                                 -1);
  g_signal_connect(cancel_button_, "clicked",
                   G_CALLBACK(+[](GtkButton *, gpointer data) {
                     static_cast<App *>(data)->command("cancel");
                   }),
                   this);
  gtk_box_append(GTK_BOX(header), cancel_button_);
  context_ = gtk_label_new("");
  gtk_widget_add_css_class(context_, "voice-context");
  gtk_label_set_wrap(GTK_LABEL(context_), true);
  gtk_label_set_xalign(GTK_LABEL(context_), 0);
  gtk_label_set_max_width_chars(GTK_LABEL(context_), 40);
  gtk_label_set_lines(GTK_LABEL(context_), 2);
  gtk_label_set_ellipsize(GTK_LABEL(context_), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(box), context_);
  scroll_ = gtk_scrolled_window_new();
  gtk_widget_add_css_class(scroll_, "voice-scroll");
  gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll_),
                                            false);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll_), 42);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll_), 188);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll_),
                                                   true);
  gtk_box_append(GTK_BOX(box), scroll_);
  text_ = gtk_label_new("");
  gtk_widget_add_css_class(text_, "voice-transcript");
  gtk_label_set_wrap(GTK_LABEL(text_), true);
  gtk_label_set_wrap_mode(GTK_LABEL(text_), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign(GTK_LABEL(text_), 0);
  gtk_label_set_yalign(GTK_LABEL(text_), 0);
  gtk_widget_set_hexpand(text_, true);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_), text_);
  warning_ = gtk_label_new("");
  gtk_widget_add_css_class(warning_, "voice-warning");
  gtk_label_set_wrap(GTK_LABEL(warning_), true);
  gtk_label_set_wrap_mode(GTK_LABEL(warning_), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign(GTK_LABEL(warning_), 0);
  gtk_label_set_max_width_chars(GTK_LABEL(warning_), 40);
  gtk_box_append(GTK_BOX(box), warning_);
  auto divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class(divider, "voice-divider");
  gtk_box_append(GTK_BOX(box), divider);
  auto row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_append(GTK_BOX(box), row);
  hint_ = gtk_label_new("");
  gtk_widget_add_css_class(hint_, "voice-hint");
  gtk_label_set_wrap(GTK_LABEL(hint_), true);
  gtk_label_set_xalign(GTK_LABEL(hint_), 0);
  gtk_label_set_max_width_chars(GTK_LABEL(hint_), 20);
  gtk_widget_set_hexpand(hint_, true);
  gtk_box_append(GTK_BOX(row), hint_);
  for (auto [label, cmd] : std::vector<std::pair<const char *, const char *>>{
           {"使用原文", "raw"},
           {"重试文字处理", "retry"},
           {"结束录音", "stop"},
           {"插入文字", "commit"}}) {
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
    else if (std::string(cmd) == "retry")
      retry_button_ = button;
    else {
      gtk_widget_add_css_class(button, "primary");
      if (std::string(cmd) == "commit")
        commit_button_ = button;
      else
        stop_button_ = button;
    }
  }
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

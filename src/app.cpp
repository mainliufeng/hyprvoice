#include "app.h"
#include "context.h"
#include "diagnostics.h"
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
#include <tuple>
#include <unistd.h>
namespace hv {
static std::string InsertionStatus(const Json &guard) {
  return guard.value("protected", false)  ? "protected"
         : guard.value("reliable", false) ? "available"
         : WindowOnlyInputTarget(guard)   ? "window"
                                          : "unknown";
}
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
  state_["asr_parts"] = Json::array();
  state_["asr_candidates_active"] = false;
  state_["asr_streaming"] = "";
  state_["asr_refined"] = "";
  state_["recovered"] = false;
  state_["incomplete"] = false;
  state_["result_source"] = "asr";
  state_["change_before"] = "";
  state_["recognition_warning"] = "";
  state_["revision_error"] = "";
  state_["action_note"] = "";
  state_["revision_instruction"] = "";
  state_["target_note"] = "";
  state_["review_token"] = "";
  state_["preferred_raw"] = false;
  state_["retry_result"] = false;
  state_["model_ready"] = false;
  state_["settings_note"] = "";
  state_["insertion_status"] = "unknown";
}
App::~App() {
  settings_cancel_ = true;
  cancel_ = true;
  stop_ = true;
  if (worker_.joinable())
    worker_.join();
  settings_panel_.reset();
  if (focus_socket_ >= 0)
    close(focus_socket_);
  if (overlay_socket_ >= 0)
    close(overlay_socket_);
  if (socket_ >= 0) {
    close(socket_);
    unlink((RuntimePath() / "control.sock").c_str());
  }
  if (lock_ >= 0)
    close(lock_);
  if (loop_)
    g_main_loop_unref(loop_);
  if (secondary_editor_window_)
    gtk_window_destroy(GTK_WINDOW(secondary_editor_window_));
  if (secondary_editor_display_) {
    gdk_display_close(secondary_editor_display_);
    g_object_unref(secondary_editor_display_);
  }
  if (window_)
    gtk_window_destroy(GTK_WINDOW(window_));
}
void App::updateLocked(const Json &patch) {
  if (patch.value("phase", std::string()) == "ready" &&
      ((patch.contains("text") && patch.at("text") != state_.at("text")) ||
       (patch.contains("raw") && patch.at("raw") != state_.at("raw"))))
    ++result_revision_;
  state_.update(patch);
  if (patch.contains("phase") && patch.at("phase") == "idle") {
    editing_ = have_draft_ = focus_editor_ = false;
    draft_text_.clear();
    state_["asr_candidates_active"] = false;
    state_["context"] = "";
    state_["context_note"] = "";
    rewrite_request_.reset();
    revision_base_.reset();
    revision_cancel_base_.reset();
    revision_undo_.clear();
    state_["revision_instruction"] = "";
    state_["revision_error"] = "";
    manual_confirmation_ = false;
    reviewed_guard_ = Json();
    review_token_.clear();
    state_["review_token"] = "";
    state_["target_note"] = "";
    preferred_raw_ = false;
    state_["preferred_raw"] = false;
    state_["retry_result"] = false;
    reviewed_text_.clear();
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
  if (version != session_version_ || cancel_) {
    if (revision_base_)
      restoreRevisionLocked("已取消语音修改，原正文已保留");
    else
      updateLocked(
          {{"phase", "idle"}, {"text", ""}, {"raw", ""}, {"error", ""}});
  }
  revision_cancel_base_.reset();
  busy_ = false;
}
Json App::snapshot() {
  std::lock_guard lock(mutex_);
  auto state = state_;
  state["editing"] = editing_;
  state["draft_text"] = have_draft_ ? draft_text_ : "";
  state["draft_pending"] = draftPendingLocked();
  state["draft_session_id"] = draft_session_id_;
  state["draft_base_revision"] = draft_base_revision_;
  state["session_id"] = session_id_;
  state["result_revision"] = result_revision_;
  state["effective_text"] =
      preferred_raw_ ? state_.at("raw") : state_.at("text");
  state["undo_revision_available"] = !revision_undo_.empty() && !delivered_;
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
    if (line.starts_with("activewindowv2>>") ||
        line.starts_with("seatinputfocus>>") ||
        line.starts_with("seatpresentation>>") ||
        line.starts_with("seatworkspace>>"))
      input_status_refresh_ = true;
    if (!delivered_ && target_.kind != "local-editor" &&
        line.starts_with("activewindowv2>>")) {
      if (!target_.route_token.empty() && target_.seat != "Hyprland")
        continue;
      auto address = line.substr(16);
      if ("0x" + address != target_.address)
        focus_changed_ = true;
    }
    if (!delivered_ && !target_.route_token.empty()) {
      const auto prefix = "seatinputfocus>>" + target_.seat + ",";
      if ((line.starts_with(prefix) && !desktop_.matches(target_)) ||
          ((line.starts_with("seatpresentation>>") ||
            line.starts_with("seatworkspace>>") ||
            (target_.seat != "Hyprland" &&
             line.starts_with("seatinputfocus>>Hyprland,"))) &&
           !desktop_.matches(target_)))
        focus_changed_ = true;
    }
  }
  if (focus_events_.size() > 65536)
    focus_events_.clear();
  if (!target_.route_token.empty() && focus_changed_ && busy_) {
    const auto phase = snapshot().at("phase");
    if (phase == "starting" || phase == "recording") {
      // A takeover change can swallow the held F8 release. Finish capture
      // locally and keep the transcript for review instead of recording on.
      pressed_ = false;
      stop_ = true;
    }
  }
}
void App::start(bool cmd) {
  if (settings_panel_ && settings_panel_->visible())
    throw std::runtime_error("请先关闭设置窗口，再回到输入位置开始录音");
  if (busy_)
    throw std::runtime_error("正在处理上一段语音，请稍候");
  if (!asr_)
    throw std::runtime_error("本地模型不可用，请检查配置并重启");
  const auto before = snapshot();
  if (cmd) {
    if (delivered_ || before.at("phase") != "ready" ||
        !ValidBody(before.at("effective_text").get<std::string>()))
      throw std::runtime_error("没有可修改的待确认正文；请先录音并保留结果");
  } else if (before.at("phase") == "ready" ||
             before.at("phase") == "delivery-uncertain" ||
             before.value("retry_available", false))
    throw std::runtime_error("请先提交或取消上一段结果");
  // Revision always uses this session's retained body and original destination.
  // No selection/clipboard/context is read to discover a revision subject.
  auto next_target = cmd ? desktop_.rebind(target_) : desktop_.target();
  Json next_guard = desktop_.inputTarget(next_target);
  auto comparable_guard = origin_guard_;
  if (cmd && comparable_guard.value("toolkit", std::string()) == "wayland" &&
      next_guard.value("toolkit", std::string()) == "wayland")
    // rebind already proved the original seat/window/PID/stable surface.
    // Refresh only its focus-generation token; content/revision/caret stay
    // strict.
    comparable_guard["route_token"] = next_guard.value("route_token", "");
  if (cmd && (next_guard.value("protected", false) ||
              !next_guard.value("reliable", false) || capture_changed_ ||
              !EquivalentInputTarget(comparable_guard, next_guard)))
    throw std::runtime_error(
        "原输入位置受保护、已变化或无法核验；正文已保留，未开始语音修改");
  auto next_watch =
      next_target.kind == "local-editor"
          ? std::make_unique<InputWatch>(next_guard,
                                         [config = config_, next_target] {
                                           Desktop isolated(config);
                                           return isolated.inputTarget(
                                               next_target);
                                         })
          : std::make_unique<InputWatch>(next_target.pid, next_guard);
  target_watch_ = std::move(next_watch);
  copy_notice_until_ = 0;
  if (!cmd)
    capture_changed_ = false;
  target_ = next_target;
  focusEvents();
  if (cmd && !desktop_.matches(target_))
    throw std::runtime_error("原输入窗口已变化；未开始语音修改");
  // Old edit/review focus events are not changes during this new capture.
  // Rebind and the strict content guard above retain original-target safety.
  focus_changed_ = false;
  delivered_ = false;
  stop_ = false;
  uint64_t version;
  {
    std::lock_guard lock(mutex_);
    version = ++session_version_;
    if (cmd) {
      revision_base_ = state_;
      revision_cancel_base_ = state_;
      revision_base_version_ = result_revision_;
    } else {
      ++session_id_;
      editing_ = have_draft_ = false;
      draft_text_.clear();
      state_["asr_parts"] = Json::array();
      state_["asr_candidates_active"] = false;
      state_["asr_streaming"] = "";
      state_["asr_refined"] = "";
      state_["recovered"] = false;
      state_["incomplete"] = false;
      state_["change_before"] = "";
      state_["result_source"] = "asr";
      result_revision_ = 0;
      revision_undo_.clear();
    }
    rewrite_request_.reset();
    manual_confirmation_ = cmd;
    origin_guard_ = next_guard;
    reviewed_guard_ = Json();
    review_token_.clear();
    preferred_raw_ = false;
    reviewed_text_.clear();
    cancel_ = false;
  }
  level_ = 0;
  recording_text_.clear();
  busy_ = true;
  auto scene = scene_;
  auto original_body =
      cmd ? before.at("effective_text").get<std::string>() : "";
  auto target = target_;
  bool contextual = !cmd && config_.data.at("context").value("enabled", false);
  update({{"insertion_status", InsertionStatus(next_guard)},
          {"phase", "starting"},
          {"text", cmd ? original_body : "正在连接麦克风…"},
          {"raw", cmd ? before.at("raw").get<std::string>() : ""},
          {"error", ""},
          {"command_mode", cmd},
          {"seconds", 0},
          {"context", ""},
          {"review_token", ""},
          {"target_note", ""},
          {"preferred_raw", false},
          {"revision_instruction", ""},
          {"recognition_warning",
           cmd ? before.value("recognition_warning", std::string()) : ""},
          {"action_note", ""},
          {"retry_result", false},
          {"context_note", contextual ? "读取当前输入框前文…" : ""}});
  if (worker_.joinable())
    worker_.join();
  worker_ = std::thread([this, scene, original_body, target, contextual, cmd,
                         version] {
    std::string last_preview, capture_error;
    bool recognizer_started = false, finish_attempted = false;
    size_t count = 0;
    auto diagnostic_at = std::chrono::steady_clock::now();
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
      if (!text.empty()) {
        if (!ValidBody(text))
          throw std::runtime_error("识别后端返回无效预览");
        last_preview = text;
      }
      preview_chars = g_utf8_strlen(text.c_str(), -1);
      if (!text.empty())
        updateSession(version, {{cmd ? "revision_instruction" : "text", text}});
    };
    try {
      // Capture before the bounded accessibility query so a slow application
      // cannot discard the beginning of a held-key utterance.
      audio_.start(config_.data.value("audio_source", std::string()));
      auto start = std::chrono::steady_clock::now();
      auto guard = origin_guard_;
      auto capture_guard = desktop_.inputTarget(target);
      {
        std::lock_guard lock(mutex_);
        if (version != session_version_ || cancel_)
          throw std::runtime_error("已取消");
        origin_guard_ = guard;
      }
      asr_->begin(&cancel_);
      recognizer_started = true;
      updateSession(version, cmd ? Json{{"phase", "recording"}}
                                 : Json{{"phase", "recording"}, {"text", ""}});
      std::string history;
      bool protected_field = guard.value("protected", false);
      protected_field =
          protected_field || capture_guard.value("protected", false);
      bool verified_field = !protected_field && !target_watch_->changed() &&
                            EquivalentInputTarget(guard, capture_guard);
      const bool window_input = !cmd && WindowOnlyInputTarget(guard) &&
                                WindowOnlyInputTarget(capture_guard);
      if (!verified_field)
        updateSession(
            version,
            {{"context_note",
              protected_field ? "密码输入框 · 不读取、复制或发送文字处理请求"
                              : "无法核验输入位置 · 不发送文字处理请求"}});
      if (contextual && verified_field) {
        auto context = desktop_.context(target);
        protected_field = context.value("protected", false);
        // Context is allowed only from the same verified control/version.
        verified_field =
            !protected_field &&
            EquivalentInputTarget(guard, desktop_.inputTarget(target));
        if (verified_field)
          history = context.value("text", std::string());
        std::string note =
            protected_field   ? "密码输入框 · 不读取或发送前文"
            : !verified_field ? "编辑位置已变化 · 不发送文字处理请求"
            : !context.value("available", false) ? "未读到输入框前文"
            : history.empty()                    ? "当前输入框没有前文"
                                                 : "参考光标前文";
        updateSession(version, {{"context", history}, {"context_note", note}});
      }
      if (protected_field && cmd)
        throw std::runtime_error("密码输入框不能使用文本修改指令");
      if (!verified_field && cmd)
        throw std::runtime_error(
            "编辑位置无法核验或已变化；未发送文本修改请求");
      if (cancel_)
        throw std::runtime_error("已取消");
      while (!stop_ && !cancel_) {
        consume(audio_.take());
        auto error = audio_.error();
        if (!error.empty()) {
          capture_error = error;
          break;
        }
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
          capture_error = "已到录音时长上限，后续内容未采集";
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      audio_.stop();
      const auto final_capture_error = audio_.error();
      if (!final_capture_error.empty())
        capture_error = final_capture_error;
      consume(audio_.take());
      if (cmd && !capture_error.empty())
        throw std::runtime_error("修改要求录音中断；原正文已保留，请重新口述");
      std::cerr << "Capture diagnostic: stopped captured_s=" << count / 16000.0
                << " preview_chars=" << preview_chars << '\n';
      if (cancel_) {
        asr_->cancel();
        updateSession(
            version,
            {{"phase", "idle"}, {"text", ""}, {"raw", ""}, {"error", ""}});
      } else {
        updateSession(version, cmd ? Json{{"phase", "finalizing"}}
                                   : Json{{"phase", "finalizing"},
                                          {"text", last_preview}});
        finish_attempted = true;
        auto result = asr_->finish(&cancel_);
        if (cancel_)
          updateSession(version,
                        {{"phase", "idle"}, {"text", ""}, {"raw", ""}});
        else if (!capture_error.empty())
          recoverResult(version, result, last_preview, capture_error);
        else if (cmd) {
          if (!ValidBody(result.text))
            throw std::runtime_error("未检测到有效修改要求；原正文已保留");
          updateSession(version, {{"revision_instruction", result.text}});
          auto current = desktop_.inputTarget(target);
          if (target_watch_->changed() || current.value("protected", false) ||
              !EquivalentInputTarget(guard, current))
            throw std::runtime_error(
                "修改录音期间位置已变化；未发送文字处理请求");
          TextRequest request{
              "", scene, original_body, "", result.warning, true, result.text};
          if (rememberRewrite(version, request)) {
            updateSession(version, {{"phase", "rewriting"}});
            completeRevision(version, ProcessText(config_, request, cancel_));
          }
        } else if (result.text.empty())
          updateSession(
              version,
              {{"phase", "idle"}, {"text", "未检测到可识别语音"}, {"raw", ""}});
        else {
          auto choices = RecognitionChoices(result.streaming, result.refined);
          const bool differences =
              std::any_of(choices.begin(), choices.end(), [](const Json &part) {
                return part.value("selectable", false);
              });
          if (differences) {
            if (result.warning.empty())
              result.warning = "两次音频识别有不同片段，请检查后确认输入。";
            std::lock_guard lock(mutex_);
            if (version == session_version_)
              manual_confirmation_ = true;
          }
          TextResult output{result.text, result.warning};
          updateSession(version, {{"asr_parts", choices},
                                  {"asr_candidates_active", differences},
                                  {"asr_streaming", result.streaming},
                                  {"asr_refined", result.refined}});
          updateSession(version, {{"raw", result.text},
                                  {"text", result.text},
                                  {"recognition_warning", result.warning}});
          // Do not send speech captured after a move into a protected or
          // unidentified control. The local guard never becomes cloud context.
          bool request_allowed = false;
          bool input_allowed = false;
          bool privacy_protected = protected_field;
          std::string insertion_status = "unknown";
          try {
            auto current = desktop_.inputTarget(target);
            insertion_status = InsertionStatus(current);
            privacy_protected =
                privacy_protected || current.value("protected", false);
            request_allowed = verified_field && !target_watch_->changed() &&
                              EquivalentInputTarget(guard, current);
            input_allowed =
                request_allowed || (window_input && !privacy_protected &&
                                    WindowOnlyInputTarget(current));
          } catch (const std::exception &) {
            request_allowed = false;
          }
          if (!request_allowed) {
            // Missing optional editor metadata is not an observed position
            // change. Keep model/context checks strict without disabling basic
            // window-bound dictation. A formerly verified editor cannot fall
            // back automatically after losing its guard.
            capture_changed_ = !input_allowed;
            updateSession(
                version, {{"context_note",
                           privacy_protected
                               ? "密码输入框 · 不读取、复制或发送文字处理请求"
                               : "位置已变化或无法核验 · 未发送文字处理请求"}});
          }
          if (request_allowed && (scene != "raw" || !history.empty())) {
            TextRequest request{result.text,
                                scene == "raw" ? "correct" : scene,
                                "",
                                history,
                                result.warning,
                                false,
                                ""};
            if (rememberRewrite(version, request)) {
              updateSession(version, {{"phase", "rewriting"}});
              output = ProcessText(config_, request, cancel_);
            }
          }
          if (output.text != result.text)
            updateSession(version, {{"asr_candidates_active", false},
                                    {"result_source", "processed"},
                                    {"change_before", result.text}});
          updateSession(version, {{"insertion_status", insertion_status},
                                  {"phase", "ready"},
                                  {"text", output.text},
                                  {"error", output.error}});
        }
      }
    } catch (const std::exception &e) {
      audio_.stop();
      if (!cmd && !cancel_) {
        Transcript recovered;
        // stop() leaves the queue intact and synchronizes the capture callback.
        // Drain once, then finish the recognizer once. User cancellation wins.
        try {
          auto queued = audio_.take();
          if (!recognizer_started && !queued.empty()) {
            asr_->begin(&cancel_);
            recognizer_started = true;
          }
          if (recognizer_started)
            consume(std::move(queued));
        } catch (const std::exception &) {
        }
        if (recognizer_started && !finish_attempted && !cancel_)
          try {
            finish_attempted = true;
            recovered = asr_->finish(&cancel_);
          } catch (const std::exception &) {
          }
        asr_->cancel();
        recoverResult(version, recovered, last_preview, e.what());
      } else if (cmd) {
        asr_->cancel();
        std::lock_guard lock(mutex_);
        if (version == session_version_ && !cancel_)
          restoreRevisionLocked(e.what());
      } else {
        asr_->cancel();
        updateSession(version,
                      {{"phase", "idle"}, {"text", ""}, {"error", ""}});
      }
    }
    target_watch_.reset();
    finishSession(version);
  });
}
void App::restoreRevisionLocked(const std::string &error) {
  if (!revision_base_)
    return;
  auto instruction = state_.value("revision_instruction", std::string());
  state_ = *revision_base_;
  revision_base_.reset();
  preferred_raw_ = state_.value("preferred_raw", false);
  manual_confirmation_ = true;
  review_token_.clear();
  reviewed_guard_ = Json();
  reviewed_text_.clear();
  state_["phase"] = "ready";
  state_["command_mode"] = false;
  state_["revision_instruction"] = instruction;
  state_["review_token"] = "";
  state_["target_note"] = "";
  // Replace the previous attempt's failure while retaining original ASR/body
  // warnings. A successful retry must not keep claiming that revision failed.
  auto previous = state_.at("error").get<std::string>();
  auto prior_failure = state_.value("revision_error", std::string());
  if (!prior_failure.empty()) {
    if (previous == prior_failure)
      previous.clear();
    else if (previous.ends_with("\n" + prior_failure))
      previous.resize(previous.size() - prior_failure.size() - 1);
  }
  state_["revision_error"] = error;
  state_["error"] = error.empty()      ? previous
                    : previous.empty() ? error
                                       : previous + "\n" + error;
}
void App::completeRevision(uint64_t version, const TextResult &output) {
  std::lock_guard lock(mutex_);
  if (version != session_version_ || cancel_ || !revision_base_ ||
      revision_base_version_ != result_revision_)
    return;
  auto before = *revision_base_;
  if (!output.processed || !ValidBody(output.text)) {
    restoreRevisionLocked(output.error.empty() ? "修改结果无效，原正文已保留"
                                               : output.error);
    return;
  }
  restoreRevisionLocked("");
  const auto previous_body = before.value("preferred_raw", false)
                                 ? before.at("raw")
                                 : before.at("text");
  revision_undo_.push_back(std::move(before));
  // Bound retained full versions to avoid unbounded memory use in long
  // sessions.
  if (revision_undo_.size() > 20)
    revision_undo_.erase(revision_undo_.begin());
  preferred_raw_ = false;
  state_["preferred_raw"] = false;
  state_["asr_candidates_active"] = false;
  state_["change_before"] = previous_body;
  state_["result_source"] = "voice";
  updateLocked({{"phase", "ready"}, {"text", output.text}});
  // Even an unchanged successful response creates a new version/undo boundary.
  if (revision_base_version_ == result_revision_)
    ++result_revision_;
  // A successful new body supersedes previous processing failures, while the
  // original recognition uncertainty remains visible through every edit.
  state_["error"] = state_.value("recognition_warning", std::string());
  if (!output.error.empty()) {
    auto previous = state_.at("error").get<std::string>();
    state_["error"] =
        previous.empty() ? output.error : previous + "\n" + output.error;
  }
  rewrite_request_.reset();
}
void App::undoRevision() {
  std::lock_guard lock(mutex_);
  if (busy_ || delivered_ || state_.at("phase") != "ready" ||
      revision_undo_.empty())
    throw std::runtime_error("没有可撤销的语音修改");
  state_ = std::move(revision_undo_.back());
  revision_undo_.pop_back();
  preferred_raw_ = state_.value("preferred_raw", false);
  ++session_version_;
  ++result_revision_;
  manual_confirmation_ = true;
  rewrite_request_.reset();
  review_token_.clear();
  reviewed_guard_ = Json();
  reviewed_text_.clear();
  state_["review_token"] = "";
  state_["target_note"] = "";
  state_["command_mode"] = false;
}
void App::editResult(const std::string &arg, bool fromDraft) {
  auto edit = Json::parse(arg);
  auto text = edit.at("text").get<std::string>();
  if (!ValidBody(text))
    throw std::runtime_error("正文不能为空或包含无效文字；原正文已保留");
  std::lock_guard lock(mutex_);
  if (!fromDraft && (editing_ || draftPendingLocked()))
    throw std::runtime_error("请先完成或放弃编辑草稿；正文没有被覆盖");
  if (busy_ || delivered_ || state_.at("phase") != "ready" ||
      edit.at("session_id") != session_id_ ||
      edit.at("base_revision") != result_revision_)
    throw std::runtime_error("本次结果已变化，未保存过期编辑");
  state_["change_before"] =
      preferred_raw_ ? state_.at("raw") : state_.at("text");
  state_["result_source"] = "manual";
  state_["asr_candidates_active"] = false;
  preferred_raw_ = false;
  ++session_version_;
  ++result_revision_;
  manual_confirmation_ = true;
  rewrite_request_.reset();
  revision_undo_.clear();
  review_token_.clear();
  reviewed_guard_ = Json();
  reviewed_text_.clear();
  state_.update({{"text", text},
                 {"preferred_raw", false},
                 {"review_token", ""},
                 {"target_note", ""}});
}
bool App::draftPendingLocked() const {
  return have_draft_ &&
         draft_text_ != (preferred_raw_ ? state_.at("raw") : state_.at("text"))
                            .get_ref<const std::string &>();
}
void App::beginEdit() {
  auto refreshed = desktop_.rebind(target_);
  std::lock_guard lock(mutex_);
  const auto body = (preferred_raw_ ? state_.at("raw") : state_.at("text"))
                        .get<std::string>();
  if (busy_ || delivered_ || state_.at("phase") != "ready" || !ValidBody(body))
    throw std::runtime_error("没有可编辑的待确认正文");
  if (!have_draft_) {
    draft_text_ = body;
    draft_session_id_ = session_id_;
    draft_base_revision_ = result_revision_;
  }
  target_ = refreshed;
  have_draft_ = editing_ = focus_editor_ = true;
  manual_confirmation_ = true;
  active_candidate_ = -1;
  review_token_.clear();
  state_["action_note"] = "";
}
void App::setDraft(const std::string &text) {
  if (!g_utf8_validate(text.data(), text.size(), nullptr) ||
      text.find('\0') != std::string::npos)
    throw std::runtime_error("草稿含无效文字；原草稿仍保留");
  std::lock_guard lock(mutex_);
  if (!editing_ || !have_draft_)
    throw std::runtime_error("请先进入整段编辑");
  draft_text_ = text;
}
void App::focusEditor() {
  focus_editor_ = false;
  const auto display_name = desktop_.editingDisplay(target_);
  if (display_name.empty()) {
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window_),
                                GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    gtk_widget_grab_focus(editor_);
    return;
  }
  // A mirrored primary overlay is deliberately not a keyboard takeover surface.
  // Put this editor on the original seat's actual Wayland display instead.
  if (secondary_editor_window_ && secondary_display_name_ != display_name) {
    gtk_window_destroy(GTK_WINDOW(secondary_editor_window_));
    secondary_editor_window_ = secondary_editor_ = nullptr;
    secondary_editor_note_ = nullptr;
    gdk_display_close(secondary_editor_display_);
    g_clear_object(&secondary_editor_display_);
  }
  if (!secondary_editor_window_) {
    secondary_editor_display_ = gdk_display_open(display_name.c_str());
    if (!secondary_editor_display_)
      throw std::runtime_error("无法打开原桌面编辑窗口；正文和草稿仍保留");
    g_object_ref(secondary_editor_display_);
    secondary_display_name_ = display_name;
    auto css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, OverlayStyle);
    gtk_style_context_add_provider_for_display(
        secondary_editor_display_, GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    secondary_editor_window_ = gtk_window_new();
    gtk_window_set_display(GTK_WINDOW(secondary_editor_window_),
                           secondary_editor_display_);
    gtk_window_set_title(GTK_WINDOW(secondary_editor_window_),
                         "HyprVoice · 编辑正文");
    gtk_window_set_default_size(GTK_WINDOW(secondary_editor_window_), 552, 340);
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "voice-panel");
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_window_set_child(GTK_WINDOW(secondary_editor_window_), box);
    auto heading = gtk_label_new("编辑正文");
    gtk_widget_add_css_class(heading, "voice-heading");
    gtk_label_set_xalign(GTK_LABEL(heading), 0);
    gtk_box_append(GTK_BOX(box), heading);
    auto scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_box_append(GTK_BOX(box), scroll);
    secondary_editor_ = gtk_text_view_new_with_buffer(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor_)));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(secondary_editor_),
                                GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(secondary_editor_, "voice-editor");
    gtk_accessible_update_property(GTK_ACCESSIBLE(secondary_editor_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "编辑完整正文", -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                  secondary_editor_);
    secondary_editor_note_ = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(secondary_editor_note_), true);
    gtk_label_set_xalign(GTK_LABEL(secondary_editor_note_), 0);
    gtk_widget_add_css_class(secondary_editor_note_, "voice-source");
    gtk_box_append(GTK_BOX(box), secondary_editor_note_);
    auto row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), row);
    auto spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, true);
    gtk_box_append(GTK_BOX(row), spacer);
    for (const auto &[label, action] :
         std::vector<std::pair<const char *, const char *>>{
             {"暂不应用", "edit-cancel"}, {"完成编辑", "edit-save"}}) {
      auto button = gtk_button_new_with_label(label);
      if (std::string(action) == "edit-save")
        gtk_widget_add_css_class(button, "primary");
      g_object_set_data_full(G_OBJECT(button), "command", g_strdup(action),
                             g_free);
      g_signal_connect(
          button, "clicked", G_CALLBACK(+[](GtkButton *b, gpointer data) {
            static_cast<App *>(data)->command(static_cast<const char *>(
                g_object_get_data(G_OBJECT(b), "command")));
          }),
          this);
      gtk_box_append(GTK_BOX(row), button);
    }
    g_signal_connect(secondary_editor_window_, "close-request",
                     G_CALLBACK(+[](GtkWindow *, gpointer data) -> gboolean {
                       static_cast<App *>(data)->command("edit-cancel");
                       return true;
                     }),
                     this);
  }
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_),
                              GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  gtk_window_present(GTK_WINDOW(secondary_editor_window_));
  gtk_widget_grab_focus(secondary_editor_);
}
void App::saveEdit() {
  Json edit;
  {
    std::lock_guard lock(mutex_);
    if (!have_draft_)
      throw std::runtime_error("没有待保存的草稿");
    edit = {{"session_id", draft_session_id_},
            {"base_revision", draft_base_revision_},
            {"text", draft_text_}};
  }
  editResult(edit.dump(), true);
  cancelEdit(true);
  update({{"action_note", "编辑已保存，请检查后确认输入"}});
}
void App::cancelEdit(bool discard) {
  {
    std::lock_guard lock(mutex_);
    editing_ = focus_editor_ = false;
    if (discard || !draftPendingLocked()) {
      have_draft_ = false;
      draft_text_.clear();
      draft_session_id_ = 0;
      draft_base_revision_ = 0;
    }
    manual_confirmation_ = true;
  }
  if (secondary_editor_window_)
    gtk_widget_set_visible(secondary_editor_window_, false);
  input_status_refresh_ = true;
  if (window_) {
    gtk_window_set_focus(GTK_WINDOW(window_), nullptr);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window_),
                                GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  }
}
void App::chooseCandidate(const std::string &arg) {
  const auto choice = Json::parse(arg);
  std::lock_guard lock(mutex_);
  if (busy_ || delivered_ || editing_ || draftPendingLocked() ||
      state_.at("phase") != "ready" ||
      !state_.value("asr_candidates_active", false) ||
      choice.at("session_id") != session_id_ ||
      choice.at("base_revision") != result_revision_)
    throw std::runtime_error("本次正文已变化，旧候选未覆盖当前文字");
  const auto index = choice.at("index").get<size_t>();
  const auto source = choice.at("source").get<std::string>();
  auto &parts = state_.at("asr_parts");
  if (index >= parts.size() || !parts[index].value("selectable", false) ||
      (source != "streaming" && source != "refined"))
    throw std::runtime_error("没有该来源的识别候选");
  parts[index]["selected"] = source;
  parts[index]["checked"] = true;
  state_["text"] = ChoiceText(parts);
  state_["result_source"] = "asr";
  state_["preferred_raw"] = preferred_raw_ = false;
  ++result_revision_;
  ++session_version_;
  manual_confirmation_ = true;
  revision_undo_.clear();
  rewrite_request_.reset();
  reviewed_guard_ = Json();
  review_token_.clear();
  state_["review_token"] = "";
  state_["action_note"] = "候选已采用；请检查后确认输入";
  active_candidate_ = -1;
}
void App::recoverResult(uint64_t version, const Transcript &result,
                        const std::string &preview, const std::string &error) {
  std::lock_guard lock(mutex_);
  if (version != session_version_ || cancel_)
    return;
  const auto text = ValidBody(result.text) ? result.text : preview;
  const auto warning =
      text.empty()
          ? "录音中断，未能恢复可用文字；请重新录音。"
          : "录音中断，已保留可识别内容；结果可能不完整，请检查后确认输入。";
  manual_confirmation_ = true;
  preferred_raw_ = false;
  rewrite_request_.reset();
  updateLocked(
      {{"phase", text.empty() ? "error" : "ready"},
       {"text", text},
       {"raw", text},
       {"preferred_raw", false},
       {"recognition_warning", warning},
       {"error", warning},
       {"capture_error", error},
       {"recovered", !text.empty()},
       {"incomplete", true},
       {"asr_streaming", result.streaming},
       {"asr_refined", result.refined},
       {"result_source", "asr"},
       {"asr_parts", RecognitionChoices(result.streaming, result.refined)},
       {"asr_candidates_active",
        !result.streaming.empty() && !result.refined.empty()}});
}
void App::retry() {
  if (busy_)
    throw std::runtime_error("正在处理本次文字，请稍候");
  auto before = snapshot();
  if (!before.value("retry_available", false) ||
      (before.at("phase") != "ready" && before.at("phase") != "error"))
    throw std::runtime_error("没有可重试的本次文字，请先录音");
  // Privacy checks are local metadata; the retained request is never rebuilt.
  auto current = desktop_.inputTarget(target_);
  if (current.value("protected", false) || !current.value("reliable", false))
    throw std::runtime_error("当前位置受保护或无法核验；未重试发送本次文字");
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
    if (request.command_mode) {
      if ((preferred_raw_ ? state_.at("raw") : state_.at("text")) !=
          request.original_body)
        throw std::runtime_error("正文已变化，请重新口述修改要求");
      revision_base_ = state_;
      revision_cancel_base_ = state_;
      revision_base_version_ = result_revision_;
    }
    version = session_version_;
    manual_confirmation_ = true;
    review_token_.clear();
    reviewed_guard_ = Json();
    preferred_raw_ = false;
    reviewed_text_.clear();
    cancel_ = false;
    busy_ = true;
    updateLocked({{"phase", "retrying"},
                  {"error", ""},
                  {"review_token", ""},
                  {"target_note", ""},
                  {"preferred_raw", false}});
    state_["retry_result"] = true;
  }
  if (worker_.joinable())
    worker_.join();
  worker_ = std::thread([this, request, version] {
    auto output = ProcessText(config_, request, cancel_);
    if (request.command_mode)
      completeRevision(version, output);
    else
      updateSession(
          version,
          {{"phase", "ready"}, {"text", output.text}, {"error", output.error}});
    finishSession(version);
  });
}
void App::selectText(bool raw) {
  auto state = snapshot();
  if (busy_ || delivered_ || state.at("phase") != "ready" ||
      state.at("command_mode").get<bool>())
    throw std::runtime_error("没有可切换显示的听写文字");
  if (preferred_raw_ != raw) {
    ++result_revision_;
    rewrite_request_.reset();
    revision_undo_.clear();
  }
  preferred_raw_ = raw;
  update({{"asr_candidates_active", false}});
  manual_confirmation_ = true;
  review_token_.clear();
  reviewed_guard_ = Json();
  reviewed_text_.clear();
  update({{"preferred_raw", raw}, {"review_token", ""}, {"target_note", ""}});
}
void App::insertCurrent() {
  // An explicit click means input at the user's current caret in the original
  // window. Bind and recheck it inside this action; never ask the user to
  // manage guard tokens. Selection, password, window and final paste checks
  // still apply.
  review();
  const auto token = review_token_;
  confirm(token);
}
void App::commit(bool raw) {
  if (busy_)
    throw std::runtime_error("结果尚未完成");
  auto state = snapshot();
  if (state.at("phase") != "ready")
    throw std::runtime_error("没有待提交的结果");
  preferred_raw_ = raw;
  review_token_.clear();
  reviewed_guard_ = Json();
  reviewed_text_.clear();
  update({{"preferred_raw", raw}, {"review_token", ""}, {"target_note", ""}});
  auto text = state.at(raw ? "raw" : "text").get<std::string>();
  if (origin_guard_.value("selections", Json::array()).size() > 0)
    throw std::runtime_error("普通听写不能覆盖选区；请取消选区后核对当前位置");
  if (capture_changed_)
    throw std::runtime_error("录音期间编辑位置曾变化或无法核验；请核对当前位置"
                             "后确认");
  deliver(text, origin_guard_);
}
void App::deliver(const std::string &text, const Json &guard) {
  try {
    desktop_.paste(target_, text, guard, WindowOnlyInputTarget(guard));
  } catch (const PasteUncertain &error) {
    delivered_ = true;
    {
      std::lock_guard lock(mutex_);
      ++session_version_;
    }
    update({{"phase", "delivery-uncertain"},
            {"text", text},
            {"context", ""},
            {"context_note", ""},
            {"error", error.what()}});
    return;
  }
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
void App::review() {
  auto state = snapshot();
  if (busy_ || delivered_ || state.at("phase") != "ready")
    throw std::runtime_error("没有可确认的本次结果");
  auto next_target = desktop_.rebind(target_);
  auto guard = desktop_.inputTarget(next_target);
  if (guard.value("protected", false))
    throw std::runtime_error("密码输入框不能接收本次结果");
  if (!guard.value("reliable", false) && !WindowOnlyInputTarget(guard))
    throw std::runtime_error("无法核验当前位置；可复制结果后自行粘贴");
  if (!guard.value("selections", Json::array()).empty())
    throw std::runtime_error(
        "当前位置有选区；普通听写不能覆盖选区，请先取消选区");
  target_ = next_target;
  auto uuid = g_uuid_string_random();
  review_token_ = uuid;
  g_free(uuid);
  reviewed_guard_ = guard;
  reviewed_text_ = state.at(preferred_raw_ ? "raw" : "text").get<std::string>();
  reviewed_version_ = session_version_;
  manual_confirmation_ = true;
  update(
      {{"review_token", review_token_},
       {"target_note",
        std::string(preferred_raw_ ? "将插入识别原文。" : "将插入处理结果。") +
            "输入位置已确认，尚未插入；点击“插入到此处”完成。"}});
}
void App::confirm(const std::string &token) {
  auto state = snapshot();
  if (busy_ || delivered_ || state.at("phase") != "ready" || token.empty() ||
      token != review_token_ || reviewed_version_ != session_version_)
    throw std::runtime_error("当前位置确认已失效，请重新核对");
  try {
    deliver(reviewed_text_, reviewed_guard_);
  } catch (...) {
    review_token_.clear();
    reviewed_guard_ = Json();
    update({{"review_token", ""}, {"target_note", "位置再次变化，请重新核对"}});
    throw;
  }
}
void App::copyResult() {
  auto state = snapshot();
  const bool uncertain = state.at("phase") == "delivery-uncertain";
  if (busy_ || (!uncertain && (delivered_ || state.at("phase") != "ready")))
    throw std::runtime_error("没有可复制的结果");
  try {
    desktop_.copy(state.at(preferred_raw_ ? "raw" : "text").get<std::string>(),
                  target_.seat);
  } catch (const std::exception &) {
    throw std::runtime_error("复制失败，文字仍保留，请重试复制。");
  }
  delivered_ = true;
  {
    std::lock_guard lock(mutex_);
    ++session_version_;
  }
  update({{"phase", "idle"},
          {"text", "文字已复制到剪贴板"},
          {"raw", ""},
          {"error", ""}});
  copy_notice_until_ = g_get_monotonic_time() + 5000000;
}
Json App::settingsValues() const {
  return {{"asr", {{"backend", config_.data.at("asr").at("backend")}}},
          {"scene", scene_},
          {"auto_commit", config_.data.at("auto_commit")},
          {"context", {{"enabled", config_.data.at("context").at("enabled")}}}};
}
void App::requireSettingsIdle() {
  if (busy_ || !delivered_ || snapshot().at("phase") == "delivery-uncertain")
    throw std::runtime_error("请先结束、提交或取消本次语音，再打开或保存设置");
}
void App::applySettings(const Json &patch) {
  requireSettingsIdle();
  auto next = config_;
  // Runtime scene selection remains in effect when the legacy backend command
  // saves only its own field. The settings panel explicitly saves all four.
  next.data["scene"] = scene_;
  try {
    ValidateSettingsPatch(patch);
    next.data.merge_patch(patch);
    ValidateConfig(next.data);
  } catch (...) {
    throw std::runtime_error("设置值无效；仅接受后端、有效场景和两个布尔开关");
  }
  auto previous = settingsValues();
  const bool replace = !asr_ || next.data.at("asr").at("backend") !=
                                    config_.data.at("asr").at("backend");
  if (worker_.joinable())
    worker_.join();
  settings_cancel_ = false;
  settings_saving_ = true;
  busy_ = true;
  update({{"settings_note", "正在验证并保存设置…"}});
  worker_ = std::thread([this, next = std::move(next), previous, patch,
                         replace]() mutable {
    auto result = std::make_unique<SettingsResult>(std::move(next));
    result->previous = previous;
    try {
      if (replace) {
        try {
          result->recognizer =
              std::make_unique<Asr>(result->config, &settings_cancel_);
        } catch (...) {
          // Worker error strings and configuration paths may contain private
          // values. Return a fixed local message instead of echoing them.
          throw std::runtime_error(
              settings_cancel_ ? "设置保存已取消"
                               : "新识别后端未能加载；原设置和原后端已保留");
        }
      }
      result->config.source_text =
          SaveSettings(result->config.source_path, result->config.source_text,
                       patch, &settings_cancel_);
      result->success = true;
    } catch (const std::exception &e) {
      result->error = e.what();
    }
    std::lock_guard lock(mutex_);
    settings_result_ = std::move(result);
    // Main-thread publication keeps config_, scene_ and the recognizer
    // coherent. busy_ stays true until that publication, so no session can
    // start in between.
  });
}
void App::finishSettings() {
  std::unique_ptr<SettingsResult> result;
  {
    std::lock_guard lock(mutex_);
    result = std::move(settings_result_);
  }
  if (!result)
    return;
  if (worker_.joinable())
    worker_.join();
  if (result->success) {
    config_ = std::move(result->config);
    if (result->recognizer)
      asr_ = std::move(result->recognizer);
    scene_ = config_.data.at("scene").get<std::string>();
    previous_settings_ = std::move(result->previous);
    update({{"phase", "idle"},
            {"backend", config_.data.at("asr").at("backend")},
            {"scene", scene_},
            {"text", ""},
            {"raw", ""},
            {"error", ""},
            {"model_ready", asr_ != nullptr},
            {"settings_note", "设置已保存并生效；可恢复上次设置为草稿"}});
  } else
    update({{"settings_note", result->error}});
  settings_saving_ = false;
  busy_ = false;
}
Json App::command(const std::string &cmd, const std::string &arg) {
  try {
    if (cmd == "status")
      return {{"ok", true}, {"state", snapshot()}};
    if (cmd == "settings-status")
      return {{"ok", true},
              {"settings", settingsValues()},
              {"saving", settings_saving_},
              {"open", settings_panel_ && settings_panel_->visible()},
              {"message", snapshot().at("settings_note")}};
    if (cmd == "diagnose") {
      if (busy_)
        throw std::runtime_error("正在处理，请稍后刷新本地诊断");
      return {{"ok", true},
              {"diagnostics", LocalDiagnostics(config_, asr_ != nullptr)}};
    }
    if (cmd == "settings") {
      requireSettingsIdle();
      if (!settings_panel_)
        settings_panel_ = std::make_unique<SettingsPanel>(
            [this](const auto &command, const auto &arg) {
              return this->command(command, arg);
            });
      update({{"settings_note", ""}});
      settings_panel_->show(config_, scene_);
      return {{"ok", true}};
    }
    if (cmd == "settings-save") {
      Json patch;
      try {
        patch = Json::parse(arg);
      } catch (...) {
        throw std::runtime_error("设置 JSON 无效；未保存");
      }
      applySettings(patch);
      return {{"ok", true}, {"saving", true}};
    }
    if (cmd == "settings-previous") {
      requireSettingsIdle();
      if (previous_settings_.is_null())
        throw std::runtime_error("本次运行还没有已保存的上次设置");
      return {{"ok", true}, {"settings", previous_settings_}};
    }
    if (cmd == "settings-cancel") {
      settings_cancel_ = true;
      return {{"ok", true}, {"saving", settings_saving_}};
    }
    {
      std::lock_guard lock(mutex_);
      static const std::vector<std::string> body_actions = {"start",
                                                            "press",
                                                            "toggle",
                                                            "command",
                                                            "revise",
                                                            "copy",
                                                            "commit",
                                                            "raw",
                                                            "review",
                                                            "confirm",
                                                            "insert-current",
                                                            "select-text",
                                                            "select-candidate",
                                                            "retry",
                                                            "undo-revision"};
      if ((editing_ || draftPendingLocked()) &&
          std::find(body_actions.begin(), body_actions.end(), cmd) !=
              body_actions.end())
        throw std::runtime_error(
            "请先完成或放弃编辑草稿；当前正文与草稿均已保留");
    }
    if (cmd == "edit-begin")
      beginEdit();
    else if (cmd == "edit-draft")
      setDraft(Json::parse(arg).at("text").get<std::string>());
    else if (cmd == "edit-save")
      saveEdit();
    else if (cmd == "edit-cancel" || cmd == "edit-discard")
      cancelEdit(cmd == "edit-discard");
    else if (cmd == "select-candidate")
      chooseCandidate(arg);
    else if (cmd == "check-target") {
      auto refreshed = desktop_.rebind(target_);
      auto guard = desktop_.inputTarget(refreshed);
      update({{"insertion_status", InsertionStatus(guard)},
              {"action_note", "原目标已核对；仍需点击确认输入"}});
      input_status_refresh_ = true;
    } else if (cmd == "start")
      start(false);
    else if (cmd == "command" || cmd == "revise") {
      auto phase = snapshot().at("phase");
      if (busy_ && snapshot().value("command_mode", false) &&
          (phase == "starting" || phase == "recording"))
        stop_ = true;
      else {
        start(true);
        revision_pressed_ = cmd == "command";
      }
    } else if (cmd == "command-release") {
      if (revision_pressed_ && busy_ && snapshot().value("command_mode", false))
        stop_ = true;
      revision_pressed_ = false;
    } else if (cmd == "stop")
      stop_ = true;
    else if (cmd == "toggle") {
      if (busy_ && !snapshot().value("command_mode", false) &&
          (snapshot()["phase"] == "recording" ||
           snapshot()["phase"] == "starting"))
        stop_ = true;
      else
        start(false);
    } else if (cmd == "press") {
      if (!pressed_) {
        pressed_ = true;
        pressed_at_ = g_get_monotonic_time();
        if (busy_ && !snapshot().value("command_mode", false) &&
            (snapshot()["phase"] == "recording" ||
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
    } else if (cmd == "cancel" || cmd == "discard") {
      copy_notice_until_ = 0;
      if (settings_saving_) {
        settings_cancel_ = true;
        return {{"ok", true}, {"saving", true}};
      }
      pressed_ = false;
      revision_pressed_ = false;
      std::lock_guard lock(mutex_);
      cancel_ = true;
      stop_ = true;
      if (cmd == "cancel" && revision_cancel_base_) {
        revision_base_ = revision_cancel_base_;
        ++session_version_;
        rewrite_request_.reset();
        updateLocked({{"phase", "cancelling"}, {"review_token", ""}});
        return {{"ok", true}, {"state", state_}};
      }
      editing_ = have_draft_ = focus_editor_ = false;
      draft_text_.clear();
      if (window_)
        gtk_layer_set_keyboard_mode(GTK_WINDOW(window_),
                                    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
      delivered_ = true;
      revision_base_.reset();
      revision_cancel_base_.reset();
      ++session_version_;
      rewrite_request_.reset();
      review_token_.clear();
      reviewed_guard_ = Json();
      updateLocked({{"phase", busy_ ? "cancelling" : "idle"},
                    {"text", ""},
                    {"raw", ""},
                    {"error", ""},
                    {"context", ""},
                    {"context_note", ""},
                    {"review_token", ""},
                    {"target_note", ""}});
    } else if (cmd == "undo-revision")
      undoRevision();
    else if (cmd == "edit-result")
      editResult(arg);
    else if (cmd == "retry")
      retry();
    else if (cmd == "commit")
      commit(preferred_raw_);
    else if (cmd == "raw")
      commit(true);
    else if (cmd == "select-text") {
      if (arg != "raw" && arg != "processed")
        throw std::runtime_error("请选择识别文字或处理后文字");
      selectText(arg == "raw");
    } else if (cmd == "insert-current")
      insertCurrent();
    else if (cmd == "review")
      review();
    else if (cmd == "confirm")
      confirm(arg);
    else if (cmd == "copy")
      copyResult();
    else if (cmd == "backend") {
      if (arg != "fun" && arg != "x-asr")
        throw std::runtime_error("Backend must be fun or x-asr");
      requireSettingsIdle();
      if (!asr_ || config_.data.at("asr").at("backend") != arg)
        applySettings({{"asr", {{"backend", arg}}}});
    } else if (cmd == "scene") {
      if (arg != "raw" && !config_.data.at("prompts").contains(arg))
        throw std::runtime_error("Unknown scene");
      if (busy_ || snapshot().value("retry_available", false))
        throw std::runtime_error("请先提交或取消本次结果，再切换场景");
      scene_ = arg;
      update({{"scene", scene_}});
    } else if (cmd == "quit") {
      settings_cancel_ = true;
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
    if (cmd.starts_with("settings") || cmd == "diagnose" ||
        (settings_panel_ && settings_panel_->visible() && delivered_)) {
      if (!busy_)
        update({{"settings_note", e.what()}});
    } else if (!busy_) {
      if (cmd == "commit" || cmd == "insert-current" || cmd == "raw" ||
          cmd == "review" || cmd == "confirm") {
        // Only local guard metadata determines the fallback. Paste protection
        // and the text/context sent to a model are unchanged.
        if (!delivered_) {
          try {
            if (desktop_.target().address == target_.address)
              update({{"insertion_status",
                       InsertionStatus(desktop_.inputTarget(target_))}});
          } catch (const std::exception &) {
            update({{"insertion_status", "unknown"}});
          }
        }
      }
      const auto rejected = snapshot();
      if (rejected.at("phase") == "ready" &&
          (rejected.value("editing", false) ||
           rejected.value("draft_pending", false) || cmd.starts_with("edit-") ||
           cmd == "check-target" || cmd == "select-candidate" ||
           cmd == "copy" || cmd == "undo-revision" || cmd == "select-text" ||
           cmd == "revise" || cmd == "command"))
        update({{"action_note", e.what()}});
      else
        update({{"error", e.what()}});
    }
    return {{"ok", false}, {"error", e.what()}};
  }
}
void App::registerOverlay() {
  if (overlay_socket_ >= 0) {
    pollfd state{overlay_socket_, POLLIN | POLLHUP, 0};
    if (poll(&state, 1, 0) > 0 && (state.revents & (POLLHUP | POLLERR))) {
      close(overlay_socket_);
      overlay_socket_ = -1;
    } else
      return;
  }
  if (g_get_monotonic_time() < overlay_retry_at_)
    return;
  overlay_retry_at_ = g_get_monotonic_time() + 1000000;
  const auto signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE"),
             runtime = std::getenv("XDG_RUNTIME_DIR");
  if (!signature || !runtime)
    return;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return;
  try {
    auto hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, signature, -1);
    const auto broker = std::string(runtime) + "/cornice/ipc-" +
                        std::string(hash).substr(0, 32) + "/desktop.sock";
    g_free(hash);
    const auto address = Address(broker);
    if (connect(fd, reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) < 0)
      throw std::runtime_error("No desktop broker");
    ucred peer{};
    socklen_t peer_size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) < 0 ||
        peer.uid != getuid())
      throw std::runtime_error("Untrusted desktop broker");
    const auto message =
        Json{{"id", "voice-" + std::to_string(getpid())},
             {"method", "register-local-overlay"},
             {"params", {{"namespaces", Json::array({"hyprvoice"})}}}}
            .dump() +
        "\n";
    if (send(fd, message.data(), message.size(), MSG_NOSIGNAL) !=
        static_cast<ssize_t>(message.size()))
      throw std::runtime_error("Registration write failed");
    pollfd response{fd, POLLIN, 0};
    if (poll(&response, 1, 1500) <= 0 || !Json::parse(Receive(fd))
                                              .value("result", Json::object())
                                              .value("registered", false))
      throw std::runtime_error("Registration unavailable");
    overlay_socket_ = fd;
  } catch (...) {
    close(fd);
  }
}
void App::sockets() {
  registerOverlay();
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
  finishSettings();
  sockets();
  focusEvents();
  auto state = snapshot();
  if (settings_panel_)
    settings_panel_->tick(settings_saving_, state.at("settings_note"));
  std::string phase = state.at("phase");
  if (!busy_ && phase == "ready" && !delivered_ &&
      !state.value("manual_confirmation", false) &&
      config_.data.value("auto_commit", true) &&
      !state.at("command_mode").get<bool>()) {
    if (!focus_changed_ && state.at("error").get<std::string>().empty()) {
      auto reply = command("commit");
      if (!reply.value("ok", false))
        focus_changed_ = true;
    } else if (focus_changed_ && state.at("error").get<std::string>().empty())
      update({{"error", "录音期间窗口已变化；请回到原窗口后点击提交"}});
    state = snapshot();
    phase = state.at("phase");
  }
  if (!busy_ && !delivered_ && phase == "ready" &&
      !state.at("command_mode").get<bool>() && !editing_ &&
      (input_status_refresh_ || display_phase_ != phase)) {
    // Only refresh the button's availability. The captured token stays stale
    // until an explicit Input action rebinds the original window and seat.
    input_status_refresh_ = false;
    std::string insertion = "unknown";
    try {
      insertion =
          InsertionStatus(desktop_.inputTarget(desktop_.rebind(target_)));
    } catch (const std::exception &) {
    }
    update({{"insertion_status", insertion}});
    state = snapshot();
  }
  const bool recording = phase == "recording";
  const bool processing = busy_ && !recording;
  const bool uncertain = phase == "delivery-uncertain";
  const bool ready = phase == "ready" || uncertain;
  const bool editing = state.value("editing", false);
  const bool pending_draft = state.value("draft_pending", false);
  const bool modifying = state.value("command_mode", false);
  const bool copied =
      phase == "idle" && g_get_monotonic_time() < copy_notice_until_;
  const auto insertion = state.value("insertion_status", "unknown");
  const bool can_insert = ready && !uncertain && !busy_ && !editing &&
                          !pending_draft &&
                          (insertion == "available" || insertion == "window");
  const bool can_act =
      ready && !uncertain && !busy_ && !editing && !pending_draft;
  const auto &parts = state.at("asr_parts");
  const auto revision = state.at("result_revision").get<uint64_t>();
  const auto session = state.at("session_id").get<uint64_t>();
  if (revision != display_result_revision_ || session != display_session_id_) {
    compare_result_ = false;
    active_candidate_ = -1;
    display_result_revision_ = revision;
    display_session_id_ = session;
  }
  const bool choices_active = state.value("asr_candidates_active", false);
  size_t different = 0, unchecked = 0;
  for (const auto &p : parts)
    if (p.value("selectable", false)) {
      ++different;
      unchecked += !p.value("checked", false);
    }
  const auto before = state.value("change_before", std::string());
  const auto effective = state.at("effective_text").get<std::string>();
  const bool changes = !before.empty() && before != effective;
  std::string status = ready              ? "已识别"
                       : phase == "error" ? "未完成"
                       : recording ? (modifying ? "正在听修改要求" : "正在听")
                       : copied    ? "已复制"
                       : phase == "loading" ? "准备中"
                                            : "正在处理";
  if (editing)
    status = "编辑草稿";
  else if (state.value("incomplete", false))
    status = "可能不完整";
  else if (!state.value("revision_error", std::string()).empty())
    status = "修改未完成 · 已保留原文";
  else if (ready && different && choices_active)
    status = unchecked ? std::to_string(unchecked) + "处待校对" : "已校对";
  else if (ready && state.value("result_source", "asr") == "voice")
    status = "语音修改 · 待确认";
  else if (ready && state.value("result_source", "asr") == "manual")
    status = "已手动编辑";
  gtk_label_set_text(GTK_LABEL(title_), status.c_str());
  if (state.value("incomplete", false) || unchecked)
    gtk_widget_add_css_class(title_, "attention");
  else
    gtk_widget_remove_css_class(title_, "attention");
  gtk_spinner_set_spinning(GTK_SPINNER(spinner_), processing);
  gtk_widget_set_visible(spinner_, processing);
  auto seconds = static_cast<int>(state.at("seconds").get<double>());
  auto duration = std::to_string(seconds / 60) + ":" +
                  (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
  gtk_label_set_text(GTK_LABEL(duration_), duration.c_str());
  gtk_widget_set_visible(duration_, recording);
  std::string body = effective;
  if (phase == "loading" || (phase == "starting" && !modifying))
    body.clear();
  if (body.empty())
    body = recording ? (state.at("backend") == "fun" ? "结束录音后会显示文字…"
                                                     : "开始说话…")
           : phase == "error" ? "没有恢复到可用文字。"
                              : "正在准备识别…";
  const auto key = phase + ":" +
                   std::to_string(state.at("result_revision").get<uint64_t>()) +
                   ":" + std::to_string(compare_result_) + ":" +
                   std::to_string(active_candidate_) + ":" + body;
  if (key != rendering_key_) {
    std::string markup = EscapeMarkup(body);
    if (ready && choices_active && different && !editing)
      markup = ChoiceMarkup(parts, true, active_candidate_);
    else if (ready && changes && !editing)
      markup = ChangeMarkup(before, effective, compare_result_);
    gtk_label_set_markup(GTK_LABEL(text_), markup.c_str());
    rendering_key_ = key;
    if (phase != display_phase_ && ready) {
      auto adj =
          gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll_));
      gtk_adjustment_set_value(adj, 0);
    }
  }
  if (editing && state.at("draft_text").get<std::string>() != rendered_draft_) {
    setting_editor_ = true;
    rendered_draft_ = state.at("draft_text").get<std::string>();
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor_)),
                             rendered_draft_.data(), rendered_draft_.size());
    setting_editor_ = false;
  }
  const bool secondary_edit =
      editing && !target_.seat.empty() && target_.seat != "Hyprland";
  gtk_stack_set_visible_child_name(
      GTK_STACK(body_stack_), editing && !secondary_edit ? "edit" : "read");
  if (focus_editor_) {
    try {
      focusEditor();
    } catch (const std::exception &e) {
      cancelEdit(false);
      update({{"action_note", e.what()}});
    }
  }
  gtk_widget_set_visible(compare_button_,
                         ready && !editing && !choices_active && changes);
  gtk_button_set_label(GTK_BUTTON(compare_button_), compare_result_ ? "返回阅读"
                                                    : choices_active
                                                        ? "识别差异"
                                                        : "查看修改");
  gtk_widget_set_sensitive(compare_button_, can_act);
  gtk_widget_set_tooltip_text(
      compare_button_, choices_active ? "比较实时与精修音频识别"
                                      : "比较文字修改前后；不是音频识别候选");
  const bool candidate_open =
      choices_active && can_act && active_candidate_ >= 0 &&
      static_cast<size_t>(active_candidate_) < parts.size() &&
      parts[active_candidate_].value("selectable", false);
  gtk_widget_set_visible(candidate_scroll_, candidate_open);
  if (candidate_open) {
    const auto &part = parts[active_candidate_];
    gtk_label_set_text(GTK_LABEL(candidate_note_),
                       part.value("granularity", "fragment") == "passage"
                           ? "文本较长 · 整段来源对照，不作字词定位"
                           : "音频识别来源 · 仅此片段不同");
    const auto source = part.at("selected").get<std::string>();
    const auto alternate = source == "refined" ? "streaming" : "refined";
    for (auto [button, which, current] :
         std::vector<std::tuple<GtkWidget *, std::string, bool>>{
             {candidate_current_, source, true},
             {candidate_alternative_, alternate, false}}) {
      auto value = part.at(which).get<std::string>();
      const auto label =
          EscapeMarkup(value.empty() ? "〔不含此段文字〕" : value) +
          "\n<span size=\"9216\" foreground=\"#617086\">" +
          (which == "refined" ? "精修识别" : "实时识别") +
          (current ? " · 保留当前" : " · 点击采用") + "</span>";
      auto child = gtk_button_get_child(GTK_BUTTON(button));
      gtk_label_set_markup(GTK_LABEL(child), label.c_str());
      auto request = new Json{{"session_id", state.at("session_id")},
                              {"base_revision", state.at("result_revision")},
                              {"index", active_candidate_},
                              {"source", which}};
      g_object_set_data_full(
          G_OBJECT(button), "choice", request,
          +[](gpointer p) { delete static_cast<Json *>(p); });
    }
  }
  std::string warning;
  auto error = state.at("error").get<std::string>();
  if (editing)
    warning = state.value("action_note", std::string());
  else if (state.value("incomplete", false))
    warning = effective.empty()
                  ? "录音中断，未能恢复文字；请重新录音。"
                  : "录音中断，已保留识别内容。请检查后确认输入。";
  else if (uncertain)
    warning = "无法确认是否已输入，请先检查原输入框，避免重复输入。";
  else if (!state.value("revision_error", std::string()).empty())
    warning = "这次语音修改未完成，原正文已保留；可重新口述修改。";
  else if (!state.value("recognition_warning", std::string()).empty())
    warning = state.at("recognition_warning").get<std::string>();
  else if (!error.empty())
    warning = error;
  gtk_label_set_text(GTK_LABEL(warning_), warning.c_str());
  gtk_widget_set_visible(warning_, !warning.empty());
  gtk_widget_set_tooltip_text(warning_,
                              state.value("capture_error", error).c_str());
  const auto instruction = state.value("revision_instruction", std::string());
  const auto instruction_note = instruction.empty()
                                    ? "修改要求单独识别，原正文保持不变。"
                                    : "修改要求：" + instruction;
  gtk_label_set_text(GTK_LABEL(instruction_), instruction_note.c_str());
  gtk_widget_set_visible(instruction_, modifying);
  auto action_note = state.value("action_note", std::string());
  if (editing || action_note == warning)
    action_note.clear();
  if (copied)
    action_note = "已复制。回到输入框后粘贴即可。";
  if (recording)
    action_note = modifying ? "结束口述后先检查新版；不会自动输入。"
                            : RecordingHint(false, pressed_);
  gtk_label_set_text(GTK_LABEL(hint_), action_note.c_str());
  gtk_widget_set_visible(hint_, !action_note.empty());
  const auto target_label =
      can_insert ? "输入到 " + target_.app : "请回原窗口，再点“核对目标”";
  gtk_label_set_text(GTK_LABEL(target_note_), target_label.c_str());
  gtk_widget_set_visible(target_note_, ready && !editing);
  gtk_widget_set_visible(target_button_, ready && !editing);
  gtk_widget_set_visible(copy_button_, ready && !editing);
  gtk_widget_set_sensitive(copy_button_,
                           ready && !busy_ && !editing && !pending_draft);
  gtk_widget_set_visible(edit_button_, ready && !uncertain && !editing);
  gtk_widget_set_sensitive(edit_button_, can_act);
  gtk_widget_set_visible(voice_button_, ready && !uncertain && !editing);
  gtk_widget_set_sensitive(voice_button_, can_act);
  gtk_widget_set_visible(commit_button_, ready && !editing);
  gtk_widget_set_sensitive(commit_button_, can_insert);
  gtk_widget_set_visible(save_button_, editing && !secondary_edit);
  gtk_widget_set_sensitive(save_button_,
                           ValidBody(state.value("draft_text", std::string())));
  gtk_widget_set_visible(pause_button_, editing && !secondary_edit);
  gtk_widget_set_visible(resume_button_, pending_draft && !editing);
  gtk_widget_set_visible(discard_draft_button_, pending_draft && !editing);
  gtk_label_set_text(GTK_LABEL(draft_note_),
                     editing ? "草稿独立保留 · 完成后再确认输入"
                             : "有尚未应用的编辑草稿");
  gtk_widget_set_visible(draft_note_, (editing && warning.empty()) ||
                                          (pending_draft && !editing));
  if (secondary_edit && secondary_editor_window_ && secondary_editor_note_) {
    gtk_label_set_text(GTK_LABEL(secondary_editor_note_),
                       warning.empty() ? "草稿独立保留 · 完成后再确认输入"
                                       : warning.c_str());
    if (warning.empty())
      gtk_widget_remove_css_class(secondary_editor_note_, "voice-warning");
    else
      gtk_widget_add_css_class(secondary_editor_note_, "voice-warning");
  }
  gtk_widget_set_visible(
      undo_button_, state.value("undo_revision_available", false) && can_act);
  gtk_widget_set_visible(retry_button_, state.value("retry_available", false) &&
                                            !editing && !pending_draft &&
                                            !busy_ && !error.empty());
  gtk_widget_set_visible(stop_button_, recording);
  gtk_widget_set_visible(cancel_button_, !editing);
  if (recording) {
    std::move(meter_history_.begin() + 1, meter_history_.end(),
              meter_history_.begin());
    meter_history_.back() = std::clamp(std::sqrt(level_.load()), 0.0f, 1.0f);
    gtk_widget_queue_draw(meter_);
  }
  gtk_widget_set_visible(meter_, recording && !modifying);
  gtk_widget_set_tooltip_text(cancel_button_,
                              modifying ? "取消本次语音修改，保留原正文"
                                        : "放弃本次结果");
  if (!editing && secondary_editor_window_)
    gtk_widget_set_visible(secondary_editor_window_, false);
  gtk_widget_set_visible(
      window_, (phase != "idle" || !error.empty() || copied) &&
                   !(editing && secondary_editor_window_ &&
                     gtk_widget_get_visible(secondary_editor_window_)));
  int screen_height = 720;
  auto monitors = gdk_display_get_monitors(gdk_display_get_default());
  if (g_list_model_get_n_items(monitors)) {
    auto monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
    GdkRectangle bounds{};
    gdk_monitor_get_geometry(monitor, &bounds);
    screen_height = bounds.height;
    g_object_unref(monitor);
  }
  const auto max_body =
      std::clamp(screen_height - 380 - (candidate_open ? 220 : 0) -
                     (!warning.empty() ? 70 : 0),
                 96, 280);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll_),
                                             max_body);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(edit_scroll_),
                                             std::min(max_body, 220));
  display_phase_ = phase;
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
  auto button = [this](const char *name, const char *action, const char *icon,
                       const char *label = "", bool primary = false) {
    auto b = gtk_button_new();
    auto child = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    if (icon && *icon) {
      auto image = gtk_image_new_from_icon_name(icon);
      gtk_image_set_pixel_size(GTK_IMAGE(image), 17);
      gtk_box_append(GTK_BOX(child), image);
    }
    if (label && *label)
      gtk_box_append(GTK_BOX(child), gtk_label_new(label));
    gtk_button_set_child(GTK_BUTTON(b), child);
    gtk_widget_add_css_class(b, primary               ? "primary"
                                : (!label || !*label) ? "icon-only"
                                                      : "secondary");
    gtk_accessible_update_property(GTK_ACCESSIBLE(b),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, name, -1);
    gtk_widget_set_tooltip_text(b, name);
    g_object_set_data_full(G_OBJECT(b), "command", g_strdup(action), g_free);
    g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton *b, gpointer data) {
                       static_cast<App *>(data)->command(
                           static_cast<const char *>(
                               g_object_get_data(G_OBJECT(b), "command")));
                     }),
                     this);
    return b;
  };
  auto outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_top(outer, 16);
  gtk_widget_set_margin_bottom(outer, 20);
  gtk_widget_set_margin_start(outer, 20);
  gtk_widget_set_margin_end(outer, 20);
  gtk_window_set_child(GTK_WINDOW(window_), outer);
  auto panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(panel, "voice-panel");
  gtk_box_append(GTK_BOX(outer), panel);
  auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 22);
  gtk_widget_set_margin_end(box, 22);
  gtk_widget_set_size_request(box, 508, -1);
  gtk_box_append(GTK_BOX(panel), box);
  auto header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(box), header);
  icon_ = gtk_image_new_from_icon_name("audio-input-microphone-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon_), 18);
  gtk_box_append(GTK_BOX(header), icon_);
  mode_ = gtk_label_new("HyprVoice");
  gtk_widget_add_css_class(mode_, "voice-heading");
  gtk_box_append(GTK_BOX(header), mode_);
  auto spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(spacer, true);
  gtk_box_append(GTK_BOX(header), spacer);
  title_ = gtk_label_new("");
  gtk_widget_add_css_class(title_, "voice-status");
  gtk_box_append(GTK_BOX(header), title_);
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
  gtk_box_append(GTK_BOX(header), spinner_);
  cancel_button_ = button("放弃本次结果", "cancel", "window-close-symbolic");
  gtk_box_append(GTK_BOX(header), cancel_button_);
  compare_button_ = gtk_button_new_with_label("识别差异");
  gtk_widget_add_css_class(compare_button_, "small");
  gtk_widget_set_halign(compare_button_, GTK_ALIGN_START);
  g_signal_connect(compare_button_, "clicked",
                   G_CALLBACK(+[](GtkButton *, gpointer data) {
                     auto self = static_cast<App *>(data);
                     self->compare_result_ = !self->compare_result_;
                     self->active_candidate_ = -1;
                   }),
                   this);
  gtk_box_append(GTK_BOX(box), compare_button_);
  body_stack_ = gtk_stack_new();
  gtk_stack_set_vhomogeneous(GTK_STACK(body_stack_), false);
  gtk_stack_set_hhomogeneous(GTK_STACK(body_stack_), false);
  gtk_box_append(GTK_BOX(box), body_stack_);
  auto scroller = [] {
    auto w = gtk_scrolled_window_new();
    gtk_widget_add_css_class(w, "voice-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(w), 508);
    gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(w), 508);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(w),
                                                     true);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(w), 260);
    return w;
  };
  scroll_ = scroller();
  gtk_stack_add_named(GTK_STACK(body_stack_), scroll_, "read");
  text_ = gtk_label_new("");
  gtk_widget_add_css_class(text_, "voice-transcript");
  gtk_label_set_wrap(GTK_LABEL(text_), true);
  gtk_label_set_wrap_mode(GTK_LABEL(text_), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign(GTK_LABEL(text_), 0);
  gtk_label_set_yalign(GTK_LABEL(text_), 0);
  gtk_label_set_max_width_chars(GTK_LABEL(text_), 54);
  gtk_widget_set_hexpand(text_, true);
  g_signal_connect(
      text_, "activate-link",
      G_CALLBACK(+[](GtkLabel *, const char *uri, gpointer data) -> gboolean {
        auto self = static_cast<App *>(data);
        const auto link = std::string(uri);
        if (!link.starts_with("candidate:"))
          return true;
        try {
          self->active_candidate_ = std::stoi(link.substr(10));
        } catch (const std::exception &) {
          self->active_candidate_ = -1;
        }
        return true;
      }),
      this);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_), text_);
  edit_scroll_ = scroller();
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(edit_scroll_),
                                             140);
  gtk_stack_add_named(GTK_STACK(body_stack_), edit_scroll_, "edit");
  editor_ = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor_), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(editor_), 1);
  gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(editor_), 1);
  gtk_widget_add_css_class(editor_, "voice-editor");
  gtk_accessible_update_property(GTK_ACCESSIBLE(editor_),
                                 GTK_ACCESSIBLE_PROPERTY_LABEL, "编辑完整正文",
                                 -1);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(edit_scroll_), editor_);
  g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor_)), "changed",
                   G_CALLBACK(+[](GtkTextBuffer *buffer, gpointer data) {
                     auto self = static_cast<App *>(data);
                     if (self->setting_editor_)
                       return;
                     GtkTextIter start;
                     GtkTextIter end;
                     gtk_text_buffer_get_bounds(buffer, &start, &end);
                     auto value =
                         gtk_text_buffer_get_text(buffer, &start, &end, false);
                     try {
                       self->setDraft(value);
                       self->rendered_draft_ = value;
                     } catch (const std::exception &e) {
                       self->update({{"action_note", e.what()}});
                     }
                     g_free(value);
                   }),
                   this);
  candidate_scroll_ = scroller();
  gtk_scrolled_window_set_max_content_height(
      GTK_SCROLLED_WINDOW(candidate_scroll_), 220);
  gtk_box_append(GTK_BOX(box), candidate_scroll_);
  candidate_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class(candidate_box_, "voice-choice-panel");
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(candidate_scroll_),
                                candidate_box_);
  candidate_note_ = gtk_label_new("音频识别来源 · 仅此片段不同");
  gtk_widget_add_css_class(candidate_note_, "voice-source");
  gtk_label_set_xalign(GTK_LABEL(candidate_note_), 0);
  gtk_box_append(GTK_BOX(candidate_box_), candidate_note_);
  for (auto &candidate : {&candidate_current_, &candidate_alternative_}) {
    *candidate = gtk_button_new();
    gtk_widget_add_css_class(*candidate, "choice");
    if (candidate == &candidate_current_)
      gtk_widget_add_css_class(*candidate, "current");
    auto label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(label), true);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 50);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_button_set_child(GTK_BUTTON(*candidate), label);
    g_signal_connect(
        *candidate, "clicked", G_CALLBACK(+[](GtkButton *b, gpointer data) {
          if (auto choice =
                  static_cast<Json *>(g_object_get_data(G_OBJECT(b), "choice")))
            static_cast<App *>(data)->command("select-candidate",
                                              choice->dump());
        }),
        this);
    gtk_box_append(GTK_BOX(candidate_box_), *candidate);
  }
  instruction_ = gtk_label_new("");
  gtk_widget_add_css_class(instruction_, "voice-instruction");
  gtk_label_set_wrap(GTK_LABEL(instruction_), true);
  gtk_label_set_max_width_chars(GTK_LABEL(instruction_), 56);
  gtk_label_set_xalign(GTK_LABEL(instruction_), 0);
  gtk_box_append(GTK_BOX(box), instruction_);
  warning_ = gtk_label_new("");
  gtk_widget_add_css_class(warning_, "voice-warning");
  gtk_label_set_wrap(GTK_LABEL(warning_), true);
  gtk_label_set_max_width_chars(GTK_LABEL(warning_), 56);
  gtk_label_set_xalign(GTK_LABEL(warning_), 0);
  gtk_box_append(GTK_BOX(box), warning_);
  draft_note_ = gtk_label_new("");
  gtk_widget_add_css_class(draft_note_, "voice-source");
  gtk_label_set_xalign(GTK_LABEL(draft_note_), 0);
  gtk_box_append(GTK_BOX(box), draft_note_);
  auto draft_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append(GTK_BOX(box), draft_row);
  resume_button_ = button("继续编辑草稿", "edit-begin",
                          "document-edit-symbolic", "继续编辑草稿");
  discard_draft_button_ =
      button("放弃草稿", "edit-discard", "edit-undo-symbolic", "放弃草稿");
  gtk_box_append(GTK_BOX(draft_row), resume_button_);
  gtk_box_append(GTK_BOX(draft_row), discard_draft_button_);
  auto divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class(divider, "voice-divider");
  gtk_box_append(GTK_BOX(box), divider);
  auto target_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append(GTK_BOX(box), target_row);
  target_note_ = gtk_label_new("");
  gtk_widget_add_css_class(target_note_, "voice-target");
  gtk_label_set_xalign(GTK_LABEL(target_note_), 0);
  gtk_label_set_ellipsize(GTK_LABEL(target_note_), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(target_note_, true);
  gtk_box_append(GTK_BOX(target_row), target_note_);
  target_button_ =
      button("核对目标", "check-target", "view-reveal-symbolic", "核对目标");
  gtk_widget_add_css_class(target_button_, "small");
  gtk_box_append(GTK_BOX(target_row), target_button_);
  auto actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_append(GTK_BOX(box), actions);
  copy_button_ = button("复制正文", "copy", "edit-copy-symbolic");
  edit_button_ = button("编辑正文", "edit-begin", "document-edit-symbolic");
  voice_button_ = button("语音修改（F9）", "revise",
                         "audio-input-microphone-symbolic", "F9");
  undo_button_ = button("撤销本次修改", "undo-revision", "edit-undo-symbolic");
  retry_button_ = button("再试一次", "retry", "view-refresh-symbolic");
  pause_button_ =
      button("暂不应用", "edit-cancel", "edit-undo-symbolic", "暂不应用");
  for (auto b : {copy_button_, edit_button_, voice_button_, undo_button_,
                 retry_button_, pause_button_})
    gtk_box_append(GTK_BOX(actions), b);
  auto expand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(expand, true);
  gtk_box_append(GTK_BOX(actions), expand);
  save_button_ = button("完成编辑", "edit-save", "object-select-symbolic",
                        "完成编辑", true);
  commit_button_ = button("确认输入", "insert-current",
                          "object-select-symbolic", "确认输入", true);
  stop_button_ = button("结束录音", "stop", "media-playback-stop-symbolic",
                        "结束录音", true);
  for (auto b : {save_button_, commit_button_, stop_button_})
    gtk_box_append(GTK_BOX(actions), b);
  hint_ = gtk_label_new("");
  gtk_widget_add_css_class(hint_, "voice-hint");
  gtk_label_set_wrap(GTK_LABEL(hint_), true);
  gtk_label_set_max_width_chars(GTK_LABEL(hint_), 56);
  gtk_label_set_xalign(GTK_LABEL(hint_), 0);
  gtk_box_append(GTK_BOX(box), hint_);
  loop_ = g_main_loop_new(nullptr, false);
  busy_ = true;
  worker_ = std::thread([this] {
    try {
      asr_ = std::make_unique<Asr>(config_, &cancel_);
      update({{"phase", "idle"}, {"text", ""}, {"model_ready", true}});
    } catch (const std::exception &e) {
      update({{"phase", "error"},
              {"text", ""},
              {"error", "本地识别模型初始化失败，请检查本地配置"}});
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
    self->settings_cancel_ = true;
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

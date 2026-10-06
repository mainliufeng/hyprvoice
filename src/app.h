#pragma once
#include "asr.h"
#include "audio.h"
#include "desktop.h"
#include "rewrite.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <gtk/gtk.h>
#include <mutex>
#include <optional>
#include <thread>
namespace hv {
class App {
public:
  explicit App(Config config);
  ~App();
  void run();
  Json command(const std::string &cmd, const std::string &arg = "");

private:
  Config config_;
  Desktop desktop_;
  Audio audio_;
  std::unique_ptr<Asr> asr_;
  std::mutex mutex_;
  Json state_;
  std::thread worker_;
  std::atomic<bool> busy_ = false, stop_ = false, cancel_ = false;
  std::atomic<float> level_ = 0;
  bool delivered_ = true, focus_changed_ = false, pressed_ = false;
  gint64 pressed_at_ = 0;
  Target target_;
  std::string selected_, scene_;
  // A pending request keeps target_/selected_ fixed until commit or cancel.
  // mutex_ protects this snapshot, its version, and result publication.
  std::optional<TextRequest> rewrite_request_;
  uint64_t session_version_ = 0;
  bool manual_confirmation_ = false;
  GtkWidget *window_ = nullptr, *title_ = nullptr, *text_ = nullptr,
            *hint_ = nullptr, *meter_ = nullptr, *raw_button_ = nullptr,
            *commit_button_ = nullptr, *stop_button_ = nullptr,
            *retry_button_ = nullptr, *cancel_button_ = nullptr,
            *mode_ = nullptr, *duration_ = nullptr, *spinner_ = nullptr,
            *icon_ = nullptr, *context_ = nullptr, *warning_ = nullptr,
            *scroll_ = nullptr;
  std::array<float, 21> meter_history_{};
  std::string display_text_, display_phase_, recording_text_;
  GMainLoop *loop_ = nullptr;
  int socket_ = -1, lock_ = -1, focus_socket_ = -1;
  std::string focus_events_;
  void start(bool command);
  void commit(bool raw);
  void retry();
  void ui();
  void sockets();
  void focusEvents();
  void update(const Json &patch);
  void updateLocked(const Json &patch);
  void updateSession(uint64_t version, const Json &patch);
  bool rememberRewrite(uint64_t version, const TextRequest &request);
  void finishSession(uint64_t version);
  Json snapshot();
  void bindSocket();
};
Json SendCommand(const std::string &command, const std::string &arg = "");
} // namespace hv

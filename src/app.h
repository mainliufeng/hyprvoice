#pragma once
#include "asr.h"
#include "audio.h"
#include "desktop.h"
#include <atomic>
#include <gtk/gtk.h>
#include <mutex>
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
  std::string selected_, history_, scene_;
  GtkWidget *window_ = nullptr, *title_ = nullptr, *text_ = nullptr,
            *hint_ = nullptr, *meter_ = nullptr, *raw_button_ = nullptr;
  GMainLoop *loop_ = nullptr;
  int socket_ = -1, lock_ = -1, focus_socket_ = -1;
  std::string focus_events_;
  void start(bool command);
  void commit(bool raw);
  void ui();
  void sockets();
  void focusEvents();
  void update(const Json &patch);
  Json snapshot();
  void bindSocket();
};
Json SendCommand(const std::string &command, const std::string &arg = "");
} // namespace hv

#pragma once
#include <atomic>
#include <mutex>
#include <pipewire/pipewire.h>
#include <string>
#include <vector>
namespace hv {
class Audio {
public:
  Audio();
  ~Audio();
  void start(const std::string &source);
  void stop();
  std::vector<float> take();
  std::string error();

private:
  pw_thread_loop *loop_ = nullptr;
  pw_stream *stream_ = nullptr;
  pw_stream_events events_{};
  std::mutex mutex_;
  std::vector<float> queue_;
  std::string error_;
  std::atomic<bool> active_ = false;
  std::atomic<pw_stream_state> state_ = PW_STREAM_STATE_UNCONNECTED;
  static void Process(void *self);
  static void State(void *self, pw_stream_state old, pw_stream_state state,
                    const char *error);
};
} // namespace hv

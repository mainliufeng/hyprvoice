#include "audio.h"
#include <chrono>
#include <spa/param/audio/format-utils.h>
#include <stdexcept>
#include <thread>
namespace hv {
Audio::Audio() {
  pw_init(nullptr, nullptr);
  events_.version = PW_VERSION_STREAM_EVENTS;
  events_.process = Process;
  events_.state_changed = State;
}
Audio::~Audio() {
  stop();
  if (loop_) {
    pw_thread_loop_stop(loop_);
    pw_thread_loop_destroy(loop_);
  }
  pw_deinit();
}
void Audio::Process(void *data) {
  auto &self = *static_cast<Audio *>(data);
  auto b = pw_stream_dequeue_buffer(self.stream_);
  if (!b)
    return;
  auto buf = b->buffer;
  if (self.active_ && buf && buf->n_datas && buf->datas[0].data &&
      buf->datas[0].chunk) {
    auto &d = buf->datas[0];
    auto ch = d.chunk;
    if (ch->offset <= d.maxsize && ch->size <= d.maxsize - ch->offset) {
      auto pcm = reinterpret_cast<const int16_t *>(
          static_cast<const char *>(d.data) + ch->offset);
      size_t n = ch->size / 2;
      std::lock_guard lock(self.mutex_);
      if (self.queue_.size() + n > 16000 * 10) {
        self.error_ = "Audio processing cannot keep up; recording aborted";
        self.active_ = false;
      } else
        for (size_t i = 0; i < n; ++i)
          self.queue_.push_back(pcm[i] / 32768.0f);
    }
  }
  pw_stream_queue_buffer(self.stream_, b);
}
void Audio::State(void *data, pw_stream_state, pw_stream_state state,
                  const char *error) {
  auto &self = *static_cast<Audio *>(data);
  self.state_ = state;
  if (state == PW_STREAM_STATE_ERROR) {
    std::lock_guard lock(self.mutex_);
    self.error_ = error ? error : "PipeWire error";
  }
}
void Audio::start(const std::string &source) {
  stop();
  {
    std::lock_guard lock(mutex_);
    queue_.clear();
    error_.clear();
  }
  if (!loop_) {
    loop_ = pw_thread_loop_new("hyprvoice-audio", nullptr);
    if (!loop_ || pw_thread_loop_start(loop_) < 0)
      throw std::runtime_error("Cannot start PipeWire loop");
  }
  pw_thread_loop_lock(loop_);
  auto props =
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                        "Capture", PW_KEY_MEDIA_ROLE, "Communication", nullptr);
  if (!source.empty())
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, source.c_str());
  stream_ = pw_stream_new_simple(pw_thread_loop_get_loop(loop_), "hyprvoice",
                                 props, &events_, this);
  if (!stream_) {
    pw_thread_loop_unlock(loop_);
    throw std::runtime_error("Cannot create PipeWire stream");
  }
  uint8_t buffer[1024];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  spa_audio_info_raw info{};
  info.format = SPA_AUDIO_FORMAT_S16_LE;
  info.rate = 16000;
  info.channels = 1;
  info.position[0] = SPA_AUDIO_CHANNEL_MONO;
  const spa_pod *params[] = {
      spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
  active_ = true;
  state_ = PW_STREAM_STATE_UNCONNECTED;
  int code = pw_stream_connect(
      stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                   PW_STREAM_FLAG_MAP_BUFFERS |
                                   PW_STREAM_FLAG_RT_PROCESS),
      params, 1);
  pw_thread_loop_unlock(loop_);
  if (code < 0) {
    stop();
    throw std::runtime_error("PipeWire connection failed");
  }
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  while (state_ != PW_STREAM_STATE_STREAMING &&
         state_ != PW_STREAM_STATE_ERROR &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  if (state_ != PW_STREAM_STATE_STREAMING) {
    auto msg = error();
    stop();
    throw std::runtime_error(
        msg.empty() ? "No microphone stream (check source and PipeWire)" : msg);
  }
}
void Audio::stop() {
  active_ = false;
  if (loop_ && stream_) {
    pw_thread_loop_lock(loop_);
    pw_stream_destroy(stream_);
    stream_ = nullptr;
    pw_thread_loop_unlock(loop_);
  }
}
std::vector<float> Audio::take() {
  std::lock_guard lock(mutex_);
  std::vector<float> out;
  out.swap(queue_);
  return out;
}
std::string Audio::error() {
  std::lock_guard lock(mutex_);
  return error_;
}
} // namespace hv

#include <atomic>
#include <mutex>
#include <pipewire/pipewire.h>
#include <string>
#include <vector>
// White-box only to feed the actual PipeWire process callback a bounded buffer.
#define private public
#include "audio.h"
#undef private
#include <cassert>
#include <spa/buffer/buffer.h>

static pw_buffer captured{};
static int returned = 0;
extern "C" pw_buffer *__wrap_pw_stream_dequeue_buffer(pw_stream *) {
  return &captured;
}
extern "C" int __wrap_pw_stream_queue_buffer(pw_stream *, pw_buffer *b) {
  assert(b == &captured);
  ++returned;
  return 0;
}
int main() {
  hv::Audio audio;
  std::vector<int16_t> pcm{16384, -16384, 8192, 4096};
  spa_chunk chunk{0, 8, 2, 0};
  spa_data data{};
  data.data = pcm.data();
  data.maxsize = 8;
  data.chunk = &chunk;
  spa_buffer buffer{};
  buffer.n_datas = 1;
  buffer.datas = &data;
  captured.buffer = &buffer;
  audio.active_ = true;
  audio.queue_.assign(159999, .25f);
  hv::Audio::Process(&audio);
  assert(!audio.active_ && !audio.error().empty() && returned == 1);
  audio.stop();
  auto retained = audio.take();
  assert(retained.size() == 160000 && retained.front() == .25f &&
         retained.back() == .5f);
  assert(audio.take().empty());
  // Invalid bounds do not enter the recognizer; the buffer is still returned.
  audio.active_ = true;
  chunk.offset = 9;
  hv::Audio::Process(&audio);
  assert(audio.take().empty() && returned == 2);
  audio.error_.clear();
  hv::Audio::State(&audio, PW_STREAM_STATE_UNCONNECTED,
                   PW_STREAM_STATE_CONNECTING, nullptr);
  assert(audio.error().empty());
  hv::Audio::CoreError(&audio, 42, 0, -32, "object error");
  assert(audio.error().empty());
  hv::Audio::CoreError(&audio, PW_ID_CORE, 0, -32, "connection lost");
  assert(audio.error() == "connection lost");
  audio.error_.clear();
  hv::Audio::State(&audio, PW_STREAM_STATE_STREAMING,
                   PW_STREAM_STATE_UNCONNECTED, nullptr);
  assert(!audio.error().empty());
  audio.error_.clear();
  audio.active_ = false;
  hv::Audio::CoreError(&audio, PW_ID_CORE, 0, -32, "after stop");
  hv::Audio::State(&audio, PW_STREAM_STATE_STREAMING,
                   PW_STREAM_STATE_UNCONNECTED, nullptr);
  assert(audio.error().empty());
}

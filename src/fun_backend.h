#pragma once
#include "asr.h"
#include <sys/types.h>

namespace hv {
// The optional native runtime lives in a resident child, allowing immediate
// cancellation without sharing llama/ggml state with the GTK application.
class FunBackend {
public:
  explicit FunBackend(const Config &config,
                      const std::atomic<bool> *cancelled = nullptr);
  ~FunBackend();
  void prepare(const std::atomic<bool> *cancelled = nullptr);
  Transcript decode(std::span<const float> pcm,
                    const std::atomic<bool> *cancelled);

private:
  std::string worker_, models_;
  int threads_, timeout_;
  int socket_ = -1;
  pid_t pid_ = -1;
  void stop();
  Json receive(int timeout_ms, const std::atomic<bool> *cancelled = nullptr);
};
} // namespace hv

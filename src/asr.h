#pragma once
#include "config.h"
#include "speech.h"
#include <atomic>
#include <sherpa-onnx/c-api/c-api.h>
#include <span>
#include <vector>
namespace hv {
class FunBackend;
struct Transcript {
  std::string streaming, text;
  bool speech = false;
};
class Asr {
public:
  explicit Asr(const Config &config);
  ~Asr();
  Asr(const Asr &) = delete;
  Asr &operator=(const Asr &) = delete;
  void begin(const std::atomic<bool> *cancelled = nullptr);
  std::string push(std::span<const float> samples);
  Transcript finish(const std::atomic<bool> *cancelled = nullptr);
  void cancel();
  bool streaming() const { return !fun_; }

private:
  const SherpaOnnxOnlineRecognizer *online_ = nullptr;
  const SherpaOnnxOfflineRecognizer *offline_ = nullptr;
  const SherpaOnnxOnlineStream *stream_ = nullptr;
  std::unique_ptr<SpeechDetector> speech_;
  std::unique_ptr<FunBackend> fun_;
  bool recording_ = false;
  std::vector<float> samples_;
  std::string current();
  std::string decodeOffline(const std::vector<float> &samples);
};
std::vector<float> ReadWave(const std::string &path);
} // namespace hv

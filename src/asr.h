#pragma once
#include "config.h"
#include "vad_trimmer.h"
#include <sherpa-onnx/c-api/c-api.h>
#include <span>
#include <vector>
namespace hv {
std::vector<std::vector<float>> SplitAudio(const std::vector<float> &samples);
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
  void begin();
  std::string push(std::span<const float> samples);
  Transcript finish();
  void cancel();

private:
  const SherpaOnnxOnlineRecognizer *online_ = nullptr;
  const SherpaOnnxOfflineRecognizer *offline_ = nullptr;
  const SherpaOnnxOnlineStream *stream_ = nullptr;
  VadTrimmer vad_;
  std::vector<float> samples_;
  std::string current();
  std::string decodeOffline(const std::vector<float> &samples);
};
std::vector<float> ReadWave(const std::string &path);
} // namespace hv

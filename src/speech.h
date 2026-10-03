#pragma once
#include <cstddef>
#include <memory>
#include <sherpa-onnx/c-api/c-api.h>
#include <span>
#include <string>
#include <vector>

namespace hv {
struct AudioRange {
  size_t begin;
  size_t end;
};
// A detector reports positions; it does not rewrite the source recording.
class SpeechDetector {
public:
  explicit SpeechDetector(const std::string &model);
  std::vector<AudioRange> locate(std::span<const float> audio);

private:
  struct Destroy {
    void operator()(const SherpaOnnxVoiceActivityDetector *p) const;
  };
  std::unique_ptr<const SherpaOnnxVoiceActivityDetector, Destroy> handle_;
};
// Validate and expand detector positions. Recognition never removes interior
// audio.
std::vector<AudioRange> ExpandSpeech(const std::vector<AudioRange> &speech,
                                     size_t recording_size, size_t padding);
std::vector<std::vector<float>>
RecognitionJobs(std::span<const float> audio,
                const std::vector<AudioRange> &speech,
                size_t maximum_samples = 30 * 16000);
} // namespace hv

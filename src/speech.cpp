#include "speech.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace hv {
void SpeechDetector::Destroy::operator()(
    const SherpaOnnxVoiceActivityDetector *p) const {
  SherpaOnnxDestroyVoiceActivityDetector(p);
}
SpeechDetector::SpeechDetector(const std::string &model) {
  SherpaOnnxVadModelConfig settings{};
  settings.sample_rate = 16000;
  settings.num_threads = 1;
  settings.provider = "cpu";
  settings.silero_vad.model = model.c_str();
  settings.silero_vad.window_size = 512;
  settings.silero_vad.threshold = 0.45f;
  settings.silero_vad.min_speech_duration = 0.15f;
  settings.silero_vad.min_silence_duration = 0.5f;
  settings.silero_vad.max_speech_duration = 60.0f;
  handle_.reset(SherpaOnnxCreateVoiceActivityDetector(&settings, 60));
  if (!handle_)
    throw std::runtime_error("Cannot load Silero VAD model: " + model);
}
std::vector<AudioRange> SpeechDetector::locate(std::span<const float> audio) {
  SherpaOnnxVoiceActivityDetectorReset(handle_.get());
  // ASR log-mel features can recognize quiet playback which amplitude-sensitive
  // VAD rejects. Normalize only the detector input, not the recorded waveform.
  double energy = 0;
  float peak = 0;
  for (float sample : audio) {
    energy += static_cast<double>(sample) * sample;
    peak = std::max(peak, std::abs(sample));
  }
  double rms = audio.empty() ? 0 : std::sqrt(energy / audio.size());
  float gain = rms > 0 && peak > 0
                   ? std::max(1.0, std::min({8.0, 0.025 / rms, 0.8 / peak}))
                   : 1.0;
  std::vector<AudioRange> found;
  auto collect = [&] {
    while (!SherpaOnnxVoiceActivityDetectorEmpty(handle_.get())) {
      std::unique_ptr<const SherpaOnnxSpeechSegment,
                      decltype(&SherpaOnnxDestroySpeechSegment)>
          segment(SherpaOnnxVoiceActivityDetectorFront(handle_.get()),
                  SherpaOnnxDestroySpeechSegment);
      if (!segment)
        throw std::runtime_error("VAD segment queue could not be read");
      if (segment->start < 0 || segment->n < 0)
        throw std::runtime_error("VAD returned an invalid sample position");
      size_t begin = std::min<size_t>(segment->start, audio.size());
      size_t count = std::min<size_t>(segment->n, audio.size() - begin);
      if (count)
        found.push_back({begin, begin + count});
      SherpaOnnxVoiceActivityDetectorPop(handle_.get());
    }
  };
  // Drain regularly so long recordings cannot overwrite pending VAD segments.
  for (size_t pos = 0; pos < audio.size(); pos += 512) {
    std::array<float, 512> frame{};
    size_t count = std::min(frame.size(), audio.size() - pos);
    std::copy_n(audio.data() + pos, count, frame.data());
    for (auto &sample : frame)
      sample *= gain;
    SherpaOnnxVoiceActivityDetectorAcceptWaveform(handle_.get(), frame.data(),
                                                  frame.size());
    collect();
  }
  SherpaOnnxVoiceActivityDetectorFlush(handle_.get());
  collect();
  auto regions = ExpandSpeech(found, audio.size(), 0);
  auto padded = regions;
  for (size_t index = 0; index < padded.size(); ++index) {
    padded[index].begin -= std::min<size_t>(4800, regions[index].begin);
    padded[index].end +=
        std::min<size_t>(4800, audio.size() - regions[index].end);
    if (index && padded[index].begin < padded[index - 1].end) {
      // Allocate shared padding at the midpoint of the intervening silence.
      size_t boundary = regions[index - 1].end +
                        (regions[index].begin - regions[index - 1].end) / 2;
      padded[index - 1].end = boundary;
      padded[index].begin = boundary;
    }
  }
  return padded;
}
std::vector<AudioRange> ExpandSpeech(const std::vector<AudioRange> &speech,
                                     size_t recording_size, size_t padding) {
  std::vector<AudioRange> ordered;
  for (auto range : speech) {
    if (range.begin > range.end || range.end > recording_size)
      throw std::runtime_error("Speech range falls outside the recording");
    if (range.begin == range.end)
      continue;
    range.begin -= std::min(padding, range.begin);
    range.end += std::min(padding, recording_size - range.end);
    ordered.push_back(range);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](auto a, auto b) { return a.begin < b.begin; });
  std::vector<AudioRange> merged;
  for (auto range : ordered) {
    if (merged.empty() || range.begin > merged.back().end)
      merged.push_back(range);
    else
      merged.back().end = std::max(merged.back().end, range.end);
  }
  return merged;
}
std::vector<std::vector<float>>
RecognitionJobs(std::span<const float> audio,
                const std::vector<AudioRange> &speech, size_t maximum_samples) {
  if (!maximum_samples)
    throw std::runtime_error("Offline recognition budget must be positive");
  auto ranges = speech;
  size_t end = 0;
  for (auto range : ranges) {
    if (range.begin < end || range.begin > range.end ||
        range.end > audio.size())
      throw std::runtime_error(
          "Recognition ranges must be disjoint and ordered");
    end = range.end;
  }
  std::vector<std::vector<float>> jobs;
  if (ranges.empty())
    return jobs;
  // VAD can miss quiet words before the first or after the last detected
  // region. Keep the entire recording at every duration, including its edges.
  // Detection gates silence-only recordings and nominates long-job cuts.
  size_t begin = 0;
  size_t finish = audio.size();
  while (begin < finish) {
    size_t boundary = std::min(finish, begin + maximum_samples);
    if (boundary < finish) {
      size_t target = begin + maximum_samples * 2 / 3;
      size_t best_distance = maximum_samples;
      for (size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i - 1].end >= ranges[i].begin)
          continue;
        size_t pause =
            ranges[i - 1].end + (ranges[i].begin - ranges[i - 1].end) / 2;
        if (pause < begin + maximum_samples / 3 ||
            pause > begin + maximum_samples)
          continue;
        size_t distance = pause > target ? pause - target : target - pause;
        if (distance < best_distance) {
          best_distance = distance;
          boundary = pause;
        }
      }
    }
    auto samples = audio.subspan(begin, boundary - begin);
    jobs.emplace_back(samples.begin(), samples.end());
    begin = boundary;
  }
  return jobs;
}
} // namespace hv

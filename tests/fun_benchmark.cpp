// Resident adapter around the pinned official Fun-ASR llama.cpp implementation.
// The upstream source remains external; its main is replaced only for this
// test.
#define main upstream_funasr_main
#include "funasr-cli.cpp"
#undef main
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sys/resource.h>

int main(int argc, char **argv) {
  if (argc != 5)
    return 2;
  auto start = std::chrono::steady_clock::now();
  asr_decoder decoder;
  if (!load_decoder(argv[1], argv[2], 1.0f, "", decoder)) {
    free_decoder(decoder);
    return 1;
  }
  llama_set_n_threads(decoder.ctx, 8, 8);
  double load_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - start)
                       .count();
  std::ifstream input(argv[4]);
  if (!input) {
    free_decoder(decoder);
    return 2;
  }
  bool failed = false;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty())
      continue;
    auto item = nlohmann::json::parse(line);
    nlohmann::json result = {
        {"id", item.at("id")}, {"text", ""}, {"model_load_ms", load_ms}};
    auto t = std::chrono::steady_clock::now();
    std::vector<float> audio;
    std::vector<std::pair<int, int>> segments;
    auto path = item.at("audio").get<std::string>();
    if (!funasr_load_audio_16k_mono(path.c_str(), audio) ||
        !funasr_vad_segments(argv[3], audio, 30000, segments)) {
      result["error"] = "Audio loading or VAD failed";
      failed = true;
    } else {
      std::string text;
      for (auto [begin, end] : segments) {
        size_t a = std::min(audio.size(), size_t(begin) * 16);
        size_t b = std::min(audio.size(), size_t(end) * 16);
        if (b > a && b - a >= WINLEN) {
          // Same offline concatenation and token limit as the official CLI.
          text += asr_decode_window(decoder, audio.data() + a, b - a, 512);
        }
      }
      result["text"] = text;
      result["speech_segments"] = segments.size();
    }
    result["asr_ms"] = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - t)
                           .count();
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    result["max_rss_kib"] = usage.ru_maxrss;
    std::cout << result.dump() << std::endl;
  }
  free_decoder(decoder);
  return failed ? 1 : 0;
}

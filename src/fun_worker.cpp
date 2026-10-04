// Adapt the pinned Apache-2.0 Fun-ASR runtime; upstream sources stay external.
#define main upstream_funasr_main
#include "funasr-cli.cpp"
#undef main
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sys/resource.h>

int main(int argc, char **argv) {
  rlimit limit{0, 0};
  setrlimit(RLIMIT_CORE, &limit);
  if (argc != 3)
    return 2;
  asr_decoder decoder;
  try {
    int threads = std::stoi(argv[2]);
    if (threads < 1 || threads > 64)
      throw std::runtime_error("Invalid Fun thread count");
    auto dir = std::filesystem::path(argv[1]);
    auto encoder = (dir / "funasr-encoder-f16.gguf").string();
    auto llm = (dir / "qwen3-0.6b-q8_0.gguf").string();
    auto vad = (dir / "fsmn-vad.gguf").string();
    if (!load_decoder(encoder.c_str(), llm.c_str(), 1.0f, "", decoder))
      throw std::runtime_error("Fun model initialization failed");
    llama_set_n_threads(decoder.ctx, threads, threads);
    std::cout << "{\"ready\":true}" << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.size() > 65536)
        throw std::runtime_error("Oversized Fun request");
      auto request = nlohmann::json::parse(line);
      std::vector<float> audio;
      auto path = request.at("audio").get<std::string>();
      if (!funasr_load_audio_16k_mono(path.c_str(), audio) ||
          audio.size() > 16000 * 600)
        throw std::runtime_error("Invalid Fun recording");
      std::vector<std::pair<int, int>> segments;
      if (!funasr_vad_segments(vad.c_str(), audio, 30000, segments))
        throw std::runtime_error("Fun VAD failed");
      std::string text;
      for (auto [begin, end] : segments) {
        size_t a = std::min(audio.size(), size_t(std::max(0, begin)) * 16);
        size_t b = std::min(audio.size(), size_t(std::max(0, end)) * 16);
        if (b > a && b - a >= WINLEN)
          text += asr_decode_window(decoder, audio.data() + a, b - a, 512);
      }
      // Same VAD, greedy decoding, token budget and concatenation as the
      // independently measured offline runtime. No legacy Silero gate.
      std::cout << nlohmann::json{{"text", text}, {"speech", !segments.empty()}}
                       .dump()
                << std::endl;
    }
  } catch (const std::exception &e) {
    std::cout << nlohmann::json{{"error", e.what()}}.dump() << std::endl;
    free_decoder(decoder);
    return 1;
  }
  free_decoder(decoder);
  return 0;
}

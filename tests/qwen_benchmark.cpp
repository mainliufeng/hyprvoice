// Thin resident benchmark adapter; inference comes from the pinned external
// runtime.
extern "C" {
#include "qwen_asr.h"
#include "qwen_asr_kernels.h"
}
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sys/resource.h>

int main(int argc, char **argv) {
  if (argc != 4 && argc != 5)
    return 2;
  if (std::string(argv[3]) != "plain" && std::string(argv[3]) != "prefix")
    return 2;
  qwen_verbose = 0;
  qwen_set_threads(8);
  auto start = std::chrono::steady_clock::now();
  auto ctx = qwen_load(argv[1]);
  if (!ctx)
    return 1;
  ctx->segment_sec = 20;
  if (argc == 5) {
    size_t used = 0;
    try {
      double seconds = std::stod(argv[4], &used);
      if (used != std::string(argv[4]).size() || !std::isfinite(seconds) ||
          seconds < 0) {
        qwen_free(ctx);
        return 2;
      }
      ctx->segment_sec = seconds;
    } catch (const std::exception &) {
      qwen_free(ctx);
      return 2;
    }
  }
  double load_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - start)
                       .count();
  std::ifstream input(argv[2]);
  if (!input) {
    qwen_free(ctx);
    return 2;
  }
  bool use_prefix = std::string(argv[3]) == "prefix";
  bool failed = false;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty())
      continue;
    auto item = nlohmann::json::parse(line);
    auto before = use_prefix ? item.value("before", std::string{}) : "";
    std::string prompt = before.empty()
                             ? ""
                             : "以下是输入框中已经存在的前文，仅供判断词语和拼"
                               "写。只转写本次音频，不重复前文：\n" +
                                   before;
    if (qwen_set_prompt(ctx, prompt.c_str()) != 0) {
      qwen_free(ctx);
      return 1;
    }
    auto t = std::chrono::steady_clock::now();
    char *text =
        qwen_transcribe(ctx, item.at("audio").get<std::string>().c_str());
    nlohmann::json result = {
        {"id", item.at("id")},
        {"model_load_ms", load_ms},
        {"asr_ms", std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t)
                       .count()},
        {"encode_ms", ctx->perf_encode_ms},
        {"decode_ms", ctx->perf_decode_ms}};
    if (text) {
      result["text"] = text;
      free(text);
    } else {
      result["text"] = "";
      result["error"] = "Qwen transcription failed";
      failed = true;
    }
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    result["max_rss_kib"] = usage.ru_maxrss;
    std::cout << result.dump() << std::endl;
  }
  qwen_free(ctx);
  return failed ? 1 : 0;
}

#pragma once
#include "config.h"
#include <atomic>
namespace hv {
struct TextRequest {
  std::string raw, scene, original_body, history, warning;
  bool command_mode = false;
  std::string revision_instruction;
};
struct TextResult {
  std::string text, error;
  bool processed = false;
};
std::string Rewrite(const Config &config, const std::string &text,
                    const std::string &scene, const std::string &original_body,
                    const std::string &history, std::atomic<bool> &cancel);
bool ValidBody(const std::string &text);
TextResult ProcessText(const Config &config, const TextRequest &request,
                       std::atomic<bool> &cancel);
} // namespace hv

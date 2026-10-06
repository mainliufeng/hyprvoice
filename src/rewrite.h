#pragma once
#include "config.h"
#include <atomic>
namespace hv {
struct TextRequest {
  std::string raw, scene, selected, history, warning;
  bool command_mode = false;
};
struct TextResult {
  std::string text, error;
};
std::string Rewrite(const Config &config, const std::string &text,
                    const std::string &scene, const std::string &selected,
                    const std::string &history, std::atomic<bool> &cancel);
TextResult ProcessText(const Config &config, const TextRequest &request,
                       std::atomic<bool> &cancel);
} // namespace hv

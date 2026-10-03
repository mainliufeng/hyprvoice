#pragma once
#include "config.h"
#include <atomic>
namespace hv {
std::string Rewrite(const Config &config, const std::string &text,
                    const std::string &scene, const std::string &selected,
                    const std::string &history, std::atomic<bool> &cancel);
}

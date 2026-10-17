// Explicit test doubles linked only into recovery_app, exercising production
// App.
#include "asr.h"
#include "audio.h"
#include "fun_backend.h"
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace hv {
namespace {
Json scenario, trace;
int takes = 0, pushes = 0;
std::string Root() { return std::getenv("XDG_RUNTIME_DIR"); }
void Save() { std::ofstream(Root() + "/recovery-trace.json") << trace.dump(); }
} // namespace
Audio::Audio() = default;
Audio::~Audio() = default;
void Audio::start(const std::string &) {
  std::ifstream(Root() + "/recovery-scenario.json") >> scenario;
  trace = {{"samples", 0}, {"finish", 0}, {"stop", 0}, {"push", 0}};
  takes = pushes = 0;
  active_ = true;
  Save();
}
void Audio::stop() {
  active_ = false;
  trace["stop"] = trace.value("stop", 0) + 1;
  Save();
}
std::vector<float> Audio::take() {
  ++takes;
  if (takes == 1)
    return std::vector<float>(640, 0);
  if (!active_ && takes >= 2 && takes < 100) {
    takes = 100;
    return std::vector<float>(320, 0);
  }
  return {};
}
std::string Audio::error() {
  if (scenario.value("interrupt", false) && takes > 0 &&
      (!scenario.value("error_at_stop", false) || !active_))
    return scenario.value("capture_error",
                          std::string("Synthetic device interruption"));
  return {};
}
Asr::Asr(const Config &, const std::atomic<bool> *) {}
Asr::~Asr() = default;
void Asr::begin(const std::atomic<bool> *) { recording_ = true; }
std::string Asr::push(std::span<const float> pcm) {
  trace["samples"] = trace.value("samples", 0) + pcm.size();
  trace["push"] = ++pushes;
  Save();
  return scenario.value("preview", std::string());
}
Transcript Asr::finish(const std::atomic<bool> *) {
  trace["finish"] = trace.value("finish", 0) + 1;
  Save();
  if (scenario.value("finish_error", false))
    throw std::runtime_error("Synthetic finalization failure");
  Transcript result;
  result.text = scenario.value("text", std::string());
  result.streaming = scenario.value("streaming", std::string());
  result.refined = scenario.value("refined", std::string());
  result.speech = !result.text.empty();
  return result;
}
void Asr::cancel() { recording_ = false; }
std::vector<float> ReadWave(const std::string &) {
  throw std::runtime_error("Test App does not transcribe files");
}
} // namespace hv

#pragma once
#include "config.h"
#include <atomic>
#include <functional>
#include <gio/gio.h>
#include <thread>

namespace hv {
// Private helper watches accessibility metadata, never input text. Any failure
// or observed movement latches until this capture ends, including move-back.
class InputWatch {
public:
  InputWatch(int pid, const Json &target);
  InputWatch(const Json &target, std::function<Json()> read);
  ~InputWatch();
  bool changed() const {
    return changed_.load() || !ready_.load() ||
           g_get_monotonic_time() - heartbeat_.load() > 1500000;
  }
  InputWatch(const InputWatch &) = delete;
  InputWatch &operator=(const InputWatch &) = delete;

private:
  GSubprocess *process_ = nullptr;
  GDataInputStream *stream_ = nullptr;
  std::thread reader_;
  std::atomic<bool> ready_ = false, changed_ = false, stop_ = false;
  std::atomic<gint64> heartbeat_ = 0;
};
} // namespace hv

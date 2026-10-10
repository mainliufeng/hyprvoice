#include "input_watch.h"
#include "context.h"
#include <chrono>

namespace hv {
InputWatch::InputWatch(int pid, const Json &target) {
  if (!target.value("reliable", false) || target.value("protected", false)) {
    changed_ = true;
    return;
  }
  auto process_id = std::to_string(pid);
  auto bus = target.at("control").at("bus").get<std::string>();
  auto path = target.at("control").at("path").get<std::string>();
  const char *argv[] = {"/proc/self/exe", "watch-target", process_id.c_str(),
                        bus.c_str(),      path.c_str(),   nullptr};
  GError *error = nullptr;
  process_ = g_subprocess_newv(
      argv,
      static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                    G_SUBPROCESS_FLAGS_STDERR_SILENCE),
      &error);
  if (!process_) {
    g_clear_error(&error);
    changed_ = true;
    return;
  }
  stream_ = g_data_input_stream_new(g_subprocess_get_stdout_pipe(process_));
  reader_ = std::thread([this] {
    while (true) {
      gsize size = 0;
      GError *error = nullptr;
      char *line =
          g_data_input_stream_read_line(stream_, &size, nullptr, &error);
      if (!line || error || size > 1024) {
        g_free(line);
        g_clear_error(&error);
        changed_ = true;
        ready_ = true;
        return;
      }
      try {
        auto event = Json::parse(std::string(line, size));
        auto now = g_get_monotonic_time();
        auto previous = heartbeat_.load();
        // Recovery must not erase a period when observation was unavailable.
        if (ready_ && previous > 0 && now - previous > 1500000)
          changed_ = true;
        if (!event.is_object() ||
            (!event.contains("ready") && !event.contains("alive") &&
             !event.contains("changed")))
          changed_ = true;
        if (event.contains("alive") && !event.at("alive").get<bool>())
          changed_ = true;
        heartbeat_ = now;
        if (event.contains("ready")) {
          if (!event.at("ready").get<bool>())
            changed_ = true;
          ready_ = true;
        }
        if (event.value("changed", false))
          changed_ = true;
      } catch (...) {
        changed_ = true;
        ready_ = true;
      }
      g_free(line);
    }
  });
  auto until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  while (!ready_ && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  if (!ready_)
    changed_ = true;
}
InputWatch::InputWatch(const Json &target, std::function<Json()> read) {
  reader_ = std::thread([this, target, read = std::move(read)] {
    while (!stop_) {
      try {
        if (!EquivalentInputTarget(target, read()))
          changed_ = true;
      } catch (...) {
        changed_ = true;
      }
      heartbeat_ = g_get_monotonic_time();
      ready_ = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
  auto until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  while (!ready_ && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  if (!ready_)
    changed_ = true;
}
InputWatch::~InputWatch() {
  stop_ = true;
  if (process_) {
    g_subprocess_force_exit(process_);
    g_subprocess_wait(process_, nullptr, nullptr);
  }
  if (reader_.joinable())
    reader_.join();
  if (stream_)
    g_object_unref(stream_);
  if (process_)
    g_object_unref(process_);
}
} // namespace hv

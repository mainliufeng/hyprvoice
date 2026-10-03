#include "process.h"
#include <atomic>
#include <chrono>
#include <gio/gio.h>
#include <stdexcept>
#include <thread>
namespace hv {
ProcessResult Run(const std::vector<std::string> &args,
                  const std::string &input, int timeout_ms) {
  if (args.empty())
    throw std::runtime_error("Empty command");
  std::vector<const char *> argv;
  for (auto &a : args)
    argv.push_back(a.c_str());
  argv.push_back(nullptr);
  GError *error = nullptr;
  auto p = g_subprocess_newv(
      argv.data(),
      static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                    G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                    G_SUBPROCESS_FLAGS_STDERR_PIPE),
      &error);
  if (!p) {
    std::string msg = error->message;
    g_error_free(error);
    throw std::runtime_error(args[0] + ": " + msg);
  }
  auto cancel = g_cancellable_new();
  std::atomic<bool> done = false;
  std::jthread timer([&] {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (!done && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!done)
      g_cancellable_cancel(cancel);
  });
  auto bytes = g_bytes_new(input.data(), input.size());
  GBytes *out = nullptr, *err = nullptr;
  bool ok = g_subprocess_communicate(p, bytes, cancel, &out, &err, &error);
  done = true;
  timer.join();
  g_bytes_unref(bytes);
  ProcessResult r;
  if (!ok) {
    g_subprocess_force_exit(p);
    g_subprocess_wait(p, nullptr, nullptr);
    r.code = 124;
    r.error = error ? error->message : "Process failed";
  } else {
    r.code =
        g_subprocess_get_if_exited(p) ? g_subprocess_get_exit_status(p) : 128;
    gsize n = 0;
    auto s = static_cast<const char *>(g_bytes_get_data(out, &n));
    r.out.assign(s, n);
    s = static_cast<const char *>(g_bytes_get_data(err, &n));
    r.error.assign(s, n);
  }
  if (out)
    g_bytes_unref(out);
  if (err)
    g_bytes_unref(err);
  if (error)
    g_error_free(error);
  g_object_unref(cancel);
  g_object_unref(p);
  if (r.out.size() > 1024 * 1024)
    throw std::runtime_error("Command output exceeds 1 MiB");
  return r;
}
std::string Checked(const std::vector<std::string> &a, const std::string &in,
                    int timeout) {
  auto r = Run(a, in, timeout);
  if (r.code)
    throw std::runtime_error(a[0] + ": " + r.error);
  return r.out;
}
} // namespace hv

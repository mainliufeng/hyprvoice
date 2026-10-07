#include "fun_backend.h"
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace hv {
namespace {
class Wave {
public:
  std::string path;
  explicit Wave(std::span<const float> pcm) {
    path = (std::filesystem::temp_directory_path() / "hyprvoice-fun-XXXXXX")
               .string();
    int fd = mkstemp(path.data());
    if (fd < 0)
      throw std::runtime_error("Cannot create temporary recording");
    try {
      // IEEE float WAV preserves the exact PipeWire / replay samples.
      uint32_t bytes = pcm.size() * 4;
      std::string wave;
      auto put = [&](uint32_t n, int width) {
        for (int i = 0; i < width; ++i)
          wave += static_cast<char>((n >> (i * 8)) & 255);
      };
      wave += "RIFF";
      put(bytes + 36, 4);
      wave += "WAVEfmt ";
      put(16, 4);
      put(3, 2);
      put(1, 2);
      put(16000, 4);
      put(64000, 4);
      put(4, 2);
      put(32, 2);
      wave += "data";
      put(bytes, 4);
      for (float f : pcm) {
        if (!std::isfinite(f))
          throw std::runtime_error("Recording contains invalid samples");
        put(std::bit_cast<uint32_t>(f), 4);
      }
      size_t pos = 0;
      while (pos < wave.size()) {
        auto n = write(fd, wave.data() + pos, wave.size() - pos);
        if (n < 0 && errno == EINTR)
          continue;
        if (n <= 0)
          throw std::runtime_error("Cannot write temporary recording");
        pos += n;
      }
      close(fd);
    } catch (...) {
      close(fd);
      unlink(path.c_str());
      throw;
    }
  }
  ~Wave() { unlink(path.c_str()); }
};
} // namespace
FunBackend::FunBackend(const Config &c, const std::atomic<bool> *cancelled)
    : worker_(ExpandPath(c.data.at("fun").at("worker"))),
      models_(ExpandPath(c.data.at("fun").at("model_dir"))),
      threads_(c.data.at("fun").at("threads")),
      timeout_(c.data.at("fun").at("timeout_seconds")) {
  if (access(worker_.c_str(), X_OK) != 0)
    throw std::runtime_error(
        "Fun worker missing; run scripts/install.sh --with-fun");
  for (auto name :
       {"funasr-encoder-f16.gguf", "qwen3-0.6b-q8_0.gguf", "fsmn-vad.gguf"})
    if (!std::filesystem::is_regular_file(std::filesystem::path(models_) /
                                          name))
      throw std::runtime_error(
          "Fun model missing: " +
          (std::filesystem::path(models_) / name).string());
  prepare(cancelled);
}
FunBackend::~FunBackend() { stop(); }
void FunBackend::stop() {
  if (socket_ >= 0)
    close(socket_);
  socket_ = -1;
  if (pid_ > 0) {
    kill(pid_, SIGKILL);
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
    }
  }
  pid_ = -1;
}
Json FunBackend::receive(int timeout_ms, const std::atomic<bool> *cancelled) {
  std::string line;
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (cancelled && *cancelled)
      throw std::runtime_error("已取消");
    pollfd p{socket_, POLLIN, 0};
    int ready = poll(&p, 1, 40);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready < 0)
      throw std::runtime_error("Fun worker communication failed");
    if (!ready)
      continue;
    char buf[4096];
    auto n = recv(socket_, buf, sizeof(buf), 0);
    if (n <= 0)
      throw std::runtime_error("Fun worker exited unexpectedly");
    line.append(buf, n);
    if (line.size() > 65536)
      throw std::runtime_error("Fun result exceeds size limit");
    auto end = line.find('\n');
    if (end != std::string::npos)
      return Json::parse(line.substr(0, end));
  }
  throw std::runtime_error("Fun recognition timed out; no text was inserted");
}
void FunBackend::prepare(const std::atomic<bool> *cancelled) {
  if (cancelled && *cancelled)
    throw std::runtime_error("已取消");
  if (pid_ > 0) {
    // A cached PID does not prove the resident worker survived the idle gap.
    // Reap an exited child before starting another session; do not wait until
    // that session's audio has been recorded to discover the broken worker.
    int status = 0;
    pid_t exited;
    do {
      exited = waitpid(pid_, &status, WNOHANG);
    } while (exited < 0 && errno == EINTR);
    if (exited == pid_ || (exited < 0 && errno == ECHILD)) {
      pid_ = -1; // Already reaped: never send a signal to a stale/reused PID.
      stop();
    } else if (exited < 0) {
      throw std::runtime_error("Cannot check Fun worker state");
    } else {
      pollfd p{socket_, POLLIN, 0};
      int ready;
      do {
        ready = poll(&p, 1, 0);
      } while (ready < 0 && errno == EINTR);
      if (ready < 0)
        throw std::runtime_error("Fun worker communication failed");
      if (socket_ >= 0 && ready == 0)
        return;
      // A closed channel or unsolicited idle reply cannot belong to the next
      // request. Discard it and initialize a fresh worker, without resending
      // any previous recording or accepting stale recognition text.
      stop();
    }
  }
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0)
    throw std::runtime_error("Cannot create Fun worker socket");
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pair[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, pair[0]);
  posix_spawn_file_actions_addclose(&actions, pair[1]);
  std::string threads = std::to_string(threads_);
  std::array<char *, 4> argv{worker_.data(), models_.data(), threads.data(),
                             nullptr};
  int error = posix_spawn(&pid_, worker_.c_str(), &actions, nullptr,
                          argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pair[1]);
  socket_ = pair[0];
  try {
    if (error)
      throw std::runtime_error("Cannot start Fun worker: " +
                               std::string(strerror(error)));
    auto reply = receive(60000, cancelled);
    if (!reply.value("ready", false))
      throw std::runtime_error(
          reply.value("error", "Fun model initialization failed"));
  } catch (...) {
    stop();
    throw;
  }
}
Transcript FunBackend::decode(std::span<const float> pcm,
                              const std::atomic<bool> *cancelled) {
  if (pcm.empty())
    return {};
  if (pcm.size() > 16000 * 600)
    throw std::runtime_error("Fun recording exceeds 600 seconds");
  try {
    Wave wave(pcm);
    auto request = Json{{"audio", wave.path}}.dump() + '\n';
    if (send(socket_, request.data(), request.size(), MSG_NOSIGNAL) !=
        static_cast<ssize_t>(request.size()))
      throw std::runtime_error("Cannot send recording to Fun worker");
    auto reply = receive(timeout_ * 1000, cancelled);
    if (reply.contains("error"))
      throw std::runtime_error(reply.at("error").get<std::string>());
    Transcript out;
    out.text = reply.at("text");
    out.speech = reply.at("speech");
    return out;
  } catch (...) {
    stop(); // Discard all stale replies and restart lazily on the next session.
    throw;
  }
}
} // namespace hv

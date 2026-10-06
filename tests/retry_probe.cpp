// Uses the production text-processing entry point with synthetic data only.
#include "rewrite.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
constexpr auto raw = "合成测试：不会支付2400元";
constexpr auto rewritten = "合成测试：不会支付2400元。";
constexpr auto instruction = "将金额改为3000元，保留不会批准付款";
constexpr auto selection = "合成选区：预算2400元，但不会批准付款。";
constexpr auto replacement = "合成选区：预算3000元，但不会批准付款。";
constexpr auto history = "合成前文：项目预算是2400元，目前不会批准付款。";
constexpr auto warning = "合成 ASR 警告：识别耗时较长";

void Require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

void CheckSuccess(const hv::TextResult &result, bool command) {
  Require(result.text == (command ? replacement : rewritten),
          "Unexpected successful result");
  Require(result.error == warning,
          "Successful processing lost the ASR warning");
}

void CheckFailure(const hv::TextResult &result, bool command) {
  Require(result.text == (command ? "" : raw),
          "Unsafe failure fallback: original text or empty command required");
  Require(result.error.find(warning) != std::string::npos,
          "Failed processing lost the ASR warning");
  Require(result.error != warning,
          "Failed processing did not report its error");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    hv::Config config(argv[1]);
    const std::string mode = argv[2];
    const bool command = mode.starts_with("command_");
    const hv::TextRequest request{command ? instruction : raw,
                                  "correct",
                                  command ? selection : "",
                                  history,
                                  warning,
                                  command};
    std::atomic<bool> cancel = mode == "cancel_pre";
    const bool cancelling =
        mode == "cancel_inflight" || mode == "cancel_then_retry";
    std::jthread canceller;
    if (cancelling) {
      canceller = std::jthread([&cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cancel.store(true);
      });
    }
    const auto result = hv::ProcessText(config, request, cancel);
    if (canceller.joinable())
      canceller.join();
    if (mode == "success" || mode == "command_success")
      CheckSuccess(result, command);
    else
      CheckFailure(result, command);

    if (mode == "retry" || mode == "command_retry" ||
        mode == "cancel_then_retry") {
      // A new attempt owns its cancellation flag; its request is unchanged.
      std::atomic<bool> retry_cancel = false;
      const auto retry_result = hv::ProcessText(config, request, retry_cancel);
      CheckSuccess(retry_result, command);
    }
    // Do not dump transcript/context or HTTP response bodies to test logs.
    std::cout << "Synthetic text-processing checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

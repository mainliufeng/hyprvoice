// Calls production HTTP code against the test-only loopback server.
#include "rewrite.h"
#include <iostream>
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  bool failure = std::string(argv[2]) != "success";
  try {
    hv::Config config(argv[1]);
    std::atomic<bool> cancel = std::string(argv[2]) == "cancel";
    auto result = hv::Rewrite(config, "不会支付2400元", "correct", "",
                              "项目预算是2400元，但目前不会批准付款。", cancel);
    if (failure || result != "不会支付2400元。")
      return 1;
    return 0;
  } catch (const std::exception &e) {
    if (!failure) {
      std::cerr << e.what() << '\n';
      return 1;
    }
    return 0;
  }
}

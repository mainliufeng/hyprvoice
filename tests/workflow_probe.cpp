// Benchmark the production text-processing stage using public transcripts.
#include "rewrite.h"
#include <fstream>
#include <glib.h>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  try {
    hv::Config config(argv[1]);
    std::ifstream input(argv[2]);
    if (!input)
      throw std::runtime_error("Cannot open benchmark input");
    std::string mode = argv[3];
    if (mode != "context" && mode != "correct")
      throw std::runtime_error("Expected context or correct mode");
    bool failed = false;
    std::string line;
    while (std::getline(input, line)) {
      if (line.empty())
        continue;
      auto item = hv::Json::parse(line);
      auto text = item.at("text").get<std::string>();
      auto before = item.value("before", std::string{});
      item["raw"] = text;
      item["llm_called"] =
          !text.empty() && (mode == "correct" || !before.empty());
      auto start = g_get_monotonic_time();
      if (item["llm_called"].get<bool>()) {
        std::atomic<bool> cancel = false;
        try {
          item["text"] =
              hv::Rewrite(config, text, "correct", "", before, cancel);
        } catch (const std::exception &e) {
          // Production retains raw text and requires explicit confirmation.
          item["rewrite_error"] = e.what();
          item["auto_commit_blocked"] = true;
          failed = true;
        }
      }
      item["rewrite_ms"] = (g_get_monotonic_time() - start) / 1000.0;
      std::cout << item.dump() << std::endl;
    }
    return failed ? 1 : 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

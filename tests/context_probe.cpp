// Real model evaluation entry point; never substitutes model responses.
#include "rewrite.h"
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    hv::Config config(argv[1]);
    std::ifstream input(argv[2]);
    if (!input)
      throw std::runtime_error("Cannot open public context cases");
    bool failed = false;
    std::string line;
    while (std::getline(input, line)) {
      if (line.empty())
        continue;
      auto test = hv::Json::parse(line);
      hv::Json result = {{"id", test.at("id")}};
      for (auto mode : {"without_context", "with_context"}) {
        std::atomic<bool> cancel = false;
        try {
          result[mode] =
              hv::Rewrite(config, test.at("transcript"), "correct", "",
                          std::string(mode) == "with_context"
                              ? test.at("before").get<std::string>()
                              : "",
                          cancel);
        } catch (const std::exception &e) {
          result[std::string(mode) + "_error"] = e.what();
          failed = true;
        }
      }
      std::cout << result.dump() << std::endl;
    }
    return failed ? 1 : 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

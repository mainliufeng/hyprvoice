// Production request path; every input/response is an explicit synthetic
// fixture.
#include "rewrite.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 6)
    return 2;
  try {
    hv::Config config(argv[1]);
    const auto cases = hv::Json::parse(hv::ReadFile(argv[2])).at("cases");
    const std::string id = argv[3], scene = argv[4], mode = argv[5];
    hv::Json fixture;
    for (const auto &candidate : cases)
      if (candidate.at("id") == id)
        fixture = candidate;
    Require(!fixture.is_null(), "Unknown synthetic case");
    const std::string selected = fixture.at("selected");
    const hv::TextRequest request{
        fixture.at("transcript"), scene, selected,
        fixture.at("history"),    "",    !selected.empty()};
    const auto expected = mode == "bad"
                              ? fixture.at("bad").get<std::string>()
                              : fixture.at("expected")
                                    .at(scene == "custom" ? "correct" : scene)
                                    .get<std::string>();
    std::atomic<bool> cancel = false;
    const auto result = hv::ProcessText(config, request, cancel);
    if (mode == "failure" || mode == "empty" || mode == "retry") {
      Require(result.text == (request.command_mode ? "" : request.raw),
              "Failure did not preserve original or empty command fallback");
      Require(!result.error.empty(), "Failure did not report an error");
    } else {
      Require(result.text == expected, "Unexpected fixture response");
      Require(result.error.empty(), "Unexpected processing error");
    }
    if (mode == "retry") {
      std::atomic<bool> retry_cancel = false;
      const auto retried = hv::ProcessText(config, request, retry_cancel);
      Require(retried.text == expected && retried.error.empty(),
              "Retry did not preserve the synthetic request");
    }
    // Return only the synthetic strategy to inspect prompt routing. Do not log
    // raw text, selected text, history, response bodies or environment keys.
    std::cout << hv::Json{{"prompt", config.data.at("prompts").at(scene)},
                          {"ok", true}}
                     .dump()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

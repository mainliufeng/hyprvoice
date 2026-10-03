#include "asr.h"
#include "process.h"
#include "text_join.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
static void check(bool b, const char *msg) {
  if (!b)
    throw std::runtime_error(msg);
}
int main() {
  try {
    // Long-input safety must preserve every sample, in order, without
    // duplicating the protective boundary windows.
    std::vector<float> pcm(16000 * 73);
    for (size_t i = 0; i < pcm.size(); ++i)
      pcm[i] = i % 71;
    auto chunks = hv::SplitAudio(pcm);
    check(chunks.size() >= 3, "long audio not split");
    std::vector<float> joined;
    for (auto &c : chunks) {
      check(c.size() <= 16000 * 33, "unsafe long chunk");
      joined.insert(joined.end(), c.begin(), c.end());
    }
    check(joined == pcm, "audio duplicated or lost across cuts");
    check(hv::SplitAudio({}).empty(), "empty audio split");
    std::string text;
    vinput::daemon::asr::AppendRecognizedText(text, "hello");
    vinput::daemon::asr::AppendRecognizedText(text, "world");
    check(text == "hello world", "English words merged");
    text = "你好";
    vinput::daemon::asr::AppendRecognizedText(text, "世界");
    check(text == "你好世界", "CJK spacing changed");
    auto r = hv::Run({"cat"}, "中文\n$(not-a-command) `literal`");
    check(r.code == 0 && r.out == "中文\n$(not-a-command) `literal`",
          "process corrupted literal text");
    r = hv::Run({"sleep", "2"}, "", 60);
    check(r.code == 124, "process deadline ignored");
    std::cout << "core checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

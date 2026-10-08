#include "asr.h"
#include "process.h"
#include "speech.h"
#include "transcript_text.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
static void check(bool b, const char *msg) {
  if (!b)
    throw std::runtime_error(msg);
}
int main() {
  try {
    // Long jobs must preserve every recording sample once and in order.
    std::vector<float> pcm(16000 * 73);
    for (size_t i = 0; i < pcm.size(); ++i)
      pcm[i] = static_cast<float>(i);
    auto chunks = hv::RecognitionJobs(pcm, {{0, pcm.size()}});
    check(chunks.size() == 3, "continuous long speech not bounded");
    std::vector<float> joined;
    for (auto &c : chunks) {
      check(c.size() <= 16000 * 30, "recognition job exceeds model budget");
      joined.insert(joined.end(), c.begin(), c.end());
    }
    check(joined == pcm, "speech samples duplicated or lost");
    check(hv::RecognitionJobs(pcm, {}).empty(), "silence creates jobs");
    auto ranges = hv::ExpandSpeech({{20, 30}, {5, 15}, {25, 40}}, 50, 3);
    check(ranges.size() == 1 && ranges[0].begin == 2 && ranges[0].end == 43,
          "padded speech overlap not merged");
    auto jobs = hv::RecognitionJobs(std::span(pcm).first(50),
                                    {{2, 10}, {20, 30}, {30, 40}}, 16);
    joined.clear();
    for (auto &job : jobs) {
      check(job.size() <= 16, "job limit ignored");
      joined.insert(joined.end(), job.begin(), job.end());
    }
    std::vector<float> expected(pcm.begin(), pcm.begin() + 50);
    check(joined == expected, "long recording samples were discarded");
    auto envelope =
        hv::RecognitionJobs(std::span(pcm).first(50), {{2, 10}, {20, 40}}, 64);
    check(envelope.size() == 1 &&
              envelope[0] ==
                  std::vector<float>(pcm.begin(), pcm.begin() + 50),
          "quiet prefix or suffix was removed from a short recording");
    bool invalid = false;
    try {
      hv::ExpandSpeech({{0, 51}}, 50, 0);
    } catch (const std::exception &) {
      invalid = true;
    }
    check(invalid, "out-of-recording range accepted");
    check(hv::AssembleTranscript({"hello", "world"}) == "hello world",
          "English word boundary lost");
    check(hv::AssembleTranscript({"你好", "世界"}) == "你好世界",
          "CJK boundary has unwanted spaces");
    check(hv::AssembleTranscript({"Café", "déjà", "vu."}) == "Café déjà vu.",
          "Unicode Latin word boundary lost");
    check(hv::AssembleTranscript({"hello.", "World"}) == "hello. World",
          "English sentence boundary lost");
    check(hv::AssembleTranscript({"hello ", "world"}) == "hello world",
          "existing whitespace duplicated");
    check(hv::AssembleTranscript({"GPT", "模型", "很好"}) == "GPT模型很好",
          "mixed-language boundary changed");
    invalid = false;
    try {
      hv::AssembleTranscript({std::string(1, char(0xff))});
    } catch (const std::exception &) {
      invalid = true;
    }
    check(invalid, "invalid recognition UTF-8 accepted");
    check(!hv::PreservesTranscriptLength(
              "因为远离大陆哺乳动物无法长途跋涉而来使得巨龟成为科隆群岛主要的食草动物",
              "长途跋涉而来使得巨龟成为科隆群岛主要的食草动物"),
          "refinement silently dropped a recognized clause");
    check(hv::PreservesTranscriptLength(
              "因为远离大陆，哺乳动物无法长途跋涉而来。",
              "因为远离大陆哺乳动物无法长途跋涉而来"),
          "punctuation changes were treated as missing speech");
    check(hv::PreservesTranscriptLength("一百二十三", "123"),
          "short number normalization was rejected");
    check(!hv::HasIsolatedLatinChange("请保留OAuth和API", "请保留Oauth和api。"),
          "Latin capitalization or punctuation required review");
    check(hv::HasIsolatedLatinChange("请保留Kafka变量", "请保留Kaf变量"),
          "refinement dropped a letter in a mixed-language term");
    check(hv::HasIsolatedLatinChange("输入GPU型号", "输入G PU型号"),
          "refinement split a mixed-language acronym");
    check(hv::HasIsolatedLatinChange("先检查API然后再检查API", "先检查API然后再检查"),
          "isolated repeated Latin term loss escaped review");
    check(!hv::HasIsolatedLatinChange("请用résumé", "请用RÉSUMÉ。"),
          "Unicode Latin case change was treated as a term change");
    check(!hv::HasIsolatedLatinChange("I should tests an API", "I should test the API"),
          "pure English word corrections were blocked by the mixed-language guard");
    check(!hv::HasIsolatedLatinChange("Hey保证明天能完成。", "没有人能保证明天能完成。"),
          "stray Latin hypothesis blocked recovery of a missing negation");
    check(!hv::HasIsolatedLatinChange("外 the words is clear", "外 the words are clear"),
          "stray Han hypothesis blocked English refinement");
    check(!hv::HasIsolatedLatinChange("安装SDK需要二步", "安装SD需要三步"),
          "changed numeric or Chinese content was treated as an isolated term");
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

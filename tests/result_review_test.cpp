#include "result_review.h"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace hv;
int main() {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"请保留识别结果。", "请保留识别记录。"},
      {"可以输入", "不可以输入"},
      {"不要删除", "删除"},
      {"使用HyprVoice完成", "使用Hyper Voice完成"},
      {"项目A预算1.5元。", "项目A预算1,5元。"},
      {"value 1.5.", "value 1,5."},
      {"金额-100元", "金额100元"},
      {"金额1,000元", "金额1000元"},
      {"数值１.５", "数值１,５"},
      {"aaa重复重复bbb", "aaa重复ccc重复bbb"},
      {"", "新增"},
      {"删除", ""},
      {"相同", "相同"}};
  for (const auto &[before, after] : cases) {
    std::string left, right;
    for (const auto &part : CompareText(before, after)) {
      left += part.before;
      right += part.after;
    }
    assert(left == before && right == after);
    if (!before.empty() && !after.empty()) {
      auto choices = RecognitionChoices(before, after);
      assert(ChoiceText(choices) == after);
      bool different = false;
      for (auto &p : choices) {
        different |= p.at("selectable").get<bool>();
        p["selected"] = "streaming";
      }
      assert(ChoiceText(choices) == before);
      assert(different == MaterialDifference(before, after));
    }
  }
  assert(RecognitionChoices("", "单一后端结果").empty());
  assert(RecognitionChoices("单一后端结果", "").empty());
  assert(!MaterialDifference("你好，世界。", "你好世界"));
  assert(MaterialDifference("1.5", "1,5"));
  assert(MaterialDifference("1,000", "1000"));
  auto numeric = RecognitionChoices("数值是1.5。", "数值是1,5。");
  int numeric_changes = 0;
  for (const auto &part : numeric)
    if (part.at("selectable")) {
      ++numeric_changes;
      assert(part.at("streaming") == "1.5" && part.at("refined") == "1,5");
    }
  assert(numeric_changes == 1);
  auto missing = RecognitionChoices("可以输入", "不可以输入");
  assert(ChoiceText(missing) == "不可以输入");
  assert(ChoiceMarkup(missing, true, -1).find("candidate:") !=
         std::string::npos);
  assert(ChoiceMarkup(missing, false, -1) == "不可以输入");
  auto markup = ChangeMarkup("旧<script>", "新<script>", true);
  assert(markup.find("strikethrough=\"true\"") != std::string::npos);
  assert(markup.find("<script>") == std::string::npos);
  assert(ChangeMarkup("before", "正文<&>", false) == "正文&lt;&amp;&gt;");
  // Long stable prefixes should not cause a quadratic full-document matrix.
  std::string long_text;
  for (int i = 0; i < 8000; ++i)
    long_text += "长";
  auto start = std::chrono::steady_clock::now();
  auto long_parts = CompareText(long_text + "前", long_text + "后");
  assert(long_parts.size() == 2 && long_parts.front().same());
  assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
  auto bounded =
      CompareText(std::string(150000, 'x'), std::string(150000, 'y'));
  assert(bounded.size() == 1 && bounded[0].passage);
  assert(bounded[0].before.size() == 150000 &&
         bounded[0].after.size() == 150000);
  // Independently exercise the token-product budget below the byte limit.
  std::string unrelated_a, unrelated_b;
  for (int i = 0; i < 2500; ++i) {
    unrelated_a += "前";
    unrelated_b += "后";
  }
  auto coarse = CompareText(unrelated_a, unrelated_b);
  assert(coarse.size() == 1 && coarse[0].passage);
  assert(coarse[0].before == unrelated_a && coarse[0].after == unrelated_b);
  std::cout << "result review contracts passed\n";
}

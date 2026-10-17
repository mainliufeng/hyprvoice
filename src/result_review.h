#pragma once
#include "config.h"
#include <string>
#include <vector>

namespace hv {
struct TextDifference {
  std::string before, after;
  bool passage = false;
  bool same() const { return before == after; }
};
// Text alignment only. These offsets do not describe audio timestamps.
std::vector<TextDifference> CompareText(const std::string &before,
                                        const std::string &after);
bool MaterialDifference(const std::string &before, const std::string &after);
Json RecognitionChoices(const std::string &streaming,
                        const std::string &refined);
std::string ChoiceText(const Json &parts);
std::string EscapeMarkup(const std::string &text);
std::string ChoiceMarkup(const Json &parts, bool compare, int active);
std::string ChangeMarkup(const std::string &before, const std::string &after,
                         bool compare);
} // namespace hv

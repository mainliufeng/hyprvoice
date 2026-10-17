#include "transcript_text.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
}
int main() {
  try {
    const std::string original = "Do not pay 2400. The final place is North";
    const std::vector<hv::TimedToken> tokens{
        {" Do", .1}, {" not", .2}, {" pay", .3}, {" 2400", .4}, {" .", 1.2},
        {" The", 3.6}, {" final", 3.7}, {" place", 3.8}, {" is", 3.9},
        {" Nor", 4.2}, {"th", 4.9}};
    const std::vector<hv::AudioRange> speech{{0, 16000}, {48000, 72000}};
    auto tail = hv::FindLateSpeechTail(original, tokens, speech, 0, 80000);
    check(tail && tail->audio.begin == 32000 && tail->audio.end == 80000,
          "Late unfinished Latin tail was not isolated at the original pause");
    check(original.substr(tail->text_offset) == " The final place is North",
          "Token mapping changed the original prefix boundary");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "the final place is Northland.") ==
          "Do not pay 2400. The final place is Northland.",
          "Completion changed existing negation, amount, punctuation or case");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset, "") == original,
          "Empty retry discarded existing recognition");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "The final place is North") == original,
          "Another unfinished result replaced the original");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "The other place is Northland.") == original,
          "Retry rewrote a completed prefix word");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "The final place inside Northland.") == original,
          "Prefix comparison ignored word boundaries");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "The final place is not Northland.") == original,
          "Retry introduced a negation");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "The final place is Northland in 2026.") == original,
          "Retry introduced a numerical token");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "The final place is Northland number two.") == original,
          "Retry introduced a spoken number");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "The final place is Northland. It can’t fail.") == original,
          "Unicode apostrophe bypassed the negation guard");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset,
          "The final place is Northland in ２０２６.") == original,
          "Unicode digits bypassed the numerical guard");
    const auto negative = "The answer is not";
    check(hv::CompleteLateSpeechTail(negative, 0, "The answer is notable.") == negative,
          "Retry changed an existing negation into another word");
    const auto numerical = "The count is five";
    check(hv::CompleteLateSpeechTail(numerical, 0, "The count is fifty.") == numerical,
          "Retry changed a spoken numerical token");
    const auto contraction = "The result isn't";
    check(hv::CompleteLateSpeechTail(contraction, 0, "The result is not clear.") == contraction,
          "Retry rewrote an unfinished contraction");
    check(hv::CompleteLateSpeechTail("Context. Frag", 8, "Fragment completed.") == "Context. Frag",
          "Leading whitespace disguised an unanchored word");
    check(hv::CompleteLateSpeechTail("Onlyword", 0, "Onlyword completed.") == "Onlyword",
          "An unanchored word allowed completion");
    check(hv::CompleteLateSpeechTail(original, tail->text_offset, "Bad UTF-8 \xff") == original,
          "Invalid UTF-8 retry was accepted");
    check(hv::CompleteLateSpeechTail("Déjà vu Éco", 2, "Déjà vu École.") == "Déjà vu Éco",
          "Insertion accepted a partial UTF-8 boundary");
    check(hv::CompleteLateSpeechTail("Déjà vu Éco", 0, "déjà   vu École.") == "Déjà vu École.",
          "Unicode completion lost the original completed prefix");

    check(hv::FindLateSpeechTail(original, {}, speech, 0, 80000) == std::nullopt,
          "Absent timestamps triggered guessed recovery");
    auto within = tokens; within.back().seconds = 4.4;
    check(!hv::FindLateSpeechTail(original, within, speech, 0, 80000),
          "Ordinary words within VAD speech triggered retry");
    auto invalid = tokens; invalid[2].seconds = std::numeric_limits<double>::quiet_NaN();
    check(!hv::FindLateSpeechTail(original, invalid, speech, 0, 80000),
          "Nonfinite timestamp allowed recovery");
    invalid = tokens; invalid[2].seconds = -.1;
    check(!hv::FindLateSpeechTail(original, invalid, speech, 0, 80000),
          "Unordered timestamp allowed recovery");
    check(!hv::FindLateSpeechTail(original + " Extra", tokens, speech, 0, 80000),
          "Unmapped original text allowed unsafe insertion");
    check(!hv::FindLateSpeechTail(original, tokens, {{0, 50000}, {48000, 72000}}, 0, 80000),
          "Overlapping speech ranges allowed recovery");
    check(!hv::FindLateSpeechTail(original, tokens, speech, 0, 40000),
          "Recovery exceeded its original job");
    check(!hv::FindLateSpeechTail("保留API。The final place is North",
          std::vector<hv::TimedToken>{{"保留API。", .2}, {"The final place is North", 4.9}},
          speech, 0, 80000), "Mixed-language recognition allowed tail rewriting");
    auto completed = tokens; completed.push_back({".", 4.99});
    check(!hv::FindLateSpeechTail(original + ".", completed, speech, 0, 80000),
          "A completed sentence allowed tail rewriting");
    auto numerical_tokens = tokens;
    numerical_tokens.resize(9); numerical_tokens.push_back({" 2400", 4.9});
    check(!hv::FindLateSpeechTail("Do not pay 2400. The final place is 2400",
          numerical_tokens, speech, 0, 80000),
          "A final numerical token allowed tail rewriting");
    std::cout << "Late speech tail checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}

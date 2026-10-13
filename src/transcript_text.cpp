#include "transcript_text.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <glib.h>
#include <stdexcept>
namespace hv {
static size_t WordCharacters(const std::string &text) {
  if (!g_utf8_validate(text.data(), text.size(), nullptr))
    throw std::runtime_error("Recognition returned invalid UTF-8 text");
  size_t count = 0;
  for (const char *p = text.c_str(); *p; p = g_utf8_next_char(p))
    count += g_unichar_isalnum(g_utf8_get_char(p)) != 0;
  return count;
}
bool PreservesTranscriptLength(const std::string &streaming,
                               const std::string &refined) {
  auto before = WordCharacters(streaming), after = WordCharacters(refined);
  // Small utterances naturally vary in spelling/number formatting. For longer
  // dictation, retain the streaming result if refinement drops over 15%.
  return before < 20 || after * 100 >= before * 85;
}
static std::vector<std::string> LatinTerms(const std::string &text) {
  if (!g_utf8_validate(text.data(), text.size(), nullptr))
    throw std::runtime_error("Recognition returned invalid UTF-8 text");
  std::vector<std::string> terms;
  std::string term;
  for (const char *p = text.c_str(); *p; p = g_utf8_next_char(p)) {
    auto c = g_utf8_get_char(p);
    if (g_unichar_get_script(c) == G_UNICODE_SCRIPT_LATIN &&
        g_unichar_isalpha(c)) {
      char letter[6];
      auto n = g_unichar_to_utf8(g_unichar_tolower(c), letter);
      term.append(letter, n);
    } else {
      if (!term.empty())
        terms.push_back(std::move(term));
      term.clear();
    }
  }
  if (!term.empty())
    terms.push_back(std::move(term));
  return terms;
}
static std::string NonLatinCharacters(const std::string &text) {
  std::string characters;
  for (const char *p = text.c_str(); *p; p = g_utf8_next_char(p)) {
    auto c = g_utf8_get_char(p);
    if (g_unichar_isalnum(c) &&
        g_unichar_get_script(c) != G_UNICODE_SCRIPT_LATIN)
      characters.append(p, g_utf8_next_char(p) - p);
  }
  return characters;
}
bool HasIsolatedLatinChange(const std::string &streaming,
                            const std::string &refined) {
  auto before = LatinTerms(streaming), after = LatinTerms(refined);
  if (before.empty() || before == after)
    return false;
  size_t han = 0, latin = 0;
  for (const char *p = streaming.c_str(); *p; p = g_utf8_next_char(p)) {
    auto c = g_utf8_get_char(p);
    han += g_unichar_get_script(c) == G_UNICODE_SCRIPT_HAN;
    latin += g_unichar_get_script(c) == G_UNICODE_SCRIPT_LATIN &&
             g_unichar_isalpha(c);
  }
  // A stray Latin hypothesis must not veto recovered Chinese clauses; a stray
  // Han character must not block English refinement. Only review an isolated
  // term disagreement when the surrounding CJK-dominant content is unchanged.
  return han > 0 && han >= latin &&
         NonLatinCharacters(streaming) == NonLatinCharacters(refined);
}
namespace {
std::optional<std::vector<size_t>> TokenOffsets(
    const std::string &text, std::span<const TimedToken> tokens, size_t samples) {
  if (tokens.empty() || !g_utf8_validate(text.data(), text.size(), nullptr))
    return {};
  std::vector<size_t> offsets{0};
  size_t position = 0;
  double previous = -1;
  for (const auto &token : tokens) {
    if (!std::isfinite(token.seconds) || token.seconds < previous ||
        token.seconds < 0 || token.seconds > samples / 16000.0 ||
        !g_utf8_validate(token.text.data(), token.text.size(), nullptr))
      return {};
    previous = token.seconds;
    for (const char *p = token.text.c_str(); *p; p = g_utf8_next_char(p)) {
      auto c = g_utf8_get_char(p);
      if (g_unichar_isspace(c))
        continue;
      while (position < text.size() &&
             g_unichar_isspace(g_utf8_get_char(text.data() + position)))
        position = g_utf8_next_char(text.data() + position) - text.data();
      if (position == text.size() || g_utf8_get_char(text.data() + position) != c)
        return {};
      position = g_utf8_next_char(text.data() + position) - text.data();
    }
    offsets.push_back(position);
  }
  while (position < text.size() &&
         g_unichar_isspace(g_utf8_get_char(text.data() + position)))
    position = g_utf8_next_char(text.data() + position) - text.data();
  return position == text.size() ? std::optional{offsets} : std::nullopt;
}
bool LatinLetter(gunichar c) {
  return g_unichar_isalpha(c) &&
         g_unichar_get_script(c) == G_UNICODE_SCRIPT_LATIN;
}
std::string CanonicalSpacing(const std::string &text) {
  std::string result;
  bool space = false;
  for (const char *p = text.c_str(); *p; p = g_utf8_next_char(p)) {
    auto c = g_utf8_get_char(p);
    if (g_unichar_isspace(c)) {
      if (!result.empty() && !space)
        result.push_back(' ');
      space = true;
    } else {
      char bytes[6];
      auto length = g_unichar_to_utf8(g_unichar_tolower(c), bytes);
      result.append(bytes, length);
      space = false;
    }
  }
  return result;
}
std::map<std::string, size_t> CriticalTerms(const std::string &text) {
  // Reject a completion that adds/removes a negation or numerical token.
  // These are general English fidelity constraints, not corpus-specific words.
  static const std::set<std::string> protected_words{
      "no", "not", "never", "neither", "nor", "without", "hardly",
      "rarely", "scarcely", "cannot", "can't", "isn't", "aren't", "wasn't",
      "weren't", "don't", "doesn't", "didn't", "won't", "wouldn't",
      "shouldn't", "couldn't", "mustn't", "hasn't", "haven't", "hadn't",
      "zero", "one", "two", "three", "four", "five", "six", "seven",
      "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
      "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty",
      "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety",
      "hundred", "thousand", "million", "billion", "trillion", "percent"};
  std::map<std::string, size_t> terms;
  std::string term;
  auto flush = [&] {
    if (!term.empty() && (protected_words.contains(term) ||
        std::any_of(term.begin(), term.end(), [](unsigned char c) {
          return c >= '0' && c <= '9';
        })))
      ++terms[term];
    term.clear();
  };
  auto normalized = CanonicalSpacing(text);
  for (const char *p = normalized.c_str(); *p; p = g_utf8_next_char(p)) {
    auto c = g_utf8_get_char(p);
    if (c == 0x2019 || c == 0x02bc)
      c = '\'';
    if (g_unichar_isdigit(c))
      term.push_back('0' + g_unichar_digit_value(c));
    else if ((c >= 'a' && c <= 'z') || c == '\'')
      term.push_back(static_cast<char>(c));
    else
      flush();
  }
  flush();
  return terms;
}
}
std::optional<LateSpeechTail> FindLateSpeechTail(
    const std::string &text, std::span<const TimedToken> tokens,
    const std::vector<AudioRange> &speech, size_t job_begin, size_t job_size) {
  auto offsets = TokenOffsets(text, tokens, job_size);
  if (!offsets || text.empty() || speech.size() < 2)
    return {};
  for (const char *p = text.c_str(); *p; p = g_utf8_next_char(p))
    if (g_unichar_isalpha(g_utf8_get_char(p)) &&
        !LatinLetter(g_utf8_get_char(p)))
      return {}; // Keep Chinese and mixed-language grouping/word choices intact.
  auto compact = CanonicalSpacing(text);
  if (!compact.empty() && compact.back() == ' ')
    compact.pop_back();
  if (compact.empty() || !LatinLetter(g_utf8_get_char(
        g_utf8_find_prev_char(compact.data(), compact.data() + compact.size()))))
    return {}; // A completed sentence or number is not a partial Latin word.
  for (size_t i = 0; i < speech.size(); ++i)
    if (speech[i].begin >= speech[i].end ||
        (i && speech[i - 1].end >= speech[i].begin))
      return {};
  const auto last = speech.back(), previous = speech[speech.size() - 2];
  if (last.begin < job_begin || last.end > job_begin + job_size)
    return {};
  const auto boundary = previous.end + (last.begin - previous.end) / 2;
  if (boundary < job_begin)
    return {};
  size_t last_word = tokens.size();
  for (size_t i = 0; i < tokens.size(); ++i)
    for (const char *p = tokens[i].text.c_str(); *p; p = g_utf8_next_char(p))
      if (g_unichar_isalnum(g_utf8_get_char(p)))
        last_word = i;
  if (last_word == tokens.size() ||
      tokens[last_word].seconds <= (last.end - job_begin) / 16000.0)
    return {};
  const auto from = (boundary - job_begin) / 16000.0;
  size_t insertion = 0;
  while (insertion < tokens.size() && tokens[insertion].seconds < from)
    ++insertion;
  if (!insertion || insertion == tokens.size())
    return {};
  return LateSpeechTail{{boundary, job_begin + job_size}, (*offsets)[insertion]};
}
std::string CompleteLateSpeechTail(const std::string &text, size_t offset,
                                   const std::string &recovered) {
  if (offset > text.size() ||
      !g_utf8_validate(text.data(), offset, nullptr) ||
      !g_utf8_validate(text.data() + offset, text.size() - offset, nullptr) ||
      !g_utf8_validate(recovered.data(), recovered.size(), nullptr))
    return text;
  for (const char *p = recovered.c_str(); *p; p = g_utf8_next_char(p))
    if (g_unichar_isalpha(g_utf8_get_char(p)) &&
        !LatinLetter(g_utf8_get_char(p)))
      return text;
  auto tail = text.substr(offset);
  auto old = CanonicalSpacing(tail);
  auto replacement = CanonicalSpacing(recovered);
  if (!old.empty() && old.back() == ' ')
    old.pop_back();
  if (!replacement.empty() && replacement.back() == ' ')
    replacement.pop_back();
  if (old.empty() || replacement.empty() ||
      (replacement.back() != '.' && replacement.back() != '?' &&
       replacement.back() != '!'))
    return text;
  const char *end = tail.data() + tail.size();
  while (end > tail.data() &&
         g_unichar_isspace(g_utf8_get_char(g_utf8_find_prev_char(tail.data(), end))))
    end = g_utf8_find_prev_char(tail.data(), end);
  const char *start = end;
  while (start > tail.data()) {
    auto previous = g_utf8_find_prev_char(tail.data(), start);
    if (!LatinLetter(g_utf8_get_char(previous)))
      break;
    start = previous;
  }
  if (start == tail.data() || start == end ||
      !g_unichar_isspace(g_utf8_get_char(g_utf8_find_prev_char(tail.data(), start))))
    return text;
  auto prefix = CanonicalSpacing(tail.substr(0, start - tail.data()));
  if (!WordCharacters(prefix) || !replacement.starts_with(prefix) ||
      WordCharacters(replacement) <= WordCharacters(old) ||
      CriticalTerms(old) != CriticalTerms(replacement))
    return text;
  // Keep completed words byte-for-byte, including their case, punctuation and
  // numerical spelling. Only the unfinished final word and its continuation
  // come from the retry, even when the recognizers format the prefix differently.
  size_t normalized = 0;
  bool space = false;
  const char *suffix = recovered.data();
  while (*suffix && normalized < prefix.size()) {
    auto c = g_utf8_get_char(suffix);
    if (g_unichar_isspace(c)) {
      if (normalized && !space)
        ++normalized;
      space = true;
    } else {
      char bytes[6];
      normalized += g_unichar_to_utf8(g_unichar_tolower(c), bytes);
      space = false;
    }
    suffix = g_utf8_next_char(suffix);
  }
  if (normalized != prefix.size())
    return text;
  while (*suffix && g_unichar_isspace(g_utf8_get_char(suffix)))
    suffix = g_utf8_next_char(suffix);
  return AssembleTranscript({text.substr(0, offset + static_cast<size_t>(start - tail.data())),
                             std::string(suffix)});

}
std::string AssembleTranscript(const std::vector<std::string> &utterances) {
  std::string output;
  for (const auto &utterance : utterances) {
    if (utterance.empty())
      continue;
    if (!g_utf8_validate(utterance.data(), utterance.size(), nullptr))
      throw std::runtime_error("Recognition returned invalid UTF-8 text");
    if (!output.empty()) {
      auto before = g_utf8_get_char(
          g_utf8_find_prev_char(output.data(), output.data() + output.size()));
      auto after = g_utf8_get_char(utterance.data());
      auto category = g_unichar_type(before);
      bool word_boundary = g_unichar_isalnum(before) ||
                           category == G_UNICODE_CLOSE_PUNCTUATION ||
                           category == G_UNICODE_FINAL_PUNCTUATION ||
                           (category == G_UNICODE_OTHER_PUNCTUATION &&
                            before != '\'' && before != '-');
      if (word_boundary && g_unichar_isalnum(after) &&
          !g_unichar_iswide(before) && !g_unichar_iswide(after))
        output.push_back(' ');
    }
    output.append(utterance);
  }
  return output;
}
} // namespace hv

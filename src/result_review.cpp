#include "result_review.h"
#include <algorithm>
#include <glib.h>
#include <span>
#include <stdexcept>

namespace hv {
namespace {
using Tokens = std::vector<std::string>;
Tokens Tokenize(const std::string &text) {
  if (!g_utf8_validate(text.data(), text.size(), nullptr))
    throw std::runtime_error("Invalid transcript encoding");
  Tokens tokens;
  bool latin = false;
  for (const char *p = text.c_str(); *p;) {
    auto next = g_utf8_next_char(p);
    auto c = g_utf8_get_char(p);
    const bool numeric_separator =
        (c == '.' || c == ',') && !tokens.empty() &&
        g_unichar_isdigit(g_utf8_get_char(g_utf8_find_prev_char(
            tokens.back().data(),
            tokens.back().data() + tokens.back().size()))) &&
        g_unichar_isdigit(g_utf8_get_char(next));
    const bool word =
        numeric_separator || g_unichar_isdigit(c) ||
        (c < 128 && (g_ascii_isalnum(c) || c == '\'' || c == '-'));
    if (word && latin)
      tokens.back().append(p, next - p);
    else
      tokens.emplace_back(p, next - p);
    latin = word;
    p = next;
  }
  return tokens;
}
std::vector<size_t> Scores(std::span<const std::string> a,
                           std::span<const std::string> b, bool reverse) {
  std::vector<size_t> row(b.size() + 1), previous;
  for (size_t i = 0; i < a.size(); ++i) {
    previous = row;
    const auto &x = a[reverse ? a.size() - 1 - i : i];
    for (size_t j = 0; j < b.size(); ++j)
      row[j + 1] = x == b[reverse ? b.size() - 1 - j : j]
                       ? previous[j] + 1
                       : std::max(previous[j + 1], row[j]);
  }
  return row;
}
// Hirschberg uses linear auxiliary memory, including for long dictation.
void Match(std::span<const std::string> a, std::span<const std::string> b,
           size_t ai, size_t bi,
           std::vector<std::pair<size_t, size_t>> &matches) {
  if (a.empty() || b.empty())
    return;
  if (a.size() == 1) {
    auto it = std::find(b.begin(), b.end(), a[0]);
    if (it != b.end())
      matches.emplace_back(ai, bi + (it - b.begin()));
    return;
  }
  const auto mid = a.size() / 2;
  auto left = Scores(a.first(mid), b, false);
  auto right = Scores(a.subspan(mid), b, true);
  size_t split = 0, score = 0;
  for (size_t j = 0; j <= b.size(); ++j)
    if (left[j] + right[b.size() - j] > score) {
      split = j;
      score = left[j] + right[b.size() - j];
    }
  std::vector<size_t>().swap(left);
  std::vector<size_t>().swap(right);
  Match(a.first(mid), b.first(split), ai, bi, matches);
  Match(a.subspan(mid), b.subspan(split), ai + mid, bi + split, matches);
}
std::string Join(std::span<const std::string> tokens) {
  std::string out;
  for (const auto &t : tokens)
    out += t;
  return out;
}
std::string Words(const std::string &text) {
  std::string out;
  for (const char *p = text.c_str(); *p;) {
    auto next = g_utf8_next_char(p);
    auto c = g_utf8_get_char(p);
    bool punctuation =
        std::string("，。！？、；：,!?;:").find(std::string(p, next - p)) !=
        std::string::npos;
    // Decimal points and negative signs are meaningful, never discarded.
    if (c == '.' || c == ',')
      punctuation =
          !(p > text.c_str() && g_unichar_isdigit(g_utf8_get_char(
                                    g_utf8_find_prev_char(text.c_str(), p)))) &&
          !g_unichar_isdigit(g_utf8_get_char(next));
    if (!g_unichar_isspace(c) && !punctuation)
      out.append(p, next - p);
    p = next;
  }
  return out;
}
} // namespace

std::vector<TextDifference> CompareText(const std::string &before,
                                        const std::string &after) {
  if (before == after)
    return {{before, after}};
  if (before.size() + after.size() > 128 * 1024)
    return {{before, after, true}};
  const auto a = Tokenize(before), b = Tokenize(after);
  std::vector<std::pair<size_t, size_t>> matches;
  size_t prefix = 0, suffix = 0;
  while (prefix < std::min(a.size(), b.size()) && a[prefix] == b[prefix])
    ++prefix;
  while (suffix < std::min(a.size(), b.size()) - prefix &&
         a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
    ++suffix;
  const auto an = a.size() - prefix - suffix, bn = b.size() - prefix - suffix;
  if (an && bn && an > 4000000 / bn) {
    std::vector<TextDifference> coarse;
    if (prefix) {
      auto text = Join(std::span(a).first(prefix));
      coarse.push_back({text, text});
    }
    coarse.push_back({Join(std::span(a).subspan(prefix, an)),
                      Join(std::span(b).subspan(prefix, bn)), true});
    if (suffix) {
      auto text = Join(std::span(a).last(suffix));
      coarse.push_back({text, text});
    }
    return coarse;
  }
  for (size_t i = 0; i < prefix; ++i)
    matches.emplace_back(i, i);
  Match(std::span(a).subspan(prefix, an), std::span(b).subspan(prefix, bn),
        prefix, prefix, matches);
  for (size_t i = 0; i < suffix; ++i)
    matches.emplace_back(a.size() - suffix + i, b.size() - suffix + i);
  std::vector<TextDifference> parts;
  size_t ai = 0, bi = 0;
  for (const auto &[x, y] : matches) {
    if (x > ai || y > bi)
      parts.push_back({Join(std::span(a).subspan(ai, x - ai)),
                       Join(std::span(b).subspan(bi, y - bi))});
    if (!parts.empty() && parts.back().same()) {
      parts.back().before += a[x];
      parts.back().after += b[y];
    } else
      parts.push_back({a[x], b[y]});
    ai = x + 1;
    bi = y + 1;
  }
  if (ai < a.size() || bi < b.size())
    parts.push_back(
        {Join(std::span(a).subspan(ai)), Join(std::span(b).subspan(bi))});
  return parts;
}
bool MaterialDifference(const std::string &before, const std::string &after) {
  return Words(before) != Words(after);
}
Json RecognitionChoices(const std::string &streaming,
                        const std::string &refined) {
  auto parts = Json::array();
  // One backend result is not evidence of an alternative hypothesis.
  if (streaming.empty() || refined.empty())
    return parts;
  for (const auto &part : CompareText(streaming, refined))
    parts.push_back(
        {{"streaming", part.before},
         {"refined", part.after},
         {"selected", "refined"},
         {"selectable", MaterialDifference(part.before, part.after)},
         {"checked", false},
         {"granularity", part.passage ? "passage" : "fragment"}});
  return parts;
}
std::string ChoiceText(const Json &parts) {
  std::string text;
  for (const auto &part : parts)
    text += part.at(part.at("selected").get<std::string>()).get<std::string>();
  return text;
}
std::string EscapeMarkup(const std::string &text) {
  auto escaped = g_markup_escape_text(text.data(), text.size());
  std::string result(escaped);
  g_free(escaped);
  return result;
}
std::string ChoiceMarkup(const Json &parts, bool compare, int active) {
  std::string markup;
  for (size_t i = 0; i < parts.size(); ++i) {
    const auto &p = parts[i];
    const auto text =
        p.at(p.at("selected").get<std::string>()).get<std::string>();
    if (compare && p.value("selectable", false)) {
      const auto label = text.empty() ? "〔不含此段文字〕" : text;
      const auto color = p.value("checked", false) ? "#4059a8" : "#79591b";
      const auto bg = static_cast<int>(i) == active ? "#ffe9ab" : "#fff4d6";
      markup += "<a href=\"candidate:" + std::to_string(i) +
                "\"><span foreground=\"" + color + "\" background=\"" + bg +
                "\" underline=\"single\">" + EscapeMarkup(label) +
                "</span></a>";
    } else
      markup += EscapeMarkup(text);
  }
  return markup;
}
std::string ChangeMarkup(const std::string &before, const std::string &after,
                         bool compare) {
  if (!compare)
    return EscapeMarkup(after);
  std::string markup;
  for (const auto &p : CompareText(before, after)) {
    if (p.same()) {
      markup += EscapeMarkup(p.after);
      continue;
    }
    if (!p.before.empty())
      markup += "<span foreground=\"#965b50\" background=\"#faece7\" "
                "strikethrough=\"true\">" +
                EscapeMarkup(p.before) + "</span>";
    if (!p.after.empty())
      markup += std::string("<span foreground=\"") +
                (p.before.empty() ? "#295c3e\" background=\"#e8f4e9\""
                                  : "#35578c\" background=\"#eaf0fc\"") +
                " underline=\"single\">" + EscapeMarkup(p.after) + "</span>";
  }
  return markup;
}
} // namespace hv

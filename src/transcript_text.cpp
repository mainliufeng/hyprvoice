#include "transcript_text.h"
#include <glib.h>
#include <stdexcept>
namespace hv {
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

#pragma once
#include <string>
#include <vector>
namespace hv {
std::string AssembleTranscript(const std::vector<std::string> &utterances);
// ASR refinement may correct words, but a much shorter result is suspect.
bool PreservesTranscriptLength(const std::string &streaming,
                               const std::string &refined);
}

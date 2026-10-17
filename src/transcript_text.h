#pragma once
#include "speech.h"
#include <string>
#include <span>
#include <optional>
#include <vector>
namespace hv {
std::string AssembleTranscript(const std::vector<std::string> &utterances);
// ASR refinement may correct words, but a much shorter result is suspect.
bool PreservesTranscriptLength(const std::string &streaming,
                               const std::string &refined);
// Flag a Latin term disagreement in otherwise unchanged CJK-dominant text;
// this does not determine which recognizer is correct.
bool HasIsolatedLatinChange(const std::string &streaming,
                            const std::string &refined);
struct TimedToken {
  std::string text;
  double seconds;
};
struct LateSpeechTail {
  AudioRange audio;
  size_t text_offset;
};
// Retry only a final Latin tail whose unfinished word was emitted after VAD
// speech ended. Missing/misaligned timestamps and CJK hypotheses decline repair.
std::optional<LateSpeechTail> FindLateSpeechTail(
    const std::string &text, std::span<const TimedToken> tokens,
    const std::vector<AudioRange> &speech, size_t job_begin, size_t job_size);
// Preserve every completed prefix word and numerical/negation marker. Reject
// empty, incomplete, shortened or incompatible retries instead of guessing.
std::string CompleteLateSpeechTail(const std::string &text, size_t offset,
                                   const std::string &recovered);
}

#pragma once
#include "config.h"
namespace hv {
// Read-only accessibility query, run in a deadline-bounded helper process.
Json ReadInputContext(int pid, int max_chars);
// Local-only target guard: no input text is returned or sent to a model.
// Unknown, protected, ambiguous or unstable targets are never reliable.
Json ReadInputTarget(int pid);
bool EquivalentInputTarget(const Json &first, const Json &second);
// Emits one ready JSON line, then sticky changed/protected lines. This helper
// intentionally stays alive until its parent terminates it, even if setup
// fails.
void WatchInputTarget(int pid, const Json &control);
} // namespace hv

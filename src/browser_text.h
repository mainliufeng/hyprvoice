#pragma once
#include "config.h"
#include <atspi/atspi.h>
#include <chrono>
namespace hv {
// Expand only embedded text within a verified focused web editor. The snapshot
// contains hashes/offsets, never text. Optional content stays local for the
// existing bounded context query. Password roles are checked before text reads.
bool ReadBrowserText(AtspiAccessible *node, Json &snapshot,
                     bool &protected_field, std::string *content,
                     std::chrono::steady_clock::time_point until);
} // namespace hv

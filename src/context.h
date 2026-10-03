#pragma once
#include "config.h"
namespace hv {
// Read-only accessibility query, run in a deadline-bounded helper process.
Json ReadInputContext(int pid, int max_chars);
} // namespace hv

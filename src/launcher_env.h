#pragma once
#include "config.h"

namespace hv {
// The service inherits its credentials. A direct launcher can read the same
// assignment without adding it to the environment of desktop helper processes.
std::string TextCredential(const Config &config);
} // namespace hv

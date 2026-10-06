#pragma once
#include "config.h"
namespace hv {
// Read-only local checks. Never captures audio or contacts a model endpoint.
Json LocalDiagnostics(const Config &config, bool recognizer_loaded);
std::string DiagnosticText(const Json &report);
} // namespace hv

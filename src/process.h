#pragma once
#include <string>
#include <vector>
namespace hv {
struct ProcessResult {
  int code;
  std::string out, error;
};
ProcessResult Run(const std::vector<std::string> &args,
                  const std::string &input = "", int timeout_ms = 2000);
std::string Checked(const std::vector<std::string> &args,
                    const std::string &input = "", int timeout_ms = 2000);
} // namespace hv

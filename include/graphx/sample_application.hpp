#pragma once
#include <string_view>
namespace graphx {
int run_diagnostic_application(int argc, char** argv);
int run_sample_application(int argc, char** argv, std::string_view type);
}  // namespace graphx

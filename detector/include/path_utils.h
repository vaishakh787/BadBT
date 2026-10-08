#pragma once

#include <string>
#include <vector>
#include "device_info.h"

namespace badbt {

// Returns the directory containing the current executable (UTF-8, no trailing slash)
std::string get_executable_directory();

// Searches candidate locations for the ONNX model file and returns the first existing path.
// Searches relative to:
//   1. Explicit override if provided
//   2. Executable dir: <exe_dir>/model/rf_model.onnx
//   3. Executable dir: <exe_dir>/rf_model.onnx
//   4. Build tree relative: <exe_dir>/../../model/rf_model.onnx
//   5. Build tree relative: <exe_dir>/../model/rf_model.onnx
//   6. Current working directory: model/rf_model.onnx
//   7. Current working directory: rf_model.onnx
std::string find_model_path(const std::string& preferred_path = "");

// UTF-8 / UTF-16 conversions
std::wstring utf8_to_wstring(const std::string& str);
// (wstring_to_utf8 is declared in device_info.h)

// Checks if a file exists on disk
bool file_exists(const std::string& path);

} // namespace badbt

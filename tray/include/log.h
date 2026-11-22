#pragma once

#include <string>

namespace badbt {

// Append one timestamped line to %APPDATA%\BadBT\badbt.log (thread-safe, best effort).
// Used to diagnose Bluetooth disconnect / blocklist behavior on real hardware.
void log_line(const std::string& message);

// Full path of the log file.
std::string log_file_path();

} // namespace badbt

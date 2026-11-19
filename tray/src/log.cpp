#include "log.h"
#include "../../detector/include/path_utils.h"

#include <ctime>
#include <fstream>
#include <mutex>

namespace badbt {

std::string log_file_path() {
    static const std::string path = get_appdata_dir() + "\\badbt.log";
    return path;
}

void log_line(const std::string& message) {
    static std::mutex mutex;
    static size_t bytes_written = 0;
    constexpr size_t kMaxBytesPerRun = 1024 * 1024; // never let a noisy run fill the disk

    std::lock_guard<std::mutex> lock(mutex);
    if (bytes_written > kMaxBytesPerRun) {
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &now);
    char stamp[32] = {0};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_buf);

    std::ofstream out(log_file_path(), std::ios::app);
    if (!out) {
        return;
    }
    std::string line = std::string(stamp) + " " + message + "\n";
    out << line;
    bytes_written += line.size();
}

} // namespace badbt

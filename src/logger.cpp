#include "logger.h"
#include <cstdio>
#include <mutex>
#include <system_error>

namespace {
std::mutex output_mutex;
void log(FILE* output, const char* level, std::string_view message) noexcept {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::fprintf(output, "[%s] ", level);
    std::fwrite(message.data(), 1, message.size(), output);
    std::fputc('\n', output);
    std::fflush(output);
}
}
void Logger::info(std::string_view message) noexcept { log(stdout, "INFO", message); }
void Logger::error(std::string_view message) noexcept { log(stderr, "ERROR", message); }
void Logger::system_error(std::string_view operation, int error_number) noexcept {
    try {
        error(std::string(operation) + ": " + std::error_code(error_number, std::generic_category()).message());
    } catch (...) {
        error(operation);
    }
}

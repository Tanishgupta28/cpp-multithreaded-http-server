#pragma once
#include <string_view>

namespace Logger {
void info(std::string_view message) noexcept;
void error(std::string_view message) noexcept;
void system_error(std::string_view operation, int error_number) noexcept;
}

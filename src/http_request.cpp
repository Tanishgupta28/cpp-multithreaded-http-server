#include "http_request.h"

bool HttpRequest::parse_request_line(std::string_view line) {
    method_.clear();
    path_.clear();
    version_.clear();

    const auto first_space = line.find(' ');
    if (first_space == std::string_view::npos || first_space == 0) {
        return false;
    }
    const auto second_space = line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos || second_space == first_space + 1) {
        return false;
    }
    const auto method = line.substr(0, first_space);
    const auto path = line.substr(first_space + 1, second_space - first_space - 1);
    const auto version = line.substr(second_space + 1);

    // HTTP method tokens allow letters, digits, and these punctuation marks.
    constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
    for (unsigned char character : method) {
        const bool letter = (character >= 'A' && character <= 'Z')
                         || (character >= 'a' && character <= 'z');
        const bool digit = character >= '0' && character <= '9';
        if (!letter && !digit && punctuation.find(character) == std::string_view::npos) {
            return false;
        }
    }
    if (path.front() != '/' || (version != "HTTP/1.0" && version != "HTTP/1.1")) {
        return false;
    }
    for (unsigned char character : path) {
        if (character <= 32 || character >= 127) {
            return false;
        }
    }
    method_ = method;
    path_ = path;
    version_ = version;
    return true;
}

#pragma once

#include <string>
#include <string_view>

class HttpRequest {
public:
    bool parse_request_line(std::string_view line);
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& version() const { return version_; }

private:
    std::string method_;
    std::string path_;
    std::string version_;
};

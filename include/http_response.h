#pragma once

#include <string>
#include <string_view>

class HttpResponse {
public:
    HttpResponse(std::string_view status, std::string_view body,
                 std::string_view content_type = "text/plain");
    std::string serialize() const;

private:
    std::string status_;
    std::string body_;
    std::string content_type_;
};

#include "http_response.h"

HttpResponse::HttpResponse(std::string_view status, std::string_view body,
                           std::string_view content_type)
    : status_(status), body_(body), content_type_(content_type) {}

std::string HttpResponse::serialize() const {
    std::string response = "HTTP/1.1 ";
    response += status_;
    response += "\r\nContent-Type: " + content_type_;
    response += "\r\nContent-Length: ";
    // Count all body bytes, including embedded NULs in binary files.
    response += std::to_string(body_.size());
    response += "\r\nConnection: close\r\n\r\n";
    response += body_;

    return response;
}

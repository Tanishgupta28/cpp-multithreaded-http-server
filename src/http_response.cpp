#include "http_response.h"

HttpResponse::HttpResponse(std::string_view status, std::string_view body)
    : status_(status), body_(body) {}

std::string HttpResponse::serialize() const {
    std::string response = "HTTP/1.1 ";
    response += status_;
    response += "\r\nContent-Type: text/plain\r\nContent-Length: ";
    // Content-Length counts body bytes, including its final newline.
    response += std::to_string(body_.size());
    response += "\r\nConnection: close\r\n\r\n";
    response += body_;

    return response;
}

#pragma once

#include "http_response.h"

class HttpRequest;

class HttpRouter {
public:
    HttpResponse route(const HttpRequest& request) const;
};

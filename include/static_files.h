#pragma once

#include "http_response.h"
#include <string_view>

class StaticFiles {
public:
    HttpResponse serve(std::string_view path) const;
};

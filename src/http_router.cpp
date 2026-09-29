#include "http_router.h"
#include "http_request.h"
#include "static_files.h"

HttpResponse HttpRouter::route(const HttpRequest& request) const {
    if (request.method() == "GET") {
        if (request.path() == "/") {
            return HttpResponse("200 OK", "Hello from C++ HTTP server!\n");
        }
        if (request.path() == "/health") {
            return HttpResponse("200 OK", "OK\n");
        }
        const StaticFiles files;
        return files.serve(request.path());
    }
    return HttpResponse("404 Not Found", "Not Found\n");
}

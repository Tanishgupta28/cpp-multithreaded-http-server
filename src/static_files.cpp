#include "static_files.h"
#include "lru_cache.h"
#include "logger.h"
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <exception>
#include <string>

namespace {
class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0 && close(value_) == -1) Logger::system_error("close file", errno);
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const { return value_; }
private:
    int value_;
};

void report_open_error(std::string_view operation, int error) {
    // Missing, inaccessible, or rejected resources are ordinary 404 responses.
    if (error != ENOENT && error != ENOTDIR && error != EACCES
        && error != ELOOP && error != ENAMETOOLONG) {
        Logger::system_error(operation, error);
    }
}

std::string_view content_type(std::string_view name) {
    const auto dot = name.rfind('.');
    const auto extension = dot == std::string_view::npos ? std::string_view{} : name.substr(dot);
    if (extension == ".html") return "text/html";
    if (extension == ".css") return "text/css";
    if (extension == ".txt") return "text/plain";
    return "application/octet-stream";
}
} // namespace

HttpResponse StaticFiles::serve(std::string_view path) const {
    const HttpResponse not_found("404 Not Found", "Not Found\n");
    if (path.size() < 2 || path.front() != '/') return not_found;
    const std::string name(path.substr(1));
    // Only a single filename is supported. No decoding or path normalization.
    if (name.front() == '.' || name.find("..") != std::string::npos
        || name.find_first_of("/\\%?#") != std::string::npos) return not_found;

    // Function-local static initialization is thread-safe in C++11 and later.
    // All short-lived StaticFiles helpers share this process-wide cache.
    static LRUCache cache(16);
    if (const auto cached = cache.get(name)) {
        Logger::info("Static cache hit: " + name);
        return HttpResponse("200 OK", cached->body, cached->content_type);
    }

    Logger::info("Static cache miss: " + name);
    int root_fd;
    do {
        root_fd = open("public", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    } while (root_fd == -1 && errno == EINTR);
    const FileDescriptor root(root_fd);
    if (root.get() < 0) {
        report_open_error("open public directory", errno);
        return not_found;
    }
    // openat anchors the lookup to the opened directory. O_NOFOLLOW rejects
    // symlinks atomically; O_NONBLOCK prevents a FIFO from blocking before fstat.
    int file_fd;
    do {
        file_fd = openat(root.get(), name.c_str(),
                         O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    } while (file_fd == -1 && errno == EINTR);
    const FileDescriptor file(file_fd);
    if (file.get() < 0) {
        report_open_error("open static file", errno);
        return not_found;
    }
    struct stat info{};
    if (fstat(file.get(), &info) == -1) {
        Logger::system_error("stat static file", errno);
        return not_found;
    }
    if (!S_ISREG(info.st_mode)) return not_found;

    std::string body;
    char buffer[4096];
    while (true) {
        const ssize_t count = read(file.get(), buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) continue;
            Logger::system_error("read static file", errno);
            return not_found;
        }
        if (count == 0) break;
        body.append(buffer, static_cast<std::size_t>(count));
    }
    const auto file_data = std::make_shared<const CachedFile>(
        CachedFile{std::move(body), std::string(content_type(name))});
    try {
        cache.put(name, file_data);
    } catch (const std::exception& error) {
        // Caching is optional: a successfully read file can still be served.
        Logger::error("Cache insertion failed; serving uncached file.");
        Logger::error(error.what());
    }
    return HttpResponse("200 OK", file_data->body, file_data->content_type);
}

#include "static_files.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string>

namespace {
class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() { if (value_ >= 0) close(value_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const { return value_; }
private:
    int value_;
};

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

    const FileDescriptor root(open("public", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (root.get() < 0) return not_found;
    // openat anchors the lookup to the opened directory. O_NOFOLLOW rejects
    // symlinks atomically; O_NONBLOCK prevents a FIFO from blocking before fstat.
    const FileDescriptor file(openat(root.get(), name.c_str(),
                                    O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (file.get() < 0) return not_found;
    struct stat info{};
    if (fstat(file.get(), &info) == -1 || !S_ISREG(info.st_mode)) return not_found;

    std::string body;
    char buffer[4096];
    while (true) {
        const ssize_t count = read(file.get(), buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) continue;
            return not_found;
        }
        if (count == 0) break;
        body.append(buffer, static_cast<std::size_t>(count));
    }
    return HttpResponse("200 OK", body, content_type(name));
}

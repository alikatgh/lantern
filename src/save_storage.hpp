// save_storage.hpp — internal regular-file saves and optional desktop isolation.
// The selected directory is pinned by its descriptor; no selected-root failure
// may fall back to the user's ordinary storage. No public game ABI is added.
#pragma once
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace lt {

inline bool saveNameOk(const char* name) {
    if (!name || !*name || std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
        return false;
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
            return false;
    }
    return true;
}

class DesktopSaveRoot {
public:
    DesktopSaveRoot() = default;
    DesktopSaveRoot(const DesktopSaveRoot&) = delete;
    DesktopSaveRoot& operator=(const DesktopSaveRoot&) = delete;
    ~DesktopSaveRoot() { close(); }

    bool configure(const char* root) {
        close();
        selected_ = root != nullptr;
        if (!selected_) return true;
        if (root[0] != '/') return false;
        // Reject a symlink at the selected directory, including one hidden by
        // trailing slashes. Ancestor aliases such as macOS /tmp remain usable.
        std::string path(root);
        while (path.size() > 1 && path.back() == '/') path.pop_back();
        fd_ = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd_ < 0) return false;
        struct stat st{};
        if (fstat(fd_, &st) != 0 || !S_ISDIR(st.st_mode)) {
            close();
            return false;
        }
        return true;
    }

    // Retain the selection on close so post-shutdown calls fail closed too.
    void close() { if (fd_ >= 0) ::close(fd_); fd_ = -1; }
    bool selected() const { return selected_; }

    FILE* openFile(const char* name, const std::string& defaultPath, bool write) const {
        if (!saveNameOk(name) || (selected_ && fd_ < 0)) return nullptr;
        const int flags = (write ? O_WRONLY | O_CREAT : O_RDONLY) |
                          O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
        int file = selected_ ? ::openat(fd_, name, flags, 0600)
                             : ::open(defaultPath.c_str(), flags, 0666);
        if (file < 0) return nullptr;
        struct stat st{};
        // Inspect before truncating. Nonblocking opens avoid hanging on FIFOs;
        // selected-root hard links cannot mutate another location's file.
        if (fstat(file, &st) != 0 || !S_ISREG(st.st_mode) ||
            (selected_ && st.st_nlink != 1) ||
            (write && ftruncate(file, 0) != 0)) {
            ::close(file);
            return nullptr;
        }
        FILE* stream = fdopen(file, write ? "wb" : "rb");
        if (!stream) ::close(file);
        return stream;
    }

private:
    bool selected_ = false;
    int fd_ = -1;
};

inline DesktopSaveRoot& desktopSaveRoot() {
    static DesktopSaveRoot root;
    return root;
}

} // namespace lt

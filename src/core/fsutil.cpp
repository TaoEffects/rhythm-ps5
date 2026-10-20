// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fsutil.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#endif
#if defined(_WIN32)
#define fsync _commit
#endif
#ifndef O_BINARY
#define O_BINARY 0  // Windows : pas de conversion des fins de ligne
#endif

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/log.h"

namespace fsu {

bool readFile(const std::string &path, std::vector<uint8_t> &out, size_t maxBytes) {
    out.clear();
    const int fd = open(path.c_str(), O_RDONLY | O_BINARY);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
        if (static_cast<size_t>(st.st_size) > maxBytes) {
            close(fd);
            return false;
        }
        out.reserve(static_cast<size_t>(st.st_size));
    }
    uint8_t buf[65536];
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        if (n == 0) break;
        if (out.size() + static_cast<size_t>(n) > maxBytes) {
            close(fd);
            return false;
        }
        out.insert(out.end(), buf, buf + n);
    }
    close(fd);
    return true;
}

bool readText(const std::string &path, std::string &out, size_t maxBytes) {
    std::vector<uint8_t> data;
    if (!readFile(path, data, maxBytes)) return false;
    out.assign(reinterpret_cast<const char *>(data.data()), data.size());
    return true;
}

bool writeFileAtomic(const std::string &path, const void *data, size_t size) {
    const std::string tmp = path + ".tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) return false;
    const auto *p = static_cast<const uint8_t *>(data);
    size_t left = size;
    while (left > 0) {
        const ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp.c_str());
            return false;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    fsync(fd);
    close(fd);
#if defined(_WIN32)
    // Windows : rename() refuse d'écraser un fichier existant.
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
#else
    if (rename(tmp.c_str(), path.c_str()) != 0) {
#endif
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool exists(const std::string &path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

bool isDir(const std::string &path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool makeDirs(const std::string &path) {
    if (path.empty() || isDir(path)) return true;
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0 && !makeDirs(path.substr(0, slash))) return false;
#if defined(_WIN32)
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

bool removeFile(const std::string &path) { return unlink(path.c_str()) == 0; }

bool listDir(const std::string &path, std::vector<Entry> &out) {
    out.clear();
    static int s_method = -1;  // méthode qui a marché, journalisée une fois
    if (DIR *dir = opendir(path.c_str())) {
        while (dirent *e = readdir(dir)) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            Entry entry;
            entry.name = e->d_name;
#if defined(_WIN32)
            entry.isDir = isDir(join(path, entry.name));  // MinGW : pas de d_type
#else
            entry.isDir = e->d_type == DT_DIR || (e->d_type == DT_UNKNOWN && isDir(join(path, entry.name)));
#endif
            out.push_back(entry);
        }
        closedir(dir);
        if (s_method != 0) LOG("fs: listage par opendir() ok");
        s_method = 0;
        return true;
    }
    const int openErr = errno;
#if defined(__FreeBSD__)
    // Repli : lecture brute des entrées avec getdents(). FreeBSD renvoie EINVAL si le
    // tampon est plus petit qu'un bloc du système de fichiers : on essaie plusieurs
    // tailles (16 Ko, 64 Ko, 256 Ko), avec et sans O_DIRECTORY.
    static size_t s_size = 0;
    const size_t sizes[] = {s_size ? s_size : 16384, 65536, 262144};
    int lastErr = 0;
    for (size_t attempt = 0; attempt < 6; ++attempt) {
        const size_t size = sizes[attempt % 3];
        const int flags = attempt < 3 ? (O_RDONLY | O_DIRECTORY) : O_RDONLY;
        const int fd = open(path.c_str(), flags);
        if (fd < 0) {
            lastErr = errno;
            continue;
        }
        std::vector<char> buf(size);
        bool ok = true;
        std::vector<Entry> found;
        for (;;) {
            const int n = getdents(fd, buf.data(), static_cast<int>(buf.size()));
            if (n < 0) {
                ok = false;
                lastErr = errno;
                break;
            }
            if (n == 0) break;
            for (int off = 0; off < n;) {
                const dirent *e = reinterpret_cast<const dirent *>(buf.data() + off);
                if (e->d_reclen == 0) break;
                off += e->d_reclen;
                if (e->d_fileno == 0) continue;
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                Entry entry;
                entry.name.assign(e->d_name, e->d_namlen);
                entry.isDir = e->d_type == DT_DIR;
                found.push_back(entry);
            }
        }
        close(fd);
        if (ok) {
            out = std::move(found);
            if (s_method != 1 || s_size != size)
                LOG("fs: opendir refuse (errno %d), listage par getdents() ok (tampon %zu, essai %zu)", openErr, size, attempt);
            s_method = 1;
            s_size = size;
            return true;
        }
    }
    LOG("fs: getdents(%s) a echoue errno=%d", path.c_str(), lastErr);
#endif
    LOG("fs: impossible de lister %s (opendir errno %d)", path.c_str(), openErr);
    return false;
}

std::string join(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string toLower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string extensionLower(const std::string &name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return "";
    return toLower(name.substr(dot + 1));
}

std::string sanitizeName(const std::string &name) {
    std::string r;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            static_cast<unsigned char>(c) < 32)
            r += '_';
        else
            r += c;
    }
    while (!r.empty() && (r.back() == '.' || r.back() == ' ')) r.pop_back();
    if (r.size() > 120) r.resize(120);
    return r.empty() ? "_" : r;
}

}  // namespace fsu

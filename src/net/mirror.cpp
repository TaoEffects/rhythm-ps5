// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/mirror.h"

#include <fcntl.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "core/fsutil.h"
#include "core/json.h"
#include "core/log.h"
#include "core/thread.h"
#include "miniz/miniz.h"
#include "net/http.h"

namespace net {

void Mirror::start(const std::string &baseUrl, const std::string &songsDir) {
    if (running_.load()) return;
    base_ = baseUrl;
    while (!base_.empty() && base_.back() == '/') base_.pop_back();
    songsDir_ = songsDir;
    running_.store(true);
    thr::start(thread_, threadMain, this, "miroir", 1024 * 1024);
    coverThreads_.resize(4);
    for (pthread_t &t : coverThreads_) thr::start(t, coverMain, this, "vignettes", 1024 * 1024);
}

void Mirror::stop() {
    if (!running_.exchange(false)) return;
    cancelAll_.store(true);
    pthread_join(thread_, nullptr);
    for (pthread_t &t : coverThreads_) pthread_join(t, nullptr);
    coverThreads_.clear();
}

void Mirror::search(const std::string &query, Sort sort, int page) {
    std::lock_guard<std::mutex> lock(mutex_);
    query_ = query;
    sort_ = sort;
    page_ = page;
    searchPending_ = true;
    searchBusy_.store(true);
}

bool Mirror::takeResults(std::vector<RemoteSet> &out, std::string &error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!resultsReady_) return false;
    resultsReady_ = false;
    out = std::move(results_);
    error = resultsError_;
    return true;
}

void Mirror::requestCover(int setId, const std::string &url) {
    std::lock_guard<std::mutex> lock(mutex_);
    coverJobs_.emplace_back(setId, url);
}

bool Mirror::takeCover(int &setId, gfx::ImageData &image) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (covers_.empty()) return false;
    setId = covers_.front().first;
    image = std::move(covers_.front().second);
    covers_.pop_front();
    return true;
}

std::shared_ptr<DownloadState> Mirror::download(const RemoteSet &set, bool quiet) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &d : downloads_)
        if (d->setId == set.id && d->phase.load() != 4) return d;
    auto d = std::make_shared<DownloadState>();
    d->setId = set.id;
    d->quiet = quiet;
    d->label = set.title.empty() ? "map " + std::to_string(set.id) : set.artist + " - " + set.title;
    downloads_.push_back(d);
    downloadJobs_.emplace_back(d, set);
    return d;
}

std::vector<std::shared_ptr<DownloadState>> Mirror::downloads() {
    std::lock_guard<std::mutex> lock(mutex_);
    return downloads_;
}

void *Mirror::threadMain(void *self) {
    static_cast<Mirror *>(self)->loop();
    return nullptr;
}

void *Mirror::coverMain(void *self) {
    static_cast<Mirror *>(self)->coverLoop();
    return nullptr;
}

void Mirror::coverLoop() {
    while (running_.load()) {
        std::pair<int, std::string> cover{0, ""};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!coverJobs_.empty()) {
                cover = coverJobs_.front();  // dans l'ordre de la liste (haut de page d'abord)
                coverJobs_.pop_front();
            }
        }
        if (!cover.first) {
            usleep(15000);
            continue;
        }
        Response r = get(cover.second, 4u << 20, nullptr, &cancelAll_);
        if (!r.ok()) continue;
        gfx::ImageData img;
        if (gfx::decodeImageMemory(r.body.data(), r.body.size(), 800, img)) {
            std::lock_guard<std::mutex> lock(mutex_);
            covers_.emplace_back(cover.first, std::move(img));
        }
    }
}

void Mirror::loop() {
    while (running_.load()) {
        bool doSearchNow = false;
        std::pair<int, std::string> cover{0, ""};
        std::pair<std::shared_ptr<DownloadState>, RemoteSet> dl;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (searchPending_) {
                doSearchNow = true;
            } else if (!downloadJobs_.empty()) {
                dl = downloadJobs_.front();
                downloadJobs_.pop_front();
            }
        }
        if (doSearchNow) {
            doSearch();
        } else if (dl.first) {
            doDownload(dl.first, dl.second);
        } else {
            usleep(20000);
        }
    }
}

RemoteSet parseSet(const json::Value &v) {
    RemoteSet s;
    s.id = static_cast<int>(v["id"].num());
    s.title = v["title"].str();
    s.artist = v["artist"].str();
    s.titleUnicode = v["title_unicode"].str();
    s.artistUnicode = v["artist_unicode"].str();
    s.creator = v["creator"].str();
    s.status = v["status"].str();
    s.bpm = v["bpm"].num();
    s.playCount = static_cast<long long>(v["play_count"].num());
    s.favourites = static_cast<long long>(v["favourite_count"].num());
    s.coverUrl = v["covers"]["card@2x"].str();
    for (const json::Value &b : v["beatmaps"].a) {
        RemoteDiff d;
        d.id = static_cast<int>(b["id"].num());
        d.version = b["version"].str();
        d.stars = static_cast<float>(b["difficulty_rating"].num());
        d.mode = static_cast<int>(b["mode_int"].num());
        if (b["total_length"].num() > s.lengthSec) s.lengthSec = static_cast<int>(b["total_length"].num());
        if (d.mode == 0) s.diffs.push_back(d);
    }
    return s;
}

void Mirror::doSearch() {
    std::string query;
    Sort sort;
    int page;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        query = query_;
        sort = sort_;
        page = page_;
        searchPending_ = false;
    }
    const char *sortKey = sort == Sort::Recent ? "ranked_date:desc" : (sort == Sort::Favourites ? "favourite_count:desc" : "play_count:desc");
    constexpr int kPerPage = 30;
    std::string url = base_ + "/api/v2/search?mode=0&amount=" + std::to_string(kPerPage) +
                      "&offset=" + std::to_string(page * kPerPage) + "&sort=" + sortKey;
    if (!query.empty()) url += "&q=" + urlEncode(query);
    else url += "&status=1";
    LOG("miroir: recherche %s", url.c_str());
    Response r = get(url, 8u << 20);
    if (!r.error.empty() && url.rfind("https://", 0) == 0) {
        // Certaines configurations de la console refusent les certificats : on tente en HTTP.
        LOG("miroir: echec HTTPS (%s), essai en HTTP", r.error.c_str());
        const std::string plain = "http://" + url.substr(8);
        Response r2 = get(plain, 8u << 20);
        if (r2.error.empty()) {
            r = std::move(r2);
            base_ = "http://" + base_.substr(8);
        }
    }
    std::vector<RemoteSet> sets;
    std::string error;
    if (!r.ok()) {
        error = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
    } else {
        json::Value root;
        std::string jerr;
        if (!json::parse(std::string(r.body.begin(), r.body.end()), root, &jerr) || root.type != json::Value::Array) {
            error = "reponse illisible (" + jerr + ")";
        } else {
            for (const json::Value &v : root.a) {
                RemoteSet s = parseSet(v);
                if (s.id > 0 && !s.diffs.empty()) sets.push_back(std::move(s));
            }
        }
    }
    LOG("miroir: %zu resultats %s", sets.size(), error.c_str());
    std::lock_guard<std::mutex> lock(mutex_);
    if (searchPending_) return;  // une nouvelle recherche a été lancée entre-temps
    results_ = std::move(sets);
    resultsError_ = error;
    resultsReady_ = true;
    searchBusy_.store(false);
}

void Mirror::doDownload(const std::shared_ptr<DownloadState> &d, const RemoteSet &requested) {
    d->phase.store(1);
    RemoteSet set = requested;
    if (set.title.empty()) {
        // Seul l'identifiant est connu (maps de base) : on demande les infos au miroir.
        Response info = get(base_ + "/api/v2/s/" + std::to_string(set.id), 4u << 20, nullptr, &cancelAll_);
        json::Value root;
        if (info.ok() && json::parse(std::string(info.body.begin(), info.body.end()), root, nullptr) &&
            root.type == json::Value::Object) {
            const RemoteSet full = parseSet(root);
            if (full.id == set.id) set = full;
        }
        if (!set.title.empty()) d->label = set.artist + " - " + set.title;
    }
    const std::string tmp = songsDir_ + "/" + std::to_string(set.id) + ".osz.part";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        d->error = "impossible d'ecrire dans " + songsDir_;
        d->phase.store(4);
        return;
    }
    const std::string url = base_ + "/d/" + std::to_string(set.id);
    LOG("miroir: telechargement %s", url.c_str());
    Response r = get(
        url, 512u << 20,
        [&](uint64_t got, uint64_t total) {
            d->received.store(got);
            d->total.store(total);
        },
        &cancelAll_,
        [&](const uint8_t *data, size_t size) {
            while (size > 0) {
                const ssize_t n = write(fd, data, size);
                if (n <= 0) return false;
                data += n;
                size -= static_cast<size_t>(n);
            }
            return true;
        });
    close(fd);
    if (!r.ok()) {
        d->error = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
        d->phase.store(4);
        unlink(tmp.c_str());
        LOG("miroir: echec %d : %s", set.id, d->error.c_str());
        return;
    }
    d->phase.store(2);
    const std::string folder =
        fsu::sanitizeName(set.title.empty() ? std::to_string(set.id) : std::to_string(set.id) + " " + set.artist + " - " + set.title);
    std::string error;
    if (!extractOsz(tmp, songsDir_ + "/" + folder, error)) {
        d->error = error;
        d->phase.store(4);
        unlink(tmp.c_str());
        return;
    }
    unlink(tmp.c_str());
    // Étoiles fournies par le miroir, lues par la bibliothèque.
    std::string stars;
    char line[64];
    for (const RemoteDiff &rd : set.diffs) {
        snprintf(line, sizeof(line), "%d %.3f\n", rd.id, rd.stars);
        stars += line;
    }
    fsu::writeFileAtomic(songsDir_ + "/" + folder + "/.stars", stars.data(), stars.size());
    d->folder = folder;
    d->phase.store(3);
    LOG("miroir: %d installe dans %s (%.1f Mo)", set.id, folder.c_str(), d->received.load() / 1048576.0);
}

bool extractOsz(const std::string &oszPath, const std::string &destDir, std::string &error) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, oszPath.c_str(), 0)) {
        error = "archive invalide";
        return false;
    }
    fsu::makeDirs(destDir);
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    int written = 0;
    std::string manifest;  // liste des fichiers : utile si la console refuse de lister le dossier
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        std::string name = st.m_filename;
        for (char &c : name)
            if (c == '\\') c = '/';
        if (name.find("..") != std::string::npos || name.empty() || name[0] == '/') continue;
        const std::string ext = fsu::extensionLower(name);
        if (ext == "mp4" || ext == "avi" || ext == "flv" || ext == "wmv" || ext == "mkv" || ext == "m4v") continue;
        const std::string out = destDir + "/" + name;
        const size_t slash = out.find_last_of('/');
        fsu::makeDirs(out.substr(0, slash));
        if (!mz_zip_reader_extract_to_file(&zip, i, out.c_str(), 0)) {
            LOG("osz: echec extraction %s", name.c_str());
            continue;
        }
        ++written;
        manifest += name + "\n";
    }
    mz_zip_reader_end(&zip);
    fsu::writeFileAtomic(destDir + "/.files", manifest.data(), manifest.size());
    if (written == 0) {
        error = "archive vide";
        return false;
    }
    return true;
}

}  // namespace net

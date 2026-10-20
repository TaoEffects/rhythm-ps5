// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/update.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include <SDL.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "core/fsutil.h"
#include "core/json.h"
#include "core/log.h"
#include "core/thread.h"
#include "net/http.h"
#include "platform/platform.h"
#include "version.h"

namespace upd {
namespace {

// « alpha1.10 » -> {1, 10} ; préfixe ignoré.
std::vector<int> numbers(const std::string &tag) {
    std::vector<int> out;
    int cur = -1;
    for (char ch : tag) {
        if (ch >= '0' && ch <= '9') cur = (cur < 0 ? 0 : cur * 10) + (ch - '0');
        else if (cur >= 0) out.push_back(cur), cur = -1;
    }
    if (cur >= 0) out.push_back(cur);
    return out;
}

}  // namespace

bool newerTag(const std::string &candidate, const std::string &current) {
    const std::vector<int> a = numbers(candidate), b = numbers(current);
    for (size_t i = 0; i < a.size() || i < b.size(); ++i) {
        const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

Updater::~Updater() {
    if (threadStarted_) pthread_join(thread_, nullptr);
}

void Updater::start() {
    if (state_.load() != Idle) return;
    state_.store(Checking);
    threadStarted_ = thr::start(thread_, checkMain, this, "maj");
    if (!threadStarted_) state_.store(Failed);
}

void *Updater::checkMain(void *self) {
    Updater &u = *static_cast<Updater *>(self);
    // Les releases du projet sont des préversions : on lit la liste (la plus récente d'abord).
    const net::Response r = net::get("https://api.github.com/repos/" RHYTHM_REPO "/releases?per_page=5", 1u << 20);
    json::Value root;
    if (!r.ok() || !json::parse(std::string(r.body.begin(), r.body.end()), root, nullptr) ||
        root.type != json::Value::Array) {
        LOG("maj: verification impossible (HTTP %d %s)", r.status, r.error.c_str());
        u.state_.store(Failed);
        return nullptr;
    }
    for (size_t i = 0; i < root.size(); ++i) {
        const json::Value &rel = root[i];
        if (rel["draft"].b) continue;
        const std::string tag = rel["tag_name"].str();
        if (!newerTag(tag, RHYTHM_RELEASE_TAG)) continue;
        if (!u.tag_.empty() && !newerTag(tag, u.tag_)) continue;
        u.tag_ = tag;
        u.pageUrl_ = rel["html_url"].str();
        u.setupUrl_.clear();
        const json::Value &assets = rel["assets"];
        for (size_t k = 0; k < assets.size(); ++k)
            if (assets[k]["name"].str() == "rhythm-ps5-setup.exe") u.setupUrl_ = assets[k]["browser_download_url"].str();
    }
    if (u.tag_.empty()) {
        LOG("maj: a jour (%s)", RHYTHM_RELEASE_TAG);
        u.state_.store(UpToDate);
    } else {
        LOG("maj: nouvelle version %s disponible", u.tag_.c_str());
        u.state_.store(Available);
    }
    return nullptr;
}

bool Updater::install() {
    if (state_.load() != Available) return false;
#if defined(_WIN32)
    if (!setupUrl_.empty()) {
        if (threadStarted_) pthread_join(thread_, nullptr);
        state_.store(Downloading);
        threadStarted_ = thr::start(thread_, downloadMain, this, "maj");
        if (!threadStarted_) state_.store(Failed);
        return false;
    }
#endif
    // macOS / Linux (ou pas d'installateur) : page de la release dans le navigateur.
    SDL_OpenURL(pageUrl_.c_str());
    return false;
}

void *Updater::downloadMain(void *self) {
#if defined(_WIN32)
    Updater &u = *static_cast<Updater *>(self);
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    const std::string path = std::string(tmp) + "rhythm-ps5-setup.exe";
    const net::Response r = net::get(u.setupUrl_, 256u << 20, [&](uint64_t got, uint64_t total) {
        if (total > 0) u.progress_.store(static_cast<float>(static_cast<double>(got) / static_cast<double>(total)));
    });
    if (!r.ok() || !fsu::writeFileAtomic(path, r.body.data(), r.body.size())) {
        LOG("maj: telechargement impossible (HTTP %d %s)", r.status, r.error.c_str());
        u.state_.store(Failed);
        return nullptr;
    }
    // Installation silencieuse : l'installateur ferme le jeu, remplace les fichiers et le relance.
    const HINSTANCE h = ShellExecuteA(nullptr, "open", path.c_str(), "/S", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(h) <= 32) {
        LOG("maj: lancement de l'installateur impossible");
        u.state_.store(Failed);
        return nullptr;
    }
    u.state_.store(Launched);
#else
    (void)self;
#endif
    return nullptr;
}

}  // namespace upd

// SPDX-License-Identifier: GPL-3.0-or-later
// Mises à jour (ordinateur) : au lancement, on regarde la dernière release GitHub du
// projet. Windows : téléchargement de l'installateur puis installation silencieuse
// (le jeu se relance) ; macOS / Linux : ouverture de la page de la release.
#pragma once

#include <atomic>
#include <string>

#include <pthread.h>

namespace upd {

class Updater {
public:
    ~Updater();
    void start();  // vérification en arrière-plan
    // Une version plus récente a été trouvée (tag, ex. « alpha1.2 »).
    bool available() const { return state_.load() == Available; }
    const std::string &latestTag() const { return tag_; }
    // Lance la mise à jour ; renvoie true si le jeu doit se fermer (installateur lancé).
    bool install();
    bool downloading() const { return state_.load() == Downloading; }
    float progress() const { return progress_.load(); }
    bool failed() const { return state_.load() == Failed; }
    // Après install() sous Windows : l'installateur est prêt, le jeu doit quitter.
    bool readyToQuit() const { return state_.load() == Launched; }

private:
    enum State { Idle, Checking, UpToDate, Available, Downloading, Launched, Failed };
    static void *checkMain(void *self);
    static void *downloadMain(void *self);
    std::atomic<int> state_{Idle};
    std::atomic<float> progress_{0};
    std::string tag_, pageUrl_, setupUrl_;
    pthread_t thread_{};
    bool threadStarted_ = false;
};

// Compare deux tags « alphaX.Y » : vrai si `candidate` est plus récent que `current`.
bool newerTag(const std::string &candidate, const std::string &current);

}  // namespace upd

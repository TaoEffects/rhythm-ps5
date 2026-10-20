// SPDX-License-Identifier: GPL-3.0-or-later
// L'application : ressources partagées, écrans, transitions, entrées, fonds,
// musique d'aperçu et scores locaux.
#pragma once

#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <pthread.h>

#include "app/context.h"
#if !defined(__PROSPERO__)
#include "app/update.h"
#endif
#include "game/library.h"
#include "game/play.h"
#include "game/skin.h"
#include "net/mirror.h"
#include "ui/lz.h"
#include "ui/osk.h"

class Game;

// Un écran gère lui-même ses animations d'entrée et de sortie, comme les
// OsuScreen de lazer : pendant une transition, l'ancien et le nouveau sont
// dessinés ensemble (le nouveau au-dessus quand on avance, dessous au retour).
class Screen {
public:
    explicit Screen(Game &g) : game(g) {}
    virtual ~Screen() = default;
    // forward : l'écran arrive « par-dessus » (true) ou on revient à lui (false).
    virtual void enter() {}
    // Lance l'animation de sortie ; renvoie sa durée en ms (l'écran reste dessiné).
    virtual double leave() { return 0; }
    virtual void update(const UiInput &in) = 0;
    virtual void render() = 0;
    // Écran de jeu : les boutons servent à frapper, pas à naviguer.
    virtual bool gameplay() const { return false; }
    virtual bool songSelect() const { return false; }
    bool forward = true;

protected:
    Game &game;
};

struct LocalScore {
    std::string key;  // dossier/fichier de la difficulté
    long long score = 0;
    double accuracy = 0;
    int maxCombo = 0, n300 = 0, n100 = 0, n50 = 0, nMiss = 0;
    double pp = 0;
    std::string rank, mods;
    long long date = 0;
};

class Game {
public:
    bool init();
    void frame(const plat::InputState &raw, double now);
    void reportSwap(double swapMs);
    void shutdown();
    bool wantsExit() const { return exit_; }
    void requestExit() { exit_ = true; }

    Context ctx;

    // Navigation entre écrans. forward = false pour un retour en arrière.
    void goTo(std::unique_ptr<Screen> next, bool forward = true);
    // Overlays par-dessus l'écran courant (options, téléchargement).
    void openOverlay(std::unique_ptr<Screen> overlay);
    void closeOverlay();
    bool overlayOpen() const { return overlay_ != nullptr; }
    std::unique_ptr<Screen> makeIntro();
    std::unique_ptr<Screen> makeFirstRun();
    std::unique_ptr<Screen> makePlayerLoader();
    std::unique_ptr<Screen> makeMainMenu();
    std::unique_ptr<Screen> makeSongSelect();
    std::unique_ptr<Screen> makePlay();
    std::unique_ptr<Screen> makeResults(const game::PlayResult &result);
    std::unique_ptr<Screen> makeOptions();
    std::unique_ptr<Screen> makeDownload();

    // Map préparée par l'écran de chargement pour la partie suivante.
    game::Beatmap preparedMap;
    std::string preparedKey;
    int retryCount = 0;
    bool quickRestart = false;

    // Sélection courante.
    int selectedSet = -1, selectedDiff = 0;
    game::Mods mods;
    const game::SetInfo *currentSet() const;
    const game::DiffInfo *currentDiff() const;
    void select(int set, int diff, bool playPreview);
    // Ferme l'overlay et ouvre la sélection sur cette map (après un téléchargement).
    void showSet(int set);
    int jumpToSet = -1;  // lu par la sélection de map si elle est déjà ouverte

    // Fond global (image de la map choisie) : assombrissement et flou animés,
    // réglés par chaque écran (comme les BackgroundScreen de lazer).
    void setBackground(float dim, float blur, double durationMs = 500, anim::Ease e = anim::Ease::OutQuint);
    void drawBackground();
    float backgroundParallax = 1;
    bool backgroundVisible = true;
    // Éléments persistants façon lazer.
    // Skins osu! : liste des dossiers, application du choix des options.
    std::vector<std::string> skinNames;
    void applySkin();
    lz::Logo logo;
    lz::Toolbar toolbar;
    bool introFlash = false;  // l'intro vient de finir : flash blanc à l'arrivée du menu
    // Vignette (image de fond réduite) d'un set, chargée à la demande ; nullptr si pas prête.
    const gfx::Texture *thumbnail(int setIndex);
    // Dernières entrées (pour les écrans qui en ont besoin au rendu).
    const UiInput &input() const { return lastInput_; }
    int frameLimit() const { return settings_.frameLimit; }
#if !defined(__PROSPERO__)
    upd::Updater &updater() { return updater_; }
#endif
    // Saisie de texte : clavier virtuel du jeu sur PS5, saisie directe ailleurs.
    bool textInputSupported() const;
    bool startTextInput(const char *title, const std::string &initial);
    int pollTextInput(std::string &out);
    // Aperçu musical : boucle à partir du PreviewTime.
    void playPreview();
    // Piste du lancement (« triangles » de cYsmix, ressources de lazer) :
    // téléchargée une fois puis jouée à chaque démarrage. 0 absente, 1 en cours,
    // 2 prête, 3 échec.
    int introTrackState() const { return introTrackState_.load(); }
    bool playIntroTrack();
    bool introTrackPlaying() const { return introTrackPlaying_; }

    // Scores locaux.
    std::vector<LocalScore> scoresFor(const std::string &key) const;
    void addScore(const LocalScore &s);
    static std::string diffKey(const game::SetInfo &set, const game::DiffInfo &diff);

    net::Mirror mirror;
    std::string lastToast;
    double toastTime = -10;
    void toast(const std::string &text);

private:
    UiInput buildInput(const plat::InputState &raw);
    void updateBackgroundLoads();
    void loadScores();
    void saveScores() const;

    gfx::SpriteBatch batch_;
    Fonts fonts_;
    gfx::GlyphCache cjk_;  // japonais (titres d'origine, recherche)
    Textures tex_;
    audio::Mixer mixer_;
    audio::Music music_;
    audio::SoundBank sfx_;
    Settings settings_;
    game::Library library_;
    game::Skin skin_;
    std::string skinsDir_, soundsDir_;
    std::string introTrackPath_;
    std::atomic<int> introTrackState_{0};
    bool introTrackPlaying_ = false;
    pthread_t introTrackThread_{};
    bool introTrackThreadStarted_ = false;
    static void *introTrackMain(void *self);
    gfx::ImageLoader images_;
    bool audioOk_ = false;

    std::unique_ptr<Screen> screen_;
    struct Leaving {
        std::unique_ptr<Screen> screen;
        double until;
        bool above;  // dessiné au-dessus de l'écran courant (retour en arrière)
    };
    std::vector<Leaving> leaving_;
    std::unique_ptr<Screen> overlay_, overlayLeaving_;
    double overlayLeaveUntil_ = 0;
    bool exit_ = false;
    double perfLast_ = 0, perfUpdate_ = 0, perfBuild_ = 0, perfSubmit_ = 0, perfSwap_ = 0, perfFrameMax_ = 0, perfSwapMax_ = 0;
    int perfFrames_ = 0;
    void handleToolbar(const UiInput &in);
    void handleVolume(UiInput &in, const plat::InputState &raw);
    void drawVolume();
    bool handleNowPlaying(const UiInput &in);
    void drawNowPlaying();
    bool nowPlayingOpen_ = false;
    anim::Tween nowPlaying_{0};
    double volumeUntil_ = 0;
    int volumeSelected_ = 1;
    bool volumeDirty_ = false;
    void handleDownloads();
    std::vector<const net::DownloadState *> handledDownloads_;

    // Fonds : version nette et version floutée, chacune en fondu enchaîné.
    gfx::Texture bgCurrent_, bgPrevious_, bgBlurCurrent_, bgBlurPrevious_;
    double bgFadeStart_ = -10, bgBlurFadeStart_ = -10;
    uint64_t bgTicket_ = 0, bgBlurTicket_ = 0;
    anim::Tween bgDim_{0.3f}, bgBlur_{0};
    std::string bgPath_;
    std::string previewAudio_;

    // Entrées
    plat::InputState prev_;
    double repeatNext_ = 0;
    int repeatDir_ = 0;
    float cursorX_ = kVW / 2, cursorY_ = kVH / 2;
    float lastMouseX_ = -1, lastMouseY_ = -1;
    bool touchWasDown_ = false;
    bool mouseActive_ = false;
    bool appliedFullscreen_ = true;
    bool padPointer_ = false;  // le pointeur suit le stick droit (menus)
    float pointerX_ = -1, pointerY_ = -1;
    float ptrTouchX_ = 0, ptrTouchY_ = 0;
    bool ptrTouchDown_ = false;
    float touchLastX_ = 0, touchLastY_ = 0;
    double lastFrame_ = -1;
    double fpsAccum_ = 0;
    int fpsFrames_ = 0;
    float fps_ = 0;

    std::vector<LocalScore> scores_;
    struct Thumb {
        gfx::Texture tex;
        uint64_t ticket = 0;
        double lastUse = 0;
        bool failed = false;
    };
    std::unordered_map<std::string, Thumb> thumbs_;
    std::unordered_map<uint64_t, std::string> thumbTickets_;
    UiInput lastInput_;
    lz::OnScreenKeyboard osk_;
#if !defined(__PROSPERO__)
    upd::Updater updater_;
    bool updateAnnounced_ = false;
#endif
};

// SPDX-License-Identifier: GPL-3.0-or-later
// Chargement avant la partie (PlayerLoader), partie (pause et échec façon
// GameplayMenuOverlay), et résultats (ResultsScreen + anneau de précision animé),
// d'après les séquences de lazer (durées et courbes reprises de son code).
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app/game.h"
#include "game/storyboard.h"
#include "core/i18n.h"
#include "core/fsutil.h"
#include "core/log.h"
#include "game/library.h"
#include "ui/lz.h"

namespace {

using namespace lz;

// ------------------------------------------------------------------ Chargement

class PlayerLoader : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        Context &c = game.ctx;
        const double t = ms(c);
        quick_ = game.quickRestart;
        game.quickRestart = false;
        start_ = t;
        // Contenu : échelle 0,7 -> 1 (650 ms OutQuint), fondu 500 ms ; infos après 500 ms.
        contentScale_.fromTo(t, quick_ ? 1 : 0.7f, 1, 650, Ease::OutQuint);
        contentA_.fromTo(t, 0, 1, 500, Ease::OutQuint, quick_ ? 500 : 0);
        metaA_.fromTo(t, 0, 1, 500, Ease::OutQuint, quick_ ? 500 : 500);
        if (quick_) black_.fromTo(t, 1, 0, 5000, Ease::OutQuint, 50);
        game.setBackground(0.2f, 1, 800, Ease::OutQuint);
        game.toolbar.show(c, false);
        // Logo : au centre, minuscule (0,15), puis glisse à sa place au-dessus du titre.
        Logo &logo = game.logo;
        logo.interactive = false;
        logo.visualiser = false;
        logo.moveTo(c, kVW / 2, kVH / 2, 0.15f, 300, Ease::OutQuint);
        logo.opacity.go(t, quick_ ? 0 : 1, 350, Ease::OutQuint);
        logoTracked_ = false;
        prepare();
    }

    double leave() override {
        const double t = ms(game.ctx);
        contentScale_.go(t, 0.7f, 600, Ease::OutQuint);
        contentA_.go(t, 0, 300, Ease::OutQuint);
        game.logo.opacity.go(t, 0, 150, Ease::OutQuint);
        game.logo.scale.go(t, game.logo.scale.at(t) * 0.8f, 600, Ease::OutQuint);
        return 600;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const double t = ms(c);
        if (!logoTracked_ && t - start_ >= 250) {
            logoTracked_ = true;
            game.logo.moveTo(c, kVW / 2, facadeY(), 0.15f, 500, Ease::InOutExpo);
        }
        // Le morceau d'aperçu s'éteint pendant le chargement.
        const float k = static_cast<float>(std::min(1.0, (t - start_) / 650.0));
        if (c.music->loaded() && !prepared_) c.music->setVolume(c.settings->musicVolume * (1 - k));
        if (in.back) {
            c.play(c.sfx->uiBack);
            game.goTo(game.makeSongSelect(), false);
            return;
        }
        if (!error_.empty()) {
            if (in.confirm) game.goTo(game.makeSongSelect(), false);
            return;
        }
        // La musique de la partie est chargée une fois l'aperçu éteint.
        if (prepared_ && !musicReady_ && t - start_ >= (quick_ ? 0 : 650)) {
            musicReady_ = true;
            const std::string path = game.preparedMap.folder + "/" + game.preparedMap.audioFile;
            if (c.music->path() != path || !c.music->loaded()) c.music->load(path);
            else c.music->pause();
        }
        // Lancement : après 1,8 s (0,5 s pour un « recommencer »), une fois prêt.
        const double delay = quick_ ? 500 : 1800 + 500;
        if (prepared_ && musicReady_ && t - start_ >= delay) {
            game.goTo(game.makePlay());
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const float a = contentA_.at(t), s = contentScale_.at(t);
        b.pushScaleAround(kVW / 2, kVH / 2, s, a);
        const game::SetInfo *set = game.currentSet();
        const game::DiffInfo *diff = game.currentDiff();
        if (set && diff) {
            const float ma = metaA_.at(t);
            b.pushAlpha(ma);
            float y = facadeY() + 36 * U + 15 * U;
            const std::string title = set->shownTitle(c.settings->originalMetadata);
            const std::string artist = set->shownArtist(c.settings->originalMetadata);
            text(c, c.fonts->semibold, kVW / 2, y, 36 * U, col::white, title, gfx::Align::Center, kVW * 0.8f, true);
            y += 36 * U + 6 * U;
            text(c, c.fonts->regular, kVW / 2, y, 26 * U, col::white, artist, gfx::Align::Center, kVW * 0.8f, true);
            y += 26 * U + 10 * U;
            // Pochette 300 x 60 avec voile de chargement.
            const float cw = 300 * U, ch = 60 * U, cx = kVW / 2 - cw / 2;
            b.shadow(cx, y + 2 * U, cw, ch, 10 * U, 8 * U, alpha(col::black, 0.4f));
            b.roundRect(cx, y, cw, ch, 10 * U, hex(0x222222));
            if (const gfx::Texture *th = game.thumbnail(game.selectedSet)) {
                const float ta = static_cast<float>(th->width) / static_cast<float>(th->height), aspect = cw / ch;
                const float cut = std::max(0.0f, (1 - ta / aspect) / 2);
                b.roundRectTex(*th, cx, y, cw, ch, 10 * U, col::white, 0, cut, 1, 1 - cut);
            }
            if (!prepared_ || t - start_ < 900) {
                b.roundRect(cx, y, cw, ch, 10 * U, alpha(col::black, 0.5f));
                spinner(c, kVW / 2, y + ch / 2, 20 * U, 1);
            }
            y += ch + 10 * U + 4 * U;
            text(c, c.fonts->regular, kVW / 2, y, 26 * U, col::white, diff->version, gfx::Align::Center, kVW * 0.8f, true);
            y += 26 * U + 8 * U;
            const float pa = static_cast<float>(std::clamp((t - start_ - 500) / 300.0, 0.0, 1.0));
            const float pillW = c.fonts->bold.measure(15 * U, "0.00") + 30 * U;
            starPill(c, kVW / 2 - pillW / 2, y, 20 * U, diff->stars, anim::ease(Ease::InQuint, pa) > 0 ? 1 : 0, diff->stars >= 0);
            y += 20 * U + 40 * U;
            // Source / auteur.
            const float gx = kVW / 2;
            c.fonts->regular.draw(b, gx - 5 * U, y, 16 * U, col::gray(204), "Source", gfx::Align::Right);
            text(c, c.fonts->semibold, gx + 5 * U, y, 16 * U, col::white, set->source.empty() ? "-" : set->source, gfx::Align::Left, 500 * U);
            y += 22 * U;
            c.fonts->regular.draw(b, gx - 5 * U, y, 16 * U, col::gray(204), "Mappeur", gfx::Align::Right);
            text(c, c.fonts->semibold, gx + 5 * U, y, 16 * U, col::white, set->creator, gfx::Align::Left, 500 * U);
            y += 22 * U + 20 * U;
            // Mods.
            const std::string mods = game.mods.label();
            if (!mods.empty()) {
                std::vector<std::string> list;
                size_t p = 0;
                while (p < mods.size()) {
                    size_t e = mods.find(' ', p);
                    if (e == std::string::npos) e = mods.size();
                    list.push_back(mods.substr(p, e - p));
                    p = e + 1;
                }
                float mx = kVW / 2 - static_cast<float>(list.size() - 1) * 40 * U / 2;
                for (const std::string &m : list) {
                    const bool red = m == "HR" || m == "DT" || m == "NC" || m == "HD";
                    const bool blue = m == "AUTO";
                    modIcon(c, blue ? "AT" : m.c_str(), mx, y + 15 * U, 26 * U, blue ? col::blue1 : red ? col::red1 : col::lime1);
                    mx += 40 * U;
                }
            }
            b.popTransform();
        }
        if (!error_.empty()) {
            c.fonts->semibold.drawShadow(b, kVW / 2, kVH - 160 * U, 22 * U, col::red1, error_.c_str(), gfx::Align::Center);
            c.fonts->regular.drawShadow(b, kVW / 2, kVH - 130 * U, 16 * U, col::white, "Rond : retour", gfx::Align::Center);
        }
        b.popTransform();
        const float bl = black_.at(t);
        if (bl > 0.001f) b.rect(0, 0, kVW, kVH, alpha(col::black, bl));
    }

private:
    // Logo en haut du bloc ; le bloc (logo -> mappeur, ~385 u) est centré verticalement.
    float facadeY() const { return kVH / 2 - 150 * U; }

    void prepare() {
        Context &c = game.ctx;
        const game::SetInfo *set = game.currentSet();
        const game::DiffInfo *diff = game.currentDiff();
        if (!set || !diff) {
            error_ = "Aucune map sélectionnée";
            return;
        }
        const std::string folder = set->path(c.songsDir);
        std::string text;
        game::Beatmap map;
        if (!fsu::readText(folder + "/" + diff->file, text) || !game::parseBeatmap(text, map, false)) {
            error_ = "Impossible de lire la map";
            return;
        }
        map.folder = folder;
        map.file = diff->file;
        game.preparedKey = Game::diffKey(*set, *diff);
        game.preparedMap = std::move(map);
        prepared_ = true;
    }

    double start_ = 0;
    bool quick_ = false, prepared_ = false, logoTracked_ = false, musicReady_ = false;
    std::string error_;
    Tween contentScale_{0.7f}, contentA_{0}, metaA_{0}, black_{0};
};

// ------------------------------------------------------------------ Partie

class PlayScreen : public Screen {
public:
    using Screen::Screen;
    bool gameplay() const override { return !play_.paused() && !failMenu_; }

    void enter() override {
        Context &c = game.ctx;
        const double t = ms(c);
        game.toolbar.show(c, false);
        game.logo.opacity.snap(0);
        game.setBackground(c.settings->backgroundDim, c.settings->blurInGame ? 1 : 0, 800, Ease::OutQuint);
        game.backgroundParallax = 0;
        fade_.fromTo(t, 0, 1, 400, Ease::OutQuint);
        game::Beatmap map = game.preparedMap;
        key_ = game.preparedKey;
        const std::string audio = map.folder + "/" + map.audioFile;
        if ((c.music->path() != audio || !c.music->loaded()) && !c.music->load(audio)) {
            error_ = i18n::tr("Musique introuvable : ") + map.audioFile;
            return;
        }
        // Storyboard : affiché par-dessus le fond, qu'on n'assombrit plus (le jeu le fait
        // après le storyboard) ; caché s'il réutilise l'image de fond, comme osu!.
        if (c.settings->storyboard && storyboard_.load(map.folder, map.file)) {
            game.setBackground(0, c.settings->blurInGame ? 1 : 0, 800, Ease::OutQuint);
            if (storyboard_.usesFile(map.background)) game.backgroundVisible = false;
            play_.setStoryboard(&storyboard_);
        }
        if (!play_.start(c, std::move(map), game.mods, nullptr)) error_ = "Map vide";
        started_ = true;
    }

    double leave() override {
        Context &c = game.ctx;
        play_.stop(c);
        game.backgroundVisible = true;
        c.music->setRate(game.mods.rate(), !game.mods.nightcore);
        c.music->setVolume(c.settings->musicVolume);
        game.backgroundParallax = 1;
        fade_.go(ms(c), 0, 300, Ease::OutQuint);
        return 300;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const double t = ms(c);
        if (!error_.empty()) {
            if (in.back || in.confirm) game.goTo(game.makeSongSelect(), false);
            return;
        }
        if (play_.paused() || failMenu_) {
            const int n = failMenu_ ? 2 : 3;
            if (in.up) cursor_ = (cursor_ + n - 1) % n;
            if (in.down) cursor_ = (cursor_ + 1) % n;
            if (in.up || in.down) c.play(c.sfx->uiHover, 0.8f);
            bool activate = in.confirm;
            for (int i = 0; i < n; ++i) {
                const float y = buttonY(i, n);
                if (in.hover(0, y - 40 * U, kVW, 80 * U) && in.mouseMoved && cursor_ != i) {
                    cursor_ = i;
                    c.play(c.sfx->uiHover, 0.7f);
                }
                if (in.clicked(0, y - 40 * U, kVW, 80 * U)) {
                    cursor_ = i;
                    activate = true;
                }
            }
            for (int i = 0; i < 3; ++i) {
                fx_[i].setHover(c, i == cursor_, 400, 200);
                fx_[i].setPressed(c, i == cursor_ && in.raw && (in.raw->mouse.buttons & 1u));
            }
#if !defined(__PROSPERO__)
            // Ordinateur : Échap deux fois de suite (moins de 0,6 s) quitte tout de suite.
            if (in.back && !failMenu_ && c.now - pausedAt_ < 0.6) {
                game.goTo(game.makeSongSelect(), false);
                return;
            }
#endif
            if ((in.options || in.back) && !failMenu_) {
                resume();
                return;
            }
            if (activate) {
                fx_[cursor_].trigger(c, 1, 100);
                c.play(c.sfx->uiSelect);
                const int action = failMenu_ ? cursor_ + 1 : cursor_;
                if (action == 0) {
                    resume();
                } else if (action == 1) {
                    ++game.retryCount;
                    game.quickRestart = true;
                    game.goTo(game.makePlayerLoader());
                } else {
                    game.goTo(game.makeSongSelect(), false);
                }
            }
            return;
        }
        if (in.options || in.back) {
            play_.setPaused(c, true);
            showMenu(false);
            pausedAt_ = c.now;
            return;
        }
        if (in.extra && play_.canSkip()) play_.skip(c);
        storyboard_.update();
        play_.update(c, in);
        if (play_.finished() && !leaving_) {
            const game::PlayResult &r = play_.result();
            if (r.failed && !failMenu_) {
                showMenu(true);
                return;
            }
            leaving_ = true;
            if (r.completed && !r.mods.autoplay) {
                LocalScore s;
                s.key = key_;
                s.score = r.score;
                s.accuracy = r.accuracy;
                s.maxCombo = r.maxCombo;
                s.n300 = r.n300;
                s.n100 = r.n100;
                s.n50 = r.n50;
                s.nMiss = r.nMiss;
                s.rank = r.rank;
                s.mods = r.mods.label();
                s.pp = r.pp;
                s.date = static_cast<long long>(time(nullptr));
                game.addScore(s);
            }
            game.retryCount = 0;
            game.goTo(game.makeResults(r));
        }
        (void)t;
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        b.pushAlpha(fade_.at(t));
        if (!error_.empty()) {
            c.fonts->semibold.drawShadow(b, kVW / 2, kVH / 2 - 40, 30 * U, col::red1, error_.c_str(), gfx::Align::Center);
            c.fonts->regular.drawShadow(b, kVW / 2, kVH / 2 + 20, 18 * U, col::white, "Rond : retour", gfx::Align::Center);
        } else if (started_) {
            play_.render(c);
        }
        b.popTransform();
        const float ma = menuA_.at(t);
        if (ma > 0.001f) renderMenu(ma);
    }

private:
    double pausedAt_ = -10;

    void showMenu(bool fail) {
        Context &c = game.ctx;
        failMenu_ = fail;
        cursor_ = 0;
        menuA_.go(ms(c), 1, 200, Ease::In);
        c.play(fail ? c.sfx->uiError : c.sfx->uiOpen, 0.7f);
    }
    void resume() {
        Context &c = game.ctx;
        menuA_.go(ms(c), 0, 200, Ease::In);
        play_.setPaused(c, false);
    }

    float buttonY(int i, int n) const { return kVH / 2 + 40 * U + (static_cast<float>(i) - (n - 1) / 2.0f) * (80 * U + 2 * U); }

    void renderMenu(float a) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        b.pushAlpha(a);
        b.rect(0, 0, kVW, kVH, alpha(col::black, 0.75f));
        const char *title = failMenu_ ? "échec" : "pause";
        c.fonts->semibold.drawShadow(b, kVW / 2, kVH / 2 - 210 * U, 48 * U, failMenu_ ? col::red1 : col::yellow, title, gfx::Align::Center, 5 * U);
        const int n = failMenu_ ? 2 : 3;
        const char *labels[3] = {"Continuer", "Recommencer", "Quitter"};
        const uint32_t cols[3] = {col::green, col::yellowDark, hex(0xaa1b27)};
        for (int i = 0; i < n; ++i) {
            const int k = failMenu_ ? i + 1 : i;
            dialogButton(c, fx_[i], kVW / 2, buttonY(i, n), kVW - 100 * U, 80 * U, labels[k], cols[k]);
        }
        // Infos (essais, progression).
        char line[96];
        snprintf(line, sizeof(line), i18n::tr("Essais : %d"), game.retryCount + 1);
        const float iy = buttonY(n - 1, n) + 80 * U;
        c.fonts->regular.drawShadow(b, kVW / 2 - 10 * U, iy, 18 * U, col::white, line, gfx::Align::Right);
        snprintf(line, sizeof(line), i18n::tr("Progression : %.0f%%"), play_.progress() * 100);
        c.fonts->regular.drawShadow(b, kVW / 2 + 10 * U, iy, 18 * U, col::white, line);
#if !defined(__PROSPERO__)
        if (!failMenu_)
            c.fonts->regular.drawShadow(b, kVW / 2, iy + 34 * U, 15 * U, alpha(col::white, 0.6f), "Échap deux fois : quitter tout de suite",
                                        gfx::Align::Center);
#endif
        b.popTransform();
    }

    game::Play play_;
    game::Storyboard storyboard_;
    std::string error_, key_;
    bool started_ = false, leaving_ = false, failMenu_ = false;
    int cursor_ = 0;
    Tween menuA_{0}, fade_{0};
    ButtonFx fx_[3];
};

// ------------------------------------------------------------------ Résultats

// Seuils de précision de lazer pour les segments de l'anneau (D C B A S SS).
const float kCut[] = {0.0f, 0.7f, 0.8f, 0.9f, 0.95f, 0.99f, 1.0f};
const char *const kRanks[] = {"D", "C", "B", "A", "S", "SS"};

float invOutPow10(float y) { return 1 - std::pow(std::max(0.0f, 1 - y), 0.1f); }

class Results : public Screen {
public:
    Results(Game &g, const game::PlayResult &r) : Screen(g), r_(r) {}

    void enter() override {
        Context &c = game.ctx;
        const double t = ms(c);
        start_ = t;
        game.toolbar.show(c, false);
        game.setBackground(0.5f, 1, 250, Ease::None);
        game.logo.opacity.go(t, 0, 200, Ease::OutQuint);
        screenA_.fromTo(t, 0, 1, 250, Ease::OutQuint);
        game.playPreview();
        // Remplissage visé : évite les encoches entre segments, plafonné hors SS.
        float target = static_cast<float>(r_.accuracy);
        if (target < 1) target = std::min(target, 0.99f - 1.0f / 360) - 0.001f;
        for (float cut : kCut)
            if (std::fabs(target - cut) < 1.0f / 360) target = cut + (target >= cut ? 1.0f : -1.0f) / 360;
        target_ = std::max(0.0f, target);
    }

    double leave() override {
        screenA_.go(ms(game.ctx), 0, 100, Ease::None);
        return 100;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const double t = ms(c) - start_;
        // Sons : souffle à 443 ms, tic-tac pendant le remplissage, applaudissement au rang.
        if (!swoosh_ && t >= 443) {
            swoosh_ = true;
            c.play(c.sfx->uiOpen, 0.5f);
        }
        if (t >= 450 && t < 450 + 3000 * 0.6 && t >= nextTick_) {
            c.play(c.sfx->uiHover, 0.25f);
            const float p = static_cast<float>((t - 450) / 1800.0);
            nextTick_ = t + 18 + p * 280;
        }
        if (!applause_ && t >= 1950 + 545) {
            applause_ = true;
            c.play(r_.failed ? c.sfx->uiError : c.sfx->uiDownloaded, 0.8f);
        }
        backFx_.setHover(c, in.hover(12 * U, kVH - 12 * U - kBackH, kBackW, kBackH));
        retryFx_.setHover(c, in.hover(kVW / 2 - 150 * U, kVH - 45 * U, 300 * U, 40 * U));
        offsetFx_.setHover(c, canSuggest() && in.hover(kVW / 2 + 160 * U, kVH - 45 * U, 300 * U, 40 * U));
        if (in.back || in.confirm || (backFx_.hovered && in.mouseClick)) {
            backFx_.trigger(c);
            c.play(c.sfx->uiBack);
            game.goTo(game.makeSongSelect(), false);
            return;
        }
        if (in.extra2 || (retryFx_.hovered && in.mouseClick)) {
            c.play(c.sfx->uiSelect);
            game.quickRestart = true;
            game.goTo(game.makePlayerLoader());
            return;
        }
        if ((in.extra || (offsetFx_.hovered && in.mouseClick)) && canSuggest()) {
            c.settings->offsetMs += static_cast<int>(std::lround(r_.meanError));
            c.saveSettings();
            offsetApplied_ = true;
            c.play(c.sfx->uiSelect);
            game.toast(i18n::tr("Décalage audio réglé à ") + std::to_string(c.settings->offsetMs) + " ms");
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double now = ms(c);
        const double t = now - start_;
        b.pushAlpha(screenA_.at(now));
        const float pw = 360 * U, ph = 586 * U;
        const float px = kVW / 2 - pw / 2, py = (kVH - 50 * U - ph) / 2 + 20 * U;
        // Panneau : couche haute (#444 -> #333) et couche du milieu (#555 -> #333), séparées.
        const float split = 26.5f * U * anim::ease(Ease::OutQuint, static_cast<float>(std::clamp((t - 300) / 200.0, 0.0, 1.0)));
        b.shadow(px, py - split, pw, ph + split * 2, 20 * U, 16 * U, alpha(col::black, 0.4f));
        b.roundRectGradient(px, py - split, pw, 53 * U + 20 * U, 20 * U, gfx::Corners::vertical(hex(0x444444), hex(0x333333)));
        b.roundRectGradient(px, py + 53 * U - 20 * U + split, pw, ph - 53 * U + 20 * U, 20 * U,
                            gfx::Corners::vertical(hex(0x555555), hex(0x333333)));
        // Avatar (joueur local) centré sur le bord haut.
        const float ay = py - split;
        b.roundRect(kVW / 2 - 40 * U, ay - 40 * U, 80 * U, 80 * U, 20 * U, hex(0x2a2a2a));
        icon(c, fa::user, kVW / 2, ay, 36 * U, col::gray(160));
        c.fonts->semibold.draw(b, kVW / 2, ay + 44 * U, 14 * U, col::white, "Joueur local", gfx::Align::Center);
        // Contenu du milieu.
        const game::SetInfo *set = game.currentSet();
        const game::DiffInfo *diff = game.currentDiff();
        float y = py + 53 * U + split + 12 * U;
        if (set) {
            text(c, c.fonts->semibold, kVW / 2, y, 20 * U, col::white, set->shownTitle(c.settings->originalMetadata),
                 gfx::Align::Center, pw - 20 * U);
            y += 24 * U;
            text(c, c.fonts->semibold, kVW / 2, y, 14 * U, col::white, set->shownArtist(c.settings->originalMetadata),
                 gfx::Align::Center, pw - 20 * U);
            y += 18 * U;
        }
        y += 22 * U;
        const float R = 115 * U;
        renderCircle(kVW / 2, y + R, R, t);
        y += 2 * R + 12 * U;
        // Score qui défile (3 s OutPow10 à partir de 450 ms).
        const float sp = t < 450 ? 0 : anim::ease(Ease::OutPow10, static_cast<float>((t - 450) / 3000.0));
        char line[96];
        formatScore(static_cast<long long>(static_cast<double>(r_.score) * sp), line, sizeof(line));
        c.fonts->light.draw(b, kVW / 2, y, 60 * U, col::white, line, gfx::Align::Center, -2 * U);
        y += 64 * U;
        // Étoiles, mods, difficulté.
        if (diff) {
            const std::string mods = r_.mods.label();
            const float pillW = c.fonts->bold.measure(10 * U, "0.00") + 22 * U;
            const float modsW = mods.empty() ? 0 : 30 * U;
            float rx = kVW / 2 - (pillW + modsW) / 2;
            starPill(c, rx, y, 15 * U, diff->stars, 1, diff->stars >= 0);
            if (!mods.empty()) c.fonts->bold.drawMid(b, rx + pillW + 8 * U, y + 7.5f * U, 13 * U, col::yellow, mods.c_str());
            y += 20 * U;
            text(c, c.fonts->semibold, kVW / 2, y, 16 * U, col::white, diff->version, gfx::Align::Center, pw - 20 * U);
            y += 20 * U;
            if (set) text(c, c.fonts->regular, kVW / 2, y, 12 * U, col::white, "mappée par " + set->creator, gfx::Align::Center, pw - 20 * U);
            y += 22 * U;
        }
        // Statistiques : précision / combo / UR, puis jugements.
        struct Stat {
            const char *head;
            std::string value;
            uint32_t color;
        };
        char acc[32], combo[32], ur[32], pp[32];
        const float sa = t < 450 ? 0 : anim::ease(Ease::Out, static_cast<float>((t - 450) / 1500.0));
        snprintf(acc, sizeof(acc), "%.2f%%", r_.accuracy * 100 * sa);
        snprintf(combo, sizeof(combo), "%dx", static_cast<int>(r_.maxCombo * sp));
        snprintf(ur, sizeof(ur), "%.1f", r_.unstableRate);
        snprintf(pp, sizeof(pp), "%.0f", r_.pp * sa);
        const Stat row1[4] = {{"PRÉCISION", acc, col::white},
                              {"COMBO MAX", combo, col::white},
                              {"PP", pp, col::white},
                              {"INSTABILITÉ", ur, col::white}};
        const Stat row2[4] = {{"GREAT", std::to_string(static_cast<int>(r_.n300 * sp)), col::blue},
                              {"OK", std::to_string(static_cast<int>(r_.n100 * sp)), col::greenLight},
                              {"MEH", std::to_string(static_cast<int>(r_.n50 * sp)), col::yellow},
                              {"MISS", std::to_string(static_cast<int>(r_.nMiss * sp)), col::red}};
        auto drawRow = [&](const Stat *st, int n, float ry, int baseIndex) {
            const float cw = (pw - 20 * U) / static_cast<float>(n);
            for (int i = 0; i < n; ++i) {
                const float fa = static_cast<float>(std::clamp((t - 450 - 200.0 * (baseIndex + i)) / 100.0, 0.0, 1.0));
                if (fa <= 0) continue;
                const float cx = px + 10 * U + cw * (static_cast<float>(i) + 0.5f);
                b.pushAlpha(fa);
                const float hw = cw - 8 * U;
                b.roundRect(cx - hw / 2, ry, hw, 12 * U, 6 * U, hex(0x222222));
                c.fonts->semibold.drawMid(b, cx, ry + 6 * U, 10 * U, st[i].color == col::white ? col::white : st[i].color, st[i].head,
                                       gfx::Align::Center);
                c.fonts->regular.draw(b, cx, ry + 15 * U, 20 * U, col::white, st[i].value.c_str(), gfx::Align::Center);
                b.popTransform();
            }
        };
        drawRow(row1, 4, y, 0);
        drawRow(row2, 4, y + 46 * U, 4);
        // Barre du bas et boutons.
        b.rect(0, kVH - 50 * U, kVW, 50 * U, hex(0x333333));
        drawBarButton(retryFx_, kVW / 2 - 150 * U, "Rejouer", fa::rotate_right, col::purple1, Pad::Square);
        if (canSuggest()) {
            snprintf(line, sizeof(line), i18n::tr("Corriger le décalage (%+.0f ms)"), r_.meanError);
            drawBarButton(offsetFx_, kVW / 2 + 160 * U, line, fa::stopwatch, col::orange1, Pad::Triangle);
        }
        backButton(c, backFx_, 12 * U, kVH - 12 * U, "retour");
        b.popTransform();
    }

private:
    void drawBarButton(ButtonFx &fx, float x, const char *label, const char *glyph, uint32_t accent, Pad pad) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float hv = fx.hover.at(ms(c));
        const float y = kVH - 45 * U, w = 300 * U, h = 40 * U;
        b.roundRect(x, y, w, h, 7 * U, lighten(hex(0x444444), 0.3f * hv), kShear);
        b.roundRect(x, y + h - 4 * U, w, 4 * U, 2 * U, accent, kShear);
        icon(c, glyph, x + 24 * U, y + h / 2, 16 * U, accent);
        c.fonts->semibold.drawMid(b, x + 44 * U, y + h / 2, 16 * U, col::white, label);
        padGlyph(c, pad, x + w - 20 * U, y + h / 2, 22 * U);
    }

    void renderCircle(float cx, float cy, float R, double t) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float appear = anim::ease(Ease::OutQuint, static_cast<float>(std::clamp(t / 200.0, 0.0, 1.0)));
        b.pushScaleAround(cx, cy, appear, 1);
        // Anneau de fond, puis anneau gradué (révélé de 150 à 950 ms).
        b.ring(cx, cy, R, R * 0.21f, alpha(hex(0x2f2f2f), 0.5f));
        const float grade = t < 150 ? 0 : anim::ease(Ease::OutPow10, static_cast<float>((t - 150) / 800.0));
        const float gr = R * 0.8f - 2.5f * U;
        const float gap = 2.0f / 360;
        for (int i = 0; i < 6; ++i) {
            const float from = kCut[i] + gap / 2, to = std::min(grade, kCut[i + 1] - gap / 2);
            if (to > from) b.arc(cx, cy, gr, gr * 0.05f, from, to, gfx::Corners::solid(rankColor(kRanks[i])));
        }
        // Anneau de précision : dégradé #7CF6FF -> #BAFFA9, rempli de 450 à 3450 ms.
        const float fill = t < 450 ? 0 : target_ * anim::ease(Ease::OutPow10, static_cast<float>((t - 450) / 3000.0));
        if (fill > 0) b.arc(cx, cy, R, R * 0.2f, 0, fill, gfx::Corners::vertical(hex(0x7cf6ff), hex(0xbaffa9)));
        // Badges de rang autour de l'anneau.
        for (int i = 0; i < 6; ++i) {
            float pos;
            if (i < 3) pos = (kCut[i] + kCut[i + 1]) / 2;
            else if (i == 3) pos = kCut[3] + (kCut[4] - kCut[3]) * 0.25f;
            else if (i == 4) pos = kCut[4] + (kCut[5] - kCut[4]) * 0.25f;
            else pos = 1.0f;
            const float badgeAcc = std::min(0.99f, i == 5 ? 1.0f : kCut[i]);
            if (r_.accuracy + 1e-6 < kCut[i] || target_ <= 0) continue;
            const double at = 450 + invOutPow10(std::min(1.0f, badgeAcc / std::max(0.001f, target_))) * 3000;
            if (t < at) continue;
            const float ba = std::min(1.0f, static_cast<float>((t - at) / 50.0));
            const float glow = 1 - std::min(1.0f, static_cast<float>((t - at) / 500.0));
            const float ang = pos * 2 * anim::kPi - anim::kPi / 2;
            const float br = R + 22 * U;
            const float bx = cx + std::cos(ang) * br, by = cy + std::sin(ang) * br;
            const uint32_t rc = rankColor(kRanks[i]);
            if (glow > 0) b.shadow(bx - 14 * U, by - 7 * U, 28 * U, 14 * U, 7 * U, 10 * U, alpha(rc, 0.6f * glow * ba));
            b.roundRect(bx - 14 * U, by - 7 * U, 28 * U, 14 * U, 7 * U, alpha(rc, ba));
            c.fonts->bold.drawMid(b, bx, by, 11 * U, alpha(darken(rc, 2.5f), ba), kRanks[i], gfx::Align::Center);
        }
        // Lettre du rang (à 1950 ms).
        if (t >= 1950) {
            const double rt = t - 1950;
            const std::string &rank = r_.rank;
            const uint32_t rc = rankColor(rank);
            const bool high = rank == "A" || rank[0] == 'S';
            float dy = 0, sc = 1, la = std::min(1.0f, static_cast<float>(rt / 200.0));
            if (!high) dy = -20 * U * (1 - anim::ease(Ease::OutBounce, static_cast<float>(std::min(1.0, rt / 200.0))));
            if (rank[0] == 'S') sc = 1.05f - 0.05f * anim::ease(Ease::OutQuint, static_cast<float>(std::min(1.0, rt / 3000.0)));
            if (high) {
                // Copie floue additive qui s'éteint.
                const float fl = 1 - anim::ease(rank == "SS" ? Ease::None : Ease::OutQuint,
                                                 static_cast<float>(std::min(1.0, rt / (rank == "SS" ? 3000.0 : 1200.0))));
                if (fl > 0) {
                    b.setBlend(gfx::Blend::Additive);
                    b.circle(cx, cy, R * 0.9f, alpha(rc, 0.35f * fl), R * 0.5f);
                    b.setBlend(gfx::Blend::Alpha);
                }
            }
            const float size = 76 * U * 1.25f * sc;
            const float rankTop = c.fonts->bold.topForCenter(cy, size);
            c.fonts->bold.drawSdf(b, cx, rankTop + dy, size, alpha(rc, 0.6f * la), rank.c_str(), gfx::Align::Center, -8 * U, 0.1f, 0.25f);
            c.fonts->bold.draw(b, cx, rankTop + dy, size, alpha(lighten(rc, 0.3f), la), rank.c_str(), gfx::Align::Center, -8 * U);
            if (r_.failed) c.fonts->bold.drawShadow(b, cx, cy + R * 0.42f, 16 * U, col::red1, "ÉCHEC", gfx::Align::Center);
        }
        b.popTransform();
    }

    static void formatScore(long long v, char *out, size_t n) {
        char raw[32];
        snprintf(raw, sizeof(raw), "%lld", v);
        std::string s(raw), r;
        int k = 0;
        for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
            r.insert(r.begin(), s[static_cast<size_t>(i)]);
            if (++k % 3 == 0 && i > 0) r.insert(r.begin(), ' ');
        }
        snprintf(out, n, "%s", r.c_str());
    }

    bool canSuggest() const { return std::fabs(r_.meanError) > 8 && r_.n300 + r_.n100 + r_.n50 > 30 && !offsetApplied_ && !r_.mods.autoplay; }

    game::PlayResult r_;
    double start_ = 0, nextTick_ = 0;
    float target_ = 0;
    bool offsetApplied_ = false, swoosh_ = false, applause_ = false;
    Tween screenA_{0};
    ButtonFx backFx_, retryFx_, offsetFx_;
};

}  // namespace

std::unique_ptr<Screen> Game::makePlayerLoader() { return std::make_unique<PlayerLoader>(*this); }
std::unique_ptr<Screen> Game::makePlay() { return std::make_unique<PlayScreen>(*this); }
std::unique_ptr<Screen> Game::makeResults(const game::PlayResult &r) { return std::make_unique<Results>(*this, r); }

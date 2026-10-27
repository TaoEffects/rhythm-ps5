// SPDX-License-Identifier: GPL-3.0-or-later
// Écran de classement façon osu!stable : bandeau noir (titre, mappeur, joueur),
// grand panneau à gauche (score, 300 / 100 / 50 / geki / katu / ratés, combo
// max, précision), grosse lettre de rang à droite avec les mods, courbe de vie,
// boutons Recommencer et Corriger le décalage. Disposition reprise de McOsu (base
// 640x480 : 1 unité = hauteur / 480).
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app/game.h"
#include "core/i18n.h"
#include "game/library.h"
#include "ui/lz.h"
#include "ui/stable.h"
#include "ui/stable_ui.h"

namespace {

using anim::Ease;
using lz::alpha;
using lz::ButtonFx;
using lz::hex;
using lz::Tween;

class StableResults : public Screen {
public:
    StableResults(Game &g, const game::PlayResult &r) : Screen(g), r_(r) {}

    void enter() override {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        start_ = t;
        game.toolbar.show(c, false);
        game.setBackground(0.6f, 0, 300, Ease::OutQuint);
        game.backgroundParallax = 0;
        game.logo.opacity.go(t, 0, 200, Ease::OutQuint);
        screenA_.fromTo(t, 0, 1, 300, Ease::OutQuint);
        grade_.fromTo(t, 1.35f, 1, 500, Ease::OutQuint, 150);
        gradeA_.fromTo(t, 0, 1, 250, Ease::OutQuint, 150);
        played_ = static_cast<long long>(time(nullptr));
        game.playPreview();
        c.play(r_.failed ? c.sfx->uiError : c.sfx->uiDownloaded, 0.8f);
    }

    double leave() override {
        screenA_.go(lz::ms(game.ctx), 0, 200, Ease::OutQuint);
        game.backgroundParallax = 1;
        return 200;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        backFx_.setHover(c, st::backHovered(in), 100, 100);
        retryFx_.setHover(c, in.mouseActive && hit(in, 0), 100, 100);
        offsetFx_.setHover(c, in.mouseActive && canSuggest() && hit(in, 1), 100, 100);
        if (in.back || in.confirm || (backFx_.hovered && in.mouseClick)) {
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
        const double t = lz::ms(c);
        b.pushAlpha(screenA_.at(t));
        renderGraph();
        renderPanel();
        renderGrade();
        renderButtons();
        // En-tête (capture de stable) : titre, mappeur, joueur et date ; « Classement » à droite.
        const game::SetInfo *set = game.currentSet();
        const game::DiffInfo *diff = game.currentDiff();
        if (set && diff) {
            const std::string title = set->shownArtist(c.settings->originalMetadata) + " - " + set->shownTitle(c.settings->originalMetadata) +
                                      " [" + diff->version + "]";
            lz::text(c, c.fonts->regular, 8 * K(), -2 * K(), 36 * K(), st::col::white, title, gfx::Align::Left, kVW * 0.6f, true);
            lz::text(c, c.fonts->regular, 8 * K(), 38 * K(), 30 * K(), st::col::white, i18n::tr("Map de ") + set->creator,
                     gfx::Align::Left, kVW * 0.6f, true);
        }
        char line[128];
        char date[64];
        const time_t tt = static_cast<time_t>(played_);
        struct tm lt;
        localtime_r(&tt, &lt);
        strftime(date, sizeof(date), "%d.%m.%Y %H:%M:%S", &lt);
        snprintf(line, sizeof(line), i18n::tr("Jouée par %s le %s."), i18n::tr("Joueur local"), date);
        c.fonts->regular.drawShadow(b, 8 * K(), 70 * K(), 30 * K(), st::col::white, line);
        const float tf = 112 * K();
        const char *head = r_.failed ? "Échec" : "Classement";
        c.fonts->light.drawSdf(b, kVW - 52 * K(), -14 * K(), tf, gfx::rgba(0, 0, 0, 120), head, gfx::Align::Right, 0, 0.05f, 0.25f);
        c.fonts->light.draw(b, kVW - 52 * K(), -14 * K(), tf, st::col::white, head, gfx::Align::Right);
        st::backButton(c, backFx_);
        b.popTransform();
    }

private:
    static float K() { return kVH / 1080.0f; }

    // Panneau de gauche : bande beige du score, puis panneau bleu des résultats.
    void renderPanel() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const game::Skin *sk = c.skin;
        char line[64];
        if (sk && sk->ui.rankingPanel.ok()) {
            const game::SkinSprite &s = sk->ui.rankingPanel;
            const float k = 713 * K() / s.h();
            b.quad(s.tex, 0, 131 * K(), s.w() * k, 713 * K(), st::col::white);
        } else {
            b.shadow(15 * K(), 158 * K(), 843 * K(), 97 * K(), 6 * K(), 10 * K(), gfx::rgba(0, 0, 0, 120));
            b.roundRectGradient(15 * K(), 158 * K(), 843 * K(), 97 * K(), 6 * K(), gfx::Corners::vertical(hex(0xf2e2bd), hex(0xc9ad7a)));
            c.fonts->light.draw(b, 26 * K(), 160 * K(), 84 * K(), gfx::rgba(255, 255, 255, 90), "Score");
            b.shadow(15 * K(), 278 * K(), 843 * K(), 559 * K(), 6 * K(), 10 * K(), gfx::rgba(0, 0, 0, 120));
            b.roundRectGradient(15 * K(), 278 * K(), 843 * K(), 559 * K(), 6 * K(), gfx::Corners::vertical(hex(0x4a8ae0), hex(0x3a72c8)));
            c.fonts->light.draw(b, 24 * K(), 270 * K(), 80 * K(), gfx::rgba(255, 255, 255, 50), "Résultats");
        }
        snprintf(line, sizeof(line), "%08lld", r_.score);
        st::number(c, line, 255 * K(), 168 * K(), 78 * K(), st::col::white, 0);
        // Grille : 300 / 100 / 50 à gauche, geki / katu / ratés à droite.
        const int counts[6] = {r_.n300, r_.n100, r_.n50, r_.nGeki, r_.nKatu, r_.nMiss};
        for (int i = 0; i < 6; ++i) {
            const float ix = i >= 3 ? 540 * K() : 90 * K(), iy = (360 + 135 * static_cast<float>(i % 3)) * K();
            judgement(i, ix, iy, 94 * K());
            snprintf(line, sizeof(line), "%dx", counts[i]);
            st::number(c, line, ix + 90 * K(), iy - 38 * K(), 74 * K(), st::col::white, 0);
        }
        // Combo et précision.
        label(sk ? &sk->ui.rankingMaxCombo : nullptr, "Combo", 22 * K(), 712 * K());
        label(sk ? &sk->ui.rankingAccuracy : nullptr, "Précision", 418 * K(), 712 * K());
        snprintf(line, sizeof(line), "%dx", r_.maxCombo);
        st::number(c, line, 30 * K(), 742 * K(), 76 * K(), st::col::white, 0);
        snprintf(line, sizeof(line), "%.2f%%", r_.accuracy * 100);
        st::number(c, line, 436 * K(), 742 * K(), 76 * K(), st::col::white, 0);
    }

    // Icône de jugement : image du skin (hit300, hit100k...) ou chiffres lumineux / kanji.
    void judgement(int i, float cx, float cy, float size) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const game::Skin *sk = c.skin;
        const game::SkinSprite *s = nullptr;
        if (sk) {
            const game::SkinSprite *list[6] = {&sk->hit300, &sk->hit100, &sk->hit50, &sk->ui.hit300g, &sk->ui.hit100k, &sk->hit0};
            s = list[i]->ok() ? list[i] : nullptr;
        }
        if (s) {
            // Comme stable : le plus petit côté de l'image ramené à la case (94 px en 1080p).
            const float k = size / std::max(1.0f, std::min(s->w(), s->h()));
            b.quad(s->tex, cx - s->w() * k / 2, cy - s->h() * k / 2, s->w() * k, s->h() * k, st::col::white);
            return;
        }
        static const char *const kText[6] = {"300", "100", "50", "激", "喝", "×"};
        static const uint32_t kCol[6] = {hex(0x7fd8ff), hex(0x9cf05a), hex(0xffd84a), hex(0x7fd8ff), hex(0x9cf05a), hex(0xff3355)};
        const float fsz = i == 5 ? size * 0.62f : i >= 3 ? size * 0.42f : size * 0.34f;
        const gfx::Font &f = c.fonts->semibold;
        const float top = f.topForCenter(cy, fsz);
        b.setBlend(gfx::Blend::Additive);
        f.drawSdf(b, cx, top, fsz, alpha(kCol[i], 0.8f), kText[i], gfx::Align::Center, 0, 0.3f, 0.35f);
        b.setBlend(gfx::Blend::Alpha);
        f.draw(b, cx, top, fsz, lz::lighten(kCol[i], 0.5f), kText[i], gfx::Align::Center);
    }

    void label(const game::SkinSprite *s, const char *text, float x, float cy) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        if (s && s->ok()) {
            const float k = 72 * K() / s->h();
            b.quad(s->tex, x, cy - 36 * K(), s->w() * k, 72 * K(), st::col::white);
            return;
        }
        const float fs = 64 * K();
        const float top = c.fonts->light.topForCenter(cy, fs);
        c.fonts->light.drawSdf(b, x, top, fs, gfx::rgba(10, 40, 90, 160), text, gfx::Align::Left, 0, 0.12f, 0.05f);
        c.fonts->light.draw(b, x, top, fs, st::col::white, text);
    }

    // Grosse lettre de rang (495 px de haut), mods à côté, pp dessous.
    void renderGrade() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = lz::ms(c);
        const float cx = kVW - 255 * K(), cy = 405 * K();
        std::string rank = r_.failed ? "D" : r_.rank;
        const bool silver = !rank.empty() && rank.back() == '+';
        if (silver) rank.pop_back();
        b.pushScaleAround(cx, cy, grade_.at(t), gradeA_.at(t));
        const game::Skin *sk = c.skin;
        const game::SkinSprite *s = sk ? &sk->ui.ranking[game::rankIndex(rank, silver)] : nullptr;
        if (s && s->ok()) {
            const float k = 495 * K() / s->h();
            b.quad(s->tex, cx - s->w() * k / 2, cy - s->h() * k / 2, s->w() * k, s->h() * k, st::col::white);
        } else {
            st::rankLetter(c, rank, silver, cx, cy, 495 * K());
        }
        b.popTransform();
        // Mods : carrés de couleur à droite de la lettre, empilés vers le bas.
        const std::string mods = r_.mods.label();
        std::vector<std::string> list;
        size_t p = 0;
        while (p < mods.size()) {
            size_t e = mods.find(' ', p);
            if (e == std::string::npos) e = mods.size();
            list.push_back(mods.substr(p, e - p));
            p = e + 1;
        }
        float my = 545 * K();
        for (const std::string &m : list) {
            const bool blue = m == "AUTO";
            const bool red = m == "HR" || m == "DT" || m == "NC" || m == "HD";
            const uint32_t mc = blue ? hex(0x1d6fd0) : red ? hex(0xc8323c) : hex(0x5a9a1e);
            const float w = 90 * K(), x = kVW - 135 * K();
            b.roundRectGradient(x, my, w, w, 6 * K(), gfx::Corners::vertical(lz::lighten(mc, 0.25f), lz::darken(mc, 0.2f)));
            b.roundRectBorder(x, my, w, w, 6 * K(), 2 * K(), alpha(st::col::white, 0.85f));
            if (blue) lz::icon(c, fa::gear, x + w / 2, my + w * 0.4f, w * 0.45f, st::col::white);
            c.fonts->semibold.drawMid(b, x + w / 2, blue ? my + w * 0.8f : my + w / 2, blue ? 22 * K() : 36 * K(), st::col::white,
                                      blue ? "Auto" : m.c_str(), gfx::Align::Center);
            my += w + 8 * K();
        }
        char line[32];
        snprintf(line, sizeof(line), "%.0fpp", r_.pp);
        c.fonts->light.drawShadow(b, cx, c.fonts->light.topForCenter(690 * K(), 52 * K()), 52 * K(), st::col::white, line, gfx::Align::Center);
    }

    // Graphe de vie (bas, au centre gauche) : panneau bleu, courbe verte (rouge sous 50 %),
    // « Parfait » par-dessus pour un combo complet.
    void renderGraph() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float x = 370 * K(), y = 862 * K(), w = 425 * K(), h = 196 * K();
        const game::Skin *sk = c.skin;
        if (sk && sk->ui.rankingGraph.ok()) {
            b.quad(sk->ui.rankingGraph.tex, x, y, w, h, st::col::white);
        } else {
            b.roundRect(x, y, w, h, 4 * K(), gfx::rgba(36, 74, 123, 220));
            b.rect(x, y, w, 3 * K(), hex(0x8ce04a));
            c.fonts->regular.draw(b, x + 6 * K(), y + 4 * K(), 20 * K(), alpha(st::col::white, 0.8f), "performance");
            c.fonts->regular.draw(b, x + w - 6 * K(), y + h - 26 * K(), 20 * K(), alpha(st::col::white, 0.8f), "temps", gfx::Align::Right);
        }
        const std::vector<float> &g = r_.hpGraph;
        if (g.size() >= 2) {
            const float gx = x + 10 * K(), gy = y + 30 * K(), gw = w - 20 * K(), gh = h - 60 * K();
            const size_t n = std::min<size_t>(g.size(), 200);
            float px = 0, py = 0;
            for (size_t i = 0; i < n; ++i) {
                // Échantillon : le minimum de la tranche (les chutes de vie restent visibles).
                const size_t a = i * g.size() / n, e = std::max(a + 1, (i + 1) * g.size() / n);
                float v = 1;
                for (size_t j = a; j < e && j < g.size(); ++j) v = std::min(v, g[j]);
                const float xx = gx + gw * static_cast<float>(i) / static_cast<float>(n - 1), yy = gy + gh * (1 - v);
                if (i > 0) b.line(px, py, xx, yy, 3 * K(), v >= 0.5f ? gfx::rgba(130, 235, 60) : gfx::rgba(255, 60, 60));
                px = xx;
                py = yy;
            }
        }
        if (!r_.fullCombo) return;
        if (sk && sk->ui.rankingPerfect.ok()) {
            const game::SkinSprite &s = sk->ui.rankingPerfect;
            const float k = 211 * K() / s.h();
            b.quad(s.tex, 581 * K() - s.w() * k / 2, 975 * K() - 105 * K(), s.w() * k, 211 * K(), st::col::white);
            return;
        }
        // « Parfait » : contour orange foncé, remplissage jaune puis orange vers le bas.
        const float fs = 190 * K(), cx = 581 * K();
        const gfx::Font &f = c.fonts->semibold;
        const float top = f.topForCenter(975 * K(), fs);
        f.drawSdf(b, cx, top, fs, gfx::rgba(150, 40, 0, 230), "Parfait", gfx::Align::Center, 0, 0.14f, 0.03f);
        f.draw(b, cx, top, fs, hex(0xff9a1e), "Parfait", gfx::Align::Center);
        b.pushClip(0, 0, kVW, top + fs * 0.62f);
        f.draw(b, cx, top, fs, hex(0xffd23c), "Parfait", gfx::Align::Center);
        b.popClip();
    }

    // Barres de droite : Recommencer (à la place de « Regarder le replay ») et Corriger le décalage.
    void buttonRect(int i, float &x, float &y, float &w, float &h) const {
        w = 530 * K(), h = i == 0 ? 120 * K() : 84 * K();
        x = kVW - w - 22 * K();
        y = i == 0 ? 750 * K() : 885 * K();
    }
    bool hit(const UiInput &in, int i) const {
        float x, y, w, h;
        buttonRect(i, x, y, w, h);
        return in.hover(x, y, w, h);
    }
    void renderButtons() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        char line[96];
        for (int i = 0; i < 2; ++i) {
            if (i == 1 && !canSuggest()) break;
            float x, y, w, h;
            buttonRect(i, x, y, w, h);
            ButtonFx &fx = i == 0 ? retryFx_ : offsetFx_;
            const float hv = fx.hover.at(lz::ms(c));
            const game::SkinSprite *s = c.skin && i == 0 && c.skin->ui.rankingRetry.ok() ? &c.skin->ui.rankingRetry : nullptr;
            if (s) {
                const float kk = h / s->h();
                b.quad(s->tex, x + w - s->w() * kk, y, s->w() * kk, h, alpha(st::col::white, 0.8f + 0.2f * hv));
                continue;
            }
            const uint32_t cc = i == 0 ? gfx::rgba(18, 79, 133) : gfx::rgba(120, 70, 10);
            b.roundRectGradient(x, y, w, h, 3 * K(), gfx::Corners::horizontal(alpha(cc, 0.95f), alpha(cc, 0.55f)), -0.1f);
            if (hv > 0.01f) {
                b.setBlend(gfx::Blend::Additive);
                b.roundRect(x, y, w, h, 3 * K(), alpha(st::col::white, 0.15f * hv), -0.1f);
                b.setBlend(gfx::Blend::Alpha);
            }
            const float fs = i == 0 ? 64 * K() : 40 * K();
            if (i == 0) {
                c.fonts->light.drawShadow(b, x + w / 2, c.fonts->light.topForCenter(y + h / 2, fs), fs, alpha(st::col::white, 0.85f + 0.15f * hv),
                                          "Recommencer", gfx::Align::Center);
            } else {
                snprintf(line, sizeof(line), i18n::tr("Corriger le décalage (%+.0f ms)"), r_.meanError);
                c.fonts->light.drawShadow(b, x + w / 2, c.fonts->light.topForCenter(y + h / 2, fs), fs, alpha(st::col::white, 0.85f + 0.15f * hv),
                                          line, gfx::Align::Center);
            }
        }
    }

    bool canSuggest() const {
        return std::fabs(r_.meanError) > 8 && r_.n300 + r_.n100 + r_.n50 > 30 && !offsetApplied_ && !r_.mods.autoplay;
    }

    game::PlayResult r_;
    double start_ = 0;
    long long played_ = 0;
    bool offsetApplied_ = false;
    Tween screenA_{0}, grade_{1}, gradeA_{0};
    ButtonFx backFx_, retryFx_, offsetFx_;
};

}  // namespace

std::unique_ptr<Screen> makeStableResults(Game &g, const game::PlayResult &r) { return std::make_unique<StableResults>(g, r); }

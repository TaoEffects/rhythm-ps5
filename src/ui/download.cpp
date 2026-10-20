// SPDX-License-Identifier: GPL-3.0-or-later
// Téléchargement de maps, façon BeatmapListingOverlay de lazer : l'overlay
// arrive en « vagues » colorées depuis le bas, puis l'en-tête, la recherche et la
// grille de cartes (palette bleue). Croix ou clic : la map se télécharge et
// s'installe toute seule (même si on ferme l'overlay).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "app/game.h"
#include "core/i18n.h"
#include "game/library.h"
#include "ui/lz.h"

namespace {

using namespace lz;

constexpr int kCols = 3;
#define kCardW std::min(430 * U, (kVW - 80 * U - (kCols - 1) * kGap) / kCols)
#define kCardH (100 * U)
#define kGap (10 * U)
#define kMargin ((kVW - kCols * kCardW - (kCols - 1) * kGap) / 2)

const char *const kTabs[] = {"Populaires", "Récentes", "Favorites"};

class Download : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        Context &c = game.ctx;
        const double t = ms(c);
        // Vagues : 4 bandes inclinées qui montent l'une après l'autre (800 ms OutSine).
        for (int i = 0; i < 4; ++i) waves_[i].fromTo(t, 0, 1, 800, Ease::OutSine, i * 50.0);
        content_.fromTo(t, 0, 1, 800, Ease::OutQuint, 200);
        c.play(c.sfx->uiOpen, 0.6f);
        if (!game.mirror.searchBusy()) refresh();
    }

    double leave() override {
        const double t = ms(game.ctx);
        content_.go(t, 0, 300, Ease::InSine);
        for (int i = 0; i < 4; ++i) waves_[i].go(t, 0, 500, Ease::InSine, (3 - i) * 40.0);
        for (auto &kv : covers_) gfx::destroyTexture(kv.second);
        covers_.clear();
        return 650;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        std::string error;
        std::vector<net::RemoteSet> results;
        if (game.mirror.takeResults(results, error)) {
            error_ = error;
            if (loadingMore_) {
                // Page suivante : ajoutée à la suite (sans doublons).
                loadingMore_ = false;
                const size_t before = results_.size();
                for (net::RemoteSet &r : results) {
                    bool dup = false;
                    for (const net::RemoteSet &e : results_) dup = dup || e.id == r.id;
                    if (!dup) results_.push_back(std::move(r));
                }
                endReached_ = results_.size() - before == 0;
                appearFrom_ = static_cast<int>(before);
            } else {
                results_ = std::move(results);
                cursor_ = 0;
                scroll_ = target_ = 0;
                endReached_ = results_.empty();
                appearFrom_ = 0;
            }
            appear_ = ms(c);
            for (const net::RemoteSet &s : results_)
                if (!s.coverUrl.empty() && !covers_.count(s.id)) game.mirror.requestCover(s.id, s.coverUrl);
        }
        int coverId = 0;
        gfx::ImageData img;
        while (game.mirror.takeCover(coverId, img)) {
            covers_[coverId] = gfx::createTexture(img.width, img.height, img.rgba.data(), true);
            coverAt_[coverId] = ms(c);
        }

        if (in.back) {
            if (!query_.empty()) {
                query_.clear();
                refresh();
                return;
            }
            c.play(c.sfx->uiBack);
            game.closeOverlay();
            return;
        }
        for (int i = 0; i < 3; ++i) {
            if (in.clicked(tabX(i), tabsY(), tabW(i), 30 * U) && tab_ != i) {
                tab_ = i;
                page_ = 0;
                c.play(c.sfx->uiHover);
                refresh();
            }
        }
        if (in.r1 || in.l1) {
            tab_ = (tab_ + (in.r1 ? 1 : 2)) % 3;
            page_ = 0;
            c.play(c.sfx->uiHover);
            refresh();
        }
        if (in.extra) refresh();
        // Recherche : clavier virtuel de la console (Carré ou clic sur la barre).
        const float sy = game.toolbar.bottom(c) + 72 * U;
        if (game.textInputSupported() && !imeOpen_ && (in.extra2 || in.clicked(kMargin, sy + 18 * U, kVW - kMargin * 2, 40 * U))) {
            imeOpen_ = game.startTextInput("Rechercher des maps", query_);
        }
        if (imeOpen_) {
            std::string text;
            const int r = game.pollTextInput(text);
            if (r != 0) {
                imeOpen_ = false;
                if (r > 0) {
                    query_ = text;
                    page_ = 0;
                    refresh();
                }
            }
            return;
        }
        typing(in);

        const int n = static_cast<int>(results_.size());
        if (n > 0) {
            const int old = cursor_;
            if (in.right && cursor_ % kCols < kCols - 1 && cursor_ + 1 < n) ++cursor_;
            if (in.left && cursor_ % kCols > 0) --cursor_;
            if (in.down) cursor_ = std::min(n - 1, cursor_ + kCols);
            if (in.up) cursor_ = std::max(0, cursor_ - kCols);
            if (in.wheel != 0) target_ = std::clamp(target_ - in.wheel * 160, 0.0f, maxScroll());
            if (in.r2) cursor_ = n - 1;
            if (in.l2) cursor_ = 0;
            bool activate = in.confirm;
            for (int i = 0; i < n; ++i) {
                float x, y;
                cardPos(i, x, y);
                if (y + kCardH < gridTop() || y > kVH) continue;
                if (in.hover(x, y, kCardW, kCardH) && in.mouseMoved) cursor_ = i;
                if (in.clicked(x, y, kCardW, kCardH)) {
                    cursor_ = i;
                    activate = true;
                }
            }
            for (int i = 0; i < n; ++i) {
                const bool h = i == cursor_;
                hoverFx_[results_[static_cast<size_t>(i)].id].setHover(c, h, 100, 500);
            }
            if (cursor_ != old) {
                c.play(c.sfx->uiHover, 0.6f);
                if (!in.mouseActive) {
                    const float row = static_cast<float>(cursor_ / kCols) * (kCardH + kGap);
                    const float visible = kVH - gridTop() - kCardH - 40 * U;
                    if (row - target_ > visible) target_ = row - visible;
                    if (row < target_) target_ = row;
                }
            }
            if (activate) {
                const net::RemoteSet &s = results_[static_cast<size_t>(cursor_)];
                hoverFx_[s.id].trigger(c, 0.4f, 1000);
                int installed = -1;
                const auto &sets = c.library->sets();
                for (size_t k = 0; k < sets.size(); ++k)
                    if (sets[k].setId == s.id) installed = static_cast<int>(k);
                if (installed >= 0) {
                    c.play(c.sfx->uiSelect);
                    game.showSet(installed);
                    return;
                } else {
                    game.mirror.download(s);
                    c.play(c.sfx->uiSelect);
                }
            }
        }
        scroll_ = anim::damp(scroll_, target_, 50, c.dt * 1000);
        // Défilement infini : la page suivante dès qu'on approche de la fin.
        const int rows = (static_cast<int>(results_.size()) + kCols - 1) / kCols;
        const bool nearEnd = cursor_ / kCols >= rows - 2 || target_ >= maxScroll() - kCardH * 2;
        if (!results_.empty() && nearEnd && !endReached_ && !loadingMore_ && !game.mirror.searchBusy() && error_.empty()) {
            loadingMore_ = true;
            ++page_;
            const net::Sort sorts[] = {net::Sort::Popular, net::Sort::Recent, net::Sort::Favourites};
            game.mirror.search(query_, sorts[tab_], page_);
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        static const Palette pal = palette(hue::blue);
        const float top = game.toolbar.bottom(c);
        // Vagues.
        const uint32_t waveCols[4] = {pal.light4, pal.dark1, pal.dark3, pal.dark4};
        const float rot[4] = {13, -7, 4, -2};
        for (int i = 0; i < 4; ++i) {
            const float k = waves_[i].at(t);
            if (k <= 0.001f) continue;
            const float h = kVH * 1.6f;
            const float y = kVH + 60 - k * (kVH - top + 140 + static_cast<float>(i) * 10);
            b.shape(1, kVW / 2, y + h / 2, kVW * 0.9f, h / 2, 0, 0, 0, gfx::Corners::solid(waveCols[i]), 0, rot[i] * anim::kPi / 180 * (1 - k * 0.6f));
        }
        const float k = content_.at(t);
        if (k <= 0.001f) return;
        b.pushTransform(0, (1 - k) * 300, 1, k);
        b.rect(0, top, kVW, kVH - top, pal.background5);

        // Grille (sous l'en-tête).
        b.pushClip(0, gridTop() - 10 * U, kVW, kVH);
        const auto downloads = game.mirror.downloads();
        std::unordered_map<int, const net::DownloadState *> byId;
        for (const auto &d : downloads) byId[d->setId] = d.get();
        for (int i = 0; i < static_cast<int>(results_.size()); ++i) {
            float x, y;
            cardPos(i, x, y);
            if (y + kCardH < gridTop() - 10 * U || y > kVH) continue;
            // Apparition en cascade.
            const int k = std::max(0, i - appearFrom_);
            const float ap = i < appearFrom_ ? 1.0f
                                             : anim::ease(Ease::OutQuint, static_cast<float>(std::clamp((t - appear_ - k * 25.0) / 400.0, 0.0, 1.0)));
            b.pushTransform(0, (1 - ap) * 30 * U, 1, ap);
            renderCard(i, x, y, byId);
            b.popTransform();
        }
        b.popClip();
        if (game.mirror.searchBusy() && loadingMore_) {
            float lx, ly;
            cardPos(static_cast<int>(results_.size()), lx, ly);
            spinner(c, kVW / 2, ly + kCardH / 2, 32 * U, 1);
        } else if (game.mirror.searchBusy()) {
            spinner(c, kVW / 2, gridTop() + 200 * U, 40 * U, 1);
        } else if (!error_.empty()) {
            text(c, c.fonts->semibold, kVW / 2, gridTop() + 160 * U, 24 * U, col::red1, i18n::tr("Erreur réseau : ") + error_, gfx::Align::Center);
            c.fonts->regular.draw(b, kVW / 2, gridTop() + 200 * U, 18 * U, pal.content2, "Triangle (ou F2) pour réessayer", gfx::Align::Center);
        } else if (results_.empty()) {
            c.fonts->semibold.draw(b, kVW / 2, gridTop() + 160 * U, 24 * U, pal.content2, "Aucun résultat", gfx::Align::Center);
        }

        // En-tête (OverlayHeader) : titre, onglets ; puis zone de recherche.
        const float hy = top;
        b.rect(0, hy, kVW, 70 * U, pal.background4);
        icon(c, fa::download, kMargin + 20 * U, hy + 35 * U, 26 * U, pal.highlight1);
        c.fonts->light.drawMid(b, kMargin + 48 * U, hy + 35 * U, 30 * U, col::white, "télécharger des maps");
        c.fonts->regular.drawMid(b, kVW - kMargin, hy + 35 * U, 16 * U, pal.content2, "depuis le miroir osu.direct", gfx::Align::Right);
        b.rect(0, hy + 70 * U, kVW, 2 * U, pal.highlight1);
        // Zone de recherche avec la pochette du premier résultat en fond.
        const float sy = hy + 72 * U, sh = 120 * U;
        b.rect(0, sy, kVW, sh, pal.background3);
        if (!results_.empty()) {
            auto cv = covers_.find(results_[0].id);
            if (cv != covers_.end()) {
                const float ta = static_cast<float>(cv->second.width) / static_cast<float>(cv->second.height);
                const float aspect = kVW / sh;
                const float cut = (1 - ta / aspect) / 2;
                b.quad(cv->second, 0, sy, kVW, sh, alpha(col::white, 0.25f), 0, std::max(0.0f, cut), 1, std::min(1.0f, 1 - cut));
                b.roundRectGradient(0, sy, kVW, sh, 0, gfx::Corners::vertical(alpha(pal.background3, 0.4f), alpha(pal.background3, 0.95f)));
            }
        }
        const float bx = kMargin, bw = kVW - kMargin * 2;
        b.roundRect(bx, sy + 18 * U, bw, 40 * U, 5 * U, alpha(pal.background6, 0.85f));
        icon(c, fa::magnifying_glass, bx + 22 * U, sy + 38 * U, 16 * U, pal.light2);
        const bool empty = query_.empty();
        const std::string shown = empty ? (game.textInputSupported() ? "rechercher (Carré)..." : "rechercher (clavier)...") : query_ + (game.textInputSupported() ? "" : "_");
        c.fonts->regular.drawMid(b, bx + 44 * U, sy + 38 * U, 20 * U, empty ? pal.light4 : col::white, shown.c_str());
        if (game.textInputSupported()) padGlyph(c, Pad::Square, bx + bw - 22 * U, sy + 38 * U, 22 * U);
        // Onglets de tri.
        c.fonts->semibold.drawMid(b, bx, tabsY() + 15 * U, 15 * U, pal.content2, "Tri");
        // L1 / R1 de part et d'autre des onglets : on voit tout de suite comment changer de tri.
        padGlyph(c, Pad::L1, tabX(0) - 16 * U, tabsY() + 15 * U, 20 * U);
        for (int i = 0; i < 3; ++i) {
            const bool sel = i == tab_;
            const float tx = tabX(i), tw = tabW(i);
            const bool hov = game.input().hover(tx, tabsY(), tw, 30 * U);
            if (sel || hov) b.roundRect(tx, tabsY(), tw, 30 * U, 15 * U, sel ? pal.background1 : alpha(pal.background1, 0.5f));
            c.fonts->semibold.drawMid(b, tx + tw / 2, tabsY() + 15 * U, 16 * U, sel ? col::white : pal.light2, kTabs[i], gfx::Align::Center);
        }
        padGlyph(c, Pad::R1, tabX(2) + tabW(2) + 16 * U, tabsY() + 15 * U, 20 * U);
        char line[64];
        snprintf(line, sizeof(line), i18n::tr("%zu maps"), results_.size());
        c.fonts->regular.drawMid(b, bx + bw, tabsY() + 15 * U, 16 * U, pal.content2, line, gfx::Align::Right);
        // Aide manette.
        const float fy = kVH - 30 * U;
        b.roundRectGradient(0, fy - 30 * U, kVW, 60 * U, 0, gfx::Corners::vertical(alpha(pal.background6, 0), alpha(pal.background6, 0.95f)));
        float hx = kMargin;
        auto hint = [&](Pad p, const char *label) {
            padGlyph(c, p, hx + 10 * U, fy + 8 * U, 20 * U);
            hx += 26 * U;
            hx += c.fonts->regular.drawMid(b, hx, fy + 8 * U, 15 * U, pal.content2, label) + 22 * U;
        };
        hint(Pad::Cross, "télécharger");
        hint(Pad::L1, "");
        hx -= 18 * U;
        hint(Pad::R1, "tri");
        hint(Pad::Square, "rechercher");
        hint(Pad::L2, "");
        hx -= 18 * U;
        hint(Pad::R2, "début / fin");
        hint(Pad::Triangle, "actualiser");
        hint(Pad::Circle, "fermer");
        c.fonts->regular.drawMid(b, kVW - kMargin, fy + 8 * U, 15 * U, pal.content2, "stick droit : pointeur", gfx::Align::Right);
        b.popTransform();
    }

private:
    float gridTop() const { return game.toolbar.bottom(game.ctx) + 72 * lz::U + 120 * lz::U + 20 * lz::U; }
    float tabsY() const { return game.toolbar.bottom(game.ctx) + 72 * lz::U + 72 * lz::U; }
    float tabW(int i) const { return game.ctx.fonts->semibold.measure(16 * U, kTabs[i]) + 30 * U; }
    float tabX(int i) const {
        float x = kMargin + 64 * U;
        for (int k = 0; k < i; ++k) x += tabW(k) + 6 * U;
        return x;
    }
    void cardPos(int i, float &x, float &y) const {
        x = kMargin + static_cast<float>(i % kCols) * (kCardW + kGap);
        y = gridTop() + static_cast<float>(i / kCols) * (kCardH + kGap) - scroll_;
    }
    float maxScroll() const {
        const int rows = (static_cast<int>(results_.size()) + kCols - 1) / kCols;
        return std::max(0.0f, static_cast<float>(rows) * (kCardH + kGap) - (kVH - gridTop() - 40 * U));
    }

    // Carte façon BeatmapCardNormal : vignette carrée, pochette en fond, infos,
    // pastilles de difficulté et zone de téléchargement à droite.
    void renderCard(int i, float x, float y, const std::unordered_map<int, const net::DownloadState *> &byId) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        static const Palette pal = palette(hue::blue);
        const double t = ms(c);
        const net::RemoteSet &s = results_[static_cast<size_t>(i)];
        ButtonFx &fx = hoverFx_[s.id];
        const float hv = fx.hover.at(t);
        const float r = 10 * U;
        auto it = byId.find(s.id);
        const bool installed = c.library->hasSetId(s.id) || (it != byId.end() && it->second->phase.load() == 3);
        const bool busy = !installed && it != byId.end() && it->second->phase.load() < 3;
        // La zone d'action s'élargit au survol (comme les cartes lazer).
        const float actionW = (30 + 30 * hv) * U;
        b.shadow(x, y + 2 * U, kCardW, kCardH, r, 6 * U, alpha(col::black, 0.25f + 0.15f * hv));
        b.roundRect(x, y, kCardW, kCardH, r, installed ? col::green3 : busy ? pal.highlight1 : pal.background1);
        const float cw = kCardW - actionW;
        auto cover = covers_.find(s.id);
        const float ca = cover != covers_.end() ? std::min(1.0f, static_cast<float>((t - coverAt_[s.id]) / 400.0)) : 0;
        b.roundRect(x, y, cw, kCardH, r, pal.background3);
        if (cover != covers_.end()) {
            // Pochette en fond (assombrie), et vignette carrée à gauche.
            const gfx::Texture &tex = cover->second;
            const float ta = static_cast<float>(tex.width) / static_cast<float>(tex.height);
            const float aspect = cw / kCardH;
            float v0 = 0, v1 = 1, u0 = 0, u1 = 1;
            if (ta < aspect) {
                v0 = (1 - ta / aspect) / 2;
                v1 = 1 - v0;
            } else {
                u0 = (1 - aspect / ta) / 2;
                u1 = 1 - u0;
            }
            b.roundRectTex(tex, x, y, cw, kCardH, r, alpha(col::white, ca * (0.35f + 0.15f * hv)), u0, v0, u1, v1);
            const float su0 = (1 - kCardH / (kCardH * ta)) / 2;
            b.roundRectTex(tex, x, y, kCardH, kCardH, r, alpha(col::white, ca), su0, 0, 1 - su0, 1);
        } else {
            b.roundRect(x, y, kCardH, kCardH, r, pal.background4);
            icon(c, fa::image, x + kCardH / 2, y + kCardH / 2, 26 * U, pal.background1);
        }
        b.roundRectGradient(x + kCardH - r, y, cw - kCardH + r, kCardH, 0,
                            gfx::Corners::horizontal(alpha(pal.background3, 0.85f), alpha(pal.background3, 0.5f)));
        const float fl = fx.flash.at(t);
        if (fl > 0) b.roundRect(x, y, kCardW, kCardH, r, alpha(col::white, fl));
        // Textes.
        const float tx = x + kCardH + 10 * U, tw = cw - kCardH - 20 * U;
        const bool orig = c.settings->originalMetadata;
        const std::string &title = orig && !s.titleUnicode.empty() ? s.titleUnicode : s.title;
        const std::string &artist = orig && !s.artistUnicode.empty() ? s.artistUnicode : s.artist;
        text(c, c.fonts->semibold, tx, y + 7 * U, 18 * U, col::white, title, gfx::Align::Left, tw);
        text(c, c.fonts->semibold, tx, y + 29 * U, 14 * U, pal.content2, i18n::tr("de ") + artist, gfx::Align::Left, tw);
        text(c, c.fonts->regular, tx, y + 47 * U, 13 * U, pal.light2, i18n::tr("mappée par ") + s.creator, gfx::Align::Left, tw);
        // Statut + pastilles de difficulté.
        const bool ranked = s.status == "ranked" || s.status == "approved";
        const uint32_t statusCol = ranked ? hex(0xb3ff66) : s.status == "loved" ? hex(0xff66ab) : hex(0xffd966);
        const char *status = ranked ? "CLASSÉE" : s.status == "loved" ? "AIMÉE" : "AUTRE";
        const float py = y + kCardH - 24 * U;
        const float sw = c.fonts->bold.measure(11 * U, status) + 12 * U;
        b.roundRect(tx, py, sw, 15 * U, 7.5f * U, statusCol);
        c.fonts->bold.drawMid(b, tx + sw / 2, py + 7.5f * U, 11 * U, alpha(col::black, 0.75f), status, gfx::Align::Center);
        float dx = tx + sw + 8 * U;
        for (size_t d = 0; d < s.diffs.size() && d < 14; ++d) {
            b.roundRect(dx, py + 1.5f * U, 7 * U, 12 * U, 3.5f * U, starColor(s.diffs[d].stars));
            dx += 8 * U;
        }
        // Zone d'action.
        const float ax = x + cw, acx = ax + actionW / 2;
        if (installed) {
            icon(c, fa::check, acx, y + kCardH / 2, 16 * U, col::white);
        } else if (it != byId.end() && it->second->phase.load() == 4) {
            icon(c, fa::xmark, acx, y + kCardH / 2, 16 * U, col::red1);
        } else if (busy) {
            const net::DownloadState *d = it->second;
            const double total = static_cast<double>(d->total.load()), got = static_cast<double>(d->received.load());
            const float frac = d->phase.load() == 2 ? 1.0f : (total > 0 ? static_cast<float>(got / total) : 0.0f);
            spinner(c, acx, y + kCardH / 2 - 8 * U, 16 * U, 1);
            char line[16];
            snprintf(line, sizeof(line), "%.0f%%", frac * 100);
            c.fonts->bold.draw(b, acx, y + kCardH / 2 + 8 * U, 11 * U, col::black, line, gfx::Align::Center);
            // Barre de progression sous la carte.
            b.roundRect(x + r, y + kCardH - 3 * U, (cw - r) * frac, 3 * U, 1.5f * U, pal.highlight1);
        } else {
            icon(c, fa::download, acx, y + kCardH / 2 - (hv > 0.5f ? 8 * U : 0), 16 * U, col::white);
            if (hv > 0.5f)
                c.fonts->semibold.draw(b, acx, y + kCardH / 2 + 6 * U, 11 * U, alpha(col::white, (hv - 0.5f) * 2),
                                       formatLength(s.lengthSec).c_str(), gfx::Align::Center);
        }
        if (i == cursor_ && !game.input().mouseActive) b.roundRectBorder(x - 2, y - 2, kCardW + 4, kCardH + 4, r + 2, 3, col::white);
    }

    static std::string formatLength(int sec) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
        return buf;
    }

    void refresh() {
        page_ = 0;
        loadingMore_ = false;
        endReached_ = false;
        const net::Sort sorts[] = {net::Sort::Popular, net::Sort::Recent, net::Sort::Favourites};
        game.mirror.search(query_, sorts[tab_], page_);
        error_.clear();
    }

    // Saisie au clavier (PC, ou clavier USB sur la console) : relance la recherche.
    void typing(const UiInput &in) {
        if (!in.raw || !in.raw->keyboard.connected) return;
        const bool *k = in.raw->keyboard.keys;
        bool changed = false;
        const bool shift = k[0xE1] || k[0xE5];
        for (int code = 0x04; code <= 0x2C; ++code) {
            if (!k[code] || prevKeys_[code]) continue;
            char ch = 0;
            if (code <= 0x1D) ch = static_cast<char>((shift ? 'A' : 'a') + (code - 0x04));
            else if (code <= 0x26) ch = static_cast<char>('1' + (code - 0x1E));
            else if (code == 0x27) ch = '0';
            else if (code == 0x2C && !query_.empty()) ch = ' ';
            else if (code == 0x2A && !query_.empty()) {
                query_.pop_back();
                changed = true;
            }
            if (ch && query_.size() < 60) {
                query_ += ch;
                changed = true;
            }
        }
        for (int i = 0; i < 256; ++i) prevKeys_[i] = k[i];
        if (changed) {
            lastType_ = game.ctx.now;
            searchPending_ = true;
        }
        if (searchPending_ && game.ctx.now - lastType_ > 0.6) {
            searchPending_ = false;
            page_ = 0;
            refresh();
        }
    }

    std::vector<net::RemoteSet> results_;
    std::string error_, query_;
    std::unordered_map<int, gfx::Texture> covers_;
    std::unordered_map<int, double> coverAt_;
    std::unordered_map<int, ButtonFx> hoverFx_;
    int tab_ = 0, page_ = 0, cursor_ = 0;
    float scroll_ = 0, target_ = 0;
    double lastType_ = 0, appear_ = 0;
    Tween waves_[4], content_{0};
    bool prevKeys_[256] = {};
    bool searchPending_ = false;
    bool loadingMore_ = false, endReached_ = false, imeOpen_ = false;
    int appearFrom_ = 0;
};

}  // namespace

std::unique_ptr<Screen> Game::makeDownload() { return std::make_unique<Download>(*this); }

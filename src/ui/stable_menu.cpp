// SPDX-License-Identifier: GPL-3.0-or-later
// Menu principal façon osu!stable :
//  - bandes noires en haut et en bas, carte du joueur et infos en haut à gauche,
//    morceau en cours et commandes de musique en haut à droite ;
//  - le logo au centre ; un clic le pousse à gauche (W/5) et les boutons sortent
//    de derrière lui (Jouer, Télécharger, Options, Quitter ; puis Solo / Retour) ;
//  - sans action pendant 10 s, le menu se referme et le logo revient au centre.
// Durées et positions reprises d'opsu! (clone libre de stable, GPL-3).
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "app/game.h"
#include "core/i18n.h"
#include "game/library.h"
#include "ui/lz.h"
#include "ui/stable.h"
#include "ui/stable_ui.h"
#include "version.h"

namespace {

using anim::Ease;
using lz::alpha;
using lz::ButtonFx;
using lz::hex;
using lz::Tween;

inline float K() { return kVH / 1080.0f; }
// Logo de 720 px de diamètre en 1080p (deux tiers de la hauteur), comme stable.
constexpr float kLogoScale = 1.065f;
// Menu ouvert : logo centré à 690 px du bord gauche en 1920 de large.
inline float openX() { return kVW / 2 - 270 * K(); }

struct Entry {
    const char *label;
    const char *glyph;
    uint32_t color;
};
// Tous violets, comme les boutons du menu de stable.
const Entry kTop[] = {{"Jouer", fa::circle_play, hex(0x7f62e6)},
                      {"Télécharger", fa::download, hex(0x7f62e6)},
                      {"Options", fa::sliders, hex(0x7f62e6)},
                      {"Quitter", fa::door_open, hex(0x7f62e6)}};
const Entry kPlay[] = {{"Solo", fa::user, hex(0x7f62e6)}, {"Retour", fa::chevron_left, hex(0x7f62e6)}};
const char *const kTips[] = {
    "F1 ouvre les mods dans la sélection de map.",
    "F2 choisit une map au hasard.",
    "Le pavé tactile peut servir à viser : essaie-le en mode absolu dans les options.",
    "Le mod DT accélère la musique sans changer sa hauteur.",
    "Les maps se téléchargent directement depuis « Télécharger ».",
    "Après une partie, le jeu peut corriger ton décalage audio tout seul.",
    "Échap deux fois pendant une partie : retour immédiat à la sélection.",
    "Le thème se change dans Options > Interface.",
};

class StableMainMenu : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        // Le compteur « tourne depuis » part du premier passage au menu.
        static double appStart = -1;
        if (appStart < 0) appStart = c.now;
        startWall_ = appStart;
        screenA_.fromTo(t, forward ? 0 : 1, 1, forward ? 600 : 300, Ease::OutQuint);
        game.toolbar.show(c, false);
        game.setBackground(0, 0, 500, Ease::OutSine);
        game.backgroundParallax = 1;
        game.introFlash = false;
        lz::Logo &logo = game.logo;
        logo.interactive = true;
        logo.visualiser = true;
        logo.triangles = true;
        logo.opacity.go(t, 1, 400, Ease::OutQuint);
        logo.y.go(t, kVH / 2, 400, Ease::OutQuint);
        logo.scale.go(t, kLogoScale, 400, Ease::OutQuint);
        if (!forward) {
            // Retour de la sélection : menu ouvert directement.
            logo.x.go(t, openX(), 400, Ease::OutQuint);
            open(false);
        } else {
            logo.x.go(t, kVW / 2, 400, Ease::OutQuint);
        }
        stats_ = st::playerStats(game.allScores());
        static int visits = 0;
        tip_ = visits++ + static_cast<int>(time(nullptr) % 7);
        if (!c.music->playing() && !c.library->scanning()) game.playPreview();
    }

    double leave() override {
        const double t = lz::ms(game.ctx);
        screenA_.go(t, 0, 300, Ease::OutQuint);
        return 300;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        if (!previewStarted_ && !c.library->scanning()) {
            previewStarted_ = true;
            if (!c.music->playing()) game.playPreview();
        }
        flashes_.update(c);
        if (exiting_) {
            if (t >= exitAt_) game.requestExit();
            return;
        }
        lz::Logo &logo = game.logo;
        if (musicControls(in)) return;
        if (!open_) {
            if (logo.clicked() || in.confirm || in.right || in.down || (in.anyKey && !in.back)) {
                c.play(c.sfx->uiOpen);
                open(true);
            }
            return;
        }
        // Menu ouvert : fermeture automatique après 10 s sans action.
        if (in.anyKey || in.mouseMoved || in.mouseClick) idleSince_ = t;
        if (t - idleSince_ > 10000) {
            close();
            return;
        }
        const int n = count();
        int hovered = -1;
        for (int i = 0; i < n; ++i) {
            float x, y, w, h;
            rect(i, x, y, w, h);
            if (in.mouseActive && in.hover(x, y, w, h)) hovered = i;
        }
        if (in.mouseActive && in.mouseMoved && hovered != selected_) {
            selected_ = hovered;
            if (hovered >= 0) c.play(c.sfx->uiHover, 0.6f);
        }
        if (in.up || in.down || in.left || in.right) {
            if (selected_ < 0) selected_ = 0;
            else if (in.up || in.left) selected_ = (selected_ + n - 1) % n;
            else selected_ = (selected_ + 1) % n;
            c.play(c.sfx->uiHover, 0.7f);
        }
        for (int i = 0; i < n; ++i) {
            const bool h = i == selected_;
            if (h != fx_[i].hovered) {
                fx_[i].hovered = h;
                fx_[i].hover.go(t, h ? 1 : 0, 350, Ease::OutBack);
            }
        }
        if (in.mouseClick && hovered >= 0) activate(hovered);
        else if (in.confirm && selected_ >= 0) activate(selected_);
        else if (logo.clicked() || in.confirm) activate(0);  // le logo : Jouer (puis Solo)
        if (in.options) {
            c.play(c.sfx->uiSelect);
            game.openOverlay(game.makeOptions());
        }
        if (in.back) {
            c.play(c.sfx->uiBack);
            if (sub_) setSub(false);
            else close();
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = lz::ms(c);
        const float a = screenA_.at(t);
        b.pushAlpha(a);
        flashes_.draw(c);
        // Bandes noires du haut et du bas.
        const float band = kVH / 9;
        b.rect(0, 0, kVW, band, gfx::rgba(0, 0, 0, 150));
        b.rect(0, kVH - band, kVW, band, gfx::rgba(0, 0, 0, 150));
        // Boutons, sous le logo (dessiné ensuite par le jeu).
        for (int i = count() - 1; i >= 0; --i) drawButton(i);
        drawTopLeft();
        drawNowPlaying();
        drawBottom();
        b.popTransform();
        if (exiting_) {
            const float k = static_cast<float>(std::min(1.0, (t - (exitAt_ - 1200)) / 1200.0));
            b.rect(0, 0, kVW, kVH, alpha(st::col::black, std::max(0.0f, k)));
        }
    }

private:
    int count() const { return sub_ ? 2 : 4; }
    const Entry &entry(int i) const { return sub_ ? kPlay[i] : kTop[i]; }

    // Boutons empilés à droite du logo ; leur bord gauche suit le cercle du logo.
    // Boutons de 117 px tous les 146 px (capture de stable en 1080p), partant de
    // derrière le logo ; leur bout droit s'écarte en éventail autour du logo.
    void rect(int i, float &x, float &y, float &w, float &h) const {
        const double t = lz::ms(game.ctx);
        const int n = count();
        h = 117 * K();
        const float step = 146 * K();
        y = kVH / 2 - (static_cast<float>(n - 1) * step + h) / 2 + static_cast<float>(i) * step;
        const float lx = game.logo.cx(game.ctx);
        const float dy = (y + h / 2 - kVH / 2) / (step * 1.5f);
        x = lx + 120 * K() + slide_[i].at(t) + 22 * K() * fx_[i].hover.at(t);
        w = 735 * K() * (1 - 0.06f * dy * dy) + 22 * K() * fx_[i].hover.at(t) * 0;
    }
    float shear(int i) const {
        const int n = count();
        return 0.22f * (static_cast<float>(i) - static_cast<float>(n - 1) / 2) / std::max(1.0f, static_cast<float>(n - 1) / 2);
    }

    void drawButton(int i) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = lz::ms(c);
        const float a = btnA_[i].at(t);
        if (a <= 0.001f) return;
        float x, y, w, h;
        rect(i, x, y, w, h);
        const Entry &e = entry(i);
        const float hv = std::clamp(fx_[i].hover.at(t), 0.0f, 1.2f);
        const float fl = fx_[i].flash.at(t);
        const float sh = shear(i);
        b.pushAlpha(a);
        // Bande violette : plus claire à gauche, bout droit incliné, liseré clair en haut.
        const uint32_t col = lz::lighten(e.color, 0.18f * hv);
        b.shadow(x, y + 5 * K(), w, h, 6 * K(), 16 * K(), gfx::rgba(0, 0, 0, 110), sh);
        b.roundRectGradient(x, y, w, h, 6 * K(), gfx::Corners::horizontal(lz::lighten(hex(0x8261e3), 0.18f * hv), lz::lighten(hex(0x523e93), 0.18f * hv)), sh);
        b.roundRectGradient(x, y, w, h, 6 * K(), gfx::Corners::vertical(alpha(st::col::white, 0.10f), alpha(st::col::black, 0.18f)), sh);
        b.roundRect(x, y, w, 2 * K(), K(), alpha(st::col::white, 0.25f), sh);
        if (fl > 0) {
            b.setBlend(gfx::Blend::Additive);
            b.roundRect(x, y, w, h, 6 * K(), alpha(st::col::white, 0.45f * fl), sh);
            b.setBlend(gfx::Blend::Alpha);
        }
        // Libellé en italique à gauche (juste après le logo), icône à droite.
        const float lx = game.logo.cx(c) + game.logo.radius(c);
        const float tx = std::max(x + 40 * K(), lx + 62 * K()) + 22 * K() * hv;
        const float ts = 58 * K();
        c.fonts->italic.drawShadow(b, tx, c.fonts->italic.topForCenter(y + h / 2, ts), ts, st::col::white, e.label);
        lz::icon(c, e.glyph, x + w - 105 * K() - sh * 0, y + h / 2, 50 * K(), st::col::white);
        b.popTransform();
    }

    void open(bool animate) {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        open_ = true;
        sub_ = false;
        idleSince_ = t;
        selected_ = game.input().mouseActive ? -1 : 0;
        if (animate) game.logo.x.go(t, openX(), 400, Ease::Out);
        game.logo.scale.go(t, kLogoScale, 400, Ease::Out);
        showButtons(animate ? 250 : 0);
        game.setBackground(0.15f, 0, 500, Ease::OutSine);
    }

    void close() {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        open_ = false;
        sub_ = false;
        game.logo.x.go(t, kVW / 2, 2200, Ease::Out);
        game.logo.scale.go(t, kLogoScale, 1200, Ease::Out);
        for (int i = 0; i < 4; ++i) {
            btnA_[i].go(t, 0, 200, Ease::None);
            slide_[i].go(t, -300 * K(), 400, Ease::InQuint);
            fx_[i].hovered = false;
            fx_[i].hover.go(t, 0, 200, Ease::Out);
        }
        game.setBackground(0, 0, 500, Ease::OutSine);
    }

    void showButtons(double delay) {
        const double t = lz::ms(game.ctx);
        for (int i = 0; i < 4; ++i) {
            const bool vis = i < count();
            if (vis) {
                slide_[i].fromTo(t, -360 * K(), 0, 450, Ease::OutQuint, delay + i * 45.0);
                btnA_[i].fromTo(t, 0, 1, 200, Ease::None, delay + i * 45.0);
            } else {
                btnA_[i].go(t, 0, 150, Ease::None);
            }
            fx_[i].hovered = false;
            fx_[i].hover.snap(0);
        }
    }

    void setSub(bool sub) {
        sub_ = sub;
        selected_ = game.input().mouseActive ? -1 : 0;
        showButtons(0);
    }

    void activate(int i) {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        idleSince_ = t;
        fx_[i].flash.fromTo(t, 1, 0, 400, Ease::Out);
        c.play(c.sfx->uiSelect);
        if (sub_) {
            if (i == 1) {
                setSub(false);
                return;
            }
            if (c.library->sets().empty() && !c.library->scanning()) {
                game.toast("Aucune map : télécharges-en d'abord !");
                game.openOverlay(game.makeDownload());
                return;
            }
            game.logo.opacity.go(t, 0, 250, Ease::Out);
            game.goTo(game.makeSongSelect());
            return;
        }
        switch (i) {
        case 0: setSub(true); break;
        case 1: game.openOverlay(game.makeDownload()); break;
        case 2: game.openOverlay(game.makeOptions()); break;
        default:
            exiting_ = true;
            exitAt_ = t + 1600;
            c.music->setVolume(0);
            close();
            break;
        }
    }

    // En haut à gauche : carte du joueur (avatar 105 px) puis trois lignes d'infos.
    void drawTopLeft() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float band = kVH / 9;
        const float cw = 455 * K();
        const UiInput &in = game.input();
        const bool hv = in.mouseActive && in.hover(0, 0, cw, band);
        st::userCard(c, stats_, 4 * K(), 4 * K(), cw, band - 8 * K(), hv ? 1 : 0);
        char line[128];
        const float fs = 26 * K(), x = 477 * K();
        float y = 2 * K();
        snprintf(line, sizeof(line), i18n::tr("Tu as %zu maps disponibles !"), c.library->sets().size());
        c.fonts->regular.drawShadow(b, x, y, fs, st::col::white, line);
        y += 31 * K();
        const long run = static_cast<long>(c.now - startWall_);
        if (run < 60) snprintf(line, sizeof(line), i18n::tr("rhythm tourne depuis %ld secondes."), run);
        else if (run < 3600) snprintf(line, sizeof(line), i18n::tr("rhythm tourne depuis %ld:%02ld."), run / 60, run % 60);
        else snprintf(line, sizeof(line), i18n::tr("rhythm tourne depuis %ld:%02ld:%02ld."), run / 3600, run / 60 % 60, run % 60);
        c.fonts->regular.drawShadow(b, x, y, fs, st::col::white, line);
        y += 31 * K();
        struct tm lt;
        if (st::localTime(static_cast<long long>(time(nullptr)), lt)) {
            snprintf(line, sizeof(line), i18n::tr("Il est %d:%02d."), lt.tm_hour, lt.tm_min);
            c.fonts->regular.drawShadow(b, x, y, fs, st::col::white, line);
        }
    }

    // En haut à droite : « En cours ♪ Artiste - Titre », puis précédent, lecture,
    // pause, stop, suivant (45 px d'écart) et une fine barre de progression.
    static constexpr int kMusicBtns = 5;
    float musicBtnY() const { return 67 * K(); }
    float musicBtnX(int i) const { return kVW - 30 * K() - static_cast<float>(kMusicBtns - 1 - i) * 45 * K(); }

    bool musicControls(const UiInput &in) {
        Context &c = game.ctx;
        if (!in.mouseClick) return false;
        const float r = 22 * K();
        for (int i = 0; i < kMusicBtns; ++i) {
            const float dx = in.mouseX - musicBtnX(i), dy = in.mouseY - musicBtnY();
            if (dx * dx + dy * dy > r * r) continue;
            c.play(c.sfx->uiSelect, 0.7f);
            const int n = static_cast<int>(c.library->sets().size());
            if (i == 1) {
                if (c.music->loaded() && !c.music->playing()) c.music->resume();
                else if (!c.music->loaded()) game.playPreview();
            } else if (i == 2) {
                if (c.music->playing()) c.music->pause();
                else if (c.music->loaded()) c.music->resume();
            } else if (i == 3) {
                if (c.music->loaded()) {
                    c.music->pause();
                    c.music->play(0);
                    c.music->pause();
                }
            } else if (n > 0) {
                const int next = ((game.selectedSet < 0 ? 0 : game.selectedSet) + (i == 4 ? 1 : n - 1)) % n;
                game.select(next, 0, true);
            }
            idleSince_ = lz::ms(c);
            return true;
        }
        // Barre de progression : clic pour se déplacer dans le morceau.
        const float sx = musicBtnX(0) - 14 * K(), sw = musicBtnX(kMusicBtns - 1) + 14 * K() - sx, sy = musicBtnY() + 24 * K();
        if (in.hover(sx, sy - 8 * K(), sw, 18 * K()) && c.music->loaded()) {
            const double len = c.music->lengthSeconds();
            if (len > 0) c.music->play(len * std::clamp((in.mouseX - sx) / sw, 0.0f, 1.0f));
            return true;
        }
        return false;
    }

    void drawNowPlaying() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const game::SetInfo *s = game.currentSet();
        std::string text = game.introTrackPlaying() ? "cYsmix - triangles"
                           : s ? s->shownArtist(c.settings->originalMetadata) + " - " + s->shownTitle(c.settings->originalMetadata)
                               : std::string(i18n::tr("Aucune musique"));
        const float fs = 28 * K();
        text = c.fonts->regular.ellipsize(fs, text, kVW * 0.3f);
        const float right = kVW - 27 * K();
        const float tw = c.fonts->regular.drawShadow(b, right, 2 * K(), fs, st::col::white, text.c_str(), gfx::Align::Right);
        lz::icon(c, fa::music, right - tw - 22 * K(), 18 * K(), 26 * K(), st::col::white);
        c.fonts->regular.drawShadow(b, right - tw - 42 * K(), -1 * K(), 15 * K(), st::col::white, "En", gfx::Align::Right);
        c.fonts->regular.drawShadow(b, right - tw - 42 * K(), 15 * K(), 15 * K(), st::col::white, "cours", gfx::Align::Right);
        const UiInput &in = game.input();
        for (int i = 0; i < kMusicBtns; ++i) {
            const float x = musicBtnX(i), y = musicBtnY();
            const float dx = in.mouseX - x, dy = in.mouseY - y;
            const bool hv = in.mouseActive && dx * dx + dy * dy < 22 * K() * 22 * K();
            const uint32_t col = alpha(st::col::white, hv ? 1 : 0.9f);
            const float sz = (hv ? 26 : 21) * K();
            if (i == 0 || i == 4) {
                // Précédent / suivant : triangle et barre.
                const float dir = i == 0 ? -1.0f : 1.0f;
                b.triangle(x + dir * 2 * K(), y, sz * 0.8f, col, 0, dir * anim::kPi / 2);
                b.rect(x + dir * sz * 0.38f - 1.5f * K(), y - sz * 0.38f, 3 * K(), sz * 0.76f, col);
            } else if (i == 1) {
                b.triangle(x + 2 * K(), y, sz * 0.85f, col, 0, anim::kPi / 2);
            } else if (i == 2) {
                b.rect(x - sz * 0.33f, y - sz * 0.38f, sz * 0.24f, sz * 0.76f, col);
                b.rect(x + sz * 0.09f, y - sz * 0.38f, sz * 0.24f, sz * 0.76f, col);
            } else {
                b.rect(x - sz * 0.36f, y - sz * 0.36f, sz * 0.72f, sz * 0.72f, col);
            }
        }
        const float sx = musicBtnX(0) - 14 * K(), sw = musicBtnX(kMusicBtns - 1) + 14 * K() - sx, sy = musicBtnY() + 24 * K();
        b.rect(sx, sy, sw, 3 * K(), gfx::rgba(255, 255, 255, 40));
        const double len = c.music->loaded() ? c.music->lengthSeconds() : 0;
        if (len > 0) {
            const double pos = std::clamp(c.music->time(*c.mixer) / len, 0.0, 1.0);
            b.rect(sx, sy, sw * static_cast<float>(pos), 3 * K(), gfx::rgba(255, 255, 255, 200));
        }
    }

    // Bas : astuce au centre (change à chaque visite), marque et version à gauche.
    void drawBottom() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float band = kVH / 9;
        const float cy = kVH - band / 2;
        const char *tip = kTips[tip_ % (sizeof(kTips) / sizeof(kTips[0]))];
        c.fonts->regular.drawShadow(b, kVW / 2, c.fonts->regular.topForCenter(cy - 12 * K(), 30 * K()), 30 * K(), st::col::white, tip,
                                    gfx::Align::Center);
        c.fonts->semibold.drawShadow(b, 10 * K(), c.fonts->semibold.topForCenter(kVH - 34 * K(), 44 * K()), 44 * K(), st::col::white, "rhythm");
        const float bw = c.fonts->semibold.measure(44 * K(), "rhythm");
        char line[96];
        snprintf(line, sizeof(line), "%s %s", i18n::tr("version"), RHYTHM_VERSION);
        c.fonts->regular.drawShadow(b, 18 * K() + bw, kVH - 52 * K(), 17 * K(), alpha(st::col::white, 0.85f), line);
        c.fonts->regular.drawShadow(b, 18 * K() + bw, kVH - 33 * K(), 17 * K(), alpha(st::col::white, 0.7f), "projet libre, sans lien avec osu!");
    }

    bool open_ = false, sub_ = false, exiting_ = false, previewStarted_ = false;
    int selected_ = -1, tip_ = 0;
    double idleSince_ = 0, exitAt_ = 0, startWall_ = 0;
    Tween screenA_{0}, slide_[4], btnA_[4];
    ButtonFx fx_[4];
    lz::SideFlashes flashes_;
    st::PlayerStats stats_;
};

}  // namespace

std::unique_ptr<Screen> makeStableMainMenu(Game &g) { return std::make_unique<StableMainMenu>(g); }

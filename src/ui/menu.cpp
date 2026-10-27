// SPDX-License-Identifier: GPL-3.0-or-later
// Menu principal, sur le modèle du ButtonSystem de lazer :
//  - état « initial » : le logo seul, au centre, qui bat au rythme ;
//  - état « haut niveau » : le logo réduit (0,5) glisse à gauche, la bande de
//    boutons inclinés se déplie (500 ms OutExpo, 150 ms après le logo) ;
//  - « jouer » : les boutons se replient, tout glisse à gauche et le logo part
//    rejoindre le pied de page de la sélection de map.
// Dimensions et durées reprises du code de lazer (en unités u).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/game.h"
#include "game/library.h"
#include "ui/lz.h"
#include "ui/stable.h"

namespace {

using namespace lz;

enum class State { Initial, TopLevel, Entering, Exit };

struct MenuButton {
    const char *label;
    const char *glyph;
    uint32_t color;
    bool left;  // à gauche du logo (Options)
    ButtonFx fx;
    Tween width{0}, contentAlpha{0};
    Tween iconY{0}, iconScale{1}, iconScaleY{1}, iconRot{0};
    long lastBeat = -1;
    int beatSide = 1;
};

#define kBtnW (140 * U)
#define kBtnH (100 * U)
#define kWedge (20 * U)
// Centre du logo en état haut niveau : 228 u à gauche du centre de l'écran.
// Le groupe (Options | logo | Jouer, Télécharger, Quitter) va de L-234,7 u à
// L+474,7 u : on place le logo pour que l'ensemble soit centré à l'écran.
#define kLogoX (kVW / 2 - 120 * U)

const char *const kTips[] = {
    "Carré (ou F1) ouvre les mods dans la sélection de map.",
    "Triangle (ou F2) choisit une map au hasard.",
    "Le pavé tactile peut servir à viser : essaie-le en mode absolu dans les options.",
    "Le mod DT accélère la musique sans changer sa hauteur.",
    "Les maps se téléchargent directement depuis l'écran « Télécharger ».",
    "Après une partie, le jeu peut corriger ton décalage audio tout seul.",
    "Clique sur le logo pour lancer la sélection de map.",
    "Le stick droit sert de pointeur dans les menus : Croix pour cliquer.",
};

class MainMenu : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        Context &c = game.ctx;
        const double t = ms(c);
        buttons_[0] = {"Options", fa::gear, hex(0x555555), true, {}, {}, {}, {}, {1}, {1}, {0}, -1, 1};
        buttons_[1] = {"Jouer", fa::play, hex(0x6644cc), false, {}, {}, {}, {}, {1}, {1}, {0}, -1, 1};
        buttons_[2] = {"Télécharger", fa::download, hex(0xa5cc00), false, {}, {}, {}, {}, {1}, {1}, {0}, -1, 1};
        buttons_[3] = {"Quitter", fa::door_open, hex(0xee3399), false, {}, {}, {}, {}, {1}, {1}, {0}, -1, 1};
        game.logo.interactive = true;
        game.backgroundParallax = 1;
        screenAlpha_.fromTo(t, forward ? 0 : 1, 1, forward ? 500 : 300, Ease::OutQuint);
        if (game.introFlash) {
            game.introFlash = false;
            flash_.fromTo(t, 1, 0, 1000, Ease::Out);
        }
        tipStart_ = t;
        tip_ = static_cast<int>(static_cast<unsigned>(t) % (sizeof(kTips) / sizeof(kTips[0])));
        if (!forward) {
            // Retour depuis la sélection de map : directement en haut niveau.
            setState(State::TopLevel, true);
            slide_.fromTo(t, -800 * U, 0, 300, Ease::OutQuint);
            game.logo.opacity.go(t, 1, 300, Ease::OutQuint);
        } else {
            // Arrivée depuis l'assistant (ou tout écran qui a masqué le logo) :
            // le logo revient au centre avant l'état initial.
            Logo &logo = game.logo;
            if (logo.opacity.target() < 0.99f) {
                logo.x.snap(kVW / 2);
                logo.y.snap(kVH / 2);
                logo.scale.fromTo(t, 0.7f, 1, 800, Ease::OutElasticHalf);
                logo.opacity.go(t, 1, 400, Ease::OutQuint);
            }
            setState(State::Initial, true);
        }
        if (!c.music->playing() && !c.library->scanning()) game.playPreview();
    }

    double leave() override {
        const double t = ms(game.ctx);
        screenAlpha_.go(t, 0, 400, Ease::OutQuint);
        return 400;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const double t = ms(c);
        if (!previewStarted_ && !c.library->scanning()) {
            previewStarted_ = true;
            if (!c.music->playing()) game.playPreview();
        }
        updateTicker();
        flashes_.update(c);
        if (state_ == State::Entering || state_ == State::Exit) {
            if (state_ == State::Exit && t >= exitAt_) game.requestExit();
            return;
        }
        Logo &logo = game.logo;
        if (state_ == State::Initial) {
            if (logo.clicked() || in.confirm || in.right || in.down || (in.anyKey && !in.back)) {
                c.play(c.sfx->uiOpen);
                setState(State::TopLevel, false);
            }
            return;
        }
        // Haut niveau : survol souris ou sélection à la manette / au clavier.
        int hovered = -1;
        for (int i = 0; i < 4; ++i) {
            const Rect r = rect(i);
            if (in.mouseActive && in.hover(r.x - kShear * kBtnH * 0.5f, r.y, r.w, r.h)) hovered = i;
        }
        if (in.mouseActive && in.mouseMoved) selected_ = hovered;
        if (in.left || in.right || in.up || in.down) {
            // Ordre visuel : Options | logo | Jouer, Télécharger, Quitter.
            static const int order[4] = {0, 1, 2, 3};
            int pos = selected_ < 0 ? 1 : selected_;
            if (in.left || in.up) pos = std::max(0, pos - 1);
            if (in.right || in.down) pos = std::min(3, pos + 1);
            if (selected_ < 0) pos = 1;
            selected_ = order[pos];
            c.play(c.sfx->uiHover, 0.8f);
        }
        for (int i = 0; i < 4; ++i) {
            MenuButton &b = buttons_[i];
            const bool h = selected_ == i;
            if (h != b.fx.hovered) {
                b.fx.hovered = h;
                b.width.go(t, h ? 1.5f : 1.0f, 500, Ease::OutElastic);
                if (!h) {
                    // Survol perdu : tout revient en place (lazer : 500 ms / 200 ms, Out).
                    b.iconY.go(t, 0, 500, Ease::Out);
                    b.iconRot.go(t, 0, 500, Ease::Out);
                    b.iconScale.go(t, 1, 200, Ease::Out);
                    b.iconScaleY.go(t, 1, 200, Ease::Out);
                } else {
                    // Survol : l'icône s'incline jusqu'au prochain temps et s'étire (1,2 x 1,08).
                    const double untilBeat = c.trackRunning ? std::max(50.0, (1 - c.beatProgress) * c.beatLength) : 300;
                    b.iconRot.go(t, b.beatSide * 8 * anim::kPi / 180, untilBeat, Ease::InOutSine);
                    b.iconScale.go(t, 1.2f, 200, Ease::Out);
                    b.iconScaleY.go(t, 1.08f, 200, Ease::Out);
                    b.lastBeat = c.beatIndexEarly;
                    c.play(c.sfx->uiHover, 0.6f);
                }
            }
            // Au rythme, l'icône survolée sautille.
            // Un mouvement par temps ; on ne saute des temps que sur les morceaux très
            // rapides (moins de 300 ms par temps), sinon l'icône s'agite trop.
            int every = 1;
            while (c.beatLength * every < 300 && every < 8) every *= 2;
            if (h && c.trackRunning && c.beatIndexEarly != b.lastBeat && c.beatIndexEarly % every == 0) {
                b.lastBeat = c.beatIndexEarly;
                const double d = c.beatLength * every / 2;
                b.beatSide = -b.beatSide;
                // À chaque temps : bascule de ±8° (2d, InOutSine), saut de 10 u puis retour,
                // étirement 1,2 au sommet puis 1,2 x 1,08 (comme MainMenuButton de lazer).
                b.iconRot.go(t, b.beatSide * 8 * anim::kPi / 180, 2 * d, Ease::InOutSine);
                b.iconY.fromTo(t, b.iconY.at(t), -10 * U, d, Ease::Out);
                b.iconY.then(0, d, Ease::In);
                b.iconScale.fromTo(t, b.iconScale.at(t), 1.2f, d, Ease::Out);
                b.iconScale.then(1.2f, d, Ease::In);
                b.iconScaleY.fromTo(t, b.iconScaleY.at(t), 1.2f, d, Ease::Out);
                b.iconScaleY.then(1.08f, d, Ease::In);
            }
            b.fx.setPressed(c, h && in.raw && (in.raw->mouse.buttons & 1u));
        }
        if (in.mouseClick && hovered >= 0) activate(hovered);
        else if (in.confirm && selected_ >= 0) activate(selected_);
        else if (in.confirm || logo.clicked()) activate(1);  // le logo lance « Jouer », comme dans lazer
        if (in.options) activate(0);
        if (in.back) {
            c.play(c.sfx->uiBack);
            setState(State::Initial, false);
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const float a = screenAlpha_.at(t);
        b.pushAlpha(a);
        flashes_.draw(c);
        // Bande des boutons (fond #323232 sur toute la largeur, 100 u de haut).
        const float strip = stripAlpha_.at(t);
        const float sy = stripScaleY_.at(t);
        // Parallaxe des boutons (0,01) ; la bande suit, et déborde des bords pour ne
        // jamais laisser voir ses extrémités.
        const UiInput &in = game.input();
        const bool para = in.mouseActive && c.settings->menuParallax;
        const float px = para ? -(in.mouseX - kVW / 2) * 0.01f : 0;
        const float py = para ? -(in.mouseY - kVH / 2) * 0.01f : 0;
        if (strip > 0.001f)
            b.rect(-100 * U + px, kVH / 2 - kBtnH / 2 * sy + py, kVW + 200 * U, kBtnH * sy, alpha(hex(0x323232), strip));
        b.pushTransform(slide_.at(t) + px, py);
        // Les boutons de gauche sont dessinés au-dessus de ceux de droite (ordre inverse).
        for (int i = 3; i >= 0; --i) drawButton(i);
        b.popTransform();
        // Logo suit la « façade » de la bande.
        trackLogo();
        drawTicker();
        drawTip();
        b.popTransform();
        const float fl = flash_.at(t);
        if (fl > 0.001f) {
            b.setBlend(gfx::Blend::Additive);
            b.rect(0, 0, kVW, kVH, alpha(col::white, fl));
            b.setBlend(gfx::Blend::Alpha);
        }
        if (state_ == State::Exit) {
            const float k = static_cast<float>(std::min(1.0, (t - (exitAt_ - 1200)) / 1200.0));
            b.rect(0, 0, kVW, kVH, alpha(col::black, std::max(0.0f, k)));
        }
    }

private:
    struct Rect {
        float x, y, w, h;
    };

    float facadeW() const { return 189.4f * U * game.logo.hoverScale.at(ms(game.ctx)); }

    Rect rect(int i) const {
        const double t = ms(game.ctx);
        const float fw = facadeW();
        const float y = kVH / 2 - kBtnH / 2;
        if (i == 0) {
            const MenuButton &b = buttons_[0];
            const float w = (kBtnW + kWedge) * b.width.at(t);
            const float right = kLogoX - fw / 2 + kWedge;
            return {right - w, y, w, kBtnH};
        }
        float x = kLogoX + fw / 2 - kWedge;
        for (int k = 1; k < i; ++k) x += (k == 1 ? kBtnW + kWedge : kBtnW) * buttons_[k].width.at(t) - kWedge;
        const float w = (i == 1 ? kBtnW + kWedge : kBtnW) * buttons_[i].width.at(t);
        return {x, y, w, kBtnH};
    }

    void drawButton(int i) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        MenuButton &m = buttons_[i];
        const Rect r = rect(i);
        if (r.w < 1) return;
        // Le contenu n'apparaît qu'au-delà de la moitié de la largeur.
        const float base = (i <= 1 ? kBtnW + kWedge : kBtnW);
        const float rel = r.w / base;
        const float ca = anim::clamp01((rel - 0.5f) / 0.3f) * m.contentAlpha.at(t);
        if (m.contentAlpha.at(t) <= 0.001f) return;
        b.pushAlpha(m.contentAlpha.at(t));
        b.shadow(r.x, r.y, r.w, r.h, 5 * U, 8 * U, gfx::rgba(0, 0, 0, 51), kShear);
        b.roundRect(r.x, r.y, r.w, r.h, 0, m.color, kShear);
        const float pr = m.fx.press.at(t) * 0.1f, fl = m.fx.flash.at(t);
        if (pr > 0 || fl > 0) {
            b.setBlend(gfx::Blend::Additive);
            b.roundRect(r.x, r.y, r.w, r.h, 0, alpha(col::white, std::max(pr, fl)), kShear);
            b.setBlend(gfx::Blend::Alpha);
        }
        // Contenu centré sur la partie visible du bouton (le logo en cache un bout),
        // icône et libellé alignés sur le même axe vertical.
        float left = r.x, right = r.x + r.w;
        const float logoR = game.logo.radius(c), logoX = game.logo.cx(c);
        if (i == 0) right = std::min(right, logoX - logoR);
        if (i == 1) left = std::max(left, logoX + logoR);
        const float cx = (left + right) / 2;
        const float iy = r.y + 40 * U + m.iconY.at(t);
        icon(c, m.glyph, cx, iy, 32 * U, alpha(col::white, ca), m.iconRot.at(t), m.iconScale.at(t), m.iconScaleY.at(t));
        const float ts = 16 * U;
        const float lc = r.y + r.h - 7 * U - ts * 0.4f;  // centre des capitales du libellé
        c.fonts->semibold.drawShadow(b, cx, c.fonts->semibold.topForCenter(lc, ts), ts, alpha(col::white, ca), m.label,
                                     gfx::Align::Center);
        b.popTransform();
    }

    void trackLogo() {
        // Rien à faire : les déplacements du logo sont lancés par setState.
    }

    void activate(int i) {
        Context &c = game.ctx;
        const double t = ms(c);
        buttons_[i].fx.trigger(c, 0.9f, 800);
        c.play(c.sfx->uiSelect);
        switch (i) {
        case 0: game.openOverlay(game.makeOptions()); break;
        case 1:
            if (c.library->sets().empty() && !c.library->scanning()) {
                game.toast("Aucune map : télécharges-en d'abord !");
                game.openOverlay(game.makeDownload());
                return;
            }
            setState(State::Entering, false);
            // Tout glisse à gauche, le logo suit puis disparaît en rétrécissant.
            slide_.go(t, -800 * U, 400, Ease::InSine);
            game.logo.x.go(t, kLogoX - 800 * U, 400, Ease::InSine);
            game.logo.opacity.go(t, 0, 300, Ease::InSine);
            game.logo.scale.go(t, 0.2f, 300, Ease::InSine);
            game.goTo(game.makeSongSelect());
            break;
        case 2: game.openOverlay(game.makeDownload()); break;
        case 3:
            setState(State::Exit, false);
            exitAt_ = t + 1600;
            c.music->setVolume(0);
            break;
        }
    }

    void setState(State s, bool instant) {
        Context &c = game.ctx;
        const double t = ms(c);
        const State prev = state_;
        state_ = s;
        Logo &logo = game.logo;
        const bool top = s == State::TopLevel;
        const double delay = prev == State::Initial && !instant ? 150 : 0;
        for (int i = 0; i < 4; ++i) {
            MenuButton &b = buttons_[i];
            if (top) {
                if (instant) {
                    b.width.snap(1);
                    b.contentAlpha.snap(1);
                } else {
                    b.width.go(t, 1, 500, Ease::OutExpo, delay);
                    b.contentAlpha.go(t, 1, 83, Ease::None, delay);
                }
            } else if (s == State::Entering) {
                b.width.go(t, 0, 400, Ease::InSine);
                b.contentAlpha.go(t, 0, 800, Ease::None);
            } else {
                b.width.go(t, 0, 500, Ease::OutExpo);
                b.contentAlpha.go(t, 0, 500, Ease::None);
            }
            b.fx.hovered = false;
        }
        if (top) selected_ = game.input().mouseActive ? -1 : 1;
        // Bande de fond.
        if (top) {
            stripAlpha_.go(t, 1, instant ? 0 : 300, Ease::None);
            stripScaleY_.go(t, 1, instant ? 0 : 400, Ease::OutQuint);
        } else {
            stripAlpha_.go(t, 0, 300, Ease::None);
            stripScaleY_.go(t, 0, 300, Ease::InSine);
        }
        // Logo et barre d'outils.
        if (top) {
            if (instant) {
                logo.x.snap(kLogoX);
                logo.y.snap(kVH / 2);
                logo.scale.go(t, 0.5f, 300, Ease::OutQuint);
            } else {
                logo.moveTo(c, kLogoX, kVH / 2, 0.5f, 200, Ease::In);
                if (logo.scale.at(t) > 0.6f) logo.impact(c);
            }
            game.toolbar.show(c, true);
            game.setBackground(0.2f, 0, 500, Ease::OutSine);
        } else if (s == State::Initial || s == State::Exit) {
            const double d = prev == State::TopLevel ? 150 : 0;
            logo.moveTo(c, kVW / 2, kVH / 2, 1, 800, Ease::OutExpo, d);
            game.toolbar.show(c, false);
            game.setBackground(0, 0, 500, Ease::OutSine);
        }
        logo.visualiser = true;
        logo.triangles = true;
    }

    void updateTicker() {
        Context &c = game.ctx;
        const game::SetInfo *s = game.currentSet();
        // Pendant « triangles », aucune map n'est jouée : rien à afficher.
        const std::string key = game.introTrackPlaying() ? "#intro" : s ? s->folder : "";
        if (key != tickerKey_ && c.music->playing()) {
            tickerKey_ = key;
            tickerStart_ = ms(c);
        }
    }

    void drawTicker() {
        Context &c = game.ctx;
        const game::SetInfo *s = game.currentSet();
        if (!s || tickerKey_.empty() || tickerKey_ == "#intro") return;
        const double dt = ms(c) - tickerStart_;
        float a = 0;
        if (dt < 400) a = static_cast<float>(dt / 400);
        else if (dt < 4000) a = 1;
        else if (dt < 4800) a = 1 - static_cast<float>((dt - 4000) / 800);
        if (a <= 0 || state_ == State::Exit) return;
        const float right = kVW - 15 * U, top = game.toolbar.bottom(c) + 5 * U;
        const std::string title = s->shownTitle(c.settings->originalMetadata);
        const std::string artist = s->shownArtist(c.settings->originalMetadata);
        c.batch->shadow(right - 420 * U, top, 420 * U, 50 * U, 30 * U, 40 * U, gfx::rgba(10, 10, 10, static_cast<uint8_t>(60 * a)));
        text(c, c.fonts->light, right, top, 24 * U, alpha(col::white, a), title, gfx::Align::Right, 600 * U, true);
        text(c, c.fonts->regular, right, top + 27 * U, 16 * U, alpha(col::white, a), artist, gfx::Align::Right, 600 * U, true);
    }

    void drawTip() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double dt = ms(c) - tipStart_;
        const char *tip = kTips[tip_];
        const double fadeOutAt = 600 + 800 + 1600 + 80.0 * static_cast<double>(strlen(tip));
        float a = 0, sc = 0.9f;
        if (dt >= 600) {
            const float k = static_cast<float>(std::min(1.0, (dt - 600) / 800.0));
            a = anim::ease(Ease::OutQuint, k);
            sc = 0.9f + 0.1f * anim::ease(Ease::OutElasticHalf, k);
        }
        if (dt > fadeOutAt) a *= 1 - anim::ease(Ease::OutQuint, static_cast<float>(std::min(1.0, (dt - fadeOutAt) / 2000.0)));
        if (a <= 0.001f || state_ == State::Exit) return;
        const float w = 600 * U + 20 * U, ts = 16 * U;
        const float h = 10 * U * 2 + ts * 2 + 8 * U;
        const float x = kVW / 2 - w / 2, y = kVH - 20 * U - h;
        b.pushScaleAround(kVW / 2, y + h / 2, sc, a);
        b.roundRect(x, y, w, h, 10 * U, alpha(hex(0x171a1c), 0.75f));
        // Titre et astuce centrés dans la boîte.
        const float hw = c.fonts->semibold.measure(ts, "Astuce") + 24 * U;
        icon(c, fa::lightbulb, kVW / 2 - hw / 2 + 8 * U, y + 10 * U + ts * 0.5f, 16 * U, col::pink0);
        c.fonts->semibold.drawMid(b, kVW / 2 - hw / 2 + 24 * U, y + 10 * U + ts * 0.5f, ts, col::pink0, "Astuce");
        text(c, c.fonts->regular, kVW / 2, c.fonts->regular.topForCenter(y + 10 * U + ts * 1.5f + 8 * U, ts), ts, col::white, tip,
             gfx::Align::Center, w - 20 * U);
        b.popTransform();
    }

    MenuButton buttons_[4];
    State state_ = State::Initial;
    int selected_ = -1;
    Tween screenAlpha_{0}, stripAlpha_{0}, stripScaleY_{0}, slide_{0}, flash_{0};
    SideFlashes flashes_;
    bool previewStarted_ = false;
    std::string tickerKey_;
    double tickerStart_ = -1e9, tipStart_ = 0, exitAt_ = 0;
    int tip_ = 0;
};

}  // namespace

std::unique_ptr<Screen> Game::makeMainMenu() {
    if (settings_.uiTheme == 1) return makeStableMainMenu(*this);
    return std::make_unique<MainMenu>(*this);
}

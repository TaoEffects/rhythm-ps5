// SPDX-License-Identifier: GPL-3.0-or-later
// Premier lancement : assistant en étapes, sur le modèle du FirstRunSetupOverlay
// de lazer. Bienvenue, maps (tutoriel et maps de base, au choix : rien n'est
// téléchargé sans le demander), façon de jouer, interface, fin.
// Les maps viennent du miroir — jamais des serveurs officiels.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app/game.h"
#include "core/i18n.h"
#include "game/library.h"
#include "ui/lz.h"

namespace {

using namespace lz;

// Identifiants de sets (BundledBeatmapDownloader de lazer) : tutoriel, puis la
// liste « toujours incluse ».
constexpr int kTutorial = 1011011;
const int kBundled[] = {123593,  140662,  151878,  163112,  190390,  241526,  299224,  319473,  342751,  385056,
                        456054,  1388906, 1841885, 1971987, 2412244, 2412260, 2412232, 2412292, 2412328, 2412331};

enum Step { Welcome, Maps, Controls, Interface, Done, StepCount };
const char *const kStepNames[StepCount] = {"Bienvenue", "Maps", "Façon de jouer", "Interface", "C'est parti"};

const Palette &pal() {
    static const Palette p = palette(hue::purple);
    return p;
}

class FirstRun : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        Context &c = game.ctx;
        const double t = ms(c);
        game.toolbar.show(c, false);
        game.logo.opacity.go(t, 0, 300, Ease::OutQuint);
        game.setBackground(0.6f, 1, 500);
        panelA_.fromTo(t, 0, 1, 500, Ease::OutQuint);
        panelY_.fromTo(t, 60 * U, 0, 600, Ease::OutQuint);
        goStep(Welcome);
        c.play(c.sfx->uiOpen, 0.7f);
    }

    double leave() override {
        const double t = ms(game.ctx);
        panelA_.go(t, 0, 300, Ease::OutQuint);
        panelY_.go(t, 60 * U, 300, Ease::OutQuint);
        return 300;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const int n = focusCount(), cc = contentCount();
        const int old = focus_;
        const bool footer = focus_ >= cc;
        // Navigation spatiale : le contenu en haut, « Précédent / Suivant » en bas.
        if (in.down && !footer) focus_ = (horizontal() || focus_ == cc - 1) ? n - 1 : focus_ + 1;
        if (in.up) {
            if (footer && cc > 0) focus_ = horizontal() ? std::clamp(lastContent_, 0, cc - 1) : cc - 1;
            else if (!footer) focus_ = std::max(0, focus_ - 1);
        }
        bool consumed = false;
        if (footer && (in.left || in.right)) {
            focus_ = in.left && step_ > 0 ? n - 2 : n - 1;
            consumed = true;
        }
        if (focus_ < cc) lastContent_ = focus_;
        if (focus_ != old) c.play(c.sfx->uiHover, 0.6f);
        if (in.r1 || in.options) next();
        if (in.l1 || in.back) prev();
        // Souris : survol et clic sur les éléments mémorisés au dernier rendu.
        for (size_t i = 0; i < hits_.size(); ++i) {
            const Hit &h = hits_[i];
            if (in.mouseActive && in.hover(h.x, h.y, h.w, h.h)) {
                if (in.mouseMoved && focus_ != h.id) focus_ = h.id;
                if (in.mouseClick) {
                    focus_ = h.id;
                    activate(0);
                    return;
                }
            }
        }
        if (!consumed && in.left) activate(-1);
        if (!consumed && in.right) activate(+1);
        if (in.confirm) activate(0);
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        hits_.clear();
        b.pushTransform(0, panelY_.at(t), 1, panelA_.at(t));
        const float W = std::min(1100 * U, kVW - 80), H = std::min(620 * U, kVH - 80);
        const float px = kVW / 2 - W / 2, py = kVH / 2 - H / 2;
        b.shadow(px, py + 6 * U, W, H, 12 * U, 24 * U, alpha(col::black, 0.5f));
        b.roundRect(px, py, W, H, 12 * U, pal().background4);
        // Étapes à gauche.
        const float sw = 260 * U;
        b.roundRect(px, py, sw, H, 12 * U, pal().background5);
        c.fonts->bold.drawMid(b, px + 26 * U, py + 40 * U, 22 * U, col::white, "configuration");
        c.fonts->bold.drawMid(b, px + 26 * U, py + 66 * U, 22 * U, col::white, "initiale");
        for (int i = 0; i < StepCount; ++i) {
            const float sy = py + 110 * U + static_cast<float>(i) * 48 * U;
            const bool cur = i == step_, past = i < step_;
            if (cur) b.roundRect(px + 12 * U, sy - 20 * U, sw - 24 * U, 40 * U, 8 * U, pal().background3);
            b.circle(px + 36 * U, sy, 12 * U, cur ? col::pink1 : past ? pal().light4 : pal().background2);
            if (past) icon(c, fa::check, px + 36 * U, sy, 11 * U, col::white);
            else c.fonts->bold.drawMid(b, px + 36 * U, sy, 12 * U, col::white, std::to_string(i + 1).c_str(), gfx::Align::Center);
            c.fonts->semibold.drawMid(b, px + 58 * U, sy, 17 * U, cur ? col::white : pal().light2, kStepNames[i]);
        }
        // Contenu.
        const float cx = px + sw + 40 * U, cw = W - sw - 80 * U;
        const float ca = contentA_.at(t);
        b.pushTransform(contentX_.at(t), 0, 1, ca);
        float y = py + 36 * U;
        c.fonts->light.drawMid(b, cx, y + 14 * U, 34 * U, col::white, title());
        y += 52 * U;
        renderStep(cx, y, cw, py + H - 90 * U);
        b.popTransform();
        // Pied : précédent / suivant.
        const float by = py + H - 70 * U;
        const int n = focusCount();
        if (step_ > 0) footerButton(cx, by, 200 * U, "Précédent", pal().background2, n - 2);
        footerButton(cx + cw - 220 * U, by, 220 * U, step_ == Done ? "Terminer" : "Suivant", col::purple1, n - 1);
        c.fonts->regular.drawMid(b, cx + cw / 2 + (step_ > 0 ? 40 * U : -60 * U), by + 25 * U, 13 * U, pal().light4,
                                 "Croix : valider  ·  L1 / R1 : étapes", gfx::Align::Center);
        b.popTransform();
    }

private:
    struct Hit {
        float x, y, w, h;
        int id;
    };

    const char *title() const {
        switch (step_) {
        case Welcome: return "bienvenue !";
        case Maps: return "des maps pour commencer";
        case Controls: return "comment tu joues ?";
        case Interface: return "l'interface";
        default: return "tout est prêt";
        }
    }

    // Éléments focalisables de l'étape, puis « Précédent » (si présent) et « Suivant ».
    int contentCount() const {
        switch (step_) {
        case Maps: return 2;
        case Controls: return 3;
        case Interface: return 4;
        default: return 0;
        }
    }
    int focusCount() const { return contentCount() + 2; }
    // Étapes dont les choix sont côte à côte (gauche / droite pour passer de l'un à l'autre).
    bool horizontal() const { return step_ == Controls; }

    void goStep(int s) {
        Context &c = game.ctx;
        const double t = ms(c);
        const int dir = s > step_ ? 1 : -1;
        step_ = std::clamp(s, 0, static_cast<int>(StepCount) - 1);
        contentX_.fromTo(t, 40 * U * static_cast<float>(dir), 0, 400, Ease::OutQuint);
        contentA_.fromTo(t, 0, 1, 300, Ease::OutQuint);
        focus_ = contentCount() > 0 ? 0 : focusCount() - 1;
        lastContent_ = 0;
        if (step_ == Controls) {
            const int mode = c.settings->inputMode;
            focus_ = lastContent_ = mode == 1 ? 0 : mode == 2 ? 1 : 2;
        }
    }

    void next() {
        Context &c = game.ctx;
        if (step_ == Done) {
            c.settings->firstRunDone = true;
            c.saveSettings();
            c.play(c.sfx->uiSelect);
            game.goTo(game.makeMainMenu());
            return;
        }
        c.play(c.sfx->uiSelect, 0.8f);
        goStep(step_ + 1);
    }
    void prev() {
        if (step_ == 0) return;
        game.ctx.play(game.ctx.sfx->uiBack, 0.8f);
        goStep(step_ - 1);
    }

    void activate(int dir) {
        Context &c = game.ctx;
        Settings &s = *c.settings;
        const int n = focusCount();
        if (focus_ == n - 1) {
            if (dir == 0) next();
            return;
        }
        if (focus_ == n - 2) {
            if (dir == 0) prev();
            return;
        }
        switch (step_) {
        case Maps:
            if (dir != 0) return;
            if (focus_ == 0) startDownloads(true);
            else startDownloads(false);
            focus_ = focus_ == 0 ? 1 : focusCount() - 1;  // on enchaîne sur le choix suivant
            break;
        case Controls:
            if (dir != 0) {
                focus_ = std::clamp(focus_ + dir, 0, 2);
                c.play(c.sfx->uiHover, 0.6f);
                return;
            }
#if defined(__PROSPERO__)
            // Clavier et souris USB pas encore pris en charge sur PS5.
            if (focus_ == 1) {
                game.toast("Clavier et souris : bientôt sur PS5");
                return;
            }
#endif
            // 0 manette, 1 clavier/souris, 2 les deux (ordre d'affichage).
            s.inputMode = focus_ == 0 ? 1 : focus_ == 1 ? 2 : 0;
            s.mouseButtonsInGame = s.inputMode != 1;
            if (s.inputMode == 1 && !s.touchpadAbsolute) s.touchpadAbsolute = true;
            c.play(c.sfx->uiSelect, 0.8f);
            lastContent_ = focus_;
            focus_ = focusCount() - 1;  // choix fait : « Suivant » prend le focus
            break;
        case Interface: {
            const int d = dir == 0 ? 1 : dir;
            if (focus_ == 0) {
                s.uiScale = std::clamp(std::round((s.uiScale + 0.05f * static_cast<float>(d)) * 20) / 20, 0.8f, 1.3f);
                setUiScale(s.uiScale);
            } else if (focus_ == 1) {
                game.toast("Les thèmes « stable » et « classique » arrivent bientôt");
            } else if (focus_ == 2) {
                s.skin = s.skin == 1 ? 0 : 1;
                game.applySkin();
            } else {
                s.refreshHz = s.refreshHz == 120 ? 60 : 120;
            }
            c.play(c.sfx->uiHover, 0.8f);
            break;
        }
        default: break;
        }
        c.saveSettings();
    }

    void startDownloads(bool tutorial) {
        Context &c = game.ctx;
        auto queue = [&](int id, std::vector<std::shared_ptr<net::DownloadState>> &into) {
            if (c.library->hasSetId(id)) return;
            net::RemoteSet set;
            set.id = id;
            into.push_back(game.mirror.download(set, true));
        };
        if (tutorial) {
            if (!tutorial_.empty()) return;
            queue(kTutorial, tutorial_);
        } else {
            if (!bundled_.empty()) return;
            for (int id : kBundled) queue(id, bundled_);
        }
        c.play(c.sfx->uiSelect);
    }

    // Bouton incliné du pied de l'assistant.
    void footerButton(float x, float y, float w, const char *label, uint32_t color, int id) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const bool f = focus_ == id;
        const float h = 50 * U;
        b.roundRect(x, y, w, h, 8 * U, f ? lighten(color, 0.25f) : color, kShear);
        if (f && !game.input().mouseActive) b.roundRectBorder(x - 2, y - 2, w + 4, h + 4, 9 * U, 2.5f, col::white, kShear);
        c.fonts->bold.drawMid(b, x + w / 2, y + h / 2, 18 * U, col::white, label, gfx::Align::Center);
        hits_.push_back({x, y, w, h, id});
    }

    // Carte sélectionnable (choix, téléchargement).
    void card(float x, float y, float w, float h, int id, bool selected) {
        gfx::SpriteBatch &b = *game.ctx.batch;
        const bool f = focus_ == id;
        b.roundRect(x, y, w, h, 10 * U, selected ? darken(col::pink1, 1.2f) : f ? pal().background2 : pal().background3);
        if (selected) b.roundRectBorder(x, y, w, h, 10 * U, 2.5f * U, col::pink1);
        if (f && !game.input().mouseActive) b.roundRectBorder(x - 3, y - 3, w + 6, h + 6, 12 * U, 2.5f, col::white);
        hits_.push_back({x, y, w, h, id});
    }

    void renderStep(float x, float y, float w, float bottom) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const Settings &s = *c.settings;
        switch (step_) {
        case Welcome:
            y += paragraph(c, c.fonts->regular, x, y, w, 19 * U, col::white,
                           "Ce jeu lit les beatmaps d'osu! et se joue à la manette, au clavier, à la souris ou au pavé "
                           "tactile de la DualSense.");
            y += 10 * U;
            y += paragraph(c, c.fonts->regular, x, y, w, 19 * U, pal().content2,
                           "Quelques réglages rapides pour bien démarrer. Tout se change plus tard dans les options.");
            y += 10 * U;
            paragraph(c, c.fonts->regular, x, y, w, 16 * U, pal().light4,
                      "Projet gratuit et non officiel : tes scores restent sur la console, rien n'est envoyé aux serveurs "
                      "d'osu!.");
            break;
        case Maps: {
            y += paragraph(c, c.fonts->regular, x, y, w, 17 * U, pal().content2,
                           "Pour ne pas démarrer avec une liste vide, tu peux récupérer le tutoriel et une sélection de maps, "
                           "comme dans le jeu d'origine. Elles viennent du miroir osu.direct.");
            y += 14 * U;
            mapCard(x, y, w, 0, "Tutoriel", "Apprends les bases (environ 8 Mo).", tutorial_, 1);
            mapCard(x, y + 104 * U, w, 1, "Maps de base", "20 maps variées pour commencer (environ 150 Mo).", bundled_, 20);
            c.fonts->regular.drawMid(b, x, std::min(bottom - 10 * U, y + 230 * U), 14 * U, pal().light4,
                                     "Les téléchargements continuent si tu passes à la suite.");
            break;
        }
        case Controls: {
            const float cw = (w - 2 * 16 * U) / 3, ch = 170 * U;
            const char *names[3] = {"Manette", "Clavier et souris", "Les deux"};
            const char *glyphs[3] = {fa::gamepad, fa::keyboard, fa::layer_group};
            const int modes[3] = {1, 2, 0};
            for (int i = 0; i < 3; ++i) {
                const float cx = x + static_cast<float>(i) * (cw + 16 * U);
                const bool sel = s.inputMode == modes[i];
                card(cx, y, cw, ch, i, sel);
                icon(c, glyphs[i], cx + cw / 2, y + 62 * U, 46 * U, sel ? col::white : pal().light2);
                c.fonts->semibold.drawMid(b, cx + cw / 2, y + 126 * U, 18 * U, col::white, names[i], gfx::Align::Center);
#if defined(__PROSPERO__)
                if (i == 1) c.fonts->regular.drawMid(b, cx + cw / 2, y + 150 * U, 14 * U, pal().light4, "bientôt sur PS5", gfx::Align::Center);
#endif
            }
            y += ch + 26 * U;
            const char *help = s.inputMode == 1
                                   ? "Viser : stick gauche (stick droit = précis) ou pavé tactile. Frapper : Croix, Carré, Rond, "
                                     "L1, R1, L2, R2. Pause : Options."
                               : s.inputMode == 2
                                   ? "Viser : souris (ou tablette). Frapper : Z et X, ou les clics. Pause : Échap. "
                                     "Les touches se changent dans les options."
                                   : "Tout fonctionne en même temps : manette, clavier et souris. Les touches se changent dans "
                                     "les options.";
            y += paragraph(c, c.fonts->regular, x, y, w, 17 * U, pal().content2, help);
#if defined(__PROSPERO__)
            paragraph(c, c.fonts->regular, x, y + 10 * U, w, 15 * U, pal().light4,
                      "Le clavier et la souris USB ne sont pas encore pris en charge sur PS5 : ils arriveront dans une "
                      "prochaine version.");
#endif
            break;
        }
        case Interface: {
            const float rh = 64 * U;
            char buf[64];
            snprintf(buf, sizeof(buf), "%.0f %%", s.uiScale * 100);
            row(x, y, w, 0, "Taille de l'interface", buf, (s.uiScale - 0.8f) / 0.5f);
            row(x, y + rh, w, 1, "Thème", "Moderne (lazer)   ·   stable, classique : bientôt", -1);
            row(x, y + rh * 2, w, 2, "Style de jeu", s.skin == 1 ? "Triangles (coloré)" : "Épuré (façon WhiteCat)", -1);
            row(x, y + rh * 3, w, 3, "Fréquence d'affichage", s.refreshHz == 120 ? "Auto (120 Hz si l'écran suit)" : "60 Hz", -1);
            c.fonts->regular.drawMid(b, x, y + rh * 4 + 6 * U, 14 * U, pal().light4, "Gauche / droite pour changer une valeur.");
            break;
        }
        default:
            y += paragraph(c, c.fonts->regular, x, y, w, 19 * U, col::white,
                           "Tu peux jouer ! Télécharge d'autres maps depuis le menu (« Télécharger »), et dépose tes skins "
                           "osu! (.osk) dans /data/rhythm-ps5/skins pour les utiliser.");
            y += 12 * U;
            paragraph(c, c.fonts->regular, x, y, w, 16 * U, pal().content2,
                      "Cette configuration peut être relancée depuis les options.");
            break;
        }
    }

    void row(float x, float y, float w, int id, const char *label, const char *value, float fill) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float h = 54 * U;
        card(x, y, w, h, id, false);
        c.fonts->semibold.drawMid(b, x + 20 * U, y + h / 2, 17 * U, col::white, label);
        const float vx = x + w * 0.45f, vw = w * 0.55f - 20 * U;
        if (fill >= 0) {
            b.roundRect(vx, y + h / 2 - 2 * U, vw - 70 * U, 4 * U, 2 * U, alpha(col::pink1, 0.3f));
            b.roundRect(vx, y + h / 2 - 2 * U, (vw - 70 * U) * std::clamp(fill, 0.0f, 1.0f), 4 * U, 2 * U, col::pink1);
            b.circle(vx + (vw - 70 * U) * std::clamp(fill, 0.0f, 1.0f), y + h / 2, 8 * U, col::white);
            c.fonts->semibold.drawMid(b, x + w - 20 * U, y + h / 2, 17 * U, pal().content2, value, gfx::Align::Right);
        } else {
            text(c, c.fonts->semibold, x + w - 20 * U, c.fonts->semibold.topForCenter(y + h / 2, 16 * U), 16 * U, pal().content2, value,
                 gfx::Align::Right, vw);
        }
    }

    void mapCard(float x, float y, float w, int id, const char *name, const char *desc,
                 const std::vector<std::shared_ptr<net::DownloadState>> &states, int expected) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float h = 90 * U;
        int fin = 0, failed = 0;
        float frac = 0;
        for (const auto &d : states) {
            const int ph = d->phase.load();
            fin += ph == 3;
            failed += ph == 4;
            const double total = static_cast<double>(d->total.load());
            frac += ph >= 3 ? 1.0f : total > 0 ? static_cast<float>(static_cast<double>(d->received.load()) / total) * 0.95f : 0;
        }
        const bool started = !states.empty();
        const bool installed = (!started && id == 0 && c.library->hasSetId(kTutorial)) || (started && fin + failed == static_cast<int>(states.size()));
        card(x, y, w, h, id, installed);
        icon(c, id == 0 ? fa::lightbulb : fa::layer_group, x + 44 * U, y + h / 2, 30 * U, installed ? col::white : col::pink1);
        c.fonts->semibold.drawMid(b, x + 84 * U, y + 30 * U, 20 * U, col::white, name);
        c.fonts->regular.drawMid(b, x + 84 * U, y + 58 * U, 15 * U, pal().content2, desc);
        char buf[64];
        if (installed) {
            snprintf(buf, sizeof(buf), failed ? i18n::tr("%d / %d installées") : i18n::tr("installé"), fin, static_cast<int>(states.size()));
            c.fonts->semibold.drawMid(b, x + w - 24 * U, y + h / 2, 16 * U, col::lime1, buf, gfx::Align::Right);
        } else if (started) {
            frac /= static_cast<float>(states.size());
            snprintf(buf, sizeof(buf), "%d / %d", fin, std::max(expected, static_cast<int>(states.size())));
            c.fonts->semibold.drawMid(b, x + w - 24 * U, y + 30 * U, 15 * U, pal().content2, buf, gfx::Align::Right);
            const float bw = 220 * U;
            b.roundRect(x + w - 24 * U - bw, y + 54 * U, bw, 6 * U, 3 * U, pal().background6);
            b.roundRect(x + w - 24 * U - bw, y + 54 * U, std::max(6 * U, bw * frac), 6 * U, 3 * U, col::pink1);
        } else {
            c.fonts->semibold.drawMid(b, x + w - 24 * U, y + h / 2, 16 * U, pal().light2, "Croix : télécharger", gfx::Align::Right);
        }
    }

    int step_ = 0, focus_ = 0, lastContent_ = 0;
    std::vector<Hit> hits_;
    std::vector<std::shared_ptr<net::DownloadState>> tutorial_, bundled_;
    Tween panelA_{0}, panelY_{0}, contentX_{0}, contentA_{1};
};

}  // namespace

std::unique_ptr<Screen> Game::makeFirstRun() { return std::make_unique<FirstRun>(*this); }

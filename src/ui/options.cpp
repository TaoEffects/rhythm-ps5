// SPDX-License-Identifier: GPL-3.0-or-later
// Options façon SettingsOverlay de lazer : panneau latéral, sections, interrupteurs
// et curseurs roses. Manette (croix directionnelle, L1/R1), clavier ou souris.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "app/game.h"
#include "core/i18n.h"
#include "core/anim.h"
#include "ui/lz.h"

namespace {

// Nom lisible d'une touche (codes HID, clavier USB ou PC).
std::string keyName(int k) {
    if (k >= 0x04 && k <= 0x1D) return std::string(1, static_cast<char>('A' + (k - 0x04)));
    if (k >= 0x1E && k <= 0x26) return std::string(1, static_cast<char>('1' + (k - 0x1E)));
    switch (k) {
    case 0x27: return "0";
    case 0x28: return "Entrée";
    case 0x2A: return "Retour arrière";
    case 0x2B: return "Tab";
    case 0x2C: return "Espace";
    case 0x2D: return "-";
    case 0x2E: return "=";
    case 0x2F: return "[";
    case 0x30: return "]";
    case 0x33: return ";";
    case 0x34: return "'";
    case 0x36: return ",";
    case 0x37: return ".";
    case 0x38: return "/";
    case 0x4F: return "Droite";
    case 0x50: return "Gauche";
    case 0x51: return "Bas";
    case 0x52: return "Haut";
    case 0xE0: return "Ctrl gauche";
    case 0xE1: return "Maj gauche";
    case 0xE2: return "Alt gauche";
    case 0xE4: return "Ctrl droit";
    case 0xE5: return "Maj droit";
    case 0xE6: return "Alt droit";
    default: {
        char b[24];
        snprintf(b, sizeof(b), i18n::tr("Touche %02X"), k);
        return b;
    }
    }
}

enum class Kind { Toggle, Slider, Choice, Key };

struct Item {
    const char *section;
    const char *label;
    const char *help;
    Kind kind;
    std::function<std::string()> value;
    std::function<void(int dir)> change;
    std::function<float()> fill;      // 0..1 pour les curseurs
    std::function<void(float)> setFill;
    int *keyRef = nullptr;  // Kind::Key : touche (code HID) à réassigner
};


// Panneau d'options façon SettingsOverlay de lazer : barre d'icônes de 70 u et
// panneau de 400 u qui glissent depuis la gauche (600 ms OutQuint), sous la
// barre d'outils. Palette violette (OverlayColourProvider Purple).
#define kSidebarW (70 * lz::U)
#define kPanelW (400 * lz::U)
#define kPad (20 * lz::U)

class Options : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        refreshAtEnter_ = c.settings->refreshHz;
        build();
        slide_.fromTo(t, 0, 1, 600, lz::Ease::OutQuint);
        c.play(c.sfx->uiOpen, 0.6f);
    }

    double leave() override {
        Context &c = game.ctx;
        c.saveSettings();
        if (c.settings->refreshHz != refreshAtEnter_)
            game.toast(i18n::tr("Fréquence ") + std::to_string(c.settings->refreshHz) + i18n::tr(" Hz : relance l'appli pour l'appliquer"));
        slide_.go(lz::ms(c), 0, 600, lz::Ease::OutQuint);
        return 600;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        // Attente d'une touche à assigner : la prochaine touche pressée (Échap annule).
        if (capture_ >= 0) {
            if (in.raw && in.raw->keyboard.connected) {
                for (int k = 4; k < 232; ++k) {
                    if (!in.raw->keyboard.keys[k] || prevKeys_[k]) continue;
                    if (k != 0x29) *items_[static_cast<size_t>(capture_)].keyRef = k;
                    capture_ = -1;
                    c.play(c.sfx->uiSelect);
                    break;
                }
                for (int k = 0; k < 256; ++k) prevKeys_[k] = in.raw->keyboard.keys[k];
            }
            if (in.back && capture_ >= 0) capture_ = -1;
            return;
        }
        if (in.raw)
            for (int k = 0; k < 256; ++k) prevKeys_[k] = in.raw->keyboard.keys[k];
        const float x0 = panelX();
        if (in.back || in.options || (in.mouseClick && in.mouseX > x0 + kSidebarW + kPanelW)) {
            c.play(c.sfx->uiBack);
            game.closeOverlay();
            return;
        }
        const int n = static_cast<int>(items_.size());
        const int old = cursor_;
        if (in.up) cursor_ = (cursor_ + n - 1) % n;
        if (in.down) cursor_ = (cursor_ + 1) % n;
        if (in.l1 || in.r1) {
            // L1/R1 : section précédente / suivante.
            int sec = sectionIndex(items_[static_cast<size_t>(cursor_)].section);
            sec = (sec + (in.r1 ? 1 : static_cast<int>(sections_.size()) - 1)) % static_cast<int>(sections_.size());
            cursor_ = sectionStart(sections_[static_cast<size_t>(sec)]);
        }
        if (in.wheel != 0) target_ = std::clamp(target_ - in.wheel * 120, 0.0f, maxScroll());
        if (in.left || in.right || in.confirm) {
            Item &it = items_[static_cast<size_t>(cursor_)];
            if (it.kind == Kind::Key) {
                if (in.confirm) capture_ = cursor_;
            } else {
                it.change(in.left ? -1 : 1);
                changed();
            }
        }
        // Souris : survol, clic, glisser sur les curseurs.
        const bool held = in.raw && (in.raw->mouse.buttons & 1u);
        const float cx = x0 + kSidebarW + kPad, cw = kPanelW - kPad * 2;
        for (int i = 0; i < n; ++i) {
            const float y = rowY(i);
            const float h = rowH(i);
            if (!in.hover(x0 + kSidebarW, y, kPanelW, h)) continue;
            if (in.mouseMoved) cursor_ = i;
            Item &it = items_[static_cast<size_t>(i)];
            if (it.kind == Kind::Slider && held && in.mouseY > y + h * 0.45f) {
                it.setFill((in.mouseX - cx) / cw);
                c.applyVolumes();
                lz::setUiScale(c.settings->uiScale);
            } else if (in.mouseClick && it.kind == Kind::Key) {
                capture_ = i;
            } else if (in.mouseClick && it.kind != Kind::Slider) {
                it.change(in.mouseX < cx + cw * 0.5f && it.kind == Kind::Choice ? -1 : 1);
                changed();
            }
        }
        for (size_t s = 0; s < sections_.size(); ++s)
            if (in.clicked(x0, sidebarY(static_cast<int>(s)), kSidebarW, kSidebarW)) {
                cursor_ = sectionStart(sections_[s]);
                c.play(c.sfx->uiSelect, 0.6f);
            }
        if (cursor_ != old) {
            c.play(c.sfx->uiHover, 0.6f);
            if (!in.mouseActive) {
                // Garde l'élément choisi visible.
                const float top = contentTop() + 140 * lz::U, bottom = kVH - 40 * lz::U;
                const float y = rowY(cursor_);
                if (y + rowH(cursor_) > bottom) target_ += y + rowH(cursor_) - bottom;
                if (y < top) target_ = std::max(0.0f, target_ - (top - y));
                target_ = std::clamp(target_, 0.0f, maxScroll());
            }
        }
        scroll_ = anim::damp(scroll_, target_, 60, c.dt * 1000);
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        using namespace lz;
        const double t = ms(c);
        const float k = slide_.at(t);
        if (k <= 0.001f) return;
        static const Palette pal = palette(hue::purple);
        const float x0 = panelX();
        const float top = contentTop();
        // Assombrit légèrement l'écran derrière.
        b.rect(0, 0, kVW, kVH, alpha(col::black, 0.35f * k));
        b.shadow(x0, top, kSidebarW + kPanelW, kVH - top, 0, 30 * U, alpha(col::black, 0.6f * k));
        // Panneau.
        b.rect(x0 + kSidebarW, top, kPanelW, kVH - top, pal.background4);
        b.pushClip(x0 + kSidebarW, top, kPanelW, kVH - top);
        const float cx = x0 + kSidebarW + kPad, cw = kPanelW - kPad * 2;
        // En-tête.
        const float hy = top + 24 * U - scroll_;
        c.fonts->light.draw(b, cx, hy, 40 * U, col::white, "options");
        c.fonts->regular.draw(b, cx, hy + 46 * U, 16 * U, pal.highlight1, "règle le jeu à ton goût");
        const char *lastSection = "";
        for (size_t i = 0; i < items_.size(); ++i) {
            Item &it = items_[i];
            const float y = rowY(static_cast<int>(i));
            const float h = rowH(static_cast<int>(i));
            if (std::string(it.section) != lastSection) {
                lastSection = it.section;
                const int si = sectionIndex(it.section);
                const float sy = y - 64 * U;
                if (sy < kVH && sy > top - 80 * U) {
                    b.rect(x0 + kSidebarW, sy - 14 * U, kPanelW, 1.5f * U, pal.background6);
                    icon(c, sectionIcon(si), cx + 12 * U, sy + 16 * U, 22 * U, pal.highlight1);
                    c.fonts->bold.drawMid(b, cx + 36 * U, sy + 16 * U, 24 * U, col::white, it.section);
                }
            }
            if (y > kVH || y + h < top) continue;
            const bool sel = static_cast<int>(i) == cursor_;
            if (sel) {
                b.roundRect(cx - 10 * U, y, cw + 20 * U, h, 5 * U, pal.background3);
                b.roundRect(cx - 10 * U, y + 6 * U, 3 * U, h - 12 * U, 2 * U, col::pink1);
            }
            std::string v = it.value();
            if (it.kind == Kind::Key && capture_ == static_cast<int>(i)) v = "appuie sur une touche...";
            const float ts = 16 * U;
            if (it.kind == Kind::Toggle) {
                c.fonts->regular.drawMid(b, cx, y + h / 2, ts, col::white, it.label);
                drawSwitch(cx + cw - 45 * U, y + h / 2 - 10 * U, v == "on", sel);
            } else if (it.kind == Kind::Slider) {
                c.fonts->regular.drawMid(b, cx, y + 16 * U, ts, col::white, it.label);
                c.fonts->semibold.drawMid(b, cx + cw, y + 16 * U, ts, pal.content2, v.c_str(), gfx::Align::Right);
                const float f = std::clamp(it.fill(), 0.0f, 1.0f);
                const float by = y + h - 16 * U;
                b.roundRect(cx, by - 2 * U, cw, 4 * U, 2 * U, alpha(col::pink1, 0.3f));
                b.roundRect(cx, by - 2 * U, cw * f, 4 * U, 2 * U, col::pink1);
                // Poignée (Nub) : pastille arrondie.
                const float nx = cx + cw * f;
                b.roundRect(nx - 14 * U, by - 6 * U, 28 * U, 12 * U, 6 * U, sel ? col::pink1 : pal.background4);
                b.roundRectBorder(nx - 14 * U, by - 6 * U, 28 * U, 12 * U, 6 * U, 2.5f * U, col::pink1);
            } else {
                c.fonts->regular.drawMid(b, cx, y + 16 * U, ts, col::white, it.label);
                const float dy = y + 30 * U, dh = 32 * U;
                b.roundRect(cx, dy, cw, dh, 5 * U, sel ? pal.background2 : pal.background5);
                c.fonts->semibold.drawMid(b, cx + 12 * U, dy + dh / 2, ts, col::white, v.c_str());
                icon(c, fa::chevron_left, cx + cw - 38 * U, dy + dh / 2, 12 * U, pal.light2);
                icon(c, fa::chevron_right, cx + cw - 16 * U, dy + dh / 2, 12 * U, pal.light2);
            }
            helpWidth_ = cw;
            if (sel && it.help) wrap(it.help, cx, y + h + 6 * U, cw, 13 * U, pal.content2);
        }
        b.popClip();
        // Barre latérale d'icônes.
        b.rect(x0, top, kSidebarW, kVH - top, pal.background5);
        const int cur = sectionIndex(items_[static_cast<size_t>(cursor_)].section);
        for (size_t s = 0; s < sections_.size(); ++s) {
            const float sy = sidebarY(static_cast<int>(s));
            const bool on = static_cast<int>(s) == cur;
            if (on) b.rect(x0 + kSidebarW - 5 * U, sy + 10 * U, 5 * U, kSidebarW - 20 * U, col::pink1);
            icon(c, sectionIcon(static_cast<int>(s)), x0 + kSidebarW / 2, sy + kSidebarW / 2, 22 * U,
                 on ? col::white : pal.light4);
        }
        // Aide manette en bas du panneau.
        const float fy = kVH - 34 * U;
        b.rect(x0 + kSidebarW, fy - 8 * U, kPanelW, 42 * U, pal.background5);
        padGlyph(c, Pad::Circle, cx + 10 * U, fy + 12 * U, 20 * U);
        c.fonts->regular.drawMid(b, cx + 26 * U, fy + 12 * U, 14 * U, pal.content2, "fermer");
        padGlyph(c, Pad::L1, cx + 100 * U, fy + 12 * U, 20 * U);
        padGlyph(c, Pad::R1, cx + 124 * U, fy + 12 * U, 20 * U);
        c.fonts->regular.drawMid(b, cx + 140 * U, fy + 12 * U, 14 * U, pal.content2, "sections");
    }

private:
    float panelX() const { return -(kSidebarW + kPanelW + 40) * (1 - slide_.at(lz::ms(game.ctx))); }
    float contentTop() const { return game.toolbar.bottom(game.ctx); }
    float sidebarY(int s) const { return contentTop() + 20 * lz::U + static_cast<float>(s) * kSidebarW; }
    float rowH(int i) const {
        const Kind k = items_[static_cast<size_t>(i)].kind;
        float h = k == Kind::Toggle ? 40 * lz::U : k == Kind::Slider ? 52 * lz::U : 70 * lz::U;
        // L'aide de l'élément choisi s'affiche dessous.
        if (i == cursor_) h += 0;
        return h;
    }
    float rowY(int i) const {
        float y = contentTop() + 130 * lz::U - scroll_;
        const char *last = "";
        for (int k = 0; k <= i && k < static_cast<int>(items_.size()); ++k) {
            if (std::string(items_[static_cast<size_t>(k)].section) != last) {
                last = items_[static_cast<size_t>(k)].section;
                y += 80 * lz::U;
            }
            if (k < i) y += rowH(k) + (k == cursor_ ? helpSpace(k) : 8 * lz::U);
        }
        return y;
    }
    float maxScroll() const {
        return std::max(0.0f, rowY(static_cast<int>(items_.size()) - 1) + scroll_ + 200 * lz::U - kVH);
    }
    int sectionStart(const std::string &s) const {
        for (size_t i = 0; i < items_.size(); ++i)
            if (s == items_[i].section) return static_cast<int>(i);
        return 0;
    }
    int sectionIndex(const std::string &s) const {
        for (size_t i = 0; i < sections_.size(); ++i)
            if (sections_[i] == s) return static_cast<int>(i);
        return 0;
    }
    const char *sectionIcon(int s) const {
        static const char *icons[] = {fa::display, fa::sliders, fa::volume_high, fa::gamepad, fa::gear};
        return icons[s % 5];
    }
    void drawSwitch(float x, float y, bool on, bool sel) {
        using namespace lz;
        gfx::SpriteBatch &b = *game.ctx.batch;
        const float w = 45 * U, h = 20 * U;
        b.roundRect(x, y, w, h, h / 2, on ? col::pink1 : alpha(col::white, 0.1f));
        b.roundRectBorder(x, y, w, h, h / 2, 2 * U, sel ? col::white : col::pink1);
        const float nx = on ? x + w - h / 2 : x + h / 2;
        b.circle(nx, y + h / 2, h / 2 - 4 * U, on ? col::white : col::pink1);
    }
    void changed() {
        game.ctx.applyVolumes();
        lz::setUiScale(game.ctx.settings->uiScale);
        game.ctx.play(game.ctx.sfx->uiHover);
    }

    // Dessine l'aide sur plusieurs lignes (ou les compte seulement) ; renvoie le nombre de lignes.
    int wrap(const char *text, float x, float y, float width, float size, uint32_t color, bool draw = true) const {
        Context &c = game.ctx;
        int lines = 0;
        std::string line, word;
        const std::string s = std::string(i18n::tr(text)) + " ";
        for (char ch : s) {
            if (ch != ' ') {
                word += ch;
                continue;
            }
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (c.fonts->regular.measure(size, candidate.c_str()) > width && !line.empty()) {
                if (draw) c.fonts->regular.draw(*c.batch, x, y, size, color, line.c_str());
                y += size * 1.35f;
                ++lines;
                line = word;
            } else {
                line = candidate;
            }
            word.clear();
        }
        if (!line.empty()) {
            if (draw) c.fonts->regular.draw(*c.batch, x, y, size, color, line.c_str());
            ++lines;
        }
        return lines;
    }
    // Place réservée sous l'élément choisi pour son aide.
    float helpSpace(int i) const {
        const char *help = items_[static_cast<size_t>(i)].help;
        if (!help) return 8 * lz::U;
        const int n = wrap(help, 0, 0, helpWidth_, 13 * lz::U, 0, false);
        return 14 * lz::U + static_cast<float>(n) * 13 * lz::U * 1.35f;
    }

    static std::string pct(float v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + " %"; }

    void slider(const char *section, const char *label, const char *help, float &v, float lo, float hi, float step,
                std::function<std::string()> fmt) {
        items_.push_back({section, label, help, Kind::Slider, std::move(fmt),
                          [&v, lo, hi, step](int d) { v = std::clamp(std::round((v + step * d) / step) * step, lo, hi); },
                          [&v, lo, hi] { return (v - lo) / (hi - lo); },
                          [&v, lo, hi, step](float f) { v = std::clamp(std::round((lo + f * (hi - lo)) / step) * step, lo, hi); }});
    }
    void key(const char *section, const char *label, const char *help, int &k) {
        Item it{section, label, help, Kind::Key, [&k] { return keyName(k); }, [](int) {}, nullptr, nullptr};
        it.keyRef = &k;
        items_.push_back(it);
    }
    // Bouton de manette qui frappe (ou non) en jeu.
    void padButton(const char *name, unsigned bit) {
        Settings &s = *game.ctx.settings;
        labels_.push_back(std::string(i18n::tr("Frappe avec ")) + name);
        items_.push_back({"Commandes", labels_.back().c_str(), "Ce bouton de la manette sert à frapper pendant les parties.",
                          Kind::Toggle,
                          [&s, bit] {
                              const unsigned m = s.padHitMask ? s.padHitMask : Settings::defaultPadHitMask();
                              return std::string(m & bit ? "on" : "off");
                          },
                          [&s, bit](int) {
                              unsigned m = s.padHitMask ? s.padHitMask : Settings::defaultPadHitMask();
                              m ^= bit;
                              s.padHitMask = m ? m : bit;  // au moins un bouton
                          },
                          nullptr, nullptr});
    }
    void toggle(const char *section, const char *label, const char *help, bool &v) {
        items_.push_back({section, label, help, Kind::Toggle, [&v] { return std::string(v ? "on" : "off"); }, [&v](int) { v = !v; },
                          nullptr, nullptr});
    }

    void build() {
        Settings &s = *game.ctx.settings;
        items_.clear();
#if defined(__PROSPERO__)
        items_.push_back({"Affichage", "Fréquence d'affichage",
                          "Auto : 120 Hz si l'écran le permet, sinon la console reste d'elle-même en 60 Hz. La PS5 ne "
                          "sort pas au-delà de 120 Hz. Un changement prend effet au prochain lancement.",
                          Kind::Choice,
                          [&s] {
                              const int real = plat::refreshHz();
                              std::string v = s.refreshHz == 120 ? "Auto (120 Hz)" : "60 Hz";
                              if (real > 0) v += i18n::tr("  ·  actuel ") + std::to_string(real) + " Hz";
                              return v;
                          },
                          [&s](int) { s.refreshHz = s.refreshHz == 60 ? 120 : 60; }, nullptr, nullptr});
#else
        items_.push_back({"Affichage", "Limite d'images",
                          "V-Sync : calé sur l'écran, sans déchirure. Sans limite : latence minimale, mais le processeur "
                          "et la carte graphique tournent à fond.",
                          Kind::Choice,
                          [&s] {
                              if (s.frameLimit == 0) return std::string("V-Sync");
                              if (s.frameLimit < 0) return std::string(i18n::tr("Illimitée"));
                              return std::to_string(s.frameLimit) + i18n::tr(" images/s");
                          },
                          [&s](int dir) {
                              static const int steps[] = {0, 240, 500, 1000, -1};
                              int i = 0;
                              while (i < 5 && steps[i] != s.frameLimit) ++i;
                              i = ((i < 5 ? i : 0) + (dir < 0 ? 4 : 1)) % 5;
                              s.frameLimit = steps[i];
                          },
                          nullptr, nullptr});
        toggle("Affichage", "Plein écran", "Plein écran ou fenêtre (Alt+Entrée bascule aussi).", s.fullscreen);
        toggle("Affichage", "Afficher les FPS", "Compteur d'images par seconde dans un coin de l'écran.", s.showFps);
#endif
        items_.push_back({"Affichage", "Style de jeu",
                          "Épuré : cercles sombres à fin liseré blanc (façon WhiteCat). Triangles : le style coloré de "
                          "lazer. Ensuite, tes skins osu! : dépose des .osk dans /data/rhythm-ps5/skins (FTP).",
                          Kind::Choice,
                          [this, &s] {
                              if (s.skin == 2) return s.skinName;
                              return std::string(s.skin == 0 ? "Épuré" : "Triangles");
                          },
                          [this, &s](int d) {
                              // Liste : Épuré, Triangles, puis les skins installés.
                              const auto &names = game.skinNames;
                              const int n = 2 + static_cast<int>(names.size());
                              int cur = s.skin;
                              if (s.skin == 2) {
                                  auto it = std::find(names.begin(), names.end(), s.skinName);
                                  cur = it == names.end() ? 0 : 2 + static_cast<int>(it - names.begin());
                              }
                              cur = (cur + (d < 0 ? n - 1 : 1)) % n;
                              s.skin = std::min(cur, 2);
                              if (cur >= 2) s.skinName = names[static_cast<size_t>(cur - 2)];
                              game.applySkin();
                          },
                          nullptr, nullptr});
        slider("Affichage", "Assombrissement du fond", "Assombrit l'image de fond pendant les parties pour mieux voir les cercles.",
               s.backgroundDim, 0, 1, 0.05f, [&s] { return pct(s.backgroundDim); });
        toggle("Affichage", "Storyboards", "Animations des maps qui en ont (images, effets).", s.storyboard);
        toggle("Affichage", "Fond flou en jeu", "Floute l'image de fond pendant les parties.", s.blurInGame);
        toggle("Affichage", "Interface en jeu", "Score, précision, combo et barre de vie pendant la partie.", s.showHud);
        toggle("Affichage", "Barre d'erreur de frappe", "Affiche en bas de l'écran si tes frappes sont en avance ou en retard.",
               s.showHitErrorBar);
        slider("Interface", "Taille de l'interface", "Agrandit ou réduit les menus (80 à 130 %).", s.uiScale, 0.8f, 1.3f, 0.05f,
               [&s] { return pct(s.uiScale); });
        items_.push_back({"Interface", "Thème", "Le thème moderne s'inspire d'osu!lazer. Les thèmes « stable » et « classique » "
                          "arrivent bientôt.",
                          Kind::Choice, [] { return std::string("Moderne (lazer)"); },
                          [this](int) { game.toast("Les thèmes « stable » et « classique » arrivent bientôt"); }, nullptr, nullptr});
        items_.push_back({"Interface", "Langue", "Langue de l'interface.", Kind::Choice,
                          [&s] {
                              return std::string(s.language == 1 ? "Français" : s.language == 2 ? "English"
                                                                                                : "Automatique (langue de la console)");
                          },
                          [&s](int dir) {
                              s.language = (s.language + (dir < 0 ? 2 : 1)) % 3;
                              i18n::setLang(i18n::resolve(s.language, plat::systemLanguage()));
                          },
                          nullptr, nullptr});
        toggle("Interface", "Métadonnées d'origine",
               "Affiche les titres et artistes dans leur langue d'origine (japonais...) quand la map les fournit.",
               s.originalMetadata);
        toggle("Interface", "Effet de parallaxe", "Le fond et le menu suivent légèrement la souris.", s.menuParallax);
        toggle("Interface", "Intro au démarrage", "La séquence d'ouverture avant le menu.", s.introEnabled);
        slider("Son", "Volume général", "Volume de tout le jeu.", s.masterVolume, 0, 1, 0.05f, [&s] { return pct(s.masterVolume); });
        slider("Son", "Musique", "Volume de la musique.", s.musicVolume, 0, 1, 0.05f, [&s] { return pct(s.musicVolume); });
        slider("Son", "Effets", "Sons de frappe et de l'interface.", s.effectsVolume, 0, 1, 0.05f, [&s] { return pct(s.effectsVolume); });
        items_.push_back({"Son", "Décalage audio",
                          "Si tu frappes toujours en retard, augmente la valeur. L'écran de résultats propose une correction "
                          "automatique.",
                          Kind::Choice, [&s] { return (s.offsetMs > 0 ? "+" : "") + std::to_string(s.offsetMs) + " ms"; },
                          [&s](int d) { s.offsetMs = std::clamp(s.offsetMs + 5 * d, -300, 300); }, nullptr, nullptr});
        items_.push_back({"Commandes", "Façon de jouer",
#if defined(__PROSPERO__)
                          "Manette seule, clavier et souris, ou les deux en même temps. Clavier et souris USB : bientôt sur PS5.",
#else
                          "Manette seule, clavier et souris, ou les deux en même temps.",
#endif
                          Kind::Choice,
                          [&s] { return std::string(s.inputMode == 1 ? "Manette" : s.inputMode == 2 ? "Clavier et souris" : "Les deux"); },
                          [&s](int d) {
                              s.inputMode = (s.inputMode + (d < 0 ? 2 : 1)) % 3;
                              s.mouseButtonsInGame = s.inputMode != 1;
                          },
                          nullptr, nullptr});
        key("Commandes", "Touche de frappe 1", "Croix ou Entrée, puis appuie sur la touche voulue (Échap annule).", s.keyHit1);
        key("Commandes", "Touche de frappe 2", "Croix ou Entrée, puis appuie sur la touche voulue (Échap annule).", s.keyHit2);
        padButton("Croix", plat::kPadCross);
        padButton("Carré", plat::kPadSquare);
        padButton("Rond", plat::kPadCircle);
        padButton("Triangle", plat::kPadTriangle);
        padButton("L1", plat::kPadL1);
        padButton("R1", plat::kPadR1);
        padButton("L2", plat::kPadL2);
        padButton("R2", plat::kPadR2);
        toggle("Commandes", "Clics de souris en jeu", "Les clics gauche et droit servent à frapper.", s.mouseButtonsInGame);
        items_.push_back({"Commandes", "Pavé tactile",
                          "Absolu : le pavé correspond à l'aire de jeu, comme une tablette (sans lissage). Relatif : il "
                          "déplace le curseur comme un trackpad.",
                          Kind::Choice, [&s] { return std::string(s.touchpadAbsolute ? "Absolu" : "Relatif"); },
                          [&s](int) { s.touchpadAbsolute = !s.touchpadAbsolute; }, nullptr, nullptr});
        slider("Commandes", "Vitesse du curseur (stick)",
               "Vitesse du curseur quand le stick gauche est poussé à fond. Le stick droit donne un mouvement fin.", s.cursorSpeed, 300,
               4000, 100, [&s] { return std::to_string(static_cast<int>(s.cursorSpeed)); });
        slider("Commandes", "Courbe du stick", "Plus la valeur est haute, plus les petits mouvements sont précis.", s.cursorCurve, 1, 3,
               0.1f, [&s] {
                   char b[16];
                   snprintf(b, sizeof(b), "%.1f", s.cursorCurve);
                   return std::string(b);
               });
        slider("Commandes", "Taille du curseur", "Taille du curseur en jeu.", s.cursorSize, 0.5f, 2.0f, 0.1f,
               [&s] { return pct(s.cursorSize); });
        toggle("Commandes", "Traînée du curseur", "La traînée qui suit le curseur en jeu.", s.cursorTrail);
#if !defined(__PROSPERO__)
        items_.push_back({"Divers", "Mise à jour", "Vérifie au lancement s'il existe une version plus récente du jeu.",
                          Kind::Choice,
                          [this] {
                              upd::Updater &u = game.updater();
                              if (u.downloading())
                                  return std::string(i18n::tr("Téléchargement... ")) +
                                         std::to_string(static_cast<int>(u.progress() * 100)) + " %";
                              if (u.available()) return std::string(i18n::tr("Installer ")) + u.latestTag();
                              if (u.failed()) return std::string(i18n::tr("Vérification impossible"));
                              return std::string(i18n::tr("À jour"));
                          },
                          [this](int) { game.updater().install(); }, nullptr, nullptr});
#endif
        items_.push_back({"Divers", "Configuration initiale", "Relance l'assistant du premier lancement (maps de base, façon de jouer...).",
                          Kind::Choice, [] { return std::string("Ouvrir"); },
                          [this](int) {
                              game.closeOverlay();
                              game.goTo(game.makeFirstRun());
                          },
                          nullptr, nullptr});
        toggle("Divers", "Infos techniques", "Affiche les images par seconde et attend 3 s au lancement que le journal réseau se connecte.", s.showDebug);
        sections_.clear();
        for (const Item &it : items_)
            if (sections_.empty() || sections_.back() != it.section) sections_.push_back(it.section);
    }

    std::vector<Item> items_;
    mutable float helpWidth_ = 300;  // largeur du texte d'aide (mise à jour au rendu)
    std::deque<std::string> labels_;  // libellés construits (adresses stables)
    std::vector<std::string> sections_;
    int cursor_ = 0;
    float scroll_ = 0, target_ = 0;
    lz::Tween slide_{0};
    int refreshAtEnter_ = 60;
    int capture_ = -1;
    bool prevKeys_[256] = {};
};

}  // namespace

std::unique_ptr<Screen> Game::makeOptions() { return std::make_unique<Options>(*this); }

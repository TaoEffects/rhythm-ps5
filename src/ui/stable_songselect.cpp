// SPDX-License-Identifier: GPL-3.0-or-later
// Sélection de map façon osu!stable :
//  - bandeau du haut : « Artiste - Titre [Difficulté] », mappeur, durée / BPM /
//    objets, cercles / sliders / spinners, CS AR OD HP et étoiles ; onglets de tri ;
//  - à droite, le carrousel de boutons : sets en rose, difficultés du set choisi
//    en bleu, difficulté active en blanc, décalés selon leur distance au centre ;
//  - à gauche, le classement local ; en bas, retour, Mode, Mods, Au hasard,
//    Options, la carte du joueur et le logo qui lance la partie ;
//  - la sélection des mods en grille (Réduction, Augmentation, Spécial).
// Mise en page reprise de McOsu (1 unité = hauteur / 480), dessinée sans image.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/game.h"
#include "core/fsutil.h"
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

inline float K() { return kVH / 1080.0f; }

// Mods proposés, rangés par ligne comme la sélection de stable.
struct ModDef {
    const char *acronym, *name, *desc;
    int row;  // 0 réduction, 1 augmentation, 2 spécial
    bool game::Mods::*flag;
};
const ModDef kMods[] = {
    {"EZ", "Easy", "Plus gros, plus lents, plus tolérants.", 0, &game::Mods::easy},
    {"NF", "No Fail", "Impossible d'échouer.", 0, &game::Mods::noFail},
    {"HT", "Half Time", "Musique ralentie (×0,75).", 0, &game::Mods::halfTime},
    {"HR", "Hard Rock", "Plus petits, plus rapides, plus stricts.", 1, &game::Mods::hardRock},
    {"DT", "Double Time", "Musique accélérée (×1,5), même hauteur.", 1, &game::Mods::doubleTime},
    {"NC", "Nightcore", "Comme DT, mais la musique monte.", 1, &game::Mods::nightcore},
    {"HD", "Hidden", "Les cercles s'effacent avant la frappe.", 1, &game::Mods::hidden},
    {"AT", "Auto", "Regarder la map se jouer toute seule.", 2, &game::Mods::autoplay},
};
constexpr int kModCount = sizeof(kMods) / sizeof(kMods[0]);
const char *const kModRows[] = {"Réduction de difficulté", "Augmentation de difficulté", "Spécial"};
const uint32_t kModRowColours[] = {hex(0x88cc33), hex(0xee5555), hex(0x66ccff)};
const char *const kSorts[] = {"Titre", "Artiste", "Mappeur", "Difficulté"};

// Élément du carrousel : un set replié, ou une difficulté du set choisi.
struct Item {
    int set = -1, diff = -1;
    float y = 0;  // position dans la liste (haut du bouton « extérieur »)
};

struct ItemAnim {
    float curve = -1;        // C : 1 au centre de la vue, 0 aux bords
    Tween hover{0}, push{0}, spread{0};
    bool hovered = false;
};

class StableSongSelect : public Screen {
public:
    using Screen::Screen;
    bool songSelect() const override { return true; }

    void enter() override {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        set_ = std::max(0, game.selectedSet);
        diff_ = std::max(0, game.selectedDiff);
        filter();
        rebuild();
        scroll_ = targetScroll();
        screenA_.fromTo(t, 0, 1, forward ? 300 : 200, Ease::OutQuint);
        slideIn_.fromTo(t, 0, 1, 600, Ease::OutQuint);
        game.toolbar.show(c, false);
        game.setBackground(0.4f, 0, 300, Ease::OutQuint);
        game.backgroundParallax = 0;
        // Le logo, au coin bas droit, sert de bouton « Jouer ».
        lz::Logo &logo = game.logo;
        logo.x.snap(logoX());
        logo.y.snap(logoY());
        logo.scale.fromTo(t, 0.5f, 0.66f, 400, Ease::OutQuint);
        logo.opacity.go(t, 1, 300, Ease::OutQuint);
        logo.interactive = true;
        logo.visualiser = false;
        logo.triangles = true;
        if (game.currentSet()) game.select(set_, diff_, true);
        stats_ = st::playerStats(game.allScores());
    }

    double leave() override {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        screenA_.go(t, 0, 200, Ease::OutQuint);
        game.logo.opacity.go(t, 0, 150, Ease::OutQuint);
        game.backgroundParallax = 1;
        return 250;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        const auto &sets = c.library->sets();
        if (lastCount_ != sets.size()) {
            filter();
            rebuild();
        }
        if (game.jumpToSet >= 0 && static_cast<size_t>(game.jumpToSet) < sets.size()) {
            query_.clear();
            filter();
            pick(game.jumpToSet, 0);
            rebuild();
            game.jumpToSet = -1;
        }
        if (imeOpen_) {
            std::string text;
            const int r = game.pollTextInput(text);
            if (r != 0) {
                imeOpen_ = false;
                if (r > 0) {
                    query_ = text;
                    filter();
                    rebuild();
                }
            }
            return;
        }
        if (modsOpen_) {
            updateMods(in);
            return;
        }
        typing(in);
        const int oldSet = set_, oldDiff = diff_;

        // Bas de l'écran : retour, boutons de navigation, carte du joueur.
        backFx_.setHover(c, st::backHovered(in), 100, 100);
        if (backFx_.hovered && in.mouseClick) {
            goBack();
            return;
        }
        for (int i = 0; i < 4; ++i) {
            float x, y, w, h;
            navRect(i, x, y, w, h);
            const bool hv = in.mouseActive && in.hover(x, y, w, h);
            navFx_[i].setHover(c, hv, 100, 100);
            if (hv && in.mouseClick) navAction(i);
        }
        if (in.back) {
            if (!query_.empty()) {
                query_.clear();
                filter();
                rebuild();
                return;
            }
            goBack();
            return;
        }
        // Onglets de tri.
        for (int i = 0; i < 4; ++i) {
            float x, y, w, h;
            sortRect(i, x, y, w, h);
            if (in.clicked(x, y, w, h) && c.settings->songSort != i) {
                c.settings->songSort = i;
                c.play(c.sfx->uiSelect, 0.7f);
                filter();
                rebuild();
            }
        }
        if (visible_.empty()) {
            if (in.confirm || game.logo.clicked()) {
                c.play(c.sfx->uiSelect);
                game.openOverlay(game.makeDownload());
            }
            return;
        }
        if (in.extra2) navAction(1);
        if (in.extra) navAction(2);
        if (in.options) navAction(3);
        if (game.textInputSupported() && !imeOpen_ && (in.touchpadClick || in.clicked(searchX(), searchY(), searchW(), searchH())))
            imeOpen_ = game.startTextInput("Rechercher une map", query_);

        // Clavier / manette : haut-bas d'élément en élément, gauche-droite de set en set.
        const int n = static_cast<int>(items_.size());
        const int pos = itemIndex(set_, diff_);
        if (in.down && n > 0) {
            const Item &it = items_[static_cast<size_t>((pos + 1) % n)];
            pick(it.set, std::max(0, it.diff));
        } else if (in.up && n > 0) {
            const Item &it = items_[static_cast<size_t>((pos - 1 + n) % n)];
            pick(it.set, it.diff >= 0 ? it.diff : lastDiff(it.set));
        }
        const int pv = visibleIndex(), nv = static_cast<int>(visible_.size());
        if (in.right) pick(visible_[static_cast<size_t>((pv + 1) % nv)], 0);
        if (in.left) pick(visible_[static_cast<size_t>((pv - 1 + nv) % nv)], 0);
        if (in.r1) pick(visible_[static_cast<size_t>(std::min(nv - 1, pv + 8))], 0);
        if (in.l1) pick(visible_[static_cast<size_t>(std::max(0, pv - 8))], 0);
        if (in.r2) pick(visible_.back(), 0);
        if (in.l2) pick(visible_.front(), 0);

        // Molette : défilement libre ; clic droit maintenu : saut direct dans la liste.
        const float maxScroll = items_.empty() ? 0 : items_.back().y;
        if (in.wheel != 0) {
            if (!freeScroll_) freeTarget_ = scroll_;
            freeScroll_ = true;
            freeTarget_ = std::clamp(freeTarget_ - in.wheel * 2.5f * kBtnH(), -viewH() / 2, maxScroll);
        }
        if (in.raw && (in.raw->mouse.buttons & 2u) && in.mouseActive && in.mouseX > viewX()) {
            freeScroll_ = true;
            freeTarget_ = std::clamp((in.mouseY - viewY()) / viewH(), 0.0f, 1.0f) * maxScroll - viewH() / 2;
        }

        // Souris dans le carrousel : survol (écarte les voisins) et clic.
        int hoveredIdx = -1;
        bool playClicked = false;
        for (size_t i = 0; i < items_.size(); ++i) {
            const Item &it = items_[i];
            float x, y;
            buttonPos(it, x, y);
            const bool hv = in.mouseActive && in.mouseX >= x && in.mouseY >= y && in.mouseY < y + kBtnH() && in.mouseY > viewY() &&
                            in.mouseY < bottomTop() && in.mouseX > viewX();
            if (hv) hoveredIdx = static_cast<int>(i);
            if (hv && in.mouseClick) {
                if (it.diff >= 0 && it.set == set_ && it.diff == diff_) playClicked = true;
                else if (it.diff >= 0) pick(it.set, it.diff);
                else pick(it.set, lastDiff(it.set));  // un set : sa difficulté la plus dure, comme stable
            }
        }
        if (hoveredIdx != lastHovered_) {
            lastHovered_ = hoveredIdx;
            if (hoveredIdx >= 0) c.play(c.sfx->uiHover, 0.5f);
            for (size_t i = 0; i < items_.size(); ++i) {
                ItemAnim &a = anim(items_[i]);
                const bool h = static_cast<int>(i) == hoveredIdx;
                if (h != a.hovered) {
                    a.hovered = h;
                    a.hover.go(t, h ? 1 : 0, 1000 * std::max(0.05f, h ? 1 - a.hover.at(t) : a.hover.at(t)), Ease::Out);
                }
                const float sp = hoveredIdx < 0 ? 0 : static_cast<int>(i) < hoveredIdx ? -1.0f : static_cast<int>(i) > hoveredIdx ? 1.0f : 0;
                a.spread.go(t, sp, 700, Ease::OutQuart, sp == 0 ? 50 : 0);
            }
        }
        if (set_ != oldSet || diff_ != oldDiff) {
            c.play(c.sfx->uiHover, 0.7f);
            changeTime_ = t;
            pending_ = true;
            pulse_.fromTo(t, 1, 0, 550, Ease::None);
            if (set_ != oldSet) rebuild();
        }
        const bool held = in.raw && ((in.raw->pad.buttons & (plat::kPadUp | plat::kPadDown)) != 0);
        if (pending_ && t - changeTime_ > (held ? 300 : 150)) {
            pending_ = false;
            game.select(set_, diff_, true);
        }
        if (in.confirm || playClicked || game.logo.clicked()) play();

        // Animations : défilement, courbe vers le centre, décalage des difficultés.
        scroll_ = anim::damp(scroll_, targetScroll(), 60, c.dt * 1000);
        for (const Item &it : items_) {
            ItemAnim &a = anim(it);
            float x, y;
            buttonPos(it, x, y);
            const float cy = y + kBtnH() / 2;
            const float target = 1 - std::clamp(std::fabs(cy - (viewY() + viewH() / 2)) / (viewH() / 2), 0.0f, 1.0f);
            a.curve = a.curve < 0 ? target : anim::damp(a.curve, target, 120, c.dt * 1000);
            const float p = it.diff >= 0 ? 0.075f : 0;
            if (std::fabs(a.push.target() - p) > 1e-4f) a.push.go(t, p, 250, Ease::Out);
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = lz::ms(c);
        const float sa = screenA_.at(t);
        if (sa <= 0.001f) return;
        b.pushAlpha(sa);
        renderLeaderboard();
        renderCarousel();
        renderSearch();
        renderTopBar();
        renderBottomBar();
        // Impulsion blanche en bas après un changement de sélection.
        const float p = pulse_.at(t);
        if (p > 0.001f) b.gradient(0, kVH * 0.5f, kVW, kVH * 0.5f, gfx::rgba(255, 255, 255, 0), gfx::rgba(255, 255, 255, static_cast<uint8_t>(25 * p)));
        b.popTransform();
        if (modsOpen_ || modsA_.at(t) > 0.001f) renderMods();
    }

private:
    // --- Géométrie (McOsu, 1 unité = hauteur / 480) ---------------------------------------

    static float kBtnH() { return 120 * K(); }   // hauteur visible d'un bouton
    static float kBtnW() { return 953 * K(); }
    static float topH() { return 128 * K(); }
    static float leftW() { return 0.265f * kVW + 5 * K(); }
    static float bottomH() { return 0.116f * kVH; }
    static float bottomTop() { return kVH - bottomH(); }
    static float viewX() { return leftW() + 6 * K(); }
    static float viewY() { return 77 * K(); }
    static float viewW() { return kVW - viewX() + 1; }
    static float viewH() { return bottomTop() + 2 - viewY(); }
    float logoX() const { return kVW - 100 * K(); }
    float logoY() const { return kVH - 40 * K(); }

    // Position à l'écran du bouton visible (coin haut gauche).
    void buttonPos(const Item &it, float &x, float &y) {
        const double t = lz::ms(game.ctx);
        ItemAnim &a = anim(it);
        const float wv = viewW();
        const float minOff = wv * 0.11f + std::max(wv - 0.85f * kBtnW(), 0.0f);
        const float curve = a.curve < 0 ? 1 : a.curve;
        float off = minOff - wv * (0.035f * curve + 0.075f * a.hover.at(t) + a.push.at(t));
        off = std::clamp(off, 0.0f, wv - 0.15f * kBtnW());
        x = viewX() + off + 12 * K() + (1 - slideIn_.at(t)) * 500 * K();
        y = viewY() + it.y - scroll_ + 12 * K() + 18 * K() * a.spread.at(t);
    }

    // Cases du bas (capture de stable, 1080p) : Mode plus large, puis Mods, Au hasard, Options.
    void navRect(int i, float &x, float &y, float &w, float &h) const {
        static const float kx[4] = {315, 446, 553, 660}, kw[4] = {120, 103, 103, 105};
        x = kx[i] * K();
        w = kw[i] * K();
        y = kVH - 125 * K();
        h = 125 * K();
    }
    // Onglets (capture de stable) : 165 x 28 en 1080p, alignés à droite sous « Tri ».
    void sortRect(int i, float &x, float &y, float &w, float &h) const {
        w = 165 * K();
        h = 28 * K();
        x = kVW - 22 * K() - static_cast<float>(4 - i) * (w + 2 * K());
        y = 80 * K();
    }
    float searchW() const { return 520 * K(); }
    float searchH() const { return 66 * K(); }
    float searchX() const { return kVW - 10 * K() - searchW(); }
    float searchY() const { return 116 * K(); }

    // --- Données --------------------------------------------------------------------------

    void filter() {
        const auto &sets = game.ctx.library->sets();
        lastCount_ = sets.size();
        visible_.clear();
        const std::string q = fsu::toLower(query_);
        for (int i = 0; i < static_cast<int>(sets.size()); ++i) {
            const game::SetInfo &s = sets[static_cast<size_t>(i)];
            if (!q.empty()) {
                std::string hay = fsu::toLower(s.title + " " + s.artist + " " + s.creator + " " + s.tags + " " + s.source + " " +
                                               s.titleUnicode + " " + s.artistUnicode);
                for (const game::DiffInfo &d : s.diffs) hay += " " + fsu::toLower(d.version);
                if (hay.find(q) == std::string::npos) continue;
            }
            visible_.push_back(i);
        }
        // Tri choisi par les onglets.
        const int mode = game.ctx.settings->songSort;
        auto key = [&](int i) -> std::string {
            const game::SetInfo &s = sets[static_cast<size_t>(i)];
            if (mode == 1) return fsu::toLower(s.artist + " " + s.title);
            if (mode == 2) return fsu::toLower(s.creator + " " + s.title);
            return fsu::toLower(s.title + " " + s.artist);
        };
        auto hardest = [&](int i) {
            float m = 0;
            for (const game::DiffInfo &d : sets[static_cast<size_t>(i)].diffs) m = std::max(m, d.stars);
            return m;
        };
        if (mode == 3) std::stable_sort(visible_.begin(), visible_.end(), [&](int a, int b) { return hardest(a) < hardest(b); });
        else std::stable_sort(visible_.begin(), visible_.end(), [&](int a, int b) { return key(a) < key(b); });
        if (!visible_.empty() && std::find(visible_.begin(), visible_.end(), set_) == visible_.end()) {
            set_ = visible_[0];
            diff_ = 0;
            pending_ = true;
            changeTime_ = lz::ms(game.ctx);
        }
        if (!sets.empty() && set_ < static_cast<int>(sets.size()))
            diff_ = std::clamp(diff_, 0, static_cast<int>(sets[static_cast<size_t>(set_)].diffs.size()) - 1);
    }

    // Liste à plat : un bouton par set, remplacé par ses difficultés pour le set choisi.
    void rebuild() {
        const auto &sets = game.ctx.library->sets();
        items_.clear();
        float y = viewH() / 4;
        bool prevOpen = false;
        for (int s : visible_) {
            const bool open = s == set_;
            if (open) {
                const int nd = static_cast<int>(sets[static_cast<size_t>(s)].diffs.size());
                for (int d = 0; d < nd; ++d) {
                    if (d == 0) y += 0.1f * 144 * K();
                    items_.push_back({s, d, y});
                    y += kBtnH();
                }
                prevOpen = true;
            } else {
                if (prevOpen) y += 0.1f * 144 * K();
                items_.push_back({s, -1, y});
                y += kBtnH();
                prevOpen = false;
            }
        }
        lastHovered_ = -2;
    }

    int itemIndex(int s, int d) const {
        for (size_t i = 0; i < items_.size(); ++i)
            if (items_[i].set == s && (items_[i].diff == d || items_[i].diff < 0)) return static_cast<int>(i);
        return 0;
    }
    int visibleIndex() const {
        auto it = std::find(visible_.begin(), visible_.end(), set_);
        return it == visible_.end() ? 0 : static_cast<int>(it - visible_.begin());
    }
    int lastDiff(int s) const {
        const auto &sets = game.ctx.library->sets();
        return std::max(0, static_cast<int>(sets[static_cast<size_t>(s)].diffs.size()) - 1);
    }
    float targetScroll() const {
        if (items_.empty()) return 0;
        if (freeScroll_) return freeTarget_;
        const Item &it = items_[static_cast<size_t>(itemIndex(set_, diff_))];
        return it.y + 72 * K() - viewH() / 2;
    }
    void pick(int s, int d) {
        set_ = s;
        diff_ = d;
        freeScroll_ = false;
    }
    ItemAnim &anim(const Item &it) { return anims_[static_cast<uint64_t>(it.set) << 16 | static_cast<uint16_t>(it.diff + 1)]; }

    // --- Actions --------------------------------------------------------------------------

    void goBack() {
        Context &c = game.ctx;
        backFx_.trigger(c);
        c.play(c.sfx->uiBack);
        game.goTo(game.makeMainMenu(), false);
    }

    void navAction(int i) {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        navFx_[i].flash.fromTo(t, 1, 0, 150, Ease::None, 50);
        switch (i) {
        case 0: game.toast("Mode : osu! standard"); break;
        case 1:
            modsOpen_ = true;
            modsA_.go(t, 1, 300, Ease::OutQuint);
            game.logo.opacity.go(t, 0, 200, Ease::OutQuint);
            c.play(c.sfx->uiOpen, 0.7f);
            break;
        case 2:
            if (!visible_.empty()) {
                const int s = visible_[static_cast<size_t>(rand() % static_cast<int>(visible_.size()))];
                pick(s, lastDiff(s));
                c.play(c.sfx->uiOpen, 0.6f);
            }
            break;
        default: game.openOverlay(game.makeOptions()); break;
        }
    }

    void play() {
        Context &c = game.ctx;
        const auto &sets = c.library->sets();
        if (visible_.empty() || set_ >= static_cast<int>(sets.size())) return;
        game.select(set_, diff_, false);
        c.play(c.sfx->uiSelect);
        c.saveSettings();
        game.goTo(game.makePlayerLoader());
    }

    void typing(const UiInput &in) {
        if (!in.raw || !in.raw->keyboard.connected) return;
        const bool *k = in.raw->keyboard.keys;
        const bool shift = k[0xE1] || k[0xE5];
        bool changed = false;
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
            if (ch && query_.size() < 50) {
                query_ += ch;
                changed = true;
            }
        }
        for (int i = 0; i < 256; ++i) prevKeys_[i] = k[i];
        if (changed) {
            filter();
            rebuild();
        }
    }

    // --- Rendu ----------------------------------------------------------------------------

    // Texte dont on donne la ligne de base et la hauteur des capitales (comme McOsu).
    float capText(const gfx::Font &f, float x, float baseline, float capH, uint32_t color, const std::string &s, float maxW = 0,
                  gfx::Align a = gfx::Align::Left, bool shadow = true) {
        Context &c = game.ctx;
        const float size = capH / std::max(0.3f, f.capHeight());
        const std::string str = maxW > 0 ? f.ellipsize(size, s, maxW) : s;
        const float top = f.topForCenter(baseline - capH / 2, size);
        if (shadow) f.draw(*c.batch, x + 1, top + 1, size, gfx::rgba(0, 0, 0, static_cast<uint8_t>(((color >> 24) & 0xff) * 0.8f)), str.c_str(), a);
        return f.draw(*c.batch, x, top, size, color, str.c_str(), a);
    }

    void renderCarousel() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        b.pushClip(viewX() - 200 * K(), viewY(), kVW, bottomTop() - viewY());
        for (const Item &it : items_) {
            float x, y;
            buttonPos(it, x, y);
            if (y > kVH || y + kBtnH() < 0) continue;
            renderButton(it, x, y);
        }
        b.popClip();
        if (visible_.empty()) {
            const float cx = viewX() + viewW() / 2;
            if (c.library->scanning()) {
                char line[64];
                snprintf(line, sizeof(line), i18n::tr("Lecture des maps... (%d %%)"), static_cast<int>(c.library->scanProgress() * 100));
                c.fonts->semibold.drawShadow(b, cx, kVH / 2, 26 * K(), st::col::white, line, gfx::Align::Center);
            } else if (c.library->sets().empty()) {
                c.fonts->semibold.drawShadow(b, cx, kVH / 2 - 20 * K(), 30 * K(), hex(0xff5555), "Aucune map installée", gfx::Align::Center);
                c.fonts->semibold.drawShadow(b, cx, kVH / 2 + 22 * K(), 24 * K(), hex(0x00ff00), "Croix / Entrée : en télécharger",
                                             gfx::Align::Center);
            }
        }
    }

    void renderButton(const Item &it, float x, float y) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const game::SetInfo &s = c.library->sets()[static_cast<size_t>(it.set)];
        const bool isDiff = it.diff >= 0;
        const bool active = isDiff && it.set == set_ && it.diff == diff_;
        const float w = kVW - x + 40 * K(), h = kBtnH() - 6 * K(), r = 12 * K();
        y += 3 * K();
        const uint32_t base = active ? gfx::rgba(255, 255, 255, 235) : isDiff ? gfx::rgba(0, 141, 223) : gfx::rgba(232, 108, 12, 240);
        const uint32_t text = active ? st::col::black : st::col::white;
        // Bouton : ombre, fond coloré, reflet en haut, liseré.
        b.shadow(x, y + 3 * K(), w, h, r, 10 * K(), gfx::rgba(0, 0, 0, 110));
        b.roundRectGradient(x, y, w, h, r, gfx::Corners::vertical(lz::lighten(base, 0.12f), lz::darken(base, 0.12f)));
        b.roundRectGradient(x, y, w, h * 0.5f, r, gfx::Corners::vertical(alpha(st::col::white, 0.16f), alpha(st::col::white, 0)));
        b.roundRectBorder(x, y, w, h, r, 1.5f * K(), active ? gfx::rgba(255, 255, 255, 255) : gfx::rgba(255, 255, 255, 70));
        // Vignette 4:3 à gauche.
        const float thW = h * 4.0f / 3.0f;
        if (const gfx::Texture *th = game.thumbnail(it.set)) {
            const float ta = static_cast<float>(th->width) / static_cast<float>(th->height), aspect = 4.0f / 3.0f;
            float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
            if (ta > aspect) {
                const float cut = (1 - aspect / ta) / 2;
                u0 = cut, u1 = 1 - cut;
            } else {
                const float cut = (1 - ta / aspect) / 2;
                v0 = cut, v1 = 1 - cut;
            }
            b.roundRectTex(*th, x + 2 * K(), y + 2 * K(), thW, h - 4 * K(), r * 0.8f, st::col::white, u0, v0, u1, v1);
        } else {
            b.roundRect(x + 2 * K(), y + 2 * K(), thW, h - 4 * K(), r * 0.8f, gfx::rgba(0, 0, 0, 90));
        }
        float tx = x + thW + 0.02f * kBtnW();
        const float sy = h / 114.0f;  // repères verticaux de McOsu sur 120 px
        // Rang local (difficultés seulement).
        if (isDiff) {
            const game::DiffInfo &d = s.diffs[static_cast<size_t>(it.diff)];
            const std::vector<LocalScore> scores = game.scoresFor(Game::diffKey(s, d));
            if (!scores.empty()) {
                std::string rank = scores.front().rank;
                const bool silver = !rank.empty() && rank.back() == '+';
                if (silver) rank.pop_back();
                const float gh = 0.45f * h;
                st::rankLetter(c, rank, silver, tx + gh * 0.45f, y + h / 2, gh, 1, true);
                tx += gh * 0.9f + 0.0125f * kBtnW();
            }
        }
        // Dans le set ouvert, titre et artiste s'effacent sur les difficultés non choisies.
        const float textA = isDiff && !active ? 0.55f : 1.0f;
        const std::string title = s.shownTitle(c.settings->originalMetadata);
        const std::string artist = s.shownArtist(c.settings->originalMetadata);
        const float maxW = kVW - tx - 30 * K();
        capText(c.fonts->regular, tx, y + 35.4f * sy, 22 * sy, gfx::withAlpha(text, textA), title, maxW);
        capText(c.fonts->regular, tx, y + 58.7f * sy, 14 * sy, gfx::withAlpha(text, textA), artist + " // " + s.creator, maxW);
        if (!isDiff) return;
        const game::DiffInfo &d = s.diffs[static_cast<size_t>(it.diff)];
        capText(c.fonts->bold, tx, y + 85 * sy, 17 * sy, text, d.version, maxW);
        // Étoiles : pleines, partielle, puis vides jusqu'à 10.
        const float st0 = 24 * sy, cy = y + 0.85f * h;
        const float stars = d.stars >= 0 ? std::min(d.stars, 25.0f) : 0;
        const int full = static_cast<int>(stars);
        float sx = tx + 12 * sy;
        for (int i = 0; i < full; ++i) {
            const float sc = std::min(1.175f, 1 + 0.015f * static_cast<float>(i));
            lz::icon(c, fa::star, sx, cy, st0 * sc, text);
            sx += 30 * sy;
        }
        const float frac = stars - static_cast<float>(full);
        int drawn = full;
        if (frac > 0.05f) {
            lz::icon(c, fa::star, sx, cy, st0 * std::max(0.5f, frac), text);
            sx += 30 * sy;
            ++drawn;
        }
        b.setBlend(gfx::Blend::Additive);
        for (int i = drawn; i < 10; ++i) {
            lz::icon(c, fa::star, sx, cy, st0 * 0.6f, gfx::rgba(255, 255, 255, 30));
            sx += 30 * sy;
        }
        b.setBlend(gfx::Blend::Alpha);
        if (d.stars < 0) capText(c.fonts->semibold, sx + 6 * sy, cy + 7 * sy, 14 * sy, gfx::withAlpha(text, 0.6f), "?");
    }

    void renderTopBar() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        b.rect(0, 0, kVW, topH(), gfx::rgba(0, 0, 0, 215));
        b.rect(0, topH() - 3 * K(), kVW, 3 * K(), hex(0x3c8ce6));
        // « Tri » et la valeur courante, puis les onglets rouges (l'actif en blanc).
        float x, y, w, h;
        sortRect(0, x, y, w, h);
        const float dx = kVW - 22 * K() - 270 * K();
        c.fonts->italic.drawShadow(b, dx - 12 * K(), c.fonts->italic.topForCenter(57 * K(), 44 * K()), 44 * K(), hex(0x7cc4ff), "Tri",
                                   gfx::Align::Right);
        b.roundRect(dx, 42 * K(), 270 * K(), 31 * K(), 3 * K(), gfx::rgba(0, 0, 0, 255));
        b.roundRectBorder(dx, 42 * K(), 270 * K(), 31 * K(), 3 * K(), 1.5f * K(), st::col::white);
        c.fonts->regular.drawMid(b, dx + 10 * K(), 57.5f * K(), 25 * K(), st::col::white, kSorts[c.settings->songSort]);
        lz::icon(c, fa::chevron_down, dx + 270 * K() - 20 * K(), 57.5f * K(), 16 * K(), st::col::white);
        for (int i = 0; i < 4; ++i) {
            sortRect(i, x, y, w, h);
            const bool on = c.settings->songSort == i;
            const bool hv = game.input().mouseActive && game.input().hover(x, y, w, h);
            b.roundRect(x, y, w, h + 6 * K(), 4 * K(), on ? hex(0xffffff) : hv ? hex(0xe0405a) : hex(0xc81e3c));
            c.fonts->semibold.drawMid(b, x + w / 2, y + h / 2 + 2 * K(), 20 * K(), on ? st::col::black : st::col::white, kSorts[i],
                                      gfx::Align::Center);
        }
        if (visible_.empty()) return;
        const game::SetInfo &s = c.library->sets()[static_cast<size_t>(set_)];
        const game::DiffInfo &d = s.diffs[static_cast<size_t>(std::clamp(diff_, 0, static_cast<int>(s.diffs.size()) - 1))];
        const float maxW = kVW - 22 * K() - 4 * 167 * K() - 60 * K();
        // Lignes de la capture de stable (1080p) : titre, mappeur, durée, objets, attributs.
        b.triangle(24 * K(), 22 * K(), 20 * K(), hex(0x7cc4ff), 0, 0);
        b.triangle(24 * K(), 31 * K(), 20 * K(), hex(0x7cc4ff), 0, 0);
        const std::string title = s.shownArtist(c.settings->originalMetadata) + " - " + s.shownTitle(c.settings->originalMetadata) + " [" +
                                  d.version + "]";
        capText(c.fonts->regular, 54 * K(), 30 * K(), 22 * K(), st::col::white, title, maxW - 54 * K());
        capText(c.fonts->regular, 54 * K(), 52 * K(), 15 * K(), st::col::white, i18n::tr("Mappée par ") + s.creator, maxW - 54 * K());
        // Durée, BPM et objets (adaptés à la vitesse) ; rouge en DT, bleu clair en HT.
        const double rate = game.mods.rate();
        const uint32_t speedCol = rate > 1 ? hex(0xff7f7f) : rate < 1 ? hex(0xadd8e6) : st::col::white;
        char line[160];
        const int secs = static_cast<int>(d.lengthMs / rate / 1000);
        snprintf(line, sizeof(line), i18n::tr("Durée : %02d:%02d  BPM : %.0f  Objets : %d"), secs / 60, secs % 60, d.bpm * rate, d.objects);
        capText(c.fonts->semibold, 8 * K(), 75 * K(), 14 * K(), speedCol, line, maxW);
        snprintf(line, sizeof(line), i18n::tr("Cercles : %d  Sliders : %d  Spinners : %d"), d.circles, d.sliders, d.spinners);
        capText(c.fonts->semibold, 8 * K(), 96 * K(), 14 * K(), st::col::white, line, maxW);
        // Attributs (modifiés par EZ / HR) et étoiles.
        float cs = d.cs, ar = d.ar, od = d.od, hp = d.hp;
        if (game.mods.easy) cs *= 0.5f, ar *= 0.5f, od *= 0.5f, hp *= 0.5f;
        if (game.mods.hardRock)
            cs = std::min(10.0f, cs * 1.3f), ar = std::min(10.0f, ar * 1.4f), od = std::min(10.0f, od * 1.4f), hp = std::min(10.0f, hp * 1.4f);
        const uint32_t attrCol = game.mods.hardRock ? hex(0xff7f7f) : game.mods.easy ? hex(0xadd8e6) : st::col::white;
        if (d.stars >= 0)
            snprintf(line, sizeof(line), i18n::tr("CS:%.3g AR:%.3g OD:%.3g HP:%.3g Étoiles:%.2f"), cs, ar, od, hp, d.stars);
        else
            snprintf(line, sizeof(line), i18n::tr("CS:%.3g AR:%.3g OD:%.3g HP:%.3g Étoiles:?"), cs, ar, od, hp);
        capText(c.fonts->regular, 8 * K(), 115 * K(), 10 * K(), attrCol, line, maxW);
    }

    void renderSearch() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float L = 26 * K();
        const std::string shown = query_.empty() ? std::string(i18n::tr(game.textInputSupported() ? "Pavé tactile pour chercher !"
                                                                                                   : "Tape pour chercher !"))
                                                 : query_ + (game.textInputSupported() ? "" : "_");
        const float prefixW = c.fonts->semibold.measure(L, i18n::tr("Recherche : "));
        const float tw = prefixW + c.fonts->semibold.measure(L, shown.c_str());
        char line[96];
        if (visible_.empty()) snprintf(line, sizeof(line), "%s", i18n::tr("Aucun résultat. Échap pour effacer."));
        else snprintf(line, sizeof(line), i18n::tr("%zu maps trouvées"), visible_.size());
        const float w = std::max(tw, c.fonts->semibold.measure(L, line)) + L + 10 * K();
        const float h = query_.empty() ? 2.2f * L : 3.4f * L;
        const float x = kVW - 10 * K() - w, y = searchY();
        b.roundRect(x, y, w, h, 4 * K(), gfx::rgba(0, 0, 0, query_.empty() ? 110 : 160));
        const float ly = y + 0.6f * L;
        c.fonts->semibold.drawShadow(b, x + L / 2, ly, L, hex(0x00ff00), "Recherche : ");
        c.fonts->semibold.drawShadow(b, x + L / 2 + prefixW, ly, L, st::col::white, shown.c_str());
        if (!query_.empty()) c.fonts->semibold.drawShadow(b, x + L / 2, ly + 1.4f * L, L, st::col::white, line);
    }

    void renderLeaderboard() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        if (visible_.empty()) return;
        const game::SetInfo &s = c.library->sets()[static_cast<size_t>(set_)];
        const game::DiffInfo &d = s.diffs[static_cast<size_t>(std::clamp(diff_, 0, static_cast<int>(s.diffs.size()) - 1))];
        const std::vector<LocalScore> scores = game.scoresFor(Game::diffKey(s, d));
        // En-tête : liste déroulante « Classement local » (une seule source ici).
        b.roundRect(12 * K(), 165 * K(), 435 * K(), 33 * K(), 3 * K(), gfx::rgba(0, 0, 0, 200));
        b.roundRectBorder(12 * K(), 165 * K(), 435 * K(), 33 * K(), 3 * K(), 1.5f * K(), st::col::white);
        c.fonts->regular.drawMid(b, 20 * K(), 181.5f * K(), 26 * K(), st::col::white, "Classement local");
        lz::icon(c, fa::chevron_down, 425 * K(), 181.5f * K(), 16 * K(), st::col::white);
        const float x0 = 5 * K(), y0 = 210 * K(), rw = 530 * K(), rh = 75 * K();
        if (scores.empty()) {
            const float cy = y0 + 0.325f * (bottomTop() - y0);
            lz::icon(c, fa::trophy, x0 + rw / 2, cy - 40 * K(), 54 * K(), alpha(hex(0x02c3e5), 0.85f));
            c.fonts->semibold.drawShadow(b, x0 + rw / 2, cy + 6 * K(), 26 * K(), hex(0x02c3e5), "Aucun score !", gfx::Align::Center);
            return;
        }
        const UiInput &in = game.input();
        char line[128];
        for (size_t i = 0; i < scores.size(); ++i) {
            const float y = y0 + static_cast<float>(i) * (rh + 2 * K());
            if (y + rh > bottomTop()) break;
            const LocalScore &sc = scores[i];
            const bool hv = in.mouseActive && in.hover(x0, y, rw, rh);
            b.roundRect(x0 - 10 * K(), y, rw + 10 * K(), rh, 8 * K(), gfx::rgba(0, 0, 0, static_cast<uint8_t>(150 * (hv ? 1.0f : 0.5f) + 50)));
            std::string rank = sc.rank;
            const bool silver = !rank.empty() && rank.back() == '+';
            if (silver) rank.pop_back();
            // Avatar (joueur local), puis la lettre de rang.
            b.rect(x0 + 7 * K(), y + 6 * K(), rh - 12 * K(), rh - 12 * K(), gfx::rgba(30, 30, 30));
            lz::icon(c, fa::user, x0 + 7 * K() + (rh - 12 * K()) / 2, y + rh / 2, (rh - 12 * K()) * 0.55f, gfx::rgba(150, 150, 150));
            const float gx = x0 + rh + 2 * K(), gw = 0.7f * rh * 0.85f;
            st::rankLetter(c, rank, silver, gx + gw / 2, y + rh / 2, 0.62f * rh, 1, true);
            const float tx = gx + gw + 0.165f * rh * 0.5f;
            capText(c.fonts->regular, tx, y + 0.25f * rh + 0.17f * rh + 0.06f * rh, 0.25f * rh, st::col::white, "Joueur local");
            snprintf(line, sizeof(line), i18n::tr("Score : %s (%dx)"), st::grouped(sc.score).c_str(), sc.maxCombo);
            capText(c.fonts->regular, tx, y + 0.75f * rh + 0.08f * rh - 0.0625f * rh, 0.18f * rh, st::col::white, line);
            const float rx = x0 + rw - 0.025f * rw, T = rh / 3;
            if (!sc.mods.empty()) capText(c.fonts->semibold, rx, T / 2 + 0.25f * T + y + 0.135f * T, 0.45f * T, st::col::white, sc.mods, 0,
                                          gfx::Align::Right);
            snprintf(line, sizeof(line), "%.2f%%", sc.accuracy * 100);
            capText(c.fonts->semibold, rx, y + 1.5f * T + 0.25f * T, 0.45f * T, st::col::white, line, 0, gfx::Align::Right);
            if (sc.pp > 0) {
                snprintf(line, sizeof(line), "%.0fpp", sc.pp);
                capText(c.fonts->semibold, rx, y + 2.5f * T + 0.2f * T, 0.38f * T, hex(0xaaaaaa), line, 0, gfx::Align::Right);
            }
            // Score récent (moins de 96 h) : flèche et âge, juste à droite de la ligne.
            const long long age = static_cast<long long>(time(nullptr)) - sc.date;
            if (age >= 0 && age < 96 * 3600) {
                if (age < 3600) snprintf(line, sizeof(line), "%lldm", age / 60);
                else snprintf(line, sizeof(line), "%lldh", age / 3600);
                lz::icon(c, fa::chevron_up, x0 + rw + 14 * K(), y + rh * 0.38f, 0.22f * rh, hex(0x88ff88));
                c.fonts->semibold.drawMid(b, x0 + rw + 14 * K(), y + rh * 0.68f, 0.2f * rh, st::col::white, line, gfx::Align::Center);
            }
        }
    }

    void renderBottomBar() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = lz::ms(c);
        const float y = bottomTop();
        b.rect(0, y, kVW, kVH - y, gfx::rgba(0, 0, 0, 235));
        // Liseré du haut : dégradé violet, rose, vert, comme le bas de la sélection de stable.
        b.roundRectGradient(0, y, kVW * 0.5f, 3 * K(), 0, gfx::Corners::horizontal(hex(0x7b4ad8), hex(0xe0408a)));
        b.roundRectGradient(kVW * 0.5f, y, kVW * 0.5f, 3 * K(), 0, gfx::Corners::horizontal(hex(0xe0408a), hex(0x60c040)));
        // Cases grises Mode, Mods, Au hasard, Options ; touche de raccourci dans le coin.
        static const char *const kLabels[4] = {"Mode", "Mods", "Au hasard", "Options"};
        static const char *const kKeys[4] = {"", "F1", "F2", "F3"};
        for (int i = 0; i < 4; ++i) {
            float x, yy, w, h;
            navRect(i, x, yy, w, h);
            const float hv = std::max(navFx_[i].hover.at(t), navFx_[i].flash.at(t));
            const bool on = i == 1 && !game.mods.label().empty();
            const uint32_t box = lz::mix(i == 0 ? gfx::rgba(70, 70, 70) : gfx::rgba(59, 59, 59), gfx::rgba(110, 110, 110), hv);
            b.rect(x, yy + 2 * K(), w, h, box);
            b.rect(x, yy + 2 * K(), w, 2 * K(), on ? hex(0x99ee44) : gfx::rgba(255, 255, 255, 30));
            const float cx = x + w / 2;
            if (i == 0) {
                b.ring(cx, yy + 48 * K(), 20 * K(), 4 * K(), st::col::white);
                b.circle(cx, yy + 48 * K(), 6 * K(), st::col::white);
                c.fonts->regular.drawShadow(b, cx, yy + 78 * K(), 27 * K(), st::col::white, kLabels[i], gfx::Align::Center);
            } else {
                // Libellé centré, sur deux lignes s'il le faut.
                const std::string lab = i18n::tr(kLabels[i]);
                const size_t sp = lab.find(' ');
                if (c.fonts->regular.measure(27 * K(), lab.c_str()) > w - 8 * K() && sp != std::string::npos) {
                    c.fonts->regular.drawShadow(b, cx, yy + 34 * K(), 26 * K(), st::col::white, lab.substr(0, sp).c_str(), gfx::Align::Center);
                    c.fonts->regular.drawShadow(b, cx, yy + 62 * K(), 26 * K(), st::col::white, lab.substr(sp + 1).c_str(), gfx::Align::Center);
                } else {
                    c.fonts->regular.drawShadow(b, cx, yy + 46 * K(), 27 * K(), st::col::white, lab.c_str(), gfx::Align::Center);
                }
                c.fonts->regular.draw(b, x + w - 5 * K(), yy + h - 26 * K(), 17 * K(), alpha(st::col::white, 0.8f), kKeys[i], gfx::Align::Right);
            }
        }
        // Mods actifs au-dessus du bouton Mods.
        const std::string mods = game.mods.label();
        if (!mods.empty()) {
            float x, yy, w, h;
            navRect(1, x, yy, w, h);
            c.fonts->bold.drawShadow(b, x + w / 2, yy - 22 * K(), 20 * K(), hex(0xffcc22), mods.c_str(), gfx::Align::Center);
        }
        // Carte du joueur.
        float ox, oy, ow, oh;
        navRect(3, ox, oy, ow, oh);
        const float uh = 112 * K(), uw = 450 * K();
        const float ux = std::max(885 * K(), ox + ow + 20 * K());
        if (ux + uw < logoX() - 200 * K()) st::userCard(c, stats_, ux, kVH - 4 * K() - uh, uw, uh);
        st::backButton(c, backFx_);
    }

    // --- Sélection des mods ----------------------------------------------------------------

    void modPos(int i, float &cx, float &cy) const {
        const ModDef &d = kMods[i];
        int col = 0;
        for (int k = 0; k < i; ++k)
            if (kMods[k].row == d.row) ++col;
        cx = kVW / 2 - 200 * K() + static_cast<float>(col) * 150 * K();
        cy = kVH * 0.3f + static_cast<float>(d.row) * 170 * K();
    }
    float modR() const { return 58 * K(); }

    void updateMods(const UiInput &in) {
        Context &c = game.ctx;
        const double t = lz::ms(c);
        auto close = [&]() {
            modsOpen_ = false;
            modsA_.go(t, 0, 250, Ease::OutQuint);
            game.logo.opacity.go(t, 1, 250, Ease::OutQuint);
            c.play(c.sfx->uiBack);
        };
        if (in.back || in.extra2) {
            close();
            return;
        }
        const ModDef &cur = kMods[modCursor_];
        int col = 0;
        for (int k = 0; k < modCursor_; ++k)
            if (kMods[k].row == cur.row) ++col;
        auto at = [&](int row, int cc) {
            int k = 0, last = 0;
            for (int i = 0; i < kModCount; ++i)
                if (kMods[i].row == row) {
                    if (k == cc) return i;
                    last = i;
                    ++k;
                }
            return last;
        };
        const int old = modCursor_;
        if (in.left && col > 0) modCursor_ = at(cur.row, col - 1);
        if (in.right) modCursor_ = at(cur.row, col + 1);
        if (in.up && cur.row > 0) modCursor_ = at(cur.row - 1, col);
        if (in.down && cur.row < 2) modCursor_ = at(cur.row + 1, col);
        if (old != modCursor_) {
            c.play(c.sfx->uiHover, 0.7f);
            modKeyboard_ = true;
        }
        bool toggle = in.confirm;
        for (int i = 0; i < kModCount; ++i) {
            float cx, cy;
            modPos(i, cx, cy);
            if (in.mouseActive && in.hover(cx - modR(), cy - modR(), 2 * modR(), 2 * modR())) {
                if (in.mouseMoved && modCursor_ != i) {
                    modCursor_ = i;
                    modKeyboard_ = false;
                }
                if (in.mouseClick) {
                    modCursor_ = i;
                    toggle = true;
                }
            }
        }
        // Boutons du bas : tout retirer, fermer.
        for (int i = 0; i < 2; ++i) {
            float x, y, w, h;
            modButtonRect(i, x, y, w, h);
            if (in.clicked(x, y, w, h)) {
                if (i == 0) {
                    game.mods = game::Mods{};
                    c.music->setRate(1, true);
                    c.play(c.sfx->uiBack, 0.8f);
                } else {
                    close();
                }
                return;
            }
        }
        if (toggle) {
            game::Mods &m = game.mods;
            const ModDef &d = kMods[modCursor_];
            m.*(d.flag) = !(m.*(d.flag));
            if (m.*(d.flag)) {
                const std::string a = d.acronym;
                if (a == "EZ") m.hardRock = false;
                if (a == "HR") m.easy = false;
                if (a == "DT") m.nightcore = m.halfTime = false;
                if (a == "NC") m.doubleTime = m.halfTime = false;
                if (a == "HT") m.doubleTime = m.nightcore = false;
                if (a == "AT") m.noFail = false;
                if (a == "NF") m.autoplay = false;
            }
            modFlash_[modCursor_].fromTo(t, 1, 0, 400, Ease::OutQuint);
            c.play(m.*(d.flag) ? c.sfx->uiSelect : c.sfx->uiBack, 0.8f);
            c.music->setRate(m.rate(), !m.nightcore);
        }
    }

    void modButtonRect(int i, float &x, float &y, float &w, float &h) const {
        w = 360 * K(), h = 64 * K();
        x = kVW / 2 + (i == 0 ? -w - 20 * K() : 20 * K());
        y = kVH * 0.3f + 2 * 170 * K() + 150 * K();
    }

    void renderMods() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = lz::ms(c);
        const float a = modsA_.at(t);
        b.pushAlpha(a);
        b.rect(0, 0, kVW, kVH, gfx::rgba(0, 0, 0, 200));
        c.fonts->light.drawShadow(b, kVW / 2, kVH * 0.06f, 60 * K(), st::col::white, "Sélection des mods", gfx::Align::Center);
        c.fonts->regular.drawShadow(b, kVW / 2, kVH * 0.06f + 74 * K(), 22 * K(), hex(0xdddddd),
                                    "Les mods changent la difficulté et le score de la partie.", gfx::Align::Center);
        char line[96];
        const float mult = game.mods.scoreMultiplier();
        snprintf(line, sizeof(line), i18n::tr("Multiplicateur de score : %.2fx"), mult);
        c.fonts->semibold.drawShadow(b, kVW / 2, kVH * 0.06f + 108 * K(), 24 * K(),
                                     mult > 1.001f ? hex(0xff7f7f) : mult < 0.999f ? hex(0x99ee66) : st::col::white, line, gfx::Align::Center);
        for (int row = 0; row < 3; ++row) {
            const float cy = kVH * 0.3f + static_cast<float>(row) * 170 * K();
            c.fonts->semibold.drawShadow(b, kVW / 2 - 300 * K(), c.fonts->semibold.topForCenter(cy, 26 * K()), 26 * K(), kModRowColours[row],
                                         kModRows[row], gfx::Align::Right);
        }
        for (int i = 0; i < kModCount; ++i) {
            const ModDef &d = kMods[i];
            float cx, cy;
            modPos(i, cx, cy);
            const bool on = game.mods.*(d.flag);
            const bool sel = i == modCursor_;
            // Badge : sélectionné, il grossit et s'incline, comme dans stable.
            const float sc = (on ? 1.12f : 1.0f) + (sel ? 0.06f : 0);
            const float ang = on ? -0.12f : 0;
            const float r = modR() * sc;
            const uint32_t mc = kModRowColours[d.row];
            b.shadow(cx - r, cy - r + 4 * K(), 2 * r, 2 * r, r, 12 * K(), gfx::rgba(0, 0, 0, 140));
            b.shape(1, cx, cy, r, r, r * 0.32f, 0, 0,
                    gfx::Corners::vertical(lz::lighten(on ? mc : lz::darken(mc, 1.2f), 0.25f), lz::darken(on ? mc : lz::darken(mc, 1.2f), 0.3f)),
                    0, ang);
            b.shape(1, cx, cy, r, r, r * 0.32f, 3 * K(), 0, gfx::Corners::solid(on ? st::col::white : alpha(st::col::white, 0.35f)), 0, ang);
            const float fl = modFlash_[i].at(t);
            if (fl > 0) {
                b.setBlend(gfx::Blend::Additive);
                b.shape(1, cx, cy, r, r, r * 0.32f, 0, 0, gfx::Corners::solid(alpha(st::col::white, 0.5f * fl)), 0, ang);
                b.setBlend(gfx::Blend::Alpha);
            }
            c.fonts->bold.drawMid(b, cx, cy, 34 * K() * sc, on ? st::col::white : alpha(st::col::white, 0.6f), d.acronym, gfx::Align::Center);
            if (sel && (modKeyboard_ || !game.input().mouseActive))
                b.shape(1, cx, cy, r + 8 * K(), r + 8 * K(), (r + 8 * K()) * 0.32f, 2.5f * K(), 0, gfx::Corners::solid(hex(0xffcc22)), 0, ang);
        }
        // Nom et description du mod survolé.
        const ModDef &d = kMods[modCursor_];
        c.fonts->bold.drawShadow(b, kVW / 2, kVH * 0.3f + 2 * 170 * K() + 70 * K(), 28 * K(), st::col::white, d.name, gfx::Align::Center);
        c.fonts->regular.drawShadow(b, kVW / 2, kVH * 0.3f + 2 * 170 * K() + 106 * K(), 22 * K(), hex(0xdddddd), d.desc, gfx::Align::Center);
        const UiInput &in = game.input();
        static const char *const kButtons[2] = {"Retirer tous les mods", "Fermer"};
        static const uint32_t kButtonCols[2] = {hex(0xcc3355), hex(0x3377cc)};
        for (int i = 0; i < 2; ++i) {
            float x, y, w, h;
            modButtonRect(i, x, y, w, h);
            const bool hv = in.mouseActive && in.hover(x, y, w, h);
            b.roundRectGradient(x, y, w, h, 10 * K(),
                                gfx::Corners::vertical(lz::lighten(kButtonCols[i], hv ? 0.45f : 0.2f), lz::darken(kButtonCols[i], 0.3f)));
            b.roundRectBorder(x, y, w, h, 10 * K(), 2 * K(), alpha(st::col::white, 0.7f));
            c.fonts->bold.drawMid(b, x + w / 2, y + h / 2, 26 * K(), st::col::white, kButtons[i], gfx::Align::Center);
        }
        b.popTransform();
    }

    std::vector<int> visible_;
    std::vector<Item> items_;
    std::unordered_map<uint64_t, ItemAnim> anims_;
    size_t lastCount_ = 0;
    int set_ = 0, diff_ = 0, lastHovered_ = -2;
    float scroll_ = 0, freeTarget_ = 0;
    bool freeScroll_ = false, pending_ = false, imeOpen_ = false;
    double changeTime_ = 0;
    std::string query_;
    bool prevKeys_[256] = {};
    Tween screenA_{0}, slideIn_{1}, pulse_{0}, modsA_{0}, modFlash_[kModCount];
    ButtonFx backFx_, navFx_[4];
    bool modsOpen_ = false, modKeyboard_ = false;
    int modCursor_ = 4;
    st::PlayerStats stats_;
};

}  // namespace

std::unique_ptr<Screen> makeStableSongSelect(Game &g) { return std::make_unique<StableSongSelect>(g); }

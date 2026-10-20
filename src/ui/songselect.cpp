// SPDX-License-Identifier: GPL-3.0-or-later
// Sélection de map, sur le modèle de la nouvelle SongSelect de lazer :
//  - à gauche, encart incliné (titre, artiste, statistiques, difficulté) puis le
//    classement local ;
//  - à droite, le carrousel : cartes de sets (72 u) et de difficultés (45 u),
//    décalées selon une courbe circulaire, positions amorties (50 ms) ;
//  - en haut à droite, la recherche ; en bas, le pied de page (retour rose,
//    Mods, Au hasard, Options) et le logo qui sert de bouton « Jouer ».
// Valeurs (tailles, durées, courbes) reprises du code de lazer, en unités u.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/game.h"
#include "core/i18n.h"
#include "core/fsutil.h"
#include "game/library.h"
#include "ui/lz.h"

namespace {

using namespace lz;

#define kSetH (72 * U)
#define kDiffH (45 * U)
#define kColW (kVW / 2)                           // colonnes gauche et droite : 50 % chacune
#define kFilterBottom (131 * U)  // bas de la zone de recherche
#define kModH (42 * U)
#define kModGap (7 * U)
#define kColPad (14 * U)
#define kColGap (24 * U)
#define kColHead (44 * U)
#define kModHeader (96 * U)
constexpr float kHalfH = kVH / 2;

const Palette &pal() {
    static const Palette p = palette(hue::blue);
    return p;
}

struct Item {
    int set = -1, diff = -1;  // diff = -1 : carte de set
    float y = 0, h = 0;       // position cible dans la liste
};

struct PanelAnim {
    float drawY = 0;
    bool placed = false;
    Tween x{0}, alpha{0}, chevron{0}, selection{0}, flash{0}, hover{0};
    bool hovered = false;
    int lastState = -1;
    long lastBeat = -1;
    Tween pulse{0};
};

// Mods proposés, rangés par colonne comme lazer.
struct ModDef {
    const char *acronym, *name, *desc;
    int type;  // 0 réduction, 1 augmentation, 2 automatisation
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
const char *const kModColumns[] = {"Réduction de difficulté", "Augmentation de difficulté", "Automatisation"};

class SongSelect : public Screen {
public:
    using Screen::Screen;
    bool songSelect() const override { return true; }

    void enter() override {
        Context &c = game.ctx;
        const double t = ms(c);
        set_ = std::max(0, game.selectedSet);
        diff_ = std::max(0, game.selectedDiff);
        filter();
        rebuild();
        scroll_ = targetScroll();
        // Arrivée : encarts depuis la gauche (-150), recherche depuis la droite, fondu.
        const double d = forward ? 0 : 0;
        wedgeX_.fromTo(t, -150 * U, 0, 600, Ease::OutQuint, d);
        wedgeA_.fromTo(t, 0, 1, 200, Ease::In, d);
        screenA_.fromTo(t, forward ? 1 : 0, 1, 300, Ease::OutQuint);
        focus_.snap(0);
        footerY_.fromTo(t, 75 * U, 0, 500, Ease::OutQuint);
        for (int i = 0; i < 4; ++i) footerBtnY_[i].fromTo(t, 100 * U, 0, 240, Ease::OutCubic, i * 30.0);
        // Le logo rejoint le coin bas droit (échelle 0,2 -> 0,4), après sa sortie du menu.
        logoAt_ = t + (forward ? 300 : 0);
        logoPending_ = true;
        game.toolbar.show(c, true);
        game.setBackground(0.1f, 0, 250, Ease::None);
        game.backgroundParallax = 1;
        if (game.currentSet()) game.select(set_, diff_, true);
        appearAt_ = t;
    }

    double leave() override {
        Context &c = game.ctx;
        const double t = ms(c);
        screenA_.go(t, 0, 300, Ease::OutQuint);
        if (forward) {
            // Vers la partie : les autres cartes s'écartent, les encarts partent.
            focus_.go(t, 1, 300, Ease::OutQuint);
            wedgeX_.go(t, -150 * U, 600, Ease::OutQuint);
            wedgeA_.go(t, 0, 200, Ease::In);
        } else {
            game.logo.scale.go(t, 0.2f, 120, Ease::Out);
            game.logo.opacity.go(t, 0, 120, Ease::Out);
        }
        footerY_.go(t, 75 * U, 500, Ease::OutQuint);
        return 600;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const double t = ms(c);
        if (logoPending_ && t >= logoAt_) {
            logoPending_ = false;
            Logo &logo = game.logo;
            logo.x.snap(logoX());
            logo.y.snap(logoY());
            logo.scale.fromTo(t, 0.2f, 0.4f, 240, Ease::OutQuint);
            logo.opacity.go(t, 1, 240, Ease::OutQuint);
            logo.interactive = true;
            logo.visualiser = false;
        }
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
        typing(in);
        if (modsOpen_) {
            updateMods(in);
            return;
        }
        // Sélection d'avant toute action (« au hasard » compris) : sert à détecter le changement.
        const int oldSet = set_, oldDiff = diff_;
        // Pied de page et bouton retour (souris).
        const float fy = footerTop();
        backFx_.setHover(c, in.hover(12 * U, kVH - 12 * U - kBackH, kBackW, kBackH));
        backFx_.setPressed(c, backFx_.hovered && in.raw && (in.raw->mouse.buttons & 1u));
        if (backFx_.hovered && in.mouseClick) {
            backFx_.trigger(c);
            goBack();
            return;
        }
        for (int i = 0; i < 3; ++i) {
            const float bx = footerBtnX(i);
            const bool h = in.hover(bx - kShear * kFooterBtnH * 0.5f, fy, kFooterBtnW, kFooterBtnH);
            footerFx_[i].setHover(c, h);
            if (h && in.mouseClick) footerAction(i);
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
        if (visible_.empty()) {
            if (in.confirm || game.logo.clicked()) {
                c.play(c.sfx->uiSelect);
                game.openOverlay(game.makeDownload());
            }
            return;
        }
        if (in.extra2) footerAction(0);
        // Recherche au clavier virtuel de la console : clic du pavé tactile ou de la barre.
        if (game.textInputSupported() && !imeOpen_ &&
            (in.touchpadClick || in.clicked(kColW, game.toolbar.height(), kColW, 60 * U))) {
            imeOpen_ = game.startTextInput("Rechercher une map", query_);
        }
        if (in.extra) footerAction(1);
        if (in.options) footerAction(2);

        const int pos = itemIndex(set_, diff_);
        const int n = static_cast<int>(items_.size());
        // Molette / pavé tactile : la liste défile librement (comme lazer), sans
        // changer la sélection ; flèches, manette et clic y reviennent.
        if (in.wheel != 0 && !items_.empty()) {
            if (!freeScroll_) freeTarget_ = scroll_;
            freeScroll_ = true;
            const float maxY = items_.back().y + items_.back().h;
            freeTarget_ = std::clamp(freeTarget_ - in.wheel * 140 * U, 0.0f, maxY);
        }
        if (in.down) {
            // Élément visible suivant ; une carte de set sélectionne sa difficulté.
            int k = pos;
            const int steps = 1;
            for (int s = 0; s < steps; ++s) {
                k = (k + 1) % n;
                if (items_[static_cast<size_t>(k)].diff < 0) {
                    // Saute la carte de set : sélectionne sa première difficulté.
                    pick(items_[static_cast<size_t>(k)].set, 0);
                    break;
                }
                pick(items_[static_cast<size_t>(k)].set, items_[static_cast<size_t>(k)].diff);
            }
        } else if (in.up) {
            const int pv = visibleIndex();
            if (diff_ > 0) {
                pick(set_, diff_ - 1);
            } else {
                const int prev = (pv - 1 + static_cast<int>(visible_.size())) % static_cast<int>(visible_.size());
                const int s = visible_[static_cast<size_t>(prev)];
                pick(s, static_cast<int>(sets[static_cast<size_t>(s)].diffs.size()) - 1);
            }
        }
        const int pv = visibleIndex();
        const int nv = static_cast<int>(visible_.size());
        if (in.right) pick(visible_[static_cast<size_t>((pv + 1) % nv)], 0);
        if (in.left) pick(visible_[static_cast<size_t>((pv - 1 + nv) % nv)], 0);
        if (in.r1) pick(visible_[static_cast<size_t>(std::min(nv - 1, pv + 8))], 0);
        if (in.l1) pick(visible_[static_cast<size_t>(std::max(0, pv - 8))], 0);
        if (in.r2) pick(visible_.back(), 0);
        if (in.l2) pick(visible_.front(), 0);

        // Souris dans le carrousel.
        bool playClicked = false;
        for (size_t i = 0; i < items_.size(); ++i) {
            const Item &it = items_[i];
            PanelAnim &pa = anim(it);
            const float x = panelX(it, pa), y = pa.drawY - scroll_ + kHalfH;
            const bool h = in.mouseActive && in.mouseX >= x && in.mouseY >= y && in.mouseY < y + it.h && in.mouseY > kFilterBottom &&
                           in.mouseY < fy;
            if (h != pa.hovered) {
                pa.hovered = h;
                pa.hover.go(t, h ? 1 : 0, h ? 100 : 1000, Ease::OutQuint);
            }
            if (h && in.mouseClick) {
                if (it.diff >= 0 && it.set == set_ && it.diff == diff_) playClicked = true;
                else pick(it.set, std::max(0, it.diff));
            }
        }
        if (set_ != oldSet || diff_ != oldDiff) {
            c.play(c.sfx->uiHover, 0.7f);
            changeTime_ = t;
            pending_ = true;
            if (set_ != oldSet) rebuild();
            anim(Item{set_, diff_}).flash.fromTo(t, 0.4f, 0, 1000, Ease::OutQuint);
        }
        // Le changement global (fond, musique) attend 150 ms (300 si une touche reste enfoncée).
        const bool held = in.raw && ((in.raw->pad.buttons & (plat::kPadUp | plat::kPadDown)) != 0);
        if (pending_ && t - changeTime_ > (held ? 300 : 150)) {
            pending_ = false;
            game.select(set_, diff_, true);
        }
        if (in.confirm || playClicked || game.logo.clicked()) play();
        scroll_ = anim::damp(scroll_, targetScroll(), 60, c.dt * 1000);
        for (Item &it : items_) {
            PanelAnim &pa = anim(it);
            pa.drawY = pa.placed ? anim::damp(pa.drawY, it.y, 50, c.dt * 1000) : it.y;
            pa.placed = true;
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const float sa = screenA_.at(t);
        if (sa <= 0.001f) return;
        b.pushAlpha(sa);
        // Dégradés noirs : à gauche (0,3 -> 0) et derrière le carrousel (0 -> 0,5).
        b.roundRectGradient(0, 0, kVW * 0.6f, kVH, 0, gfx::Corners::horizontal(alpha(col::black, 0.3f), alpha(col::black, 0)));
        b.roundRectGradient(kColW, 0, kColW, kVH, 0, gfx::Corners::horizontal(alpha(col::black, 0), alpha(col::black, 0.5f)));
        renderCarousel();
        renderFilter();
        if (!visible_.empty()) {
            renderTitleWedge();
            renderLeaderboard();
        } else {
            renderEmpty();
        }
        renderFooter();
        b.popTransform();
        if (modsOpen_ || modsA_.at(t) > 0.001f) renderMods();
    }

private:
    // --- Données -------------------------------------------------------------------------

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
        if (!visible_.empty() && std::find(visible_.begin(), visible_.end(), set_) == visible_.end()) {
            set_ = visible_[0];
            diff_ = 0;
            pending_ = true;
            changeTime_ = ms(game.ctx);
        }
        if (!sets.empty() && set_ < static_cast<int>(sets.size()))
            diff_ = std::clamp(diff_, 0, static_cast<int>(sets[static_cast<size_t>(set_)].diffs.size()) - 1);
    }

    // Liste à plat : cartes de sets, et difficultés du set ouvert ; espacements de lazer.
    void rebuild() {
        const auto &sets = game.ctx.library->sets();
        items_.clear();
        float y = 0;
        int prevKind = -1;  // 0 set fermé, 1 set ouvert, 2 difficulté
        for (int s : visible_) {
            const bool open = s == set_;
            const int kind = open ? 1 : 0;
            if (prevKind >= 0) y += gapBetween(prevKind, kind);
            items_.push_back({s, -1, y, kSetH});
            y += kSetH;
            prevKind = kind;
            if (!open) continue;
            const auto &diffs = sets[static_cast<size_t>(s)].diffs;
            for (int d = 0; d < static_cast<int>(diffs.size()); ++d) {
                y += gapBetween(prevKind, 2);
                items_.push_back({s, d, y, kDiffH});
                y += kDiffH;
                prevKind = 2;
            }
        }
        // Une carte qui apparaît part de la position de celle du dessus.
        for (size_t i = 0; i < items_.size(); ++i) {
            PanelAnim &pa = anim(items_[i]);
            if (!pa.placed && i > 0) {
                pa.drawY = anim(items_[i - 1]).drawY;
                pa.placed = true;
            }
        }
    }

    static float gapBetween(int a, int b) {
        if (a == 0 && b == 0) return 0;          // sets fermés : collés
        if (b == 1 || a == 1) return 6 * U + 3 * U;
        if (a == 2 && b == 2) return 3 * U;
        return 9 * U;                            // difficulté -> set suivant
    }

    int itemIndex(int s, int d) const {
        for (size_t i = 0; i < items_.size(); ++i)
            if (items_[i].set == s && items_[i].diff == d) return static_cast<int>(i);
        return 0;
    }
    int visibleIndex() const {
        auto it = std::find(visible_.begin(), visible_.end(), set_);
        return it == visible_.end() ? 0 : static_cast<int>(it - visible_.begin());
    }
    float targetScroll() const {
        if (items_.empty()) return 0;
        if (freeScroll_) return freeTarget_;
        const Item &it = items_[static_cast<size_t>(itemIndex(set_, diff_))];
        return it.y + it.h / 2;
    }
    void pick(int s, int d) {
        set_ = s;
        diff_ = d;
        freeScroll_ = false;  // la sélection reprend la main sur le défilement
    }
    PanelAnim &anim(const Item &it) { return anims_[static_cast<uint64_t>(it.set) << 16 | static_cast<uint16_t>(it.diff + 1)]; }

    float panelX(const Item &it, PanelAnim &pa) {
        const double t = ms(game.ctx);
        const bool selected = it.diff < 0 ? it.set == set_ : (it.set == set_ && it.diff == diff_);
        const bool kbd = it.set == set_ && it.diff == diff_;
        float off;
        if (it.diff < 0)
            off = 20 * U + 10 * U + (selected ? 0 : 100 * U) + (kbd ? 0 : 25 * U);
        else
            off = 60 * U + 10 * U + (selected ? 0 : 50 * U) + (kbd ? 0 : 25 * U);
        const int state = static_cast<int>(off);
        if (state != pa.lastState) {
            if (pa.lastState < 0) pa.x.snap(off);
            else pa.x.go(t, off, 400, Ease::OutQuint);
            pa.lastState = state;
        }
        // Courbe circulaire : les cartes s'écartent vers les bords haut et bas.
        const float cy = pa.drawY - scroll_ + kHalfH + it.h / 2;
        const float d = std::fabs(1 - cy / kHalfH);
        const float curve = (3 - std::sqrt(std::max(0.0f, 9 - d * d))) * kHalfH;
        // Pendant le départ vers la partie, les autres cartes glissent de 300 px.
        const float focus = selected || it.set == set_ ? 0 : 300 * focus_.at(t);
        return kColW + pa.x.at(t) + curve + focus;
    }

    // --- Actions -------------------------------------------------------------------------

    void goBack() {
        Context &c = game.ctx;
        c.play(c.sfx->uiBack);
        game.goTo(game.makeMainMenu(), false);
    }

    void footerAction(int i) {
        Context &c = game.ctx;
        footerFx_[i].trigger(c);
        switch (i) {
        case 0:
            modsOpen_ = true;
            modsA_.go(ms(c), 1, 400, Ease::OutQuint);
            game.logo.opacity.go(ms(c), 0, 200, Ease::OutQuint);
            for (int k = 0; k < 3; ++k) modColY_[k].fromTo(ms(c), 120 * U, 0, 500, Ease::OutQuint, k * 30.0);
            c.play(c.sfx->uiOpen, 0.7f);
            break;
        case 1:
            if (!visible_.empty()) {
                pick(visible_[static_cast<size_t>(rand() % static_cast<int>(visible_.size()))], 0);
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

    // --- Mods ----------------------------------------------------------------------------

    void updateMods(const UiInput &in) {
        Context &c = game.ctx;
        const double t = ms(c);
        auto close = [&]() {
            modsOpen_ = false;
            modsA_.go(t, 0, 300, Ease::OutQuint);
            game.logo.opacity.go(t, 1, 300, Ease::OutQuint);
            for (int k = 0; k < 3; ++k) modColY_[k].go(t, 60 * U, 300, Ease::OutQuint);
            c.play(c.sfx->uiBack);
        };
        if (in.back || in.extra2) {
            close();
            return;
        }
        // Grille : colonne = type, ligne = rang dans la colonne.
        int col = kMods[modCursor_].type, row = 0;
        for (int i = 0; i < modCursor_; ++i)
            if (kMods[i].type == col) ++row;
        auto at = [&](int cc, int rr) {
            int k = 0;
            for (int i = 0; i < kModCount; ++i)
                if (kMods[i].type == cc) {
                    if (k == rr) return i;
                    ++k;
                }
            for (int i = kModCount - 1; i >= 0; --i)
                if (kMods[i].type == cc) return i;
            return 0;
        };
        const int old = modCursor_;
        if (in.left && col > 0) modCursor_ = at(col - 1, row);
        if (in.right && col < 2) modCursor_ = at(col + 1, row);
        if (in.up) modCursor_ = at(col, std::max(0, row - 1));
        if (in.down) modCursor_ = at(col, row + 1);
        if (old != modCursor_) c.play(c.sfx->uiHover, 0.7f);
        bool toggle = in.confirm ? true : false;
        for (int i = 0; i < kModCount; ++i) {
            float x, y;
            modRect(i, x, y);
            if (in.hover(x, y, modW(), kModH)) {
                if (in.mouseMoved) modCursor_ = i;
                if (in.mouseClick) {
                    modCursor_ = i;
                    toggle = true;
                }
            }
        }
        if (in.mouseClick && (in.mouseY < modsTop() - 20 * U || in.mouseY > kVH - kFooterH - 20 * U)) {
            close();
            return;
        }
        if (toggle) {
            game::Mods &m = game.mods;
            const ModDef &d = kMods[modCursor_];
            m.*(d.flag) = !(m.*(d.flag));
            // Mods incompatibles entre eux.
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
            modFlash_[modCursor_].fromTo(t, 0.5f, 0, 600, Ease::OutQuint);
            c.play(m.*(d.flag) ? c.sfx->uiSelect : c.sfx->uiBack, 0.8f);
            // Aperçu au bon tempo, comme lazer.
            c.music->setRate(m.rate(), !m.nightcore);
        }
    }

    // Mise en page centrée : en-tête puis 3 colonnes, le tout au milieu de l'espace libre.
    float modW() const { return 300 * U; }
    float colW() const { return modW() + kColPad * 2; }
    float modsTop() const {
        const float blockH = kModHeader + kColHead + 4 * (kModH + kModGap) + kColPad;
        const float areaTop = game.toolbar.height(), areaBottom = kVH - kFooterH - 40 * U;
        return (areaTop + areaBottom) / 2 - blockH / 2;
    }
    float colX(int col) const {
        const float total = 3 * colW() + 2 * kColGap;
        return kVW / 2 - total / 2 + static_cast<float>(col) * (colW() + kColGap);
    }
    float colTop(int col) const { return modsTop() + kModHeader + modColY_[col].at(ms(game.ctx)); }
    void modRect(int i, float &x, float &y) const {
        const int col = kMods[i].type;
        int row = 0;
        for (int k = 0; k < i; ++k)
            if (kMods[k].type == col) ++row;
        x = colX(col) + kColPad;
        y = colTop(col) + kColHead + static_cast<float>(row) * (kModH + kModGap);
    }

    void renderMods() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const float a = modsA_.at(t);
        b.pushAlpha(a);
        b.rect(0, 0, kVW, kVH, alpha(col::black, 0.82f));
        // En-tête centré : titre, explication, multiplicateur.
        const float top = modsTop();
        c.fonts->light.drawMid(b, kVW / 2, top + 18 * U, 34 * U, col::white, "mods", gfx::Align::Center);
        c.fonts->regular.drawMid(b, kVW / 2, top + 48 * U, 15 * U, pal().content2,
                                 "Les mods changent la façon de jouer. Certains changent aussi le score.", gfx::Align::Center);
        char line[64];
        const float mult = game.mods.scoreMultiplier();
        snprintf(line, sizeof(line), i18n::tr("Multiplicateur de score : x%.2f"), mult);
        const float lw = c.fonts->semibold.measure(15 * U, line) + 24 * U;
        b.roundRect(kVW / 2 - lw / 2, top + 64 * U, lw, 22 * U, 11 * U, pal().background5);
        c.fonts->semibold.drawMid(b, kVW / 2, top + 75 * U, 15 * U, mult > 1.001f ? col::red1 : mult < 0.999f ? col::lime1 : col::white,
                                  line, gfx::Align::Center);
        // Colonnes.
        for (int col = 0; col < 3; ++col) {
            int count = 0;
            for (int i = 0; i < kModCount; ++i) count += kMods[i].type == col;
            const float cx = colX(col), cy = colTop(col);
            const float ch = kColHead + static_cast<float>(count) * (kModH + kModGap) - kModGap + kColPad;
            b.shadow(cx, cy + 3 * U, colW(), ch, 10 * U, 10 * U, alpha(col::black, 0.35f));
            b.roundRect(cx, cy, colW(), ch, 10 * U, pal().background5);
            b.roundRect(cx, cy, colW(), 4 * U, 2 * U, modTypeColor(col));
            c.fonts->semibold.drawMid(b, cx + colW() / 2, cy + kColHead / 2 + 2 * U, 17 * U, col::white, kModColumns[col], gfx::Align::Center);
        }
        for (int i = 0; i < kModCount; ++i) {
            float x, y;
            modRect(i, x, y);
            const ModDef &d = kMods[i];
            const bool on = game.mods.*(d.flag);
            const bool sel = i == modCursor_;
            const uint32_t accent = modTypeColor(d.type);
            const float h = kModH, w = modW(), mid = y + h / 2;
            // Le contenu suit l'inclinaison du panneau.
            auto sx = [&](float yy) { return -kShear * (yy - mid); };
            b.shadow(x, y + 2 * U, w, h, 7 * U, 4 * U, alpha(col::black, 0.25f), kShear);
            b.roundRect(x, y, w, h, 7 * U, on ? accent : (sel ? pal().background2 : pal().background3), kShear);
            b.roundRect(x + 30 * U, y, w - 30 * U, h, 7 * U, on ? darken(accent, 0.7f) : (sel ? pal().background3 : pal().background4), kShear);
            const float fl = modFlash_[i].at(t);
            if (fl > 0) b.roundRect(x, y, w, h, 7 * U, alpha(col::white, fl * 0.3f), kShear);
            if (sel && !game.input().mouseActive) b.roundRectBorder(x - 2, y - 2, w + 4, h + 4, 8 * U, 2.5f, col::white, kShear);
            modIcon(c, d.acronym, x + 30 * U + 28 * U, mid, 22 * U, on ? col::white : accent);
            const float nameY = y + 15 * U, descY = y + 30 * U;
            c.fonts->semibold.drawMid(b, x + 92 * U + sx(nameY), nameY, 17 * U, col::white, d.name);
            const std::string desc = c.fonts->regular.ellipsize(12 * U, d.desc, w - 100 * U);
            c.fonts->regular.drawMid(b, x + 92 * U + sx(descY), descY, 12 * U, on ? alpha(col::white, 0.85f) : pal().content2, desc.c_str());
        }
        b.popTransform();
    }

    // --- Rendu ---------------------------------------------------------------------------

    void renderCarousel() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        // Ordre de dessin : les plus éloignées de la sélection d'abord.
        std::vector<size_t> order(items_.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        const float selY = items_.empty() ? 0 : items_[static_cast<size_t>(itemIndex(set_, diff_))].y;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t bb) {
            const float da = (items_[a].diff < 0 ? 2.0f : 1.0f) * 1e6f - std::fabs(items_[a].y - selY);
            const float db = (items_[bb].diff < 0 ? 2.0f : 1.0f) * 1e6f - std::fabs(items_[bb].y - selY);
            return da > db;
        });
        for (size_t idx : order) {
            const Item &it = items_[idx];
            PanelAnim &pa = anim(it);
            const float y = pa.drawY - scroll_ + kHalfH;
            if (y > kVH || y + it.h < 0) continue;
            const float x = panelX(it, pa);
            const float ap = anim::ease(Ease::OutQuint, static_cast<float>(std::clamp((t - appearAt_) / 400.0, 0.0, 1.0)));
            b.pushAlpha(ap);
            if (it.diff < 0)
                renderSetPanel(it, pa, x, y);
            else
                renderDiffPanel(it, pa, x, y);
            b.popTransform();
        }
    }

    void panelChrome(PanelAnim &pa, float x, float y, float w, float h, bool selected, uint32_t accent) {
        gfx::SpriteBatch &b = *game.ctx.batch;
        const float r = 10 * U;
        if (selected) {
            // Lueur creuse autour de la carte (couleur d'accent).
            b.shadow(x - 1, y - 1, w + 2, h + 2, r, 3 * U, alpha(accent, 0.8f));
        } else {
            b.shadow(x, y + U, w, h, r, 4 * U, alpha(col::black, 0.2f));
        }
        (void)pa;
    }

    void panelOverlays(PanelAnim &pa, float x, float y, float w, float h, bool selected, uint32_t accent) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const float r = 10 * U;
        b.setBlend(gfx::Blend::Additive);
        const float hv = pa.hover.at(t);
        if (hv > 0) b.roundRect(x, y, w, h, r, alpha(col::blue1, 0.1f * hv));
        if (selected) {
            // Calque de sélection : dégradé sur les 80 % de droite, pulsé au rythme.
            if (c.trackRunning && c.beatIndexEarly != pa.lastBeat) {
                pa.lastBeat = c.beatIndexEarly;
                double bl = c.beatLength;
                while (bl < 250) bl *= 2;
                pa.pulse.fromTo(t, pa.pulse.at(t), 0.8f, 40, Ease::Out);
                pa.pulse.then(0.4f, bl, Ease::Out);
            }
            const float p = c.trackRunning ? pa.pulse.at(t) : 0.5f;
            b.roundRectGradient(x + w * 0.2f, y, w * 0.8f, h, r, gfx::Corners::horizontal(alpha(accent, 0), alpha(accent, 0.5f * p)));
        }
        const float fl = pa.flash.at(t);
        if (fl > 0) b.roundRect(x, y, w, h, r, alpha(col::white, fl));
        b.setBlend(gfx::Blend::Alpha);
    }

    void renderSetPanel(const Item &it, PanelAnim &pa, float x, float y) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const game::SetInfo &s = c.library->sets()[static_cast<size_t>(it.set)];
        const bool open = it.set == set_;
        const float w = kVW + 10 * U - x, h = it.h, r = 10 * U;
        const uint32_t accent = hex(0x4ebfff);
        if (open != (pa.chevron.target() > 0.5f)) pa.chevron.go(t, open ? 1 : 0, open ? 600 : 400, open ? Ease::OutElasticQuarter : Ease::OutQuint);
        panelChrome(pa, x, y, w, h, open, accent);
        // Fente du chevron (blanche quand ouvert) puis contenu.
        const float slot = 18 * U * pa.chevron.at(t);
        if (slot > 0.5f) {
            b.roundRect(x, y, w, h, r, col::white);
            icon(c, fa::chevron_right, x + slot / 2 + 1, y + h / 2, 8 * U, pal().background5);
        }
        const float cx = x + slot;
        b.roundRectGradient(cx, y, w - slot, h, r, gfx::Corners::horizontal(pal().background3, pal().background4));
        if (const gfx::Texture *th = game.thumbnail(it.set)) {
            const float aspect = (w - slot) / h, ta = static_cast<float>(th->width) / static_cast<float>(th->height);
            const float cut = std::max(0.0f, (1 - ta / aspect) / 2);
            b.roundRectTex(*th, cx, y, w - slot, h, r, col::white, 0, cut, 1, 1 - cut);
            // Voile sombre dégradé (plus foncé à gauche pour le texte), contenu dans la carte.
            const float vw = w - slot;
            b.roundRectGradient(cx, y, vw * 0.45f, h, r, gfx::Corners::horizontal(alpha(col::black, 0.55f), alpha(col::black, 0.45f)));
            b.roundRectGradient(cx + vw * 0.45f - r, y, vw * 0.55f + r, h, r,
                                gfx::Corners::horizontal(alpha(col::black, 0.45f), alpha(col::black, 0.2f)));
        }
        panelOverlays(pa, cx, y, w - slot, h, open, accent);
        const float tx = cx + 15 * U;
        const std::string title = s.shownTitle(c.settings->originalMetadata);
        const std::string artist = s.shownArtist(c.settings->originalMetadata);
        // Trois lignes centrées sur leurs capitales : titre, artiste, pastilles.
        text(c, c.fonts->bold, tx, c.fonts->bold.topForCenter(y + 17 * U, 22 * U), 22 * U, col::white, title, gfx::Align::Left,
             kVW - tx - 40 * U, true);
        text(c, c.fonts->semibold, tx, c.fonts->semibold.topForCenter(y + 37 * U, 16 * U), 16 * U, col::white, artist,
             gfx::Align::Left, kVW - tx - 40 * U, true);
        // Statut (local) et pastilles de difficulté.
        const float pc = y + h - 14 * U;  // centre de la rangée
        const float py = pc - 7 * U;
        const float sw = c.fonts->bold.measure(11 * U, "LOCALE") + 14 * U;
        b.roundRect(tx, py, sw, 14 * U, 7 * U, alpha(col::black, 0.55f));
        c.fonts->bold.drawMid(b, tx + sw / 2, pc, 11 * U, pal().content2, "LOCALE", gfx::Align::Center);
        float dx = tx + sw + 5 * U;
        const size_t shown = std::min<size_t>(s.diffs.size(), 12);
        b.roundRect(dx, py, static_cast<float>(shown) * 8 * U + 7 * U, 14 * U, 7 * U, alpha(col::black, 0.55f));
        dx += 4 * U;
        for (size_t d = 0; d < shown; ++d) {
            const float st = s.diffs[d].stars;
            b.roundRect(dx, pc - 5 * U, 7 * U, 10 * U, 3.5f * U, st >= 0 ? starColor(st) : hex(0x999999));
            dx += 8 * U;
        }
        if (s.diffs.size() > 12) {
            char more[16];
            snprintf(more, sizeof(more), "+%zu", s.diffs.size() - 12);
            c.fonts->semibold.drawMid(b, dx + 6 * U, pc, 12 * U, alpha(col::white, 0.7f), more);
        }
    }

    void renderDiffPanel(const Item &it, PanelAnim &pa, float x, float y) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const game::SetInfo &s = c.library->sets()[static_cast<size_t>(it.set)];
        const game::DiffInfo &d = s.diffs[static_cast<size_t>(it.diff)];
        const bool sel = it.set == set_ && it.diff == diff_;
        const float w = kVW + 10 * U - x, h = it.h, r = 10 * U;
        const bool known = d.stars >= 0;
        const uint32_t sc = known ? starColor(d.stars) : hex(0x999999);
        panelChrome(pa, x, y, w, h, sel, sc);
        // Fente gauche couleur étoiles avec l'icône du mode.
        b.roundRect(x, y, w, h, r, sc);
        const float slot = 14 * U;
        b.circle(x + slot / 2 + 1 * U, y + h / 2, 4.5f * U, known ? starTextColor(d.stars) : col::black);
        const float cx = x + slot;
        b.roundRectGradient(cx, y, w - slot, h, r, gfx::Corners::horizontal(pal().background3, pal().background4));
        b.roundRectGradient(cx, y, w - slot, h, r, gfx::Corners::horizontal(alpha(sc, 0.25f), alpha(sc, 0)));
        panelOverlays(pa, cx, y, w - slot, h, sel, sc);
        float tx = cx + 6.5f * U;
        // Rang local (meilleur score).
        const std::vector<LocalScore> scores = game.scoresFor(Game::diffKey(s, d));
        if (!scores.empty()) {
            const LocalScore &best = scores.front();
            const uint32_t rc = rankColor(best.rank);
            b.roundRect(tx, y + h / 2 - 8 * U, 32 * U, 16 * U, 8 * U, rc);
            c.fonts->bold.drawMid(b, tx + 16 * U, y + h / 2, 12 * U, darken(rc, 2.5f), best.rank.c_str(), gfx::Align::Center);
            tx += 32 * U + 5 * U;
        }
        const float nameW = c.fonts->semibold.drawMid(b, tx, y + 13 * U, 16 * U, col::white, d.version.c_str());
        text(c, c.fonts->semibold, tx + nameW + 6 * U, c.fonts->semibold.topForCenter(y + 13 * U, 14 * U), 14 * U, pal().content2,
             i18n::tr("mappée par ") + s.creator, gfx::Align::Left,
             std::max(0.0f, kVW - (tx + nameW + 30 * U)));
        const float pw = starPill(c, tx, y + 24 * U, 15 * U, d.stars, 1, known);
        // Compteur d'étoiles (10 petites étoiles, remplies selon la note).
        const float full = known ? std::min(10.0f, d.stars) : 0;
        for (int k = 0; k < 10; ++k) {
            const float fill = std::clamp(full - static_cast<float>(k), 0.0f, 1.0f);
            const float ss = 8 * U * (0.6f + 0.4f * fill);
            icon(c, fa::star, tx + pw + 12 * U + static_cast<float>(k) * 11 * U, y + 24 * U + 7.5f * U, ss,
                 fill > 0 ? sc : alpha(col::white, 0.15f));
        }
    }

    void renderTitleWedge() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const game::SetInfo &s = c.library->sets()[static_cast<size_t>(set_)];
        const game::DiffInfo &d = s.diffs[static_cast<size_t>(std::clamp(diff_, 0, static_cast<int>(s.diffs.size()) - 1))];
        const float wx = wedgeX_.at(t), wa = wedgeA_.at(t);
        if (wa <= 0.001f) return;
        b.pushTransform(wx, 0, 1, wa);
        const float x = -20 * U, top = -20 * U;
        const float pad = 32 * U, tb = game.toolbar.height();
        const float h = tb + 20 * U + 250 * U;
        const float w = kColW + 20 * U;
        // Fond : Background5 opaque à 90 % sur 70 %, puis fondu ; reflet additif à gauche.
        b.roundRectGradient(x, top, w, h, 10 * U, gfx::Corners::horizontal(alpha(pal().background5, 0.9f), alpha(pal().background5, 0.6f)), kShear);
        b.roundRect(x, top, w * 0.7f, h, 10 * U, alpha(pal().background5, 0.9f), kShear);
        b.setBlend(gfx::Blend::Additive);
        b.roundRectGradient(x, top, w * 0.6f, h, 10 * U, gfx::Corners::horizontal(alpha(pal().background2, 0.5f), alpha(pal().background2, 0)), kShear);
        b.setBlend(gfx::Blend::Alpha);
        // Le contenu suit l'inclinaison : décalage selon la hauteur.
        auto sx = [&](float yy) { return x + pad + 20 * U - kShear * (yy - (top + h / 2)); };
        float y = top + 20 * U + tb + pad * 0.5f;
        // Pastille de statut.
        const float pw = c.fonts->bold.measure(14 * U, "LOCALE") + 12 * U;
        b.roundRect(sx(y + 9 * U), y, pw, 18 * U, 9 * U, alpha(col::black, 0.5f));
        c.fonts->bold.drawMid(b, sx(y + 9 * U) + pw / 2, y + 9 * U, 13 * U, pal().content2, "LOCALE", gfx::Align::Center);
        y += 18 * U + 4 * U;
        const std::string title = s.shownTitle(c.settings->originalMetadata);
        const std::string artist = s.shownArtist(c.settings->originalMetadata);
        text(c, c.fonts->bold, sx(y + 16 * U), y - 2 * U, 32 * U, col::white, title, gfx::Align::Left, w - pad * 3, true);
        y += 32 * U + 2 * U;
        text(c, c.fonts->semibold, sx(y + 9 * U), y, 18 * U, col::white, artist, gfx::Align::Left, w - pad * 3, true);
        y += 18 * U + 8 * U;
        // Statistiques : durée et BPM (adaptés au mod de vitesse), objets.
        const double rate = game.mods.rate();
        char bpm[48], len[32], objs[48];
        snprintf(bpm, sizeof(bpm), "%.0f", d.bpm * rate);
        snprintf(len, sizeof(len), "%d:%02d", static_cast<int>(d.lengthMs / rate / 1000) / 60, static_cast<int>(d.lengthMs / rate / 1000) % 60);
        snprintf(objs, sizeof(objs), "%d", d.objects);
        struct Stat {
            const char *glyph, *value;
        } stats[] = {{fa::clock, len}, {fa::music, bpm}, {fa::bullseye, objs}};
        float stx = sx(y + 15 * U);
        for (const Stat &st : stats) {
            const float vw = c.fonts->semibold.measure(18 * U, st.value);
            const float bw = 18 * U + 8 * U + vw + 20 * U;
            b.roundRect(stx, y, bw, 30 * U, 5 * U, alpha(col::black, 0.2f), kShear);
            icon(c, st.glyph, stx + 10 * U + 9 * U, y + 15 * U, 16 * U, pal().content2);
            c.fonts->semibold.drawMid(b, stx + 10 * U + 18 * U + 6 * U, y + 15 * U, 18 * U, pal().content2, st.value);
            stx += bw + 4 * U;
        }
        y += 30 * U + 10 * U;
        // Encart de difficulté : pastille d'étoiles, nom, auteur ; barres des attributs.
        const float dx = sx(y + 40 * U) - 8 * U, dw = w - pad * 2.2f, dh = 83 * U;
        const uint32_t sc = d.stars >= 0 ? starColor(d.stars) : hex(0x999999);
        b.roundRect(dx, y, dw, dh, 10 * U, sc, kShear);
        float rx = sx(y + 15 * U) + 4 * U;
        const float pillW = starPill(c, rx, y + 5 * U, 20 * U, d.stars, 1, d.stars >= 0);
        const uint32_t fg = d.stars >= 0 ? starTextColor(d.stars) : alpha(col::black, 0.75f);
        rx += pillW + 6 * U;
        const float rowMid = y + 15 * U, rowTop = c.fonts->semibold.topForCenter(rowMid, 16 * U);
        rx += c.fonts->semibold.draw(b, rx, rowTop, 16 * U, fg, d.version.c_str()) + 6 * U;
        rx += c.fonts->regular.draw(b, rx, rowTop, 16 * U, fg, "mappée par") + 5 * U;
        text(c, c.fonts->semibold, rx, rowTop, 16 * U, fg, s.creator, gfx::Align::Left, std::max(10.0f, dx + dw - rx - 20 * U));
        const float iy = y + 30 * U;
        b.roundRect(dx + 2 * U - kShear * 26.5f * U * 0, iy, dw - 4 * U, 53 * U - 2 * U, 8 * U, alpha(pal().background5, 0.92f), kShear);
        // Barres : cercles / sliders / spinners puis CS AR OD HP (modifiés par les mods).
        float cs = d.cs, ar = d.ar, od = d.od, hp = d.hp;
        if (game.mods.easy) cs *= 0.5f, ar *= 0.5f, od *= 0.5f, hp *= 0.5f;
        if (game.mods.hardRock) cs = std::min(10.0f, cs * 1.3f), ar = std::min(10.0f, ar * 1.4f), od = std::min(10.0f, od * 1.4f),
                                hp = std::min(10.0f, hp * 1.4f);
        if (rate != 1.0) {
            // AR et OD effectifs une fois la vitesse appliquée.
            const double preempt = ar < 5 ? 1200 + 600 * (5 - ar) / 5 : 1200 - 750 * (ar - 5) / 5;
            const double p2 = preempt / rate;
            ar = static_cast<float>(p2 > 1200 ? 5 - (p2 - 1200) / 120 : 5 + (1200 - p2) / 150);
            const double w300 = (80 - 6 * od) / rate;
            od = static_cast<float>((80 - w300) / 6);
        }
        struct Bar {
            const char *label;
            float value, max, base;
        };
        const float total = static_cast<float>(std::max(1, d.objects));
        const Bar bars[] = {{"Cercles", static_cast<float>(d.circles), total, static_cast<float>(d.circles)},
                            {"Sliders", static_cast<float>(d.sliders), total, static_cast<float>(d.sliders)},
                            {"CS", cs, 10, d.cs},
                            {"AR", ar, 10, d.ar},
                            {"OD", od, 10, d.od},
                            {"HP", hp, 10, d.hp}};
        float bx = sx(iy + 26 * U) + 26 * U;
        for (int i = 0; i < 6; ++i) {
            const Bar &br = bars[i];
            if (i == 2) bx += 24 * U;
            const float bw = 70 * U;
            c.fonts->semibold.draw(b, bx, iy + 8 * U, 12 * U, pal().content2, br.label);
            char v[16];
            if (i < 2) snprintf(v, sizeof(v), "%.0f", br.value);
            else snprintf(v, sizeof(v), "%.1f", br.value);
            const uint32_t vc = i >= 2 && br.value > br.base + 0.05f ? col::red1 : i >= 2 && br.value < br.base - 0.05f ? col::lime1 : col::white;
            c.fonts->semibold.draw(b, bx + bw, iy + 8 * U, 12 * U, vc, v, gfx::Align::Right);
            b.roundRect(bx, iy + 30 * U, bw, 2 * U, U, alpha(col::black, 0.5f));
            b.roundRect(bx, iy + 30 * U, bw * std::clamp(br.value / br.max, 0.0f, 1.0f), 2 * U, U, vc == col::white ? col::white : vc);
            bx += bw + 8 * U;
        }
        b.popTransform();
    }

    void renderLeaderboard() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const game::SetInfo &s = c.library->sets()[static_cast<size_t>(set_)];
        const game::DiffInfo &d = s.diffs[static_cast<size_t>(std::clamp(diff_, 0, static_cast<int>(s.diffs.size()) - 1))];
        const float wa = wedgeA_.at(t);
        if (wa <= 0.001f) return;
        b.pushTransform(wedgeX_.at(t), 0, 1, wa);
        const float top = game.toolbar.height() + 250 * U + 4 * U + 10 * U;
        const float x0 = 30 * U;
        // En-tête : onglet « Classement local ».
        const float tw = c.fonts->semibold.measure(16 * U, "Classement local") + 30 * U;
        b.roundRect(x0 - kShear * 0, top, tw, 26 * U, 13 * U, pal().background3, kShear);
        c.fonts->semibold.drawMid(b, x0 + tw / 2, top + 13 * U, 16 * U, col::white, "Classement local", gfx::Align::Center);
        const std::vector<LocalScore> scores = game.scoresFor(Game::diffKey(s, d));
        const std::string key = Game::diffKey(s, d);
        if (key != boardKey_) {
            boardKey_ = key;
            boardAt_ = t;
        }
        float y = top + 40 * U;
        if (scores.empty()) {
            const float a = std::min(1.0f, static_cast<float>((t - boardAt_ - 200) / 300));
            if (a > 0) {
                c.fonts->regular.draw(b, x0 + 10 * U, y + 10 * U, 16 * U, alpha(pal().content2, a), "Pas encore de score sur cette difficulté.");
                c.fonts->regular.draw(b, x0 + 10 * U, y + 34 * U, 14 * U, alpha(pal().light4, a),
#if defined(__PROSPERO__)
                                      "Tes scores restent sur la console.");
#else
                                      "Tes scores restent sur cet appareil.");
#endif
            }
        }
        const float maxY = footerTop() - 10 * U;
        char line[128];
        for (size_t i = 0; i < scores.size(); ++i) {
            const float rowH = 50 * U;
            if (y + rowH > maxY) break;
            // Apparition en cascade : 200 + 30 ms par ligne, depuis -20 u.
            const float k = anim::ease(Ease::OutQuint, static_cast<float>(std::clamp((t - boardAt_ - 200 - 30.0 * static_cast<double>(i)) / 300.0, 0.0, 1.0)));
            if (k <= 0) break;
            const LocalScore &sc = scores[i];
            const float rx = x0 - kShear * (y - top) * 0 + (1 - k) * -20 * U;
            const float rw = kColW - 80 * U;
            b.pushAlpha(k);
            b.roundRect(rx, y, rw, rowH, 10 * U, pal().background4, kShear);
            snprintf(line, sizeof(line), "#%zu", i + 1);
            c.fonts->bold.drawMid(b, rx + 28 * U, y + rowH / 2, 18 * U, pal().content2, line, gfx::Align::Center);
            const uint32_t rc = rankColor(sc.rank);
            b.roundRect(rx + 56 * U, y + rowH / 2 - 10 * U, 40 * U, 20 * U, 10 * U, rc, kShear);
            c.fonts->bold.drawMid(b, rx + 76 * U, y + rowH / 2, 15 * U, darken(rc, 2.5f), sc.rank.c_str(), gfx::Align::Center);
            snprintf(line, sizeof(line), "%lld", sc.score);
            c.fonts->light.drawMid(b, rx + 110 * U, y + 17 * U, 22 * U, col::white, line);
            if (sc.pp > 0)
                snprintf(line, sizeof(line), "%.0fpp   %.2f%%   %dx   %s", sc.pp, sc.accuracy * 100, sc.maxCombo, sc.mods.c_str());
            else
                snprintf(line, sizeof(line), "%.2f%%   %dx   %s", sc.accuracy * 100, sc.maxCombo, sc.mods.c_str());
            c.fonts->semibold.drawMid(b, rx + 112 * U, y + 36 * U, 13 * U, pal().content2, line);
            c.fonts->regular.drawMid(b, rx + rw - 20 * U, y + rowH / 2, 14 * U, pal().light4, relativeDate(sc.date).c_str(), gfx::Align::Right);
            b.popTransform();
            y += rowH + 4 * U;
        }
        b.popTransform();
    }

    static std::string relativeDate(long long date) {
        const long long now = static_cast<long long>(time(nullptr));
        const long long d = std::max(0LL, now - date);
        char buf[48];
        if (d < 3600) snprintf(buf, sizeof(buf), i18n::tr("il y a %lld min"), d / 60);
        else if (d < 86400) snprintf(buf, sizeof(buf), i18n::tr("il y a %lld h"), d / 3600);
        else snprintf(buf, sizeof(buf), i18n::tr("il y a %lld j"), d / 86400);
        return buf;
    }

    void renderFilter() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const float wa = wedgeA_.at(t);
        const float x = kColW + 20 * U - wedgeX_.at(t), top = -10 * U;
        const float w = kVW - x + 40 * U, h = kFilterBottom - top;
        b.pushAlpha(wa);
        b.roundRectGradient(x, top, w, h, 10 * U, gfx::Corners::horizontal(alpha(pal().background5, 0.6f), alpha(pal().background5, 0.9f)), kShear);
        const float bx = x + 18 * U, bw = kVW - bx - 30 * U;
        float y = game.toolbar.height() + 12 * U;
        b.roundRect(bx, y, bw, 42 * U, 7 * U, pal().background3, kShear);
        icon(c, fa::magnifying_glass, bx + 24 * U - kShear * 0, y + 21 * U, 15 * U, pal().light2);
        const bool empty = query_.empty();
        const std::string shown =
            empty ? (game.textInputSupported() ? "pavé tactile pour chercher..." : "tape pour chercher...") : query_ + (game.textInputSupported() ? "" : "_");
        c.fonts->semibold.drawMid(b, bx + 46 * U, y + 21 * U, 18 * U, empty ? pal().light4 : col::white, shown.c_str());
        char line[64];
        snprintf(line, sizeof(line), i18n::tr("%zu correspondance%s"), visible_.size(), visible_.size() > 1 ? "s" : "");
        c.fonts->regular.drawMid(b, bx + bw - 16 * U, y + 21 * U, 14 * U, col::orange1, line, gfx::Align::Right);
        y += 42 * U + 8 * U;
        // Mods actifs et vitesse.
        const std::string mods = game.mods.label();
        c.fonts->semibold.drawMid(b, bx + 4 * U, y + 12 * U, 14 * U, pal().content2, "Mods :");
        if (mods.empty()) {
            c.fonts->regular.drawMid(b, bx + 52 * U, y + 12 * U, 14 * U, pal().light4, "aucun  (Carré / F1)");
        } else {
            float mx = bx + 70 * U;
            size_t p = 0;
            while (p < mods.size()) {
                size_t e = mods.find(' ', p);
                if (e == std::string::npos) e = mods.size();
                const std::string acr = mods.substr(p, e - p);
                int type = 1;
                for (const ModDef &d : kMods)
                    if (acr == d.acronym || (acr == "AUTO" && std::string(d.acronym) == "AT")) type = d.type;
                modIcon(c, acr == "AUTO" ? "AT" : acr.c_str(), mx, y + 12 * U, 18 * U, modTypeColor(type));
                mx += 34 * U;
                p = e + 1;
            }
        }
        b.popTransform();
    }

    void renderEmpty() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float cx = kColW / 2;
        if (c.library->scanning()) {
            spinner(c, cx, kVH / 2 - 30 * U, 40 * U, 1);
            c.fonts->semibold.draw(b, cx, kVH / 2 + 10 * U, 20 * U, col::white, "Lecture des maps...", gfx::Align::Center);
            return;
        }
        icon(c, fa::compact_disc, cx, kVH / 2 - 60 * U, 60 * U, alpha(col::white, 0.3f));
        c.fonts->semibold.draw(b, cx, kVH / 2, 26 * U, col::white,
                               c.library->sets().empty() ? "Aucune map installée" : "Aucune map ne correspond", gfx::Align::Center);
        c.fonts->regular.draw(b, cx, kVH / 2 + 40 * U, 18 * U, pal().content2,
                              c.library->sets().empty() ? "Croix pour en télécharger" : "Rond pour effacer la recherche", gfx::Align::Center);
    }

    // --- Pied de page ----------------------------------------------------------------------

    float footerTop() const { return kVH - kFooterH + footerY_.at(ms(game.ctx)); }
    float footerBtnX(int i) const { return (12 + 240 + 60) * U + static_cast<float>(i) * (kFooterBtnW + 7 * U); }
    float logoX() const { return kVW - 76 * U; }
    float logoY() const { return kVH - 36 * U; }

    void renderFooter() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        const float fy = footerTop();
        b.rect(0, fy, kVW, kVH - fy + 2, pal().background5);
        const char *glyphs[3] = {fa::wand_magic_sparkles, fa::shuffle, fa::sliders};
        const char *labels[3] = {"Mods", "Au hasard", "Options"};
        const uint32_t accents[3] = {col::lime1, col::blue1, col::purple1};
        const int pads[3] = {static_cast<int>(Pad::Square), static_cast<int>(Pad::Triangle), static_cast<int>(Pad::Options)};
        for (int i = 0; i < 3; ++i) {
            // Barre des mods actifs au-dessus du bouton Mods.
            const float bx = footerBtnX(i), by = fy - kFooterBtnH + 10 * U + 25 * U + footerBtnY_[i].at(t);
            if (i == 0 && !game.mods.label().empty()) {
                const float mult = game.mods.scoreMultiplier();
                b.roundRect(bx + 4 * U, by - 30 * U - 5 * U, kFooterBtnW, 30 * U, 10 * U, pal().background3, kShear);
                b.roundRect(bx + 4 * U, by - 30 * U - 5 * U, kFooterBtnW * 0.35f, 30 * U, 10 * U, pal().background5, kShear);
                char m[16];
                snprintf(m, sizeof(m), "%.2fx", mult);
                c.fonts->bold.drawMid(b, bx + 4 * U + kFooterBtnW * 0.175f, by - 20 * U, 13 * U,
                                   mult > 1.001f ? col::red1 : mult < 0.999f ? col::lime1 : col::white, m, gfx::Align::Center);
                const std::string lab = game.mods.label();
                text(c, c.fonts->bold, bx + 4 * U + kFooterBtnW * 0.675f, c.fonts->bold.topForCenter(by - 20 * U, 13 * U), 13 * U, col::white, lab, gfx::Align::Center,
                     kFooterBtnW * 0.6f);
            }
            footerButton(c, footerFx_[i], bx, by, glyphs[i], labels[i], accents[i], i == 0 && modsOpen_, 1, pads[i]);
        }
        backButton(c, backFx_, 12 * U, kVH - 12 * U + footerY_.at(t), "retour");
    }

    void typing(const UiInput &in) {
        if (!in.raw || !in.raw->keyboard.connected || modsOpen_) return;
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

    std::vector<int> visible_;
    std::vector<Item> items_;
    std::unordered_map<uint64_t, PanelAnim> anims_;
    size_t lastCount_ = 0;
    int set_ = 0, diff_ = 0;
    float scroll_ = 0;
    bool freeScroll_ = false;  // défilement libre (molette) en cours
    float freeTarget_ = 0;
    double changeTime_ = 0, appearAt_ = 0, boardAt_ = 0;
    bool pending_ = false;
    std::string query_, boardKey_;
    bool prevKeys_[256] = {};
    Tween wedgeX_{0}, wedgeA_{0}, screenA_{0}, focus_{0}, footerY_{0};
    Tween footerBtnY_[4];
    ButtonFx backFx_, footerFx_[3];
    bool modsOpen_ = false;
    bool logoPending_ = false, imeOpen_ = false;
    double logoAt_ = 0;
    int modCursor_ = 4;
    Tween modsA_{0}, modColY_[3], modFlash_[kModCount];
};

}  // namespace

std::unique_ptr<Screen> Game::makeSongSelect() { return std::make_unique<SongSelect>(*this); }

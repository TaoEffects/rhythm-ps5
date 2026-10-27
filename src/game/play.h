// SPDX-License-Identifier: GPL-3.0-or-later
// Partie en cours : horloge musicale, jugements (cercles, sliders, spinners),
// score v1, précision, vie, et rendu du terrain et du HUD (style moderne).
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "app/context.h"
#include "game/beatmap.h"
#include "game/difficulty.h"
#include "game/skin.h"

namespace game {

struct Mods {
    bool noFail = false, easy = false, hardRock = false, hidden = false;
    bool autoplay = false;  // la map se joue toute seule (score non enregistré)
    bool doubleTime = false, nightcore = false, halfTime = false;  // NC = DT avec la hauteur qui monte
    double rate() const { return doubleTime || nightcore ? 1.5 : halfTime ? 0.75 : 1.0; }
    float scoreMultiplier() const;
    std::string label() const;  // "NF HD" etc.
};

struct PlayResult {
    long long score = 0;
    double accuracy = 1.0;
    int maxCombo = 0, n300 = 0, n100 = 0, n50 = 0, nMiss = 0;
    bool failed = false, completed = false;
    double unstableRate = 0, meanError = 0;
    double pp = 0, stars = 0;  // performance (calcul façon lazer) et étoiles avec mods
    int nGeki = 0, nKatu = 0;  // combos terminés tout en 300 / en 300 et 100 (écran de classement stable)
    bool fullCombo = false;
    std::vector<float> hpGraph;  // vie après chaque objet jugé
    std::string rank;
    Mods mods;
};

class Play {
public:
    bool start(Context &ctx, Beatmap map, const Mods &mods, const gfx::Texture *background);
    void stop(Context &ctx);
    // Renvoie false quand la partie est terminée (fin de map ou échec confirmé).
    void update(Context &ctx, const UiInput &in);
    void render(Context &ctx);
    // Storyboard dessiné sous / au-dessus des objets (nullptr : aucun).
    void setStoryboard(const class Storyboard *sb) { storyboard_ = sb; }

    void setPaused(Context &ctx, bool paused);
    bool paused() const { return paused_; }
    bool finished() const { return finished_; }
    bool failed() const { return failed_; }
    bool canSkip() const;
    // Thème classique : le bouton « Passer » du coin bas droit est-il sous ce point ?
    bool skipButtonHit(float x, float y) const;
    void skip(Context &ctx);
    const PlayResult &result() const { return result_; }
    const Beatmap &map() const { return map_; }
    double progress() const;

private:
    struct SliderEvent {
        double time;
        int kind;  // 0 tick, 1 retour, 2 fin
        int span;
    };
    struct State {
        bool headDone = false;
        int headResult = -1;  // -1 en attente, 0 raté, 50/100/300
        bool done = false;
        int result = -1;
        double doneAt = 0;
        float errorMs = 0;
        double headAt = 0;
        // Slider
        std::vector<SliderEvent> events;
        size_t nextEvent = 0;
        int partsTotal = 1, partsHit = 0;
        bool tracking = false;
        // Spinner
        float rotation = 0, lastAngle = 0, rpm = 0;
        bool angleValid = false;
        float required = 1;
        int bonusShown = 0;
    };
    struct Popup {
        float x, y;
        double time;
        int result;
    };
    struct ErrorMark {
        float ms;
        double time;
    };

    double gameTime(Context &ctx) const;
    void judge(Context &ctx, size_t index, int result, double now, bool breaksCombo = true);
    void addScore(int value, bool comboIncrement);
    void comboBreak(Context &ctx);
    void playHitSound(Context &ctx, const HitObject &o, int soundBits, float x);
    void handlePress(Context &ctx, double t);
    void updateSlider(Context &ctx, size_t i, double t);
    void updateSpinner(Context &ctx, size_t i, double t, double dtMs, const UiInput &in);
    void finishResult();
    void autoplay(Context &ctx, double t);
    float toScreenX(float x) const { return fieldX_ + x * scale_; }
    float toScreenY(float y) const { return fieldY_ + y * scale_; }
    uint32_t comboColor(int index) const;
    void renderSliderBody(Context &ctx, const HitObject &o, float alpha, uint32_t color);
    void renderHud(Context &ctx, double t);
    void renderHudStable(Context &ctx, double t);  // thème classique
    void renderSkinnedObject(Context &ctx, size_t i, double t, float alpha, uint32_t color);
    void renderSkinnedCursor(Context &ctx);
    void skinSprite(Context &ctx, const SkinSprite &sp, float cx, float cy, float scale, uint32_t color, float angle = 0) const;
    void skinNumber(Context &ctx, int number, float cx, float cy, float scale, uint32_t color) const;
    const Skin *skin_ = nullptr;

    Beatmap map_;
    Mods mods_;
    std::vector<State> state_;
    const gfx::Texture *background_ = nullptr;
    const Storyboard *storyboard_ = nullptr;
    gfx::RenderTarget sliderTarget_;

    float cs_ = 5, ar_ = 5, od_ = 5, hpSetting_ = 5;
    float radius_ = 32;        // rayon en pixels virtuels
    float scale_ = 2.25f;      // pixels osu! -> virtuels
    float fieldX_ = 0, fieldY_ = 0;
    double preempt_ = 1200, fadeIn_ = 800, w300_ = 50, w100_ = 100, w50_ = 150;
    double startTime_ = 0;     // temps musical de départ (ms, peut être négatif)
    double firstObject_ = 0, lastObject_ = 0;
    size_t firstActive_ = 0;   // premier objet non terminé

    float cursorX_ = kVW / 2, cursorY_ = kVH / 2;
    std::deque<std::pair<float, float>> trail_;
    bool held_ = false;

    long long score_ = 0;
    double displayScore_ = 0;
    int combo_ = 0, maxCombo_ = 0;
    int n300_ = 0, n100_ = 0, n50_ = 0, nMiss_ = 0;
    double comboPopTime_ = -10;
    float hp_ = 1.0f, displayHp_ = 1.0f;
    double hpGainAt_ = -10;
    std::vector<float> hpTrace_;  // dernière remontée de la vie (marqueur de la barre stable)
    float difficultyMultiplier_ = 2;
    std::vector<float> errors_;
    int keyCount_[2] = {0, 0};
    unsigned keysHeld_ = 0;
    std::deque<Popup> popups_;
    std::deque<ErrorMark> errorMarks_;
    double lastT_ = 0;
    double lastWall_ = 0;

    bool paused_ = false, finished_ = false, failed_ = false;
    double failTime_ = -1, endTime_ = -1;
    PlayResult result_;
    std::vector<DifficultyAttributes> timed_;  // difficulté après chaque objet (pp en direct)
    float displayPp_ = 0;
    double livePp() const;
    DifficultyMods difficultyMods() const;
};

}  // namespace game

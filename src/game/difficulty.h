// SPDX-License-Identifier: GPL-3.0-or-later
// Difficulté (étoiles) et performance (pp) osu!standard, réécrites d'après les
// algorithmes d'osu!lazer (ppy/osu, licence MIT) : aim (avec et sans sliders),
// vitesse, puis formule de pp. Le bonus de rythme de la vitesse est simplifié :
// les valeurs sont proches de celles d'osu!, sans être exactes à la décimale.
#pragma once

#include <vector>

#include "game/beatmap.h"

namespace game {

struct DifficultyMods {
    double rate = 1.0;  // DT 1.5, HT 0.75
    bool hardRock = false, easy = false, hidden = false;
};

struct DifficultyAttributes {
    double stars = 0, aim = 0, speed = 0;
    double sliderFactor = 1;     // aim sans sliders / aim avec
    double speedNoteCount = 0;
    double ar = 0, od = 0;       // ajustés à la vitesse
    int maxCombo = 0, circles = 0, sliders = 0, spinners = 0;
};

struct ScoreStats {
    int n300 = 0, n100 = 0, n50 = 0, nMiss = 0, maxCombo = 0;
};

// Attributs de la map entière.
DifficultyAttributes computeDifficulty(const Beatmap &map, const DifficultyMods &mods);
// Attributs après chaque objet (compteur de pp en direct) : out[i] = map tronquée à i+1 objets.
std::vector<DifficultyAttributes> computeTimedDifficulty(const Beatmap &map, const DifficultyMods &mods);
double computePp(const DifficultyAttributes &attr, const ScoreStats &score, const DifficultyMods &mods, bool noFail);

}  // namespace game

// SPDX-License-Identifier: GPL-3.0-or-later
// Skins au format osu! (stable) : un dossier (ou une archive .osk importée) avec
// skin.ini, des images (hitcircle.png, approachcircle.png, default-0.png...) et
// des sons. Aucun skin n'est fourni : l'utilisateur dépose les siens (FTP) dans
// le dossier « skins » ; ils sont importés et proposés dans les options.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "render/batch.h"

namespace game {

struct SkinSprite {
    gfx::Texture tex;
    float scale = 1;  // 0,5 pour une image @2x
    bool ok() const { return tex.id != 0; }
    float w() const { return static_cast<float>(tex.width) * scale; }
    float h() const { return static_cast<float>(tex.height) * scale; }
};

// Éléments d'interface d'un skin, utilisés par le thème classique (stable) quand
// le skin les fournit ; sinon le jeu les dessine lui-même.
struct SkinUi {
    SkinSprite scoreDigits[10], scoreComma, scoreDot, scorePercent, scoreX;
    SkinSprite comboDigits[10], comboX;
    int scoreOverlap = 0, comboOverlap = 0;
    SkinSprite ranking[8], rankingSmall[8];  // XH SH X S A B C D
    SkinSprite hit300g, hit100k;
    SkinSprite pauseContinue, pauseRetry, pauseBack, pauseOverlay, failBackground;
    SkinSprite menuBack;
    SkinSprite selectionMode, selectionModeOver, selectionMods, selectionModsOver, selectionRandom, selectionRandomOver,
        selectionOptions, selectionOptionsOver;
    SkinSprite scorebarBg, scorebarColour;
    SkinSprite rankingPanel, rankingPerfect, rankingTitle, rankingRetry, rankingReplay, rankingGraph, rankingMaxCombo,
        rankingAccuracy;
    std::vector<SkinSprite *> all();
};

// Index des lettres de rang dans SkinUi::ranking : SS argent, S argent, SS, S, A, B, C, D.
int rankIndex(const std::string &rank, bool hidden);

class Skin {
public:
    ~Skin() { unload(); }
    bool load(const std::string &dir, const std::string &name);
    void unload();
    bool loaded() const { return loaded_; }
    const std::string &name() const { return name_; }
    const std::string &dir() const { return dir_; }

    // skin.ini
    std::vector<uint32_t> comboColours;
    uint32_t sliderBorder = 0xFFFFFFFFu;
    bool hasTrackOverride = false;
    uint32_t sliderTrackOverride = 0;
    bool overlayAboveNumber = true;
    int hitCircleOverlap = -2;
    bool cursorExpand = true, cursorCentre = true, cursorRotate = true;

    SkinSprite hitcircle, hitcircleoverlay, approachcircle, sliderstartcircle, sliderstartcircleoverlay, reversearrow,
        sliderfollowcircle, sliderscorepoint, cursor, cursortrail, cursormiddle;
    SkinSprite spinnerCircle, spinnerBackground, spinnerApproach, spinnerTop, spinnerBottom, spinnerMiddle, spinnerMiddle2,
        spinnerClear, spinnerSpin;
    SkinSprite hit0, hit50, hit100, hit300;
    std::vector<SkinSprite> sliderb;
    SkinSprite digits[10];
    SkinUi ui;

private:
    SkinSprite sprite(const std::string &base);
    void parseIni(const std::string &text);
    std::string resolve(const std::string &file) const;

    std::string dir_, name_;
    std::vector<std::string> files_;  // noms réels (pour retrouver la casse)
    bool loaded_ = false;
    std::string numberPrefix_ = "default", scorePrefix_ = "score", comboPrefix_;
};

// Importe les .osk du dossier (puis les retire) ; renvoie la liste des skins disponibles.
std::vector<std::string> scanSkins(const std::string &skinsDir);
// Décompresse une archive .osk dans skinsDir/<nom> (noms de fichiers en minuscules).
bool importOsk(const std::string &oskPath, const std::string &skinsDir, std::string &name);

}  // namespace game

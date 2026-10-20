// SPDX-License-Identifier: GPL-3.0-or-later
// Réglages persistants (fichier texte clé=valeur dans le dossier inscriptible).
#pragma once

#include <string>

struct Settings {
    // Affichage
    int refreshHz = 120;           // 120 (si l'écran suit, sinon la console reste en 60) ou 60 forcé
    float backgroundDim = 0.75f;   // assombrissement du fond en jeu (0..1)
    bool showDebug = false;
    int language = 0;              // 0 langue de la console, 1 français, 2 anglais
    bool showHitErrorBar = true;
    // Son
    float masterVolume = 0.8f;
    float musicVolume = 0.7f;
    float effectsVolume = 0.8f;
    int offsetMs = 0;              // décalage universel (positif = notes plus tard)
    // Entrées
    float cursorSpeed = 1400.0f;   // stick : pixels/s à fond (écran 1920 px)
    float cursorCurve = 1.6f;      // courbe de réponse du stick
    bool touchpadAbsolute = true;  // pavé tactile = zone absolue (comme une tablette)
    // Divers
    std::string lastSetFolder;     // dernière map sélectionnée
    int lastDifficulty = 0;
    std::string mirror = "https://osu.direct";
    // Touches (codes HID) et boutons de manette qui servent à frapper.
    int keyHit1 = 0x1D, keyHit2 = 0x1B;  // Z et X
    unsigned padHitMask = 0x0;           // 0 = valeur par défaut (voir defaultPadHitMask)
    bool mouseButtonsInGame = true;
    // Affichage en jeu
    float cursorSize = 1.0f;
    bool cursorTrail = true;
    bool showHud = true;
    bool blurInGame = false;
    // Interface
    bool menuParallax = true;
    bool introEnabled = true;
    bool storyboard = true;        // storyboards des maps en jeu
    int frameLimit = 0;            // ordinateur : 0 V-Sync, sinon images/s max (-1 illimité)
    bool showFps = false;          // compteur d'images par seconde
    bool fullscreen = true;        // ordinateur : plein écran
    bool showPpCounter = true;     // compteur de pp en jeu
    bool originalMetadata = false;
    float uiScale = 1.0f;           // taille de l'interface (0,8 à 1,3)
    int inputMode = 0;              // 0 manette + clavier/souris, 1 manette, 2 clavier/souris
    int uiTheme = 0;                // 0 moderne (lazer) ; stable et classique à venir  // titres et artistes dans leur langue d'origine (japonais...)
    bool firstRunDone = false;     // maps de base proposées au premier lancement
    int version = 2;               // version du fichier (migrations des valeurs par défaut)
    int skin = 1;                  // style de jeu : 0 épuré (façon WhiteCat), 1 triangles (façon lazer), 2 skin osu!
    std::string skinName;          // skin osu! choisi (dossier dans « skins »)

    // Croix, Carré, Rond, L1, R1, L2, R2.
    static unsigned defaultPadHitMask() { return 1u | 2u | 4u | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7); }
    bool load(const std::string &path);
    bool save(const std::string &path) const;
};

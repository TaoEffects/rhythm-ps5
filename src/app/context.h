// SPDX-License-Identifier: GPL-3.0-or-later
// État partagé entre les écrans : rendu, polices, sons, réglages, bibliothèque.
#pragma once

#include <cstdint>
#include <string>

#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/synth.h"
#include "core/settings.h"
#include "platform/platform.h"
#include "render/batch.h"
#include "render/image.h"
#include "render/text.h"

namespace game {
class Library;
class Skin;
}

// Écran virtuel : hauteur fixe de 1080 unités, largeur selon le format de l'écran
// (1920 en 16:9, 1728 en 16:10, 1440 en 4:3, 2520 en 21:9) : rien n'est étiré.
constexpr float kVH = 1080.0f;
inline float kVW = 1920.0f;
inline void setVirtualAspect(float aspect) {
    const float w = kVH * aspect;
    kVW = w < 1440.0f ? 1440.0f : (w > 2520.0f ? 2520.0f : w);
}

struct Fonts {
    gfx::Font light, regular, semibold, bold;
    gfx::Font italic;  // Source Sans 3 demi-gras italique (thème classique)
    gfx::Font icons;  // Font Awesome Free (voir ui/icons.h)
};

struct Textures {
    gfx::Texture disc;      // disque plein anticrénelé
    gfx::Texture ring;      // anneau fin (cercle d'approche)
    gfx::Texture ringThick; // anneau épais (bord des cercles)
    gfx::Texture glow;      // point lumineux flou
    gfx::Texture arrow;     // flèche de retour des sliders
    gfx::Texture triangle;  // triangle (décor, lazer)
    gfx::Texture spinner;   // disque de spinner (dégradé radial)
    gfx::Texture circleTriangles;  // motif de triangles masqué en disque (cercles façon lazer)
};

// Entrées interprétées pour un écran (fronts montants et répétition automatique).
struct UiInput {
    bool up = false, down = false, left = false, right = false;  // navigation (avec répétition)
    bool confirm = false, back = false, options = false, extra = false, extra2 = false;
    bool l1 = false, r1 = false, l2 = false, r2 = false;
    bool anyKey = false;
    bool touchpadClick = false;  // clic du pavé tactile (recherche)
    // Jeu
    int hitPresses = 0;     // frappes depuis l'image précédente
    int key1Presses = 0, key2Presses = 0;  // dont touche 1 / touche 2 (compteur de touches)
    unsigned hitKeysHeld = 0;              // bit 0 : touche 1, bit 1 : touche 2
    bool hitHeld = false;   // au moins une touche de frappe enfoncée
    float cursorX = kVW / 2, cursorY = kVH / 2;
    float stickAngle = 0;   // direction du stick gauche (spinners)
    bool stickActive = false;
    bool mouseMoved = false, mouseClick = false, mouseRightClick = false;
    bool mouseActive = false;    // la souris a servi récemment : afficher son curseur, survol actif
    float mouseX = 0, mouseY = 0;  // position souris (écran virtuel)
    float wheel = 0;
    // Clic gauche dans un rectangle (et survol).
    bool hover(float x, float y, float w, float h) const {
        return mouseActive && mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;
    }
    bool clicked(float x, float y, float w, float h) const { return mouseClick && hover(x, y, w, h); }
    const plat::InputState *raw = nullptr;
};

struct Context {
    gfx::SpriteBatch *batch = nullptr;
    Fonts *fonts = nullptr;
    Textures *tex = nullptr;
    audio::Mixer *mixer = nullptr;
    audio::Music *music = nullptr;
    audio::SoundBank *sfx = nullptr;
    Settings *settings = nullptr;
    game::Library *library = nullptr;
    gfx::ImageLoader *images = nullptr;
    const game::Skin *skin = nullptr;  // skin osu! actif (nullptr : style intégré)
    std::string dataDir;       // dossier inscriptible (réglages, scores)
    std::string songsDir;      // dossier des maps
    double now = 0;            // horloge murale (s)
    // Synchronisation sur la musique en cours (pour les animations au rythme).
    double beatLength = 500;   // ms
    float beatProgress = 0;    // 0..1 dans le temps courant
    long beatIndex = 0;
    float beatPulse = 0;       // 1 juste après un temps, décroît
    bool kiai = false;
    long beatIndexEarly = 0;   // temps courant avec 60 ms d'avance (animations « sur » le temps)
    float amplitude = 0;       // niveau sonore de la musique (0..1)
    bool trackRunning = false;
    float dt = 0;

    void play(const audio::Sample &s, float gain = 1.0f) const { mixer->play(&s, gain); }
    void saveSettings() const;
    void applyVolumes() const;
};

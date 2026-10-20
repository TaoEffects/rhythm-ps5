// SPDX-License-Identifier: GPL-3.0-or-later
// Couche plateforme : tout ce qui differe entre la PS5 et le PC passe par ici.
// Le reste du code (rendu, audio, jeu) ne doit jamais inclure d'en-tete Sony ou SDL.
#pragma once

#include <string>

#include <cstdint>

namespace plat {

// Boutons logiques de la manette (independants du bit-mask Sony).
enum PadButton : uint32_t {
    kPadCross = 1u << 0,
    kPadCircle = 1u << 1,
    kPadSquare = 1u << 2,
    kPadTriangle = 1u << 3,
    kPadL1 = 1u << 4,
    kPadR1 = 1u << 5,
    kPadL2 = 1u << 6,
    kPadR2 = 1u << 7,
    kPadL3 = 1u << 8,
    kPadR3 = 1u << 9,
    kPadUp = 1u << 10,
    kPadDown = 1u << 11,
    kPadLeft = 1u << 12,
    kPadRight = 1u << 13,
    kPadOptions = 1u << 14,
    kPadTouchpad = 1u << 15,
};

struct PadState {
    bool connected = false;
    uint32_t buttons = 0;           // combinaison de PadButton
    uint32_t rawButtons = 0;        // valeur brute de la plateforme (affichee en debug)
    float lx = 0, ly = 0;           // stick gauche, -1..1
    float rx = 0, ry = 0;           // stick droit, -1..1
    float l2 = 0, r2 = 0;           // gachettes analogiques, 0..1
    int touchCount = 0;             // doigts sur le pave tactile
    float touchX[2] = {0, 0};       // coordonnees brutes du pave tactile
    float touchY[2] = {0, 0};
    uint64_t timestampUs = 0;       // horodatage de l'etat (0 si inconnu)
};

// Etat clavier indexe par code d'usage HID (0x04 = A, 0x1D = Z, 0x1B = X, ...).
struct KeyboardState {
    bool connected = false;
    bool keys[256] = {};
};

struct MouseState {
    bool connected = false;
    float x = 0, y = 0;             // en pixels de la fenetre
    uint32_t buttons = 0;           // bit 0 = gauche, bit 1 = droit, bit 2 = milieu
    float wheel = 0;                // crans de molette depuis le dernier appel (+ = vers le haut)
};

struct InputState {
    PadState pad;
    KeyboardState keyboard;
    MouseState mouse;
    bool quitRequested = false;     // fermeture de fenetre (PC)
};

struct InitParams {
    const char *title = "rhythm-ps5";
    int width = 1920;               // PS5 : 1920x1080, 2560x1440 ou 3840x2160
    int height = 1080;
    int refreshHz = 60;             // PS5 : 60 ou 120
    bool fullscreen = true;         // ordinateur : plein écran (fenêtré sans bordure)
};

// Appele depuis le thread audio : remplir `frames` trames stereo float (L, R) a 48 kHz.
using AudioCallback = void (*)(float *interleavedStereo, int frames, void *user);

constexpr int kAudioRate = 48000;

// Initialise la sortie video et un contexte OpenGL 3.3 core courant.
bool init(const InitParams &params);
void shutdown();

const char *name();                 // "ps5" ou "sdl2"
int width();
int height();
int refreshHz();                    // frequence effectivement obtenue (0 si inconnue)

void pollInput(InputState &out);
void swapBuffers();
// Synchronisation verticale (ordinateur ; sans effet sur PS5).
void setVsync(bool on);
// Plein écran (ordinateur ; Alt+Entrée bascule aussi). Sans effet sur PS5.
void setFullscreen(bool on);
bool fullscreen();

double nowSeconds();                // horloge monotone
void sleepSeconds(double seconds);

// Demarre un thread audio qui appelle `cb` par blocs de `framesPerBlock` trames.
bool audioStart(AudioCallback cb, void *user, int framesPerBlock);
void audioStop();
int audioBlockFrames();             // taille de bloc effectivement obtenue

// Dossier inscriptible pour la config et les logs ("/download0" sur PS5).
// Langue de la console (« fr », « en »...), pour choisir celle de l'interface.
const char *systemLanguage();
const char *writableDir();
// Dossier en lecture seule des donnees embarquees ("/app0/assets" sur PS5).
const char *assetsDir();

// Notification systeme (toast PS5). Sans effet sur PC.
void notify(const char *message);
// Clavier virtuel du système (PS5) : ouvre la saisie ; pollTextInput renvoie 1 quand
// le texte est validé (dans out), -1 si annulé, 0 sinon. textInputSupported() est
// faux sur PC (on tape directement au clavier).
bool textInputSupported();
bool startTextInput(const char *title, const std::string &initial);
int pollTextInput(std::string &out);

// Demande la fermeture propre de l'application.
void requestExit();

}  // namespace plat

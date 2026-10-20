// SPDX-License-Identifier: GPL-3.0-or-later
// Plateforme PC (Linux) : SDL2 pour la fenêtre, les entrées et l'audio. Sert à
// développer et tester sans la console ; une manette DualSense branchée au PC
// est gérée par SDL_GameController avec la même correspondance de boutons.
#define SDL_MAIN_HANDLED  // main() reste le point d'entrée (Windows)
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "core/log.h"
#include "platform/platform.h"
#include "render/gl.h"

namespace plat {
namespace {

SDL_Window *g_window = nullptr;
SDL_GLContext g_context = nullptr;
SDL_GameController *g_pad = nullptr;
SDL_AudioDeviceID g_audio = 0;
AudioCallback g_audioCb = nullptr;
void *g_audioUser = nullptr;
int g_audioBlock = 0;
int g_width = 0, g_height = 0;
bool g_exitRequested = false;

void audioThunk(void *, Uint8 *stream, int len) {
    const int frames = len / static_cast<int>(sizeof(float) * 2);
    g_audioCb(reinterpret_cast<float *>(stream), frames, g_audioUser);
}

float axis(SDL_GameControllerAxis a) {
    const float v = static_cast<float>(SDL_GameControllerGetAxis(g_pad, a)) / 32767.0f;
    return v < -1 ? -1 : (v > 1 ? 1 : v);
}

void openFirstPad() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            g_pad = SDL_GameControllerOpen(i);
            if (g_pad) {
                LOG("sdl2: manette ouverte: %s", SDL_GameControllerName(g_pad));
                return;
            }
        }
    }
}

}  // namespace

bool init(const InitParams &params) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        LOG("sdl2: SDL_Init a echoue: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Fenêtre à la moitié de la résolution demandée : le rendu travaille en
    // coordonnées virtuelles et s'adapte à la taille réelle.
    // RHYTHM_FULLSIZE=1 : taille réelle (captures d'écran de test).
    const char *full = getenv("RHYTHM_FULLSIZE");
    const bool testSize = full && *full == '1';
    const int div = testSize ? 1 : 2;
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (params.fullscreen && !testSize) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    int ww = params.width / div, wh = params.height / div;
    if (const char *size = getenv("RHYTHM_WINDOW")) sscanf(size, "%dx%d", &ww, &wh);  // tests : taille imposée
    g_window = SDL_CreateWindow(params.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    if (!g_window) {
        LOG("sdl2: SDL_CreateWindow a echoue: %s", SDL_GetError());
        return false;
    }
    g_context = SDL_GL_CreateContext(g_window);
    if (!g_context) {
        LOG("sdl2: contexte OpenGL 3.3 core impossible: %s", SDL_GetError());
        return false;
    }
    // Le jeu dessine son propre curseur : celui du système est masqué dans la fenêtre.
    SDL_ShowCursor(SDL_DISABLE);
#if defined(_WIN32)
    if (!gl::loadFunctions([](const char *name) { return SDL_GL_GetProcAddress(name); })) return false;
#endif
    SDL_GL_SetSwapInterval(1);
    SDL_GL_GetDrawableSize(g_window, &g_width, &g_height);
    openFirstPad();
    return true;
}

void shutdown() {
    audioStop();
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (g_context) SDL_GL_DeleteContext(g_context);
    if (g_window) SDL_DestroyWindow(g_window);
    g_pad = nullptr;
    g_context = nullptr;
    g_window = nullptr;
    SDL_Quit();
}

const char *name() { return "sdl2"; }
int width() { return g_width; }
int height() { return g_height; }

int refreshHz() {
    SDL_DisplayMode mode{};
    if (g_window && SDL_GetWindowDisplayMode(g_window, &mode) == 0) return mode.refresh_rate;
    return 0;
}

void pollInput(InputState &out) {
    float wheel = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_KEYDOWN:
            // Alt+Entrée : plein écran / fenêtre.
            if (e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT) && !e.key.repeat)
                setFullscreen(!fullscreen());
            break;
        case SDL_QUIT:
            out.quitRequested = true;
            break;
        case SDL_MOUSEWHEEL:
            wheel += e.wheel.preciseY;
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!g_pad) openFirstPad();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad))) {
                SDL_GameControllerClose(g_pad);
                g_pad = nullptr;
                LOG("sdl2: manette retiree");
            }
            break;
        default:
            break;
        }
    }
    if (g_exitRequested) out.quitRequested = true;
    SDL_GL_GetDrawableSize(g_window, &g_width, &g_height);

    // Clavier : les scancodes SDL suivent les codes d'usage HID, comme sur PS5.
    int numKeys = 0;
    const Uint8 *keys = SDL_GetKeyboardState(&numKeys);
    out.keyboard.connected = true;
    for (int i = 0; i < 256; ++i) out.keyboard.keys[i] = i < numKeys && keys[i];

    // Souris : coordonnées converties de la fenêtre vers les pixels du framebuffer.
    int mx = 0, my = 0, ww = 1, wh = 1;
    const Uint32 mb = SDL_GetMouseState(&mx, &my);
    SDL_GetWindowSize(g_window, &ww, &wh);
    out.mouse.connected = true;
    out.mouse.x = static_cast<float>(mx) * static_cast<float>(g_width) / static_cast<float>(ww > 0 ? ww : 1);
    out.mouse.y = static_cast<float>(my) * static_cast<float>(g_height) / static_cast<float>(wh > 0 ? wh : 1);
    out.mouse.wheel = wheel;
    out.mouse.buttons = (mb & SDL_BUTTON_LMASK ? 1u : 0u) | (mb & SDL_BUTTON_RMASK ? 2u : 0u) |
                        (mb & SDL_BUTTON_MMASK ? 4u : 0u);

    PadState &p = out.pad;
    p = PadState{};
    if (!g_pad) return;
    p.connected = true;
    struct Map {
        SDL_GameControllerButton b;
        uint32_t bit;
    };
    static const Map map[] = {
        {SDL_CONTROLLER_BUTTON_A, kPadCross},
        {SDL_CONTROLLER_BUTTON_B, kPadCircle},
        {SDL_CONTROLLER_BUTTON_X, kPadSquare},
        {SDL_CONTROLLER_BUTTON_Y, kPadTriangle},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, kPadL1},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, kPadR1},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, kPadL3},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, kPadR3},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, kPadUp},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, kPadDown},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, kPadLeft},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, kPadRight},
        {SDL_CONTROLLER_BUTTON_START, kPadOptions},
        {SDL_CONTROLLER_BUTTON_TOUCHPAD, kPadTouchpad},
    };
    for (const Map &m : map) {
        if (SDL_GameControllerGetButton(g_pad, m.b)) {
            p.buttons |= m.bit;
            p.rawButtons |= 1u << m.b;
        }
    }
    p.lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
    p.ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    p.rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    p.ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    p.l2 = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    p.r2 = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (p.l2 > 0.5f) p.buttons |= kPadL2;
    if (p.r2 > 0.5f) p.buttons |= kPadR2;
    // Pavé tactile (DualSense/DS4 via SDL) : coordonnées normalisées 0..1, mises à
    // l'échelle 1920x1080 pour ressembler aux valeurs brutes de la PS5.
    for (int f = 0; f < 2 && f < SDL_GameControllerGetNumTouchpadFingers(g_pad, 0); ++f) {
        Uint8 state = 0;
        float x = 0, y = 0, pressure = 0;
        if (SDL_GameControllerGetTouchpadFinger(g_pad, 0, f, &state, &x, &y, &pressure) == 0 && state) {
            p.touchX[p.touchCount] = x * 1919.0f;
            p.touchY[p.touchCount] = y * 1079.0f;
            p.touchCount++;
        }
    }
    p.timestampUs = static_cast<uint64_t>(nowSeconds() * 1e6);
}

void swapBuffers() { SDL_GL_SwapWindow(g_window); }
void setFullscreen(bool on) {
    if (g_window && fullscreen() != on) SDL_SetWindowFullscreen(g_window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
bool fullscreen() { return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0; }

void setVsync(bool on) {
    // V-Sync adaptatif si possible (-1), sinon classique.
    if (!on) SDL_GL_SetSwapInterval(0);
    else if (SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(1);
}

double nowSeconds() {
    static const Uint64 freq = SDL_GetPerformanceFrequency();
    static const Uint64 start = SDL_GetPerformanceCounter();
    return static_cast<double>(SDL_GetPerformanceCounter() - start) / static_cast<double>(freq);
}

void sleepSeconds(double seconds) {
    if (seconds > 0) std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

bool audioStart(AudioCallback cb, void *user, int framesPerBlock) {
    g_audioCb = cb;
    g_audioUser = user;
    SDL_AudioSpec want{}, have{};
    want.freq = kAudioRate;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = static_cast<Uint16>(framesPerBlock);
    want.callback = audioThunk;
    g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!g_audio) {
        LOG("sdl2: audio indisponible: %s", SDL_GetError());
        return false;
    }
    g_audioBlock = have.samples;
    LOG("sdl2: audio %d Hz, %d canaux, blocs de %d trames", have.freq, have.channels, have.samples);
    SDL_PauseAudioDevice(g_audio, 0);
    return true;
}

void audioStop() {
    if (g_audio) SDL_CloseAudioDevice(g_audio);
    g_audio = 0;
}

int audioBlockFrames() { return g_audioBlock; }

// Données (réglages, maps, skins) : RHYTHM_DATA, sinon le dossier de l'utilisateur
// (~/.local/share, ~/Library/Application Support, %APPDATA%).
const char *systemLanguage() {
    // Langue préférée de l'utilisateur (Windows, macOS, Linux) ; LANG en secours.
    static char lang[16] = "";
    if (!lang[0]) {
        if (SDL_Locale *locales = SDL_GetPreferredLocales()) {
            if (locales[0].language) std::snprintf(lang, sizeof(lang), "%s", locales[0].language);
            SDL_free(locales);
        }
        if (!lang[0]) {
            const char *env = getenv("LANG");
            std::snprintf(lang, sizeof(lang), "%s", env && *env ? env : "en");
        }
    }
    return lang;
}

const char *writableDir() {
    static std::string dir;
    if (dir.empty()) {
        const char *env = getenv("RHYTHM_DATA");
        if (env && *env) {
            dir = env;
        } else if (char *pref = SDL_GetPrefPath("rhythm", "rhythm-ps5")) {
            dir = pref;
            SDL_free(pref);
            while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
        } else {
            dir = ".";
        }
    }
    return dir.c_str();
}
bool textInputSupported() { return false; }
bool startTextInput(const char *, const std::string &) { return false; }
int pollTextInput(std::string &) { return 0; }
// Ressources : RHYTHM_ASSETS, sinon « assets » à côté de l'exécutable (paquets
// macOS / Windows), sinon celles du dépôt.
const char *assetsDir() {
    static std::string dir;
    if (dir.empty()) {
        const char *env = getenv("RHYTHM_ASSETS");
        if (env && *env) {
            dir = env;
        } else {
            if (char *base = SDL_GetBasePath()) {
                const std::string candidate = std::string(base) + "assets";
                SDL_free(base);
                if (FILE *f = fopen((candidate + "/fonts/Outfit-Regular.ttf").c_str(), "rb")) {
                    fclose(f);
                    dir = candidate;
                }
            }
            if (dir.empty()) dir = RHYTHM_ASSETS_DIR;
        }
    }
    return dir.c_str();
}

void notify(const char *message) { LOG("notification: %s", message); }

void requestExit() { g_exitRequested = true; }

}  // namespace plat

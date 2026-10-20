// SPDX-License-Identifier: GPL-3.0-or-later
// Plateforme PS5 (application native en dossier, lancée comme un titre installé).
//  - vidéo : EGL + OpenGL 3.3 core du SDK ps5-opengl (Mesa sur le vrai GPU) ;
//  - manette : ScePad (lecture de l'état courant à chaque image) ;
//  - clavier et souris USB : SceKeyboard / SceMouse, chargés à la demande ;
//  - audio : SceAudioOut, 48 kHz, thread dédié qui bloque sur sceAudioOutOutput.
// Les structures Sony ci-dessous reprennent celles du fork SDL de ps5-payload-dev
// (src/joystick/ps5/SDL_ps5joystick.h, src/video/ps5/SDL_ps5keyboard.c), déjà
// utilisées par plusieurs portages ; les static_assert en verrouillent la taille.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <ps5_opengl_display_modes.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/fsutil.h"
#include "core/log.h"
#include "core/thread.h"
#include "platform/platform.h"
#include "render/gl.h"

namespace {

// --- ABI Sony (sous-ensemble) ----------------------------------------------------

struct SonyPadTouch {
    uint16_t x;
    uint16_t y;
    uint8_t finger;
    uint8_t pad[3];
};

struct SonyPadData {
    uint32_t buttons;
    uint8_t leftX, leftY;
    uint8_t rightX, rightY;
    uint8_t l2, r2;
    uint16_t padding;
    float quat[4];
    float velocity[3];
    float acceleration[3];
    uint8_t touchFingers;
    uint8_t touchPad1[3];
    uint32_t touchPad2;
    SonyPadTouch touch[2];
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t connectedCount;
    uint8_t unknown[15];
};
static_assert(sizeof(SonyPadData) == 120, "taille de l'etat manette");
static_assert(offsetof(SonyPadData, touchFingers) == 0x34, "offset pave tactile");
static_assert(offsetof(SonyPadData, connected) == 0x4c, "offset connexion manette");

struct SonyPadColor {
    uint8_t r, g, b, a;
};

enum : uint32_t {
    kSonyL3 = 0x0002,
    kSonyR3 = 0x0004,
    kSonyOptions = 0x0008,
    kSonyUp = 0x0010,
    kSonyRight = 0x0020,
    kSonyDown = 0x0040,
    kSonyLeft = 0x0080,
    kSonyL2 = 0x0100,
    kSonyR2 = 0x0200,
    kSonyL1 = 0x0400,
    kSonyR1 = 0x0800,
    kSonyTriangle = 0x1000,
    kSonyCircle = 0x2000,
    kSonyCross = 0x4000,
    kSonySquare = 0x8000,
    kSonyTouchpad = 0x100000,
    kSonyIntercepted = 0x80000000,  // l'interface système a le focus
};

struct SonyKeyboardData {
    uint64_t junk0[2];
    uint8_t available;
    uint32_t junk1[2];
    uint32_t modifiers;
    uint16_t scankey[16];
    uint64_t junk3[4];
};
static_assert(offsetof(SonyKeyboardData, modifiers) == 28, "offset modificateurs clavier");
static_assert(offsetof(SonyKeyboardData, scankey) == 32, "offset touches clavier");

// SceMouseData (identique PS4/PS5) : déplacements relatifs depuis la lecture précédente.
struct SonyMouseData {
    uint64_t timestamp;
    uint8_t connected;
    uint32_t buttons;   // 1 gauche, 2 droit, 4 milieu
    int32_t x, y;
    int32_t wheel;
    int32_t tilt;
    uint8_t reserved[8];
};
static_assert(sizeof(SonyMouseData) == 40, "taille de l'etat souris");
struct SonyMouseOpenParam {
    uint8_t behaviorFlag;
    uint8_t reserved[7];
};

struct SonyNotification {
    uint8_t reserved[45];
    char message[3075];
};

constexpr int kAudioPortMain = 0;
constexpr int kAudioFormatS16Stereo = 1;
constexpr int kUserSystem = 0xFF;

}  // namespace

extern "C" {
int sceUserServiceInitialize(void *);
int sceUserServiceGetInitialUser(int *);
int scePadInit(void);
int scePadOpen(int user, int type, int index, const void *param);
int scePadReadState(int handle, SonyPadData *data);
int scePadSetLightBar(int handle, const SonyPadColor *color);
int scePadClose(int handle);
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *res);
int sceKernelDlsym(int handle, const char *symbol, void **address);
const char *sceKernelGetFsSandboxRandomWord(void);
// Imports directs (bibliothèques de liaison du SDK), en secours si dlsym échoue :
// résolus au premier appel, une fois le module chargé.
int sceKeyboardInit(void);
int sceKeyboardOpen(int, int, int, void *);
int sceKeyboardReadState(int, void *);
int sceKeyboardClose(int);
int sceImeDialogInit(const void *, void *);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(void *);
int sceImeDialogTerm(void);
int sceAudioOutInit(void);
int sceAudioOutOpen(int user, int type, int index, uint32_t len, uint32_t freq, uint32_t param);
int sceAudioOutOutput(int handle, const void *data);
int sceAudioOutClose(int handle);
int sceSystemServiceLoadExec(const char *path, char *const argv[]);
int sceSystemServiceParamGetInt(int paramId, int *value);
int sceKernelSendNotificationRequest(int device, void *request, size_t size, int blocking);
}

// Tas applicatif de ps5-opengl : 128 Mio par défaut, trop juste pour des maps
// (musique, images, .osz). Le runtime essaie des tailles plus petites si besoin.
extern "C" const size_t ps5_opengl_heap_size = 1024u * 1024u * 1024u;

namespace plat {
namespace {

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;
int g_width = 0, g_height = 0, g_refresh = 0;
bool g_refreshQueried = false;

int g_user = -1;
int g_pad = -1;
int g_keyboard = -1;
int g_mouse = -1;
float g_mouseX = 960, g_mouseY = 540;
double g_nextPadRetry = 0, g_nextMouseRetry = 0, g_nextKeyboardRetry = 0;
bool g_mouseWasConnected = false, g_keyboardWasConnected = false;

// Modules système qui ne sont pas chargés d'office dans le processus : un appel
// direct par la table d'imports saute à l'adresse 0 (plantage de la touche Carré). On les charge donc nous-mêmes et on résout chaque fonction ; si
// quelque chose manque, la fonction est simplement indisponible.
int loadSystemModule(const char *name) {
    // Une appli est dans un bac à sable : /system n'y existe pas forcément, les
    // bibliothèques sont sous /<mot aléatoire>/common/lib. On essaie les deux.
    const char *word = sceKernelGetFsSandboxRandomWord();
    char prefixes[6][96];
    int n = 0;
    if (word && *word) {
        std::snprintf(prefixes[n++], sizeof(prefixes[0]), "/%s/common/lib/", word);
        std::snprintf(prefixes[n++], sizeof(prefixes[0]), "/%s/priv/lib/", word);
    }
    std::snprintf(prefixes[n++], sizeof(prefixes[0]), "/system/common/lib/");
    std::snprintf(prefixes[n++], sizeof(prefixes[0]), "/system/priv/lib/");
    std::snprintf(prefixes[n++], sizeof(prefixes[0]), "/system_ex/common_ex/lib/");
    prefixes[n++][0] = 0;  // nom seul : le chargeur cherche lui-même
    for (int i = 0; i < n; ++i) {
        char path[192];
        std::snprintf(path, sizeof(path), "%s%s", prefixes[i], name);
        const int h = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr);
        LOG("ps5: module %s -> 0x%08x", path, static_cast<unsigned>(h));
        if (h >= 0) return h;
    }
    return -1;
}

// Les bibliothèques système exportent leurs fonctions sous un identifiant (NID) :
// SHA-1 du nom suivi d'un sel fixe, 8 premiers octets en base64 (11 caractères),
// comme nid_encode() du SDK payload.
void sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> m(data, data + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    const uint64_t bits = static_cast<uint64_t>(len) * 8;
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = static_cast<uint32_t>(m[off + i * 4]) << 24 | static_cast<uint32_t>(m[off + i * 4 + 1]) << 16 |
                   static_cast<uint32_t>(m[off + i * 4 + 2]) << 8 | m[off + i * 4 + 3];
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else f = b ^ c ^ d, k = 0xCA62C1D6;
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rol(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    for (int i = 0; i < 20; ++i) out[i] = static_cast<uint8_t>(h[i / 4] >> (24 - (i % 4) * 8));
}

std::string nidOf(const char *name) {
    static const uint8_t salt[16] = {0x51, 0x8d, 0x64, 0xa6, 0x35, 0xde, 0xd8, 0xc1,
                                     0xe6, 0xb0, 0x39, 0xb1, 0xc3, 0xe5, 0x52, 0x30};
    std::vector<uint8_t> in(name, name + std::strlen(name));
    in.insert(in.end(), salt, salt + 16);
    uint8_t d[20];
    sha1(in.data(), in.size(), d);
    // Les 8 premiers octets, dans l'ordre inverse (comme le bswap64 du SDK), puis des zéros.
    uint8_t v[9] = {};
    for (int i = 0; i < 8; ++i) v[i] = d[7 - i];
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    std::string out;
    for (int i = 0; i < 9 && out.size() < 11; i += 3) {
        const uint32_t abc = static_cast<uint32_t>(v[i]) << 16 | static_cast<uint32_t>(v[i + 1]) << 8 | v[i + 2];
        out += b64[(abc >> 18) & 63];
        out += b64[(abc >> 12) & 63];
        if (out.size() < 11) out += b64[(abc >> 6) & 63];
        if (out.size() < 11) out += b64[abc & 63];
    }
    return out.substr(0, 11);
}

template <class F>
bool resolve(int module, const char *symbol, F &fn) {
    fn = nullptr;
    if (module < 0) return false;
    // Par le nom d'abord, puis par l'identifiant (seul, puis avec les suffixes de bibliothèque).
    const std::string nid = nidOf(symbol);
    const std::string tries[] = {symbol, nid, nid + "#A#A", nid + "#B#B"};
    for (const std::string &name : tries) {
        void *address = nullptr;
        if (sceKernelDlsym(module, name.c_str(), &address) == 0 && address) {
            LOG("ps5: symbole %s trouve (%s)", symbol, name.c_str());
            fn = reinterpret_cast<F>(address);
            return true;
        }
    }
    LOG("ps5: symbole %s introuvable (nid %s)", symbol, nid.c_str());
    return false;
}

struct KeyboardApi {
    int (*init)() = nullptr;
    int (*open)(int, int, int, void *) = nullptr;
    int (*read)(int, SonyKeyboardData *) = nullptr;
    int (*close)(int) = nullptr;
} g_kb;

struct MouseApi {
    int (*init)() = nullptr;
    int (*open)(int, int, int, SonyMouseOpenParam *) = nullptr;
    int (*read)(int, SonyMouseData *, int) = nullptr;
    int (*close)(int) = nullptr;
} g_ms;

// Garde-fou : si l'initialisation du clavier/souris a planté au lancement
// précédent, le fichier témoin est resté ; on saute alors cette étape une fois.
constexpr const char *kInputMarker = "/download0/usb-input.pending";

void openMouse() {
    if (!g_ms.open || g_user < 0) return;
    SonyMouseOpenParam param{};
    g_mouse = g_ms.open(g_user, 0, 0, &param);
    LOG("ps5: sceMouseOpen = 0x%08x", static_cast<unsigned>(g_mouse));
}

void initUsbInput() {
    if (FILE *f = std::fopen(kInputMarker, "rb")) {
        std::fclose(f);
        std::remove(kInputMarker);
        LOG("ps5: l'init clavier/souris a plante au lancement precedent : ignoree cette fois");
        return;
    }
    if (FILE *f = std::fopen(kInputMarker, "wb")) std::fclose(f);
    const int kb = loadSystemModule("libSceKeyboard.sprx");
    if (!(resolve(kb, "sceKeyboardInit", g_kb.init) && resolve(kb, "sceKeyboardOpen", g_kb.open) &&
          resolve(kb, "sceKeyboardReadState", g_kb.read) && resolve(kb, "sceKeyboardClose", g_kb.close)) &&
        kb >= 0) {
        LOG("ps5: clavier par imports directs");
        g_kb.init = sceKeyboardInit;
        g_kb.open = sceKeyboardOpen;
        g_kb.read = reinterpret_cast<int (*)(int, SonyKeyboardData *)>(sceKeyboardReadState);
        g_kb.close = sceKeyboardClose;
    }
    if (g_kb.init) {
        LOG("ps5: sceKeyboardInit = 0x%08x", static_cast<unsigned>(g_kb.init()));
        int junk = 0;
        g_keyboard = g_kb.open(g_user, 0, 0, &junk);
        LOG("ps5: sceKeyboardOpen = 0x%08x", static_cast<unsigned>(g_keyboard));
    }
    const int ms = loadSystemModule("libSceMouse.sprx");
    if (resolve(ms, "sceMouseInit", g_ms.init) && resolve(ms, "sceMouseOpen", g_ms.open) &&
        resolve(ms, "sceMouseRead", g_ms.read) && resolve(ms, "sceMouseClose", g_ms.close)) {
        LOG("ps5: sceMouseInit = 0x%08x", static_cast<unsigned>(g_ms.init()));
        openMouse();
    } else {
        g_ms = MouseApi{};
    }
    std::remove(kInputMarker);
}
bool g_padWasConnected = false;
uint16_t g_touchMaxX = 0, g_touchMaxY = 0;

pthread_t g_audioThread;
bool g_audioThreadStarted = false;
AudioCallback g_audioCb = nullptr;
void *g_audioUser = nullptr;
std::atomic<bool> g_audioRunning{false};
int g_audioHandle = -1;
int g_audioBlock = 0;

bool g_exitRequested = false;

bool eglCheck(bool ok, const char *stage) {
    if (!ok) LOG("ps5: ECHEC %s (egl=0x%x)", stage, eglGetError());
    return ok;
}

void openPad() {
    if (g_user < 0) return;
    LOG("ps5: -> scePadOpen(user=%d)", g_user);
    g_pad = scePadOpen(g_user, 0, 0, nullptr);
    if (g_pad < 0) {
        LOG("ps5: scePadOpen a echoue: 0x%08x", static_cast<unsigned>(g_pad));
        return;
    }
    LOG("ps5: manette ouverte (handle %d)", g_pad);
    const SonyPadColor color{255, 60, 160, 255};  // barre lumineuse rose : l'appli tourne
    LOG("ps5: -> scePadSetLightBar");
    const int r = scePadSetLightBar(g_pad, &color);
    LOG("ps5: <- scePadSetLightBar = 0x%08x", static_cast<unsigned>(r));
}

float stick(uint8_t v) {
    const float f = (static_cast<float>(v) - 127.5f) / 127.5f;
    return f < -1 ? -1 : (f > 1 ? 1 : f);
}

void *audioLoop(void *) {
    thr::logStackInfo("audio");
    AudioCallback cb = g_audioCb;
    void *user = g_audioUser;
    std::vector<float> mixBuffer(static_cast<size_t>(g_audioBlock) * 2);
    std::vector<int16_t> outBuffer(static_cast<size_t>(g_audioBlock) * 2);
    while (g_audioRunning.load(std::memory_order_relaxed)) {
        cb(mixBuffer.data(), g_audioBlock, user);
        for (size_t i = 0; i < mixBuffer.size(); ++i) {
            float v = mixBuffer[i] * 32767.0f;
            v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
            outBuffer[i] = static_cast<int16_t>(v);
        }
        // Bloque jusqu'à ce que le matériel accepte le bloc : c'est notre cadence.
        const int r = sceAudioOutOutput(g_audioHandle, outBuffer.data());
        if (r < 0) {
            LOG("ps5: sceAudioOutOutput a echoue: 0x%08x, arret du thread audio", static_cast<unsigned>(r));
            break;
        }
    }
    return nullptr;
}

}  // namespace

bool init(const InitParams &params) {
    // Services utilisateur (manette, clavier) : sans eux, scePadOpen échoue.
    LOG("ps5: -> sceUserServiceInitialize");
    int r = sceUserServiceInitialize(nullptr);
    LOG("ps5: sceUserServiceInitialize = 0x%08x", static_cast<unsigned>(r));
    r = sceUserServiceGetInitialUser(&g_user);
    LOG("ps5: utilisateur initial = %d (0x%08x)", g_user, static_cast<unsigned>(r));
    LOG("ps5: -> scePadInit");
    r = scePadInit();
    LOG("ps5: scePadInit = 0x%08x", static_cast<unsigned>(r));
    openPad();
    initUsbInput();
    thr::logStackInfo("principal (apres entrees)");
    LOG("ps5: -> EGL");

    // Vidéo : le mode d'affichage se choisit AVANT eglInitialize.
    const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                       EGL_NONE};
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
                                        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                                        EGL_NONE};
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglCheck(g_display != EGL_NO_DISPLAY, "eglGetDisplay")) return false;
    if (!eglSetDisplayModePS5(g_display, params.width, params.height)) {
        LOG("ps5: mode %dx%d refuse, on garde 1920x1080", params.width, params.height);
    }
    if (!eglSetDisplayRefreshPS5(g_display, params.refreshHz)) {
        LOG("ps5: frequence %d Hz refusee", params.refreshHz);
    }
    EGLint major = 0, minor = 0;
    if (!eglCheck(eglInitialize(g_display, &major, &minor), "eglInitialize")) return false;
    LOG("ps5: EGL %d.%d", major, minor);
    if (!eglCheck(eglBindAPI(EGL_OPENGL_API), "eglBindAPI")) return false;
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglCheck(eglChooseConfig(g_display, configAttributes, &config, 1, &count) && count == 1, "eglChooseConfig"))
        return false;
    g_surface = eglCreateWindowSurface(g_display, config, static_cast<EGLNativeWindowType>(0), nullptr);
    if (!eglCheck(g_surface != EGL_NO_SURFACE, "eglCreateWindowSurface")) return false;
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, contextAttributes);
    if (!eglCheck(g_context != EGL_NO_CONTEXT, "eglCreateContext")) return false;
    if (!eglCheck(eglMakeCurrent(g_display, g_surface, g_surface, g_context), "eglMakeCurrent")) return false;
    eglSwapInterval(g_display, 1);
    EGLint w = 0, h = 0;
    eglQuerySurface(g_display, g_surface, EGL_WIDTH, &w);
    eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &h);
    g_width = w > 0 ? w : params.width;
    g_height = h > 0 ? h : params.height;
    LOG("ps5: surface %dx%d", g_width, g_height);
    return true;
}

void shutdown() {
    audioStop();
    if (g_pad >= 0) scePadClose(g_pad);
    if (g_keyboard >= 0 && g_kb.close) g_kb.close(g_keyboard);
    if (g_mouse >= 0 && g_ms.close) g_ms.close(g_mouse);
    g_pad = g_keyboard = g_mouse = -1;
    if (g_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_context != EGL_NO_CONTEXT) eglDestroyContext(g_display, g_context);
        if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_display, g_surface);
        eglTerminate(g_display);
    }
    g_display = EGL_NO_DISPLAY;
    g_surface = EGL_NO_SURFACE;
    g_context = EGL_NO_CONTEXT;
    if (g_exitRequested) {
        LOG("ps5: fermeture via sceSystemServiceLoadExec(\"exit\")");
        logx::shutdown();
        sceSystemServiceLoadExec("exit", nullptr);
    }
}

const char *name() { return "ps5"; }
int width() { return g_width; }
int height() { return g_height; }

int refreshHz() {
    // La fréquence acceptée par la sortie n'est définitive qu'après la 1re image.
    if (!g_refreshQueried && g_display != EGL_NO_DISPLAY) {
        EGLint rw = 0, rh = 0, hz = 0;
        if (eglGetDisplayModePS5(g_display, &rw, &rh, &hz)) {
            g_refresh = hz;
            g_refreshQueried = true;
            LOG("ps5: mode d'affichage effectif %dx%d @ %d Hz", rw, rh, hz);
        }
    }
    return g_refresh;
}

void pollInput(InputState &out) {
    PadState &p = out.pad;
    p = PadState{};
    if (g_pad < 0 && nowSeconds() >= g_nextPadRetry) {
        g_nextPadRetry = nowSeconds() + 2.0;
        openPad();
    }
    if (g_pad >= 0) {
        SonyPadData d{};
        const int r = scePadReadState(g_pad, &d);
        p.connected = r == 0 && d.connected;
        if (p.connected != g_padWasConnected) {
            LOG("ps5: manette %s", p.connected ? "connectee" : "deconnectee");
            g_padWasConnected = p.connected;
        }
        // Si l'interface système a le focus (bouton PS), on ignore les entrées.
        if (p.connected && !(d.buttons & kSonyIntercepted)) {
            const uint32_t b = d.buttons;
            p.rawButtons = b;
            struct Map {
                uint32_t sony, ours;
            };
            static const Map map[] = {
                {kSonyCross, kPadCross},       {kSonyCircle, kPadCircle}, {kSonySquare, kPadSquare},
                {kSonyTriangle, kPadTriangle}, {kSonyL1, kPadL1},         {kSonyR1, kPadR1},
                {kSonyL2, kPadL2},             {kSonyR2, kPadR2},         {kSonyL3, kPadL3},
                {kSonyR3, kPadR3},             {kSonyUp, kPadUp},         {kSonyDown, kPadDown},
                {kSonyLeft, kPadLeft},         {kSonyRight, kPadRight},   {kSonyOptions, kPadOptions},
                {kSonyTouchpad, kPadTouchpad},
            };
            for (const Map &m : map)
                if (b & m.sony) p.buttons |= m.ours;
            p.lx = stick(d.leftX);
            p.ly = stick(d.leftY);
            p.rx = stick(d.rightX);
            p.ry = stick(d.rightY);
            p.l2 = static_cast<float>(d.l2) / 255.0f;
            p.r2 = static_cast<float>(d.r2) / 255.0f;
            p.touchCount = d.touchFingers > 2 ? 2 : d.touchFingers;
            for (int i = 0; i < p.touchCount; ++i) {
                p.touchX[i] = d.touch[i].x;
                p.touchY[i] = d.touch[i].y;
                if (d.touch[i].x > g_touchMaxX || d.touch[i].y > g_touchMaxY) {
                    g_touchMaxX = d.touch[i].x > g_touchMaxX ? d.touch[i].x : g_touchMaxX;
                    g_touchMaxY = d.touch[i].y > g_touchMaxY ? d.touch[i].y : g_touchMaxY;
                }
            }
            p.timestampUs = d.timestamp;
        }
    }

    KeyboardState &k = out.keyboard;
    k = KeyboardState{};
    if (g_kb.open && g_keyboard < 0 && nowSeconds() >= g_nextKeyboardRetry) {
        g_nextKeyboardRetry = nowSeconds() + 2.0;
        int junk = 0;
        g_keyboard = g_kb.open(g_user, 0, 0, &junk);
        if (g_keyboard >= 0) LOG("ps5: clavier ouvert (handle %d)", g_keyboard);
    }
    if (g_keyboard >= 0 && g_kb.read) {
        SonyKeyboardData d{};
        const int rr = g_kb.read(g_keyboard, &d);
        const bool ok = rr == 0 && d.available;
        static int diag = 0;
        if (diag < 5 && (++diag, true)) LOG("ps5: lecture clavier = 0x%08x, dispo %d", static_cast<unsigned>(rr), d.available);
        if (ok != g_keyboardWasConnected) {
            LOG("ps5: clavier %s", ok ? "connecte" : "deconnecte");
            g_keyboardWasConnected = ok;
        }
        if (ok) {
            k.connected = true;
            for (uint16_t code : d.scankey)
                if (code > 0 && code < 256) k.keys[code] = true;
            for (int bit = 0; bit < 8; ++bit)  // Ctrl, Maj, Alt, Gui gauche puis droite
                if (d.modifiers & (1u << bit)) k.keys[0xE0 + bit] = true;
        }
    }
    MouseState &m = out.mouse;
    m = MouseState{};
    if (g_ms.read && g_mouse < 0 && nowSeconds() >= g_nextMouseRetry) {
        g_nextMouseRetry = nowSeconds() + 2.0;
        openMouse();
    }
    if (g_mouse >= 0 && g_ms.read) {
        SonyMouseData data[16];
        std::memset(data, 0, sizeof(data));
        const int n = g_ms.read(g_mouse, data, 16);
        static int diag = 0;
        if (diag < 5 && (++diag, true)) LOG("ps5: lecture souris = %d (connectee %d)", n, n > 0 ? data[0].connected : -1);
        bool connected = g_mouseWasConnected;
        // Vitesse : 1 compte = 1 pixel en 1080p, proportionnel au-delà.
        const float speed = g_height > 0 ? g_height / 1080.0f : 1.0f;
        static uint32_t buttons = 0;
        for (int i = 0; i < n && i < 16; ++i) {
            connected = data[i].connected != 0;
            if (!connected) continue;
            g_mouseX += data[i].x * speed;
            g_mouseY += data[i].y * speed;
            m.wheel += static_cast<float>(data[i].wheel);
            buttons = data[i].buttons;
        }
        if (n < 0) connected = false;
        if (connected != g_mouseWasConnected) {
            LOG("ps5: souris %s", connected ? "connectee" : "deconnectee");
            g_mouseWasConnected = connected;
        }
        const float w = static_cast<float>(g_width > 0 ? g_width : 1920);
        const float h = static_cast<float>(g_height > 0 ? g_height : 1080);
        g_mouseX = g_mouseX < 0 ? 0 : (g_mouseX > w - 1 ? w - 1 : g_mouseX);
        g_mouseY = g_mouseY < 0 ? 0 : (g_mouseY > h - 1 ? h - 1 : g_mouseY);
        m.connected = connected;
        m.x = g_mouseX;
        m.y = g_mouseY;
        m.buttons = connected ? (buttons & 7u) : 0;
    }
    out.quitRequested = g_exitRequested;
}

void swapBuffers() { eglSwapBuffers(g_display, g_surface); }

double nowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

void sleepSeconds(double seconds) {
    if (seconds > 0) usleep(static_cast<useconds_t>(seconds * 1e6));
}

bool audioStart(AudioCallback cb, void *user, int framesPerBlock) {
    int r = sceAudioOutInit();
    LOG("ps5: sceAudioOutInit = 0x%08x", static_cast<unsigned>(r));
    // Tailles de bloc acceptées : multiples de 256 jusqu'à 2048.
    int block = (framesPerBlock + 255) / 256 * 256;
    block = block < 256 ? 256 : (block > 2048 ? 2048 : block);
    g_audioHandle = sceAudioOutOpen(kUserSystem, kAudioPortMain, 0, static_cast<uint32_t>(block), kAudioRate,
                                    kAudioFormatS16Stereo);
    if (g_audioHandle < 0) {
        LOG("ps5: sceAudioOutOpen a echoue: 0x%08x", static_cast<unsigned>(g_audioHandle));
        return false;
    }
    g_audioBlock = block;
    LOG("ps5: audio ouvert (handle %d), %d Hz, blocs de %d trames (%.2f ms)", g_audioHandle, kAudioRate, block,
        1000.0 * block / kAudioRate);
    g_audioRunning.store(true);
    g_audioCb = cb;
    g_audioUser = user;
    g_audioThreadStarted = thr::start(g_audioThread, audioLoop, nullptr, "audio");
    return true;
}

void audioStop() {
    g_audioRunning.store(false);
    if (g_audioThreadStarted) pthread_join(g_audioThread, nullptr);
    g_audioThreadStarted = false;
    if (g_audioHandle >= 0) sceAudioOutClose(g_audioHandle);
    g_audioHandle = -1;
}

int audioBlockFrames() { return g_audioBlock; }

const char *writableDir() { return "/download0"; }

const char *systemLanguage() {
    // SCE_SYSTEM_SERVICE_PARAM_ID_LANG : 2 français, 22 français (Canada).
    int lang = -1;
    const int r = sceSystemServiceParamGetInt(1, &lang);
    LOG("ps5: langue de la console = %d (0x%08x)", lang, static_cast<unsigned>(r));
    return r == 0 && (lang == 2 || lang == 22) ? "fr" : "en";
}
const char *assetsDir() { return "/app0/assets"; }

// --- Clavier virtuel (SceImeDialog) -----------------------------------------
// Structures reprises du portage SDL de ps5-payload-dev ; wchar_t fait 16 bits
// sur cette cible, comme le texte UTF-16 attendu par le système.
namespace {
struct SceImeDialogParam {
    int userId;
    int type;                 // 0 défaut
    uint64_t supportedLanguages;
    int enterLabel;           // 2 « Rechercher »
    int inputMethod;
    void *filter;
    uint32_t option;
    uint32_t maxTextLength;
    char16_t *inputTextBuffer;
    float posx, posy;
    int halign, valign;
    const char16_t *placeholder;
    const char16_t *title;
    int8_t reserved[16];
};
struct SceImeDialogResult {
    int outcome;              // 0 ok, 1 annulé, 2 interrompu
    int8_t reserved[12];
};
}  // namespace
namespace {
constexpr const char *kImeMarker = "/download0/ime.pending";
struct ImeApi {
    int (*init)(const SceImeDialogParam *, void *) = nullptr;
    int (*status)() = nullptr;
    int (*result)(SceImeDialogResult *) = nullptr;
    int (*term)() = nullptr;
    bool tried = false;
} g_ime;

bool loadIme() {
    if (!g_ime.tried) {
        g_ime.tried = true;
        const int h = loadSystemModule("libSceImeDialog.sprx");
        if (!(resolve(h, "sceImeDialogInit", g_ime.init) && resolve(h, "sceImeDialogGetStatus", g_ime.status) &&
              resolve(h, "sceImeDialogGetResult", g_ime.result) && resolve(h, "sceImeDialogTerm", g_ime.term))) {
            g_ime.init = nullptr;
            // Garde-fou : si l'appel direct a planté la dernière fois, on n'essaie plus.
            if (h >= 0 && !fsu::exists(kImeMarker)) {
                LOG("ps5: clavier virtuel par imports directs");
                g_ime.init = reinterpret_cast<int (*)(const SceImeDialogParam *, void *)>(sceImeDialogInit);
                g_ime.status = sceImeDialogGetStatus;
                g_ime.result = reinterpret_cast<int (*)(SceImeDialogResult *)>(sceImeDialogGetResult);
                g_ime.term = sceImeDialogTerm;
            }
        }
    }
    return g_ime.init != nullptr;
}

char16_t g_imeText[256];
char16_t g_imeTitle[128];
bool g_imeOpen = false;

void toUtf16(const std::string &in, char16_t *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < in.size() && o + 2 < cap;) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        uint32_t cp = c;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra) cp = c & (0x3F >> extra);
        ++i;
        for (int k = 0; k < extra && i < in.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(in[i]) & 0x3F);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out[o++] = static_cast<char16_t>(0xD800 + (cp >> 10));
            out[o++] = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out[o++] = static_cast<char16_t>(cp);
        }
    }
    out[o] = 0;
}

std::string fromUtf16(const char16_t *in) {
    std::string out;
    for (size_t i = 0; in[i]; ++i) {
        uint32_t cp = in[i];
        if (cp >= 0xD800 && cp < 0xDC00 && in[i + 1] >= 0xDC00 && in[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}
}  // namespace

bool textInputSupported() { return true; }

bool startTextInput(const char *title, const std::string &initial) {
    if (g_imeOpen) return false;
    if (!loadIme()) {
        LOG("ps5: clavier virtuel indisponible");
        return false;
    }
    toUtf16(initial, g_imeText, sizeof(g_imeText) / sizeof(g_imeText[0]));
    toUtf16(title ? title : "", g_imeTitle, sizeof(g_imeTitle) / sizeof(g_imeTitle[0]));
    SceImeDialogParam p;
    std::memset(&p, 0, sizeof(p));
    p.userId = g_user;
    p.enterLabel = 2;
    p.maxTextLength = 120;
    p.inputTextBuffer = g_imeText;
    p.title = g_imeTitle;
    p.posx = 960;
    p.posy = 540;
    p.halign = 1;
    p.valign = 1;
    if (FILE *f = std::fopen(kImeMarker, "wb")) std::fclose(f);
    const int r = g_ime.init(&p, nullptr);
    std::remove(kImeMarker);
    LOG("ps5: clavier virtuel -> 0x%x", r);
    g_imeOpen = r == 0;
    return g_imeOpen;
}

int pollTextInput(std::string &out) {
    if (!g_imeOpen) return 0;
    const int status = g_ime.status();
    if (status != 2) return 0;  // 1 en cours, 2 terminé
    SceImeDialogResult result;
    std::memset(&result, 0, sizeof(result));
    g_ime.result(&result);
    g_ime.term();
    g_imeOpen = false;
    if (result.outcome != 0) return -1;
    out = fromUtf16(g_imeText);
    return 1;
}

void notify(const char *message) {
    static SonyNotification request;
    std::memset(&request, 0, sizeof(request));
    std::snprintf(request.message, sizeof(request.message), "%s", message);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

void requestExit() { g_exitRequested = true; }

}  // namespace plat

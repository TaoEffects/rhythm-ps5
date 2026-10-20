// SPDX-License-Identifier: GPL-3.0-or-later
// Boîte à outils de l'interface façon osu!lazer : palette (OsuColour,
// OverlayColourProvider), unités, triangles animés, boutons inclinés, logo,
// visualiseur, barre d'outils... Tout est réécrit d'après le comportement et les
// valeurs du code de lazer (MIT) ; aucun asset officiel n'est utilisé.
//
// Unités : lazer dispose son interface sur 1024x768 « unités » (u) mis à
// l'échelle ; en 1920x1080, 1 u = 1,40625 pixel virtuel. Les valeurs reprises de
// lazer sont écrites en u et multipliées par U.
#pragma once

#include <string>
#include <vector>

#include "app/context.h"
#include "core/anim.h"
#include "ui/icons.h"

namespace lz {

using anim::Ease;
using anim::Tween;

// Unité de mise en page : 1,40625 px par u en 1080p, multipliée par la taille
// d'interface choisie dans les options (setUiScale).
inline float U = 1.40625f;
inline void setUiScale(float s) { U = 1.40625f * s; }
constexpr float kShear = 0.2f;  // OsuGame.SHEAR

inline double ms(const Context &c) { return c.now * 1000.0; }

constexpr uint32_t hex(uint32_t rgb, uint8_t a = 255) {
    return gfx::rgba(static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb), a);
}
uint32_t mix(uint32_t a, uint32_t b, float t);
// Comme osu-framework : Lighten multiplie par (1 + k), Darken divise par (1 + k).
uint32_t lighten(uint32_t c, float k);
uint32_t darken(uint32_t c, float k);
uint32_t hsl(float hueDeg, float s, float l, uint8_t a = 255);
inline uint32_t alpha(uint32_t c, float a) { return gfx::withAlpha(c, a); }

// OsuColour.
namespace col {
constexpr uint32_t white = hex(0xffffff), black = hex(0x000000);
constexpr uint32_t purpleLighter = hex(0xeeeeff), purpleLight = hex(0xaa88ff), purple = hex(0x8866ee),
                   purpleDark = hex(0x6644cc), purpleDarker = hex(0x441188);
constexpr uint32_t pinkLighter = hex(0xffddee), pinkLight = hex(0xff99cc), pink = hex(0xff66aa), pinkDark = hex(0xcc5288),
                   pinkDarker = hex(0xbb1177);
constexpr uint32_t blueLighter = hex(0xddffff), blueLight = hex(0x99eeff), blue = hex(0x66ccff), blueDark = hex(0x44aadd),
                   blueDarker = hex(0x2299bb);
constexpr uint32_t yellowLighter = hex(0xffffdd), yellowLight = hex(0xffdd55), yellow = hex(0xffcc22),
                   yellowDark = hex(0xeeaa00), yellowDarker = hex(0xcc6600);
constexpr uint32_t greenLighter = hex(0xeeffcc), greenLight = hex(0xb3d944), green = hex(0x88b300),
                   greenDark = hex(0x668800), greenDarker = hex(0x445500);
constexpr uint32_t redLighter = hex(0xffeded), redLight = hex(0xed7787), red = hex(0xed1121), redDark = hex(0xba0011),
                   redDarker = hex(0x870000);
// Nuances de thème 0..4.
constexpr uint32_t pink0 = hex(0xff99c7), pink1 = hex(0xff66ab), pink2 = hex(0xeb4791), pink3 = hex(0xcc3378),
                   pink4 = hex(0x6b2e49);
constexpr uint32_t purple0 = hex(0xb299ff), purple1 = hex(0x8c66ff), purple2 = hex(0x7047eb), purple3 = hex(0x5933cc),
                   purple4 = hex(0x3d2e6b);
constexpr uint32_t blue0 = hex(0x99ddff), blue1 = hex(0x66ccff), blue2 = hex(0x47b4eb), blue3 = hex(0x3399cc),
                   blue4 = hex(0x2e576b);
constexpr uint32_t green0 = hex(0x99ffa2), green1 = hex(0x66ff73), green2 = hex(0x47eb55), green3 = hex(0x33cc40),
                   green4 = hex(0x2e6b33);
constexpr uint32_t lime0 = hex(0xccff99), lime1 = hex(0xb2ff66), lime2 = hex(0x99eb47), lime3 = hex(0x7fcc33),
                   lime4 = hex(0x4c6b2e);
constexpr uint32_t orange0 = hex(0xffe699), orange1 = hex(0xffd966), orange2 = hex(0xebc247), orange3 = hex(0xcca633),
                   orange4 = hex(0x6b5c2e);
constexpr uint32_t red0 = hex(0xff9b9b), red1 = hex(0xff6666), red2 = hex(0xeb4747), red3 = hex(0xcc3333),
                   red4 = hex(0x6b2e2e);
inline constexpr uint32_t gray(uint8_t v, uint8_t a = 255) { return gfx::rgba(v, v, v, a); }
}  // namespace col

// OverlayColourProvider : nuances d'une teinte (Blue = 200, Pink = 333, Purple = 255...).
struct Palette {
    uint32_t highlight1, content1, content2, light1, light2, light3, light4, dark1, dark2, dark3, dark4, dark5, dark6,
        foreground1, background1, background2, background3, background4, background5, background6;
};
Palette palette(float hue);
namespace hue {
constexpr float red = 0, pink = 333, orange = 45, lime = 90, green = 125, aquamarine = 160, blue = 200, purple = 255,
                plum = 320;
}

uint32_t starColor(float stars);
uint32_t starTextColor(float stars);
uint32_t rankColor(const std::string &rank);

// Icône Font Awesome centrée en (cx, cy) ; size = hauteur du carré de l'icône.
void icon(Context &c, const char *glyph, float cx, float cy, float size, uint32_t color, float angle = 0, float sx = 1, float sy = 1);
// Roue de chargement (LoadingSpinner).
void spinner(Context &c, float cx, float cy, float size, float alpha);
// Pastille d'étoiles (StarRatingDisplay). Renvoie la largeur.
float starPill(Context &c, float x, float y, float h, float stars, float alpha = 1, bool knownStars = true);
// Icône de mod (sigle sur pastille inclinée de la couleur du type).
void modIcon(Context &c, const char *acronym, float cx, float cy, float h, uint32_t color, float alpha = 1);
uint32_t modTypeColor(int type);  // 0 réduction, 1 augmentation, 2 automatisation, 3 conversion, 4 fun

// Triangles qui montent (osu.Game.Graphics.Backgrounds.Triangles).
class Triangles {
public:
    float spawnRatio = 1, velocity = 1, scaleAdjust = 1;
    explicit Triangles(uint32_t seed = 1) : seed_(seed) {}
    // w, h en pixels virtuels.
    void update(float w, float h, float dtMs);
    void draw(Context &c, float x, float y, uint32_t dark, uint32_t light, float alpha, float outline = 0) const;

private:
    struct T {
        float x, y, scale, shade;
    };
    float rnd();
    float gauss();
    void spawn(T &t, bool anywhere);
    std::vector<T> t_;
    float w_ = 0, h_ = 0;
    uint32_t seed_;
};

// État animé d'un bouton (survol, appui, éclair au clic).
struct ButtonFx {
    Tween hover{0}, press{0}, flash{0};
    bool hovered = false, pressed = false;
    void setHover(const Context &c, bool h, double inMs = 150, double outMs = 150, Ease e = Ease::OutQuint);
    void setPressed(const Context &c, bool p);
    void trigger(const Context &c, float strength = 0.9f, double ms = 800);
};

// Indice de bouton manette discret à côté d'un libellé (Croix, Rond...).
enum class Pad { Cross, Circle, Square, Triangle, L1, R1, L2, R2, Options };
void padGlyph(Context &c, Pad p, float cx, float cy, float size, float alpha = 1);

// Bouton retour rose du pied de page (ScreenBackButton) ; x, y = coin bas gauche.
void backButton(Context &c, ButtonFx &fx, float x, float bottom, const char *label, float alpha = 1);
#define kBackW (240 * lz::U)
#define kBackH (50 * lz::U)
// Bouton du pied de page (ScreenFooterButton) 116x75 u ; x = gauche, top = haut.
void footerButton(Context &c, ButtonFx &fx, float x, float top, const char *glyph, const char *label, uint32_t accent,
                  bool active, float alpha = 1, int padHint = -1);
#define kFooterBtnW (116 * lz::U)
#define kFooterBtnH (75 * lz::U)
#define kFooterH (50 * lz::U)
// Bouton de dialogue (pause) : boîte inclinée de largeur relative animée.
void dialogButton(Context &c, ButtonFx &fx, float cx, float cy, float fullW, float h, const char *label,
                  uint32_t color, float alpha = 1);


// Le logo du jeu (équivalent d'OsuLogo, avec notre propre identité) : persistant,
// il passe d'un écran à l'autre en glissant.
class Logo {
public:
    Tween x{kVW / 2}, y{kVH / 2}, scale{1}, opacity{0};
    Tween hoverScale{1}, bounce{1}, flash{0}, impactAlpha{0}, impactScale{0.94f};
    bool interactive = true;
    bool visualiser = true;
    bool triangles = true;
    // Taille du disque visible à l'échelle 1 (481 u, comme lazer).
    static float size() { return 481 * U; }

    void moveTo(const Context &c, float tx, float ty, float tscale, double dur, Ease e, double delay = 0);
    void impact(const Context &c);
    void click(const Context &c);
    void update(Context &c, const UiInput &in);
    void draw(Context &c);
    bool hovered() const { return hovered_; }
    bool clicked() const { return clicked_; }
    float radius(const Context &c) const { return size() * 0.5f * scale.at(ms(c)) * hoverScale.at(ms(c)); }
    float cx(const Context &c) const { return x.at(ms(c)); }
    float cy(const Context &c) const { return y.at(ms(c)); }

private:
    Triangles tri_{7};
    static constexpr int kLogoBars = 120;
    float bars_[kLogoBars] = {};
    float barTarget_[kLogoBars] = {};
    int barOffset_ = 0;
    double lastBarUpdate_ = 0;
    long lastBeat_ = -1;
    double beatAt_ = -1e9;
    float beatAdj_ = 0;
    Tween beatScale_{1}, ripple_{0}, rippleScale_{1};
    float ampScale_ = 1;
    float triVelocity_ = 1;
    bool hovered_ = false, clicked_ = false;
};

// Flashs bleus sur les bords au rythme (MenuSideFlashes).
class SideFlashes {
public:
    void update(Context &c);
    void draw(Context &c, float alpha = 1) const;

private:
    Tween left_{0}, right_{0};
    long lastBeat_ = -1;
};

// Barre d'outils du haut (Toolbar).
class Toolbar {
public:
    enum Action { None, Settings, Home, Download, Music };
    Tween shown{0};
    void show(const Context &c, bool s);
    Action update(Context &c, const UiInput &in);
    void draw(Context &c);
    static float height() { return 40 * U; }
    float bottom(const Context &c) const { return height() * shown.at(ms(c)); }

private:
    ButtonFx fx_[4];
    float bx_[4] = {};
    int hovered_ = -1;
};

// Texte avec ombre douce, et texte ellipsé dans une largeur.
float text(Context &c, const gfx::Font &f, float x, float y, float size, uint32_t color, const std::string &s,
           gfx::Align a = gfx::Align::Left, float maxW = 0, bool shadow = false);

// Mots pour le retour à la ligne : coupés aux espaces, et chaque caractère japonais
// ou chinois (écrits sans espaces) forme un mot collé au précédent.
struct WrapToken {
    std::string text;
    bool space = true;     // une espace le sépare du mot précédent
    bool newline = false;  // saut de ligne forcé avant ce mot
};
std::vector<WrapToken> wrapTokens(const std::string &s);

// Texte sur plusieurs lignes (coupé aux espaces) ; renvoie la hauteur occupée.
float paragraph(Context &c, const gfx::Font &f, float x, float y, float width, float size, uint32_t color, const std::string &s,
                gfx::Align a = gfx::Align::Left, float lineHeight = 1.4f);

// Curseur du menu.
void cursor(Context &c, float x, float y, bool pressed);

}  // namespace lz

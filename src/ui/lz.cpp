// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/lz.h"

#include "core/i18n.h"

#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lz {
namespace {

float chan(uint32_t c, int i) { return static_cast<float>((c >> (i * 8)) & 0xFF) / 255.0f; }
uint32_t pack(float r, float g, float b, float a) {
    auto q = [](float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return gfx::rgba(q(r), q(g), q(b), q(a));
}

}  // namespace

uint32_t mix(uint32_t a, uint32_t b, float t) {
    return pack(chan(a, 0) + (chan(b, 0) - chan(a, 0)) * t, chan(a, 1) + (chan(b, 1) - chan(a, 1)) * t,
                chan(a, 2) + (chan(b, 2) - chan(a, 2)) * t, chan(a, 3) + (chan(b, 3) - chan(a, 3)) * t);
}

uint32_t lighten(uint32_t c, float k) {
    return pack(chan(c, 0) * (1 + k), chan(c, 1) * (1 + k), chan(c, 2) * (1 + k), chan(c, 3));
}

uint32_t darken(uint32_t c, float k) {
    return pack(chan(c, 0) / (1 + k), chan(c, 1) / (1 + k), chan(c, 2) / (1 + k), chan(c, 3));
}

uint32_t hsl(float h, float s, float l, uint8_t a) {
    h = std::fmod(h, 360.0f) / 360.0f;
    auto f = [&](float p, float q, float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f / 6) return p + (q - p) * 6 * t;
        if (t < 0.5f) return q;
        if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
        return p;
    };
    float r = l, g = l, b = l;
    if (s > 0) {
        const float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        r = f(p, q, h + 1.0f / 3);
        g = f(p, q, h);
        b = f(p, q, h - 1.0f / 3);
    }
    return pack(r, g, b, a / 255.0f);
}

Palette palette(float h) {
    Palette p;
    p.highlight1 = hsl(h, 1, 0.7f);
    p.content1 = hsl(h, 0.4f, 1);
    p.content2 = hsl(h, 0.4f, 0.9f);
    p.light1 = hsl(h, 0.4f, 0.8f);
    p.light2 = hsl(h, 0.4f, 0.75f);
    p.light3 = hsl(h, 0.4f, 0.7f);
    p.light4 = hsl(h, 0.4f, 0.5f);
    p.dark1 = hsl(h, 0.2f, 0.35f);
    p.dark2 = hsl(h, 0.2f, 0.3f);
    p.dark3 = hsl(h, 0.2f, 0.25f);
    p.dark4 = hsl(h, 0.2f, 0.2f);
    p.dark5 = hsl(h, 0.2f, 0.15f);
    p.dark6 = hsl(h, 0.2f, 0.1f);
    p.foreground1 = hsl(h, 0.2f, 0.6f);
    p.background1 = hsl(h, 0.1f, 0.4f);
    p.background2 = hsl(h, 0.1f, 0.3f);
    p.background3 = hsl(h, 0.1f, 0.25f);
    p.background4 = hsl(h, 0.1f, 0.2f);
    p.background5 = hsl(h, 0.1f, 0.15f);
    p.background6 = hsl(h, 0.1f, 0.1f);
    return p;
}

namespace {
struct Stop {
    float at;
    uint32_t c;
};
uint32_t spectrum(const Stop *stops, int n, float v) {
    if (v <= stops[0].at) return stops[0].c;
    for (int i = 1; i < n; ++i)
        if (v < stops[i].at) return mix(stops[i - 1].c, stops[i].c, (v - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
    return stops[n - 1].c;
}
}  // namespace

uint32_t starColor(float stars) {
    stars = std::round(stars * 100) / 100;
    if (stars < 0.1f) return hex(0xaaaaaa);
    static const Stop s[] = {{0.1f, hex(0x4290fb)}, {1.25f, hex(0x4fc0ff)}, {2.0f, hex(0x4fffd5)}, {2.5f, hex(0x7cff4f)},
                             {3.3f, hex(0xf6f05c)}, {4.2f, hex(0xff8068)},  {4.9f, hex(0xff4e6f)}, {5.8f, hex(0xc645b8)},
                             {6.7f, hex(0x6563de)}, {7.7f, hex(0x18158e)},  {9.0f, hex(0x000000)}};
    return spectrum(s, 11, stars);
}

uint32_t starTextColor(float stars) {
    if (stars < 6.5f) return gfx::rgba(0, 0, 0, 191);
    if (stars < 9) return col::orange1;
    static const Stop s[] = {{9.0f, hex(0xf6f05c)}, {9.9f, hex(0xff8068)}, {10.6f, hex(0xff4e6f)}, {11.5f, hex(0xc645b8)},
                             {12.4f, hex(0x6563de)}};
    return spectrum(s, 5, stars);
}

uint32_t rankColor(const std::string &r) {
    if (r == "SS" || r == "X" || r == "XH" || r == "SSH") return hex(0xde31ae);
    if (r == "S" || r == "SH") return hex(0x02b5c3);
    if (r == "A") return hex(0x88da20);
    if (r == "B") return hex(0xe3b130);
    if (r == "C") return hex(0xff8e5d);
    if (r == "D") return hex(0xff5a5a);
    return hex(0x3f3f3f);
}

uint32_t modTypeColor(int type) {
    switch (type) {
    case 0: return col::lime1;
    case 1: return col::red1;
    case 2: return col::blue1;
    case 3: return col::purple1;
    default: return col::pink1;
    }
}

void icon(Context &c, const char *glyph, float cx, float cy, float size, uint32_t color, float angle, float sx, float sy) {
    // Centrage sur la boîte réelle de l'icône (les icônes n'ont pas toutes la même forme).
    c.fonts->icons.drawGlyphCentered(*c.batch, glyph, cx, cy, size, color, angle, sx, sy);
}

void spinner(Context &c, float cx, float cy, float size, float a) {
    if (a <= 0) return;
    // Rotation continue (un tour en 3150 ms), comme LoadingSpinner.
    const float angle = static_cast<float>(std::fmod(ms(c), 3150.0) / 3150.0 * 2 * anim::kPi);
    gfx::SpriteBatch &b = *c.batch;
    // Arc de 300° (un seul quad) qui tourne : un tour en 3150 ms, comme LoadingSpinner.
    const float turn = static_cast<float>(std::fmod(ms(c), 3150.0) / 3150.0);
    const float r = size * 0.5f, th = size * 0.14f;
    const uint32_t col = alpha(col::white, a);
    if (turn + 0.833f <= 1.0f) {
        b.arc(cx, cy, r, th, turn, turn + 0.833f, gfx::Corners::solid(col));
    } else {
        b.arc(cx, cy, r, th, turn, 1.0f, gfx::Corners::solid(col));
        b.arc(cx, cy, r, th, 0, turn + 0.833f - 1.0f, gfx::Corners::solid(col));
    }
}

float starPill(Context &c, float x, float y, float h, float stars, float a, bool known) {
    char buf[16];
    if (known)
        snprintf(buf, sizeof(buf), "%.2f", stars);
    else
        snprintf(buf, sizeof(buf), "?");
    const float ts = h * 0.72f;
    const float tw = c.fonts->bold.measure(ts, buf);
    const float w = h * 0.55f + h * 0.8f + tw + h * 0.55f;
    const uint32_t bg = known ? starColor(stars) : hex(0x777777);
    const uint32_t fg = known ? starTextColor(stars) : gfx::rgba(0, 0, 0, 191);
    c.batch->roundRect(x, y, w, h, h * 0.5f, alpha(bg, a));
    icon(c, fa::star, x + h * 0.55f + h * 0.32f, y + h * 0.5f, h * 0.62f, gfx::withAlpha(fg, a));
    c.fonts->bold.drawMid(*c.batch, x + h * 0.55f + h * 0.8f, y + h * 0.5f, ts, gfx::withAlpha(fg, a), buf);
    return w;
}

void modIcon(Context &c, const char *acr, float cx, float cy, float h, uint32_t color, float a) {
    const float w = h * 1.45f;
    gfx::SpriteBatch &b = *c.batch;
    b.shadow(cx - w / 2, cy - h / 2 + h * 0.06f, w, h, h * 0.28f, h * 0.12f, gfx::rgba(0, 0, 0, static_cast<uint8_t>(70 * a)), kShear);
    b.roundRect(cx - w / 2, cy - h / 2, w, h, h * 0.28f, alpha(darken(color, 0.9f), a), kShear);
    b.roundRectBorder(cx - w / 2, cy - h / 2, w, h, h * 0.28f, h * 0.07f, alpha(color, a), kShear);
    // Le sigle suit l'inclinaison : décalage nul au centre.
    c.fonts->bold.drawMid(*c.batch, cx, cy, h * 0.56f, alpha(color, a), acr, gfx::Align::Center);
}

// ---------------------------------------------------------------- Triangles

float Triangles::rnd() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return static_cast<float>(seed_ >> 8) / 16777216.0f;
}

float Triangles::gauss() {
    const float u1 = std::max(1e-6f, rnd()), u2 = rnd();
    return std::sqrt(-2 * std::log(u1)) * std::cos(2 * anim::kPi * u2);
}

void Triangles::spawn(T &t, bool anywhere) {
    t.scale = std::max(0.1f, 0.5f + 0.16f * gauss());
    t.shade = rnd();
    t.x = rnd();
    const float hrel = h_ > 0 ? (100 * 0.866f * U * t.scale * scaleAdjust) / h_ : 0;
    t.y = anywhere ? rnd() * (1 + hrel) : 1 + hrel * 0.5f;
}

void Triangles::update(float w, float h, float dtMs) {
    w_ = w;
    h_ = h;
    const float aim = std::max(1.0f, (w / U) * (h / U) * 0.002f / (scaleAdjust * scaleAdjust) * spawnRatio);
    const size_t n = static_cast<size_t>(aim);
    if (t_.size() > n) t_.resize(n);
    while (t_.size() < n) {
        T t{};
        spawn(t, true);
        t_.push_back(t);
    }
    if (h <= 0) return;
    for (T &t : t_) {
        t.y -= dtMs / 1000.0f * 50 * velocity * std::max(0.5f, t.scale) * U / h;
        const float hrel = (100 * 0.866f * U * t.scale * scaleAdjust) / h;
        if (t.y < -hrel * 0.6f) spawn(t, false);
    }
    // Les plus grands d'abord (dessinés dessous).
    std::sort(t_.begin(), t_.end(), [](const T &a, const T &b) { return a.scale > b.scale; });
}

void Triangles::draw(Context &c, float x, float y, uint32_t dark, uint32_t light, float a, float outline) const {
    if (a <= 0) return;
    gfx::SpriteBatch &b = *c.batch;
    const float a3 = a * a * a;
    for (const T &t : t_) {
        const float side = 100 * U * t.scale * scaleAdjust;
        b.triangle(x + t.x * w_, y + t.y * h_, side, alpha(mix(dark, light, t.shade), a3), outline * side);
    }
}

// ---------------------------------------------------------------- Boutons

void ButtonFx::setHover(const Context &c, bool h, double inMs, double outMs, Ease e) {
    if (h == hovered) return;
    hovered = h;
    hover.go(ms(c), h ? 1 : 0, h ? inMs : outMs, e);
}

void ButtonFx::setPressed(const Context &c, bool p) {
    if (p == pressed) return;
    pressed = p;
    if (p)
        press.go(ms(c), 1, 2000, Ease::OutQuint);
    else
        press.go(ms(c), 0, 1000, Ease::OutElastic);
}

void ButtonFx::trigger(const Context &c, float strength, double dur) { flash.fromTo(ms(c), strength, 0, dur, Ease::OutQuint); }

void backButton(Context &c, ButtonFx &fx, float x, float bottom, const char *label, float a) {
    const double t = ms(c);
    gfx::SpriteBatch &b = *c.batch;
    const float w = kBackW, h = kBackH, y = bottom - h;
    const float sc = 1 - 0.1f * fx.press.at(t);
    b.pushScaleAround(x + w / 2, y + h / 2, sc, a);
    const float hv = fx.hover.at(t);
    const uint32_t dark = lighten(hex(0xde31ae), 0.2f * hv), light = lighten(hex(0xff86dd), 0.2f * hv);
    b.shadow(x, y + 2 * U, w, h, 7 * U, 4 * U, gfx::rgba(0, 0, 0, 64), kShear);
    b.roundRectGradient(x, y, w, h, 7 * U, gfx::Corners::vertical(light, dark), kShear);
    b.roundRect(x + 2 * U, y + 2 * U, w - 4 * U, h - 4 * U, 5 * U, dark, kShear);
    const float fl = fx.flash.at(t);
    if (fl > 0) b.roundRect(x, y, w, h, 7 * U, alpha(col::white, fl * 0.9f), kShear);
    const float ts = 17 * U;
    const float lw = c.fonts->semibold.measure(ts, label);
    const float total = 17 * U + 20 * U + lw;
    const float cx0 = x + w / 2 - total / 2 - 10 * U;
    icon(c, fa::chevron_left, cx0 + 8 * U, y + h / 2, 17 * U, col::white);
    c.fonts->semibold.drawMid(b, cx0 + 17 * U + 20 * U, y + h / 2, ts, col::white, label);
    b.popTransform();
}

void footerButton(Context &c, ButtonFx &fx, float x, float top, const char *glyph, const char *label, uint32_t accent,
                  bool active, float a, int padHint) {
    const double t = ms(c);
    gfx::SpriteBatch &b = *c.batch;
    static const Palette pal = palette(hue::aquamarine);
    const float w = kFooterBtnW, h = kFooterBtnH, r = 10 * U;
    const float hv = fx.hover.at(t);
    b.pushAlpha(a);
    b.shadow(x, top + 2 * U, w, h, r, 4 * U, gfx::rgba(0, 0, 0, 64), kShear);
    uint32_t bg = lighten(pal.background3, 0.2f * hv);
    uint32_t fg = col::white, ic = accent;
    if (active) {
        bg = accent;
        fg = ic = pal.background6;
    }
    b.roundRect(x, top, w, h, r, bg, kShear);
    // Lueur : dégradé vertical de l'accent.
    b.roundRectGradient(x, top, w, h, r, gfx::Corners::vertical(alpha(accent, 0), alpha(accent, 0.2f)), kShear);
    const float fl = fx.flash.at(t);
    if (fl > 0) {
        b.setBlend(gfx::Blend::Additive);
        b.roundRect(x, top, w, h, r, alpha(col::white, fl), kShear);
        b.setBlend(gfx::Blend::Alpha);
    }
    // Le contenu suit l'inclinaison : décalé selon sa hauteur.
    auto sx = [&](float yy) { return x + w / 2 - kShear * (yy - (top + h / 2)); };
    icon(c, glyph, sx(top + 20 * U), top + 20 * U, 16 * U, ic);
    const float ts = 16 * U;
    c.fonts->semibold.drawMid(b, sx(top + 43 * U), top + 43 * U, ts, fg, label, gfx::Align::Center);
    b.roundRect(sx(top + h - 12.5f * U) - 50 * U, top + h - 15 * U, 100 * U, 5 * U, 3 * U, ic, kShear);
    if (padHint >= 0) padGlyph(c, static_cast<Pad>(padHint), sx(top + 12 * U) + w / 2 - 16 * U, top + 12 * U, 15 * U, 0.8f);
    b.popTransform();
}

void dialogButton(Context &c, ButtonFx &fx, float cx, float cy, float fullW, float h, const char *label,
                  uint32_t color, float a) {
    const double t = ms(c);
    gfx::SpriteBatch &b = *c.batch;
    const float hv = fx.hover.at(t), pr = fx.press.at(t);
    const float rel = 0.8f + 0.1f * hv - 0.018f * pr;
    const float w = fullW * rel;
    const float x = cx - w / 2, y = cy - h / 2;
    b.pushAlpha(a);
    // Lueur derrière (3 boîtes : fondu, plein, fondu).
    if (hv > 0) {
        const float gw = fullW * (0.864f + 0.108f * hv), gx = cx - gw / 2;
        const uint32_t gc = alpha(color, 0.5f * hv);
        const gfx::Corners l = gfx::Corners::horizontal(alpha(color, 0), gc), r = gfx::Corners::horizontal(gc, alpha(color, 0));
        b.roundRectGradient(gx, y, gw * 0.125f + 1, h, 0, l);
        b.roundRect(gx + gw * 0.125f, y, gw * 0.75f, h, 0, gc);
        b.roundRectGradient(gx + gw * 0.875f - 1, y, gw * 0.125f + 1, h, 0, r);
    }
    b.shadow(x, y, w, h, 5 * U, 6 * U, gfx::rgba(0, 0, 0, 13), kShear);
    b.roundRect(x, y, w, h, 5 * U, color, kShear);
    // Léger reflet vertical à la place des triangles.
    b.roundRectGradient(x, y, w, h, 5 * U, gfx::Corners::vertical(alpha(col::white, 0.12f), alpha(col::white, 0)), kShear);
    const float fl = fx.flash.at(t);
    if (fl > 0) b.roundRect(x, y, w, h, 5 * U, alpha(col::white, fl * 0.05f), kShear);
    const float ts = 28 * U * (1 + 0.02f * hv);
    c.fonts->bold.drawShadow(b, cx, c.fonts->bold.topForCenter(cy, ts), ts, col::white, label, gfx::Align::Center, ts * 0.4f * hv * 0.35f);
    b.popTransform();
}

void padGlyph(Context &c, Pad p, float cx, float cy, float s, float a) {
    gfx::SpriteBatch &b = *c.batch;
    b.circle(cx, cy, s * 0.5f, gfx::rgba(20, 20, 24, static_cast<uint8_t>(200 * a)));
    const float r = s * 0.27f, th = s * 0.075f;
    switch (p) {
    case Pad::Cross:
        b.line(cx - r * 0.8f, cy - r * 0.8f, cx + r * 0.8f, cy + r * 0.8f, th * 1.3f, alpha(hex(0x7cb2e8), a));
        b.line(cx + r * 0.8f, cy - r * 0.8f, cx - r * 0.8f, cy + r * 0.8f, th * 1.3f, alpha(hex(0x7cb2e8), a));
        break;
    case Pad::Circle: b.ring(cx, cy, r, th * 1.3f, alpha(hex(0xff6b6b), a)); break;
    case Pad::Square: b.roundRectBorder(cx - r * 0.85f, cy - r * 0.85f, r * 1.7f, r * 1.7f, th, th * 1.3f, alpha(hex(0xe59ad1), a)); break;
    case Pad::Triangle: b.triangle(cx, cy + r * 0.12f, r * 2.0f, alpha(hex(0x40e0b0), a), th * 1.3f); break;
    case Pad::Options: icon(c, fa::bars, cx, cy, s * 0.5f, alpha(col::white, a)); break;
    default: {
        const char *t = p == Pad::L1 ? "L1" : p == Pad::R1 ? "R1" : p == Pad::L2 ? "L2" : "R2";
        c.fonts->bold.drawMid(b, cx, cy, s * 0.5f, alpha(col::white, a), t, gfx::Align::Center);
    }
    }
}

// ---------------------------------------------------------------- Logo

void Logo::moveTo(const Context &c, float tx, float ty, float ts, double dur, Ease e, double delay) {
    const double t = ms(c);
    x.go(t, tx, dur, e, delay);
    y.go(t, ty, dur, e, delay);
    scale.go(t, ts, dur, e, delay);
}

void Logo::impact(const Context &c) {
    impactAlpha.fromTo(ms(c), 1, 0, 250, Ease::In);
    impactScale.fromTo(ms(c), 0.94f, 1.12f, 250, Ease::None);
}

void Logo::click(const Context &c) { flash.fromTo(ms(c), 0.4f, 0, 1500, Ease::OutExpo); }

void Logo::update(Context &c, const UiInput &in) {
    const double t = ms(c);
    const float r = radius(c);
    const float dx = in.mouseX - cx(c), dy = in.mouseY - cy(c);
    const bool over = interactive && opacity.at(t) > 0.5f && in.mouseActive && dx * dx + dy * dy < r * r;
    if (over != hovered_) {
        hovered_ = over;
        hoverScale.go(t, over ? 1.1f : 1.0f, 500, Ease::OutElastic);
    }
    clicked_ = over && in.mouseClick;
    if (clicked_) {
        click(c);
        bounce.fromTo(t, 0.9f, 1.0f, 500, Ease::OutElastic);
    }

    // Battement (60 ms d'avance) : petite compression puis retour, onde, triangles.
    if (c.trackRunning && c.beatIndexEarly != lastBeat_) {
        lastBeat_ = c.beatIndexEarly;
        const float adj = std::min(1.0f, 0.4f + c.amplitude);
        beatAdj_ = adj;
        beatAt_ = t;
        beatScale_.fromTo(t, 1, 1 - 0.02f * adj, 60, Ease::Out);
        ripple_.fromTo(t, 0.15f * adj, 0, c.beatLength, Ease::OutQuint);
        rippleScale_.fromTo(t, 1, 1 + 0.04f * adj, c.beatLength, Ease::OutQuint);
        if (c.kiai && flash.at(t) < 0.4f) flash.fromTo(t, 0.2f * adj, 0, c.beatLength, Ease::Out);
        triVelocity_ += adj * (c.kiai ? 6 : 3);
    }
    if (beatAt_ > 0 && t - beatAt_ >= 60 && beatScale_.target() < 1) beatScale_.go(t, 1, c.beatLength * 2, Ease::OutQuint);
    const float dtMs = c.dt * 1000;
    ampScale_ = anim::lerp(1 - std::max(0.0f, c.amplitude - 0.4f) * 0.04f, ampScale_, std::pow(0.9f, dtMs));
    const float targetVel = c.trackRunning ? 0.5f * (c.kiai ? 4 : 2) : 0.5f;
    triVelocity_ = anim::lerp(targetVel, triVelocity_, std::pow(0.995f, dtMs));

    // Visualiseur : nouvelles amplitudes toutes les 50 ms, lissées entre voisines,
    // puis chaque barre monte en douceur vers sa cible et redescend lentement.
    if (t - lastBarUpdate_ >= 50) {
        lastBarUpdate_ = t;
        const auto spec = c.music->spectrum();
        float raw[kLogoBars];
        for (int i = 0; i < kLogoBars; ++i) {
            const int src = (i + barOffset_) % kLogoBars;
            const float band = c.trackRunning ? spec[static_cast<size_t>(src * audio::Music::kBands / kLogoBars)] : 0;
            raw[i] = band * band * 0.55f * (c.kiai ? 1.0f : 0.5f) * 2;
        }
        for (int i = 0; i < kLogoBars; ++i) {
            const float l = raw[(i + kLogoBars - 1) % kLogoBars], r = raw[(i + 1) % kLogoBars];
            barTarget_[i] = std::max(barTarget_[i], raw[i] * 0.5f + (l + r) * 0.25f);
        }
        barOffset_ = (barOffset_ + 2) % kLogoBars;
    }
    const float rise = 1 - std::pow(0.982f, dtMs);
    for (int i = 0; i < kLogoBars; ++i) {
        float &v = bars_[i];
        if (barTarget_[i] > v) v += (barTarget_[i] - v) * rise;
        v = std::max(0.0f, v - dtMs * 0.0016f * (v + 0.03f));
        barTarget_[i] = std::max(0.0f, barTarget_[i] - dtMs * 0.0016f * (barTarget_[i] + 0.03f));
    }
}

void Logo::draw(Context &c) {
    const double t = ms(c);
    const float a = opacity.at(t);
    if (a <= 0.001f) return;
    gfx::SpriteBatch &b = *c.batch;
    const float s = scale.at(t) * hoverScale.at(t) * bounce.at(t) * beatScale_.at(t) * ampScale_;
    const float px = cx(c), py = cy(c);
    const float D = size() * s;     // disque visible
    const float R = D * 0.5f;
    const float full = 256 * U * s;  // rayon du sprite complet (512 u)

    // Visualiseur : 2 tours de barres partant du bord (moins nombreuses et plus
    // larges que lazer, pour un rendu plus doux).
    if (visualiser) {
        b.setBlend(gfx::Blend::Additive);
        const float bw = D * 0.018f;
        const uint32_t barCol = alpha(col::white, 0.13f * a);
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < kLogoBars; ++i) {
                const float amp = bars_[i];
                if (amp < 1.0f / 600) continue;
                const float len = 600 * U * amp * scale.at(t);
                const float ang = (static_cast<float>(i) / kLogoBars * 360 + static_cast<float>(j) * 180) * anim::kPi / 180;
                const float mid = R + len * 0.5f;
                b.rotated(b.white(), px + std::cos(ang) * mid, py + std::sin(ang) * mid, len, bw, ang, barCol);
            }
        b.setBlend(gfx::Blend::Alpha);
    }

    // Onde au rythme (copie additive du logo).
    const float rip = ripple_.at(t);
    if (rip > 0.001f) {
        b.setBlend(gfx::Blend::Additive);
        b.circle(px, py, full * 0.985f * rippleScale_.at(t), alpha(col::white, rip * a));
        b.setBlend(gfx::Blend::Alpha);
    }

    // Ombre, disque rose en dégradé, triangles.
    b.circle(px, py + R * 0.02f, full * 0.99f, gfx::rgba(0, 0, 0, static_cast<uint8_t>(60 * a)), R * 0.04f);
    b.shape(1, px, py, R, R, R, 0, 0,
            gfx::Corners::vertical(alpha(hex(0xff66ab), a), alpha(hex(0xcc5289), a)));
    // Reflet doux en haut du disque (pas de triangles).
    b.shape(1, px, py - R * 0.35f, R * 0.8f, R * 0.55f, R * 0.55f, 0, R * 0.25f,
            gfx::Corners::vertical(alpha(col::white, 0.10f * a), alpha(col::white, 0)));
    const float fl = flash.at(t);
    if (fl > 0) {
        b.setBlend(gfx::Blend::Additive);
        b.circle(px, py, R, alpha(col::white, fl * a));
        b.setBlend(gfx::Blend::Alpha);
    }
    // Anneau blanc épais et nom au centre.
    b.ring(px, py, full * 0.985f, full * 0.075f, alpha(col::white, a));
    const float ts = D * 0.25f;
    const float ly = c.fonts->bold.topForCenter(py, ts);
    c.fonts->bold.drawSdf(b, px, ly + D * 0.012f, ts, gfx::rgba(0, 0, 0, static_cast<uint8_t>(60 * a)), "rhythm",
                          gfx::Align::Center, -ts * 0.02f, 0.06f, 0.15f);
    c.fonts->bold.draw(b, px, ly, ts, alpha(col::white, a), "rhythm", gfx::Align::Center, -ts * 0.02f);
    // Impact (anneau qui s'élargit).
    const float ia = impactAlpha.at(t);
    if (ia > 0) b.ring(px, py, R * impactScale.at(t) / 0.94f, 10 * U * s, alpha(col::white, ia * a));
}

// ---------------------------------------------------------------- Flashs latéraux

void SideFlashes::update(Context &c) {
    if (!c.trackRunning || c.beatIndexEarly == lastBeat_) return;
    lastBeat_ = c.beatIndexEarly;
    const double t = ms(c);
    const bool downbeat = c.beatIndexEarly % 4 == 0;
    const float m = c.kiai ? 0.953f : 1.364f;
    const float a = std::clamp(0.1f + (c.amplitude - 0.25f) / m, 0.1f, 1.0f);
    auto flashSide = [&](Tween &tw) {
        tw.fromTo(t, tw.at(t), a, 65, Ease::None);
        tw.then(0, c.beatLength, Ease::In);
    };
    if (c.kiai) {
        flashSide(c.beatIndexEarly % 2 == 0 ? left_ : right_);
    } else if (downbeat) {
        flashSide(left_);
        flashSide(right_);
    }
}

void SideFlashes::draw(Context &c, float a) const {
    const double t = ms(c);
    gfx::SpriteBatch &b = *c.batch;
    b.setBlend(gfx::Blend::Additive);
    const float w = 200 * U;
    const float l = left_.at(t) * a, r = right_.at(t) * a;
    if (l > 0) b.roundRectGradient(0, 0, w, kVH, 0, gfx::Corners::horizontal(alpha(col::blue, 0.6f * l), alpha(col::blue, 0)));
    if (r > 0)
        b.roundRectGradient(kVW - w, 0, w, kVH, 0, gfx::Corners::horizontal(alpha(col::blue, 0), alpha(col::blue, 0.6f * r)));
    b.setBlend(gfx::Blend::Alpha);
}

// ---------------------------------------------------------------- Barre d'outils

void Toolbar::show(const Context &c, bool s) {
    const double t = ms(c);
    if ((shown.target() > 0.5f) == s) return;
    shown.go(t, s ? 1 : 0, 500, Ease::OutQuint);
}

Toolbar::Action Toolbar::update(Context &c, const UiInput &in) {
    const double t = ms(c);
    if (shown.at(t) < 0.9f) return None;
    const float y = bottom(c) - height();
    hovered_ = -1;
    for (int i = 0; i < 4; ++i) {
        const bool h = in.hover(bx_[i], y, height(), height());
        if (h) hovered_ = i;
        fx_[i].setHover(c, h, 300, 200);
        if (h && in.mouseClick) {
            fx_[i].flash.fromTo(t, 100.0f / 255, 0, 800, Ease::OutQuint);
            c.play(c.sfx->uiSelect, 0.7f);
            return i == 0 ? Settings : i == 1 ? Home : i == 2 ? Download : Music;
        }
    }
    return None;
}

void Toolbar::draw(Context &c) {
    const double t = ms(c);
    const float sh = shown.at(t);
    if (sh <= 0.001f) return;
    gfx::SpriteBatch &b = *c.batch;
    const float y = -(1 - sh) * height();
    b.pushAlpha(std::min(1.0f, sh * 3));
    b.rect(0, y, kVW, height(), hex(0x1a1a1a));
    bx_[0] = 0;
    bx_[1] = height();
#if defined(__PROSPERO__)
    const float rightPad = 8 * U;  // pas d'horloge sur PS5
#else
    const float rightPad = c.fonts->semibold.measure(18 * U, "00:00") + 36 * U;
#endif
    bx_[2] = kVW - rightPad - height() * 2;
    bx_[3] = kVW - rightPad - height();
    const char *glyphs[4] = {fa::gear, fa::house, fa::download, fa::music};
    const char *tips[4] = {"Options", "Accueil", "Télécharger des maps", "Musique"};
    for (int i = 0; i < 4; ++i) {
        const float hv = fx_[i].hover.at(t), fl = fx_[i].flash.at(t);
        if (hv > 0 || fl > 0) {
            b.setBlend(gfx::Blend::Additive);
            b.roundRect(bx_[i] + 3 * U, y + 3 * U, height() - 6 * U, height() - 6 * U, 6 * U,
                        alpha(hex(0x505050), hv * 180.0f / 255 + fl));
            b.setBlend(gfx::Blend::Alpha);
        }
        icon(c, glyphs[i], bx_[i] + height() / 2, y + height() / 2, 20 * U, col::white);
        if (hv > 0.01f) {
            const float ts = 15 * U;
            const float tw = c.fonts->bold.measure(ts, tips[i]);
            float tx = bx_[i] + height() / 2 - tw / 2;
            tx = std::clamp(tx, 8.0f, kVW - tw - 8);
            b.roundRect(tx - 8, y + height() + 6, tw + 16, ts + 14, 6, alpha(gfx::rgba(0, 0, 0, 200), hv));
            c.fonts->bold.drawMid(b, tx, y + height() + 6 + (ts + 14) / 2, ts, alpha(col::white, hv), tips[i]);
        }
    }
#if !defined(__PROSPERO__)
    // Horloge (sur PS5, l'heure locale n'est pas accessible depuis l'appli pour l'instant).
    time_t now = time(nullptr);
    struct tm lt;
#if defined(_WIN32)
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    c.fonts->semibold.drawMid(b, kVW - 20 * U, y + height() / 2, 18 * U, col::white, buf, gfx::Align::Right);
#endif
    b.popTransform();
}

// ---------------------------------------------------------------- Divers

float text(Context &c, const gfx::Font &f, float x, float y, float size, uint32_t color, const std::string &s, gfx::Align a,
           float maxW, bool shadow) {
    const std::string str = maxW > 0 ? f.ellipsize(size, s, maxW) : s;
    return shadow ? f.drawShadow(*c.batch, x, y, size, color, str.c_str(), a) : f.draw(*c.batch, x, y, size, color, str.c_str(), a);
}

std::vector<WrapToken> wrapTokens(const std::string &s) {
    std::vector<WrapToken> out;
    WrapToken cur;
    bool pendingSpace = false, pendingNewline = false;
    auto push = [&] {
        if (cur.text.empty()) return;
        out.push_back(cur);
        cur = WrapToken{};
    };
    for (size_t i = 0; i < s.size();) {
        const unsigned char c0 = static_cast<unsigned char>(s[i]);
        if (c0 == ' ' || c0 == '\n') {
            push();
            pendingSpace = true;
            if (c0 == '\n') pendingNewline = true;
            ++i;
            continue;
        }
        const size_t len = c0 < 0x80 ? 1 : c0 < 0xE0 ? 2 : c0 < 0xF0 ? 3 : 4;
        const std::string ch = s.substr(i, len);
        i += len;
        // U+3000 à U+9FFF (kana, kanji, ponctuation) et formes pleine chasse U+FF00 à U+FFEF.
        const bool cjk = len == 3 && ((c0 >= 0xE3 && c0 <= 0xE9) || c0 == 0xEF);
        if (!cjk) {
            if (cur.text.empty()) {
                cur.space = pendingSpace && !out.empty();
                cur.newline = pendingNewline;
                pendingSpace = pendingNewline = false;
            }
            cur.text += ch;
            continue;
        }
        // Ponctuation de fin (、。」）！？) : jamais en début de ligne, collée au mot d'avant.
        static const char *const kNoBreakBefore[] = {"、", "。", "」", "』", "）", "！", "？", "ー", "っ", "ゃ", "ゅ", "ょ", "ッ", "ャ", "ュ", "ョ"};
        bool glue = false;
        for (const char *p : kNoBreakBefore) glue |= ch == p;
        if (glue && (!cur.text.empty() || !out.empty()) && !pendingSpace) {
            if (cur.text.empty()) out.back().text += ch;
            else cur.text += ch;
            continue;
        }
        push();
        cur.space = pendingSpace && !out.empty();
        cur.newline = pendingNewline;
        pendingSpace = pendingNewline = false;
        cur.text = ch;
        push();
    }
    push();
    return out;
}

float paragraph(Context &c, const gfx::Font &f, float x, float y, float width, float size, uint32_t color, const std::string &s,
                gfx::Align a, float lineHeight) {
    std::string line, word;
    float yy = y;
    auto flush = [&]() {
        if (!line.empty()) f.draw(*c.batch, x, yy, size, color, line.c_str(), a);
        yy += size * lineHeight;
        line.clear();
    };
    for (const WrapToken &w : wrapTokens(i18n::tr(s))) {
        if (w.newline) flush();
        const std::string candidate = line.empty() ? w.text : line + (w.space ? " " : "") + w.text;
        if (!line.empty() && f.measure(size, candidate.c_str()) > width) {
            flush();
            line = w.text;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) flush();
    return yy - y;
}

void cursor(Context &c, float x, float y, bool pressed) {
    gfx::SpriteBatch &b = *c.batch;
    const float s = pressed ? 0.9f : 1.0f;
    const float len = 34 * s;
    // Flèche : triangle allongé pointant en haut à gauche, contour sombre.
    const float ang = -0.62f;
    const float cx = x + std::sin(-ang) * len * 0.45f, cy = y + std::cos(-ang) * len * 0.45f;
    b.triangle(cx, cy + 1.5f, len * 0.9f, gfx::rgba(0, 0, 0, 120), 0, ang);
    b.triangle(cx, cy, len * 0.82f, col::white, 0, ang);
    b.triangle(cx, cy, len * 0.82f, col::pink1, len * 0.08f, ang);
}

}  // namespace lz

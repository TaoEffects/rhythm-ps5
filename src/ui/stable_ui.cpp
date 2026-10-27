// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/stable_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/skin.h"

namespace st {

uint32_t rankColour(const std::string &rank, bool silver) {
    if (rank == "SS" || rank == "S") return silver ? lz::hex(0xdde4ee) : lz::hex(0xffbe1e);
    if (rank == "A") return lz::hex(0x7ed321);
    if (rank == "B") return lz::hex(0x2f8fe8);
    if (rank == "C") return lz::hex(0xc650e0);
    return lz::hex(0xe8323c);
}

float number(Context &c, const std::string &text, float x, float y, float h, uint32_t color, int align, bool combo) {
    gfx::SpriteBatch &b = *c.batch;
    const game::SkinUi *ui = c.skin ? &c.skin->ui : nullptr;
    const game::SkinSprite *digits = ui ? (combo ? ui->comboDigits : ui->scoreDigits) : nullptr;
    if (digits && digits[0].ok()) {
        // Chiffres du skin : hauteur imposée, chevauchement du skin.ini.
        const float k = h / digits[0].h();
        const float overlap = static_cast<float>(combo ? ui->comboOverlap : ui->scoreOverlap) * k;
        auto glyph = [&](char ch) -> const game::SkinSprite * {
            if (ch >= '0' && ch <= '9') return &digits[ch - '0'];
            if (ch == ',') return &ui->scoreComma;
            if (ch == '.') return &ui->scoreDot;
            if (ch == '%') return &ui->scorePercent;
            if (ch == 'x') return combo ? &ui->comboX : &ui->scoreX;
            return nullptr;
        };
        float total = 0;
        for (char ch : text)
            if (const game::SkinSprite *s = glyph(ch); s && s->ok()) total += s->w() * k - overlap;
        total += overlap;
        float px = align == 0 ? x : align == 1 ? x - total / 2 : x - total;
        for (char ch : text) {
            const game::SkinSprite *s = glyph(ch);
            if (!s || !s->ok()) continue;
            b.quad(s->tex, px, y, s->w() * k, s->h() * k, color);
            px += s->w() * k - overlap;
        }
        return total;
    }
    // Sans skin : chiffres épais à contour sombre, comme les polices de score de stable.
    const gfx::Align a = align == 0 ? gfx::Align::Left : align == 1 ? gfx::Align::Center : gfx::Align::Right;
    const float size = h * 1.18f;
    const gfx::Font &f = c.fonts->regular;
    const float top = f.topForCenter(y + h / 2, size);
    f.drawSdf(b, x, top, size, gfx::rgba(0, 0, 0, static_cast<uint8_t>(((color >> 24) & 0xff) * 0.7f)), text.c_str(), a, 0, 0.1f, 0.05f);
    return f.draw(b, x, top, size, color, text.c_str(), a);
}

void rankLetter(Context &c, const std::string &rank, bool silver, float cx, float cy, float h, float alpha, bool small) {
    gfx::SpriteBatch &b = *c.batch;
    if (c.skin) {
        const game::SkinSprite &s =
            (small ? c.skin->ui.rankingSmall : c.skin->ui.ranking)[game::rankIndex(rank, silver)];
        if (s.ok()) {
            const float k = h / s.h();
            b.quad(s.tex, cx - s.w() * k / 2, cy - h / 2, s.w() * k, h, gfx::withAlpha(lz::col::white, alpha));
            return;
        }
    }
    // Lettre dessinée façon stable : ombre décalée foncée, contour, remplissage plus clair en haut.
    const uint32_t rc = rankColour(rank, silver);
    const float size = h * 1.15f;
    const gfx::Font &f = c.fonts->bold;
    const float top = f.topForCenter(cy, size);
    const char *txt = rank.c_str();
    const float sp = rank == "SS" ? -h * 0.12f : 0;
    const uint32_t dark = lz::darken(rc, small ? 1.2f : 0.9f);
    if (!small) f.drawSdf(b, cx + h * 0.035f, top + h * 0.035f, size, gfx::withAlpha(lz::darken(rc, 1.6f), 0.9f * alpha), txt,
                          gfx::Align::Center, sp, 0.14f, 0.03f);
    f.drawSdf(b, cx, top, size, gfx::withAlpha(dark, alpha), txt, gfx::Align::Center, sp, small ? 0.08f : 0.12f, 0.02f);
    f.draw(b, cx, top, size, gfx::withAlpha(rc, alpha), txt, gfx::Align::Center, sp);
    if (!small) {
        b.pushClip(cx - h * 2, cy - h, h * 4, h * 0.85f);
        f.draw(b, cx, top, size, gfx::withAlpha(lz::lighten(rc, 0.25f), alpha), txt, gfx::Align::Center, sp);
        b.popClip();
    }
}

bool localTime(long long t, struct ::tm &out) {
#if defined(__PROSPERO__)
    (void)t;
    (void)out;
    return false;
#else
    const time_t tt = static_cast<time_t>(t);
#if defined(_WIN32)
    return localtime_s(&out, &tt) == 0;
#else
    return localtime_r(&tt, &out) != nullptr;
#endif
#endif
}

std::string grouped(long long v) {
    char raw[32];
    snprintf(raw, sizeof(raw), "%lld", v);
    std::string s(raw), r;
    int k = 0;
    for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
        r.insert(r.begin(), s[static_cast<size_t>(i)]);
        if (++k % 3 == 0 && i > 0) r.insert(r.begin(), ',');
    }
    return r;
}

void cursor(Context &c, float x, float y, bool pressed) {
    gfx::SpriteBatch &b = *c.batch;
    const float k = kVH / 768.0f;
    if (c.skin && c.skin->cursor.ok()) {
        const game::Skin &sk = *c.skin;
        const float grow = pressed && sk.cursorExpand ? 1.15f : 1.0f;
        const float ang = sk.cursorRotate ? static_cast<float>(std::fmod(c.now, 10.0) / 10.0 * 2 * anim::kPi) : 0;
        const float w = sk.cursor.w() * k * grow, h = sk.cursor.h() * k * grow;
        if (ang != 0) b.rotated(sk.cursor.tex, x, y, w, h, ang, lz::col::white);
        else b.quad(sk.cursor.tex, x - w / 2, y - h / 2, w, h, lz::col::white);
        if (sk.cursormiddle.ok())
            b.quad(sk.cursormiddle.tex, x - sk.cursormiddle.w() * k / 2, y - sk.cursormiddle.h() * k / 2, sk.cursormiddle.w() * k,
                   sk.cursormiddle.h() * k, lz::col::white);
        return;
    }
    // Curseur par défaut de stable : disque jaune orangé bordé de blanc, halo léger.
    const float r = 15 * k * (pressed ? 1.15f : 1.0f);
    b.circle(x, y, r * 1.6f, gfx::rgba(255, 190, 60, 50), r * 0.8f);
    b.circle(x, y, r, gfx::rgba(255, 255, 255, 235));
    b.circle(x, y, r * 0.8f, gfx::rgba(255, 196, 46));
    b.circle(x, y, r * 0.45f, gfx::rgba(255, 232, 150));
}

}  // namespace st

namespace st {

float backButtonW() { return 128 * kVH / 1080.0f; }
float backButtonH() { return 65 * kVH / 1080.0f; }

bool backHovered(const UiInput &in) {
    return in.mouseActive && in.hover(0, kVH - 18 * kVH / 1080.0f - backButtonH(), backButtonW() + 10, backButtonH());
}

void backButton(Context &c, ButtonFx &fx, float alpha) {
    gfx::SpriteBatch &b = *c.batch;
    const float k = kVH / 1080.0f;
    const float a = fx.hover.at(lz::ms(c));
    const float w = backButtonW() + 14 * k * a, h = backButtonH();
    const float y = kVH - 18 * k - h;
    b.pushAlpha(alpha);
    if (c.skin && c.skin->ui.menuBack.ok()) {
        const game::SkinSprite &s = c.skin->ui.menuBack;
        const float ks = 121.5f * k / std::max(s.w() / 225 * 87, s.h());  // 225 x 87 à 54 px osu, comme stable
        b.pushScaleAround(0, kVH, 1 + 0.01f * a, 1);
        b.quad(s.tex, 0, kVH - s.h() * ks, s.w() * ks, s.h() * ks, lz::col::white);
        b.popTransform();
    } else {
        // Languette rose inclinée : flèche dans un rond et « retour ».
        const uint32_t pink = lz::lighten(gfx::rgba(232, 72, 150), 0.15f * a);
        b.roundRectGradient(-20 * k, y, w + 20 * k, h, 3 * k, gfx::Corners::vertical(lz::lighten(pink, 0.12f), lz::darken(pink, 0.12f)), 0.18f);
        const float cy = y + h / 2;
        b.circle(22 * k, cy, 13 * k, lz::col::white);
        b.triangle(20 * k, cy, 13 * k, pink, 0, -anim::kPi / 2);
        c.fonts->regular.drawMid(b, 44 * k, cy, 30 * k, lz::col::white, "retour");
    }
    if (a > 0.001f && !(c.skin && c.skin->ui.menuBack.ok())) {
        b.setBlend(gfx::Blend::Additive);
        b.roundRect(-20 * k, y, w + 20 * k, h, 3 * k, gfx::withAlpha(lz::col::white, 0.15f * a), 0.18f);
        b.setBlend(gfx::Blend::Alpha);
    }
    b.popTransform();
}

}  // namespace st

#include <map>

#include "app/game.h"
#include "core/i18n.h"

namespace st {

PlayerStats playerStats(const std::vector<::LocalScore> &scores) {
    PlayerStats s;
    std::map<std::string, const ::LocalScore *> best;
    for (const ::LocalScore &sc : scores) {
        s.totalScore += sc.score;
        ++s.plays;
        auto it = best.find(sc.key);
        if (it == best.end() || sc.pp > it->second->pp || (sc.pp == it->second->pp && sc.score > it->second->score)) best[sc.key] = &sc;
    }
    std::vector<const ::LocalScore *> list;
    for (const auto &kv : best) list.push_back(kv.second);
    std::sort(list.begin(), list.end(), [](const ::LocalScore *a, const ::LocalScore *b) { return a->pp > b->pp; });
    double w = 1, wsum = 0, acc = 0;
    for (const ::LocalScore *sc : list) {
        s.pp += sc->pp * w;
        acc += sc->accuracy * w;
        wsum += w;
        w *= 0.95;
    }
    s.accuracy = wsum > 0 ? acc / wsum : 0;
    // Score requis pour le niveau n (formule d'osu!).
    auto need = [](int n) {
        if (n <= 100) return 5000.0 / 3 * (4.0 * n * n * n - 3.0 * n * n - n) + 1.25 * std::pow(1.8, n - 60);
        return 26931190827.0 + 99999999999.0 * (n - 100);
    };
    const double total = static_cast<double>(s.totalScore);
    int n = 1;
    while (n < 200 && need(n) <= total) ++n;
    s.level = n;
    const double lo = n > 1 ? need(n - 1) : 0, hi = need(n);
    s.levelProgress = static_cast<float>(std::clamp((total - lo) / std::max(1.0, hi - lo), 0.0, 1.0));
    return s;
}

void userCard(Context &c, const PlayerStats &s, float x, float y, float w, float h, float hover, float alpha) {
    gfx::SpriteBatch &b = *c.batch;
    b.pushAlpha(alpha);
    b.rect(x, y, w, h, gfx::rgba(255, 255, 255, static_cast<uint8_t>(45 * hover)));
    const float pad = h * 0.06f, av = h - 2 * pad;
    b.rect(x + pad, y + pad, av, av, gfx::rgba(26, 26, 26));
    lz::icon(c, fa::user, x + pad + av / 2, y + pad + av / 2, av * 0.55f, gfx::rgba(150, 150, 150));
    const float tx = x + pad + av + h * 0.0775f;
    c.fonts->regular.drawShadow(b, tx, y + h * 0.04f, h * 0.25f, lz::col::white, "Joueur local");
    char line[96];
    snprintf(line, sizeof(line), i18n::tr("Performance : %spp"), grouped(std::llround(s.pp)).c_str());
    c.fonts->regular.drawShadow(b, tx, y + h * 0.36f, h * 0.15f, lz::col::white, line);
    snprintf(line, sizeof(line), i18n::tr("Précision : %.2f%%"), s.accuracy * 100);
    c.fonts->regular.drawShadow(b, tx, y + h * 0.54f, h * 0.15f, lz::col::white, line);
    snprintf(line, sizeof(line), i18n::tr("Niv. %d"), s.level);
    const float lw = c.fonts->regular.drawShadow(b, tx, y + h * 0.74f, h * 0.15f, lz::col::white, line);
    const float bx = tx + lw + h * 0.08f, by = y + h * 0.77f, bw = x + w - pad - bx, bh = h * 0.1f;
    if (bw > 10) {
        b.roundRect(bx, by, bw, bh, bh / 2, gfx::rgba(64, 64, 64));
        if (s.levelProgress > 0) b.roundRect(bx, by, bw * s.levelProgress, bh, bh / 2, gfx::rgba(255, 219, 124));
        b.roundRectBorder(bx, by, bw, bh, bh / 2, 1.5f, gfx::rgba(170, 170, 170));
    }
    b.popTransform();
}

}  // namespace st

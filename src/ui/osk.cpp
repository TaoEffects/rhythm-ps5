// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/osk.h"

#include <algorithm>
#include <cmath>

#include "core/i18n.h"
#include "ui/lz.h"

namespace lz {
namespace {

using anim::Ease;

const char *const kRows[4] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm-.!"};
constexpr int kCols = 10, kRowsCount = 5;  // 4 rangées de lettres + espace / effacer / OK
constexpr int kMaxKeys = 4 * kCols + 3;
const char *const kSpace = "espace", *const kBack = "effacer", *const kOk = "OK";

const Palette &pal() {
    static const Palette p = palette(hue::purple);
    return p;
}

}  // namespace

void OnScreenKeyboard::open(Context &c, const std::string &title, const std::string &initial) {
    open_ = true;
    result_ = 0;
    title_ = title;
    text_ = initial;
    row_ = 1;
    col_ = 0;
    slide_.fromTo(ms(c), 0, 1, 350, Ease::OutQuint);
    c.play(c.sfx->uiOpen, 0.6f);
}

int OnScreenKeyboard::poll(std::string &out) {
    if (open_ || result_ == 0) return 0;
    const int r = result_;
    result_ = 0;
    if (r > 0) out = text_;
    return r;
}

void OnScreenKeyboard::finish(int result) {
    open_ = false;
    result_ = result;
}

void OnScreenKeyboard::layout(const Context &, Key *keys, int &count) const {
    const float kw = 74 * U, kh = 56 * U, gap = 8 * U;
    const float total = kCols * kw + (kCols - 1) * gap;
    const float x0 = kVW / 2 - total / 2;
    const float y0 = kVH - 20 * U - kRowsCount * kh - (kRowsCount - 1) * gap;
    count = 0;
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < kCols; ++k)
            keys[count++] = {x0 + k * (kw + gap), y0 + r * (kh + gap), kw, kh, r, k, std::string(1, kRows[r][k])};
    // Dernière rangée : espace (6 colonnes), effacer (2), OK (2).
    const float y = y0 + 4 * (kh + gap);
    const float unit = kw + gap;
    keys[count++] = {x0, y, 6 * unit - gap, kh, 4, 0, kSpace};
    keys[count++] = {x0 + 6 * unit, y, 2 * unit - gap, kh, 4, 1, kBack};
    keys[count++] = {x0 + 8 * unit, y, 2 * unit - gap, kh, 4, 2, kOk};
}

void OnScreenKeyboard::press(Context &c, const std::string &label) {
    if (label == kOk) {
        c.play(c.sfx->uiSelect);
        finish(1);
    } else if (label == kBack) {
        if (!text_.empty()) {
            size_t cut = text_.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(text_[cut]) & 0xC0) == 0x80) --cut;
            text_.resize(cut);
        }
        c.play(c.sfx->uiHover, 0.6f);
    } else {
        if (text_.size() < 100) text_ += label == kSpace ? " " : label;
        c.play(c.sfx->uiHover, 0.6f);
    }
}

void OnScreenKeyboard::update(Context &c, const UiInput &in) {
    if (!open_) return;
    Key keys[kMaxKeys];
    int n = 0;
    layout(c, keys, n);
    // Pointeur : survol et clic.
    if (in.mouseActive) {
        for (int i = 0; i < n; ++i)
            if (in.hover(keys[i].x, keys[i].y, keys[i].w, keys[i].h)) {
                row_ = keys[i].row;
                col_ = keys[i].col;
                if (in.mouseClick) {
                    press(c, keys[i].label);
                    return;
                }
            }
    }
    const int oldRow = row_, oldCol = col_;
    auto colsIn = [](int r) { return r == 4 ? 3 : kCols; };
    // Passage vers / depuis la dernière rangée : on garde la position horizontale.
    auto toRow4 = [](int col) { return col < 6 ? 0 : col < 8 ? 1 : 2; };
    auto fromRow4 = [](int col) { return col == 0 ? 2 : col == 1 ? 6 : 8; };
    if (in.left) col_ = (col_ + colsIn(row_) - 1) % colsIn(row_);
    if (in.right) col_ = (col_ + 1) % colsIn(row_);
    if (in.down && row_ < 4) {
        ++row_;
        if (row_ == 4) col_ = toRow4(col_);
    }
    if (in.up && row_ > 0) {
        if (row_ == 4) col_ = fromRow4(col_);
        --row_;
    }
    if (row_ != oldRow || col_ != oldCol) c.play(c.sfx->uiHover, 0.4f);
    if (in.confirm) {
        for (int i = 0; i < n; ++i)
            if (keys[i].row == row_ && keys[i].col == col_) press(c, keys[i].label);
    }
    if (in.extra2) press(c, kBack);   // Carré
    if (in.extra) press(c, kSpace);   // Triangle
    if (in.r1 || in.options) press(c, kOk);
    if (in.back) {
        c.play(c.sfx->uiBack);
        finish(-1);
    }
}

void OnScreenKeyboard::draw(Context &c) const {
    if (!open_) return;
    gfx::SpriteBatch &b = *c.batch;
    const float k = slide_.at(ms(c));
    Key keys[kMaxKeys];
    int n = 0;
    layout(c, keys, n);
    b.rect(0, 0, kVW, kVH, alpha(col::black, 0.5f * k));
    const float top = keys[0].y - 110 * U;
    b.pushTransform(0, (1 - k) * (kVH - top), 1, k);
    b.rect(0, top, kVW, kVH - top, alpha(pal().background4, 0.97f));
    b.rect(0, top, kVW, 2 * U, col::pink1);
    // Titre et zone de texte.
    const float fx = keys[0].x, fw = keys[kCols - 1].x + keys[kCols - 1].w - fx;
    c.fonts->light.drawMid(b, fx, top + 28 * U, 24 * U, col::white, title_.c_str());
    const float ty = top + 50 * U, th = 44 * U;
    b.roundRect(fx, ty, fw, th, 6 * U, pal().background6);
    const std::string shown = c.fonts->regular.ellipsize(20 * U, text_, fw - 40 * U);
    const float tw = c.fonts->regular.drawMid(b, fx + 14 * U, ty + th / 2, 20 * U, col::white, shown.c_str());
    if (static_cast<long>(ms(c) / 500) % 2 == 0) b.rect(fx + 16 * U + tw, ty + 10 * U, 2 * U, th - 20 * U, col::pink1);
    // Touches.
    for (int i = 0; i < n; ++i) {
        const Key &key = keys[i];
        const bool focus = key.row == row_ && key.col == col_;
        const bool ok = key.label == kOk;
        uint32_t bg = ok ? col::purple1 : pal().background2;
        if (focus) bg = ok ? lighten(col::purple1, 0.25f) : pal().background1;
        b.roundRect(key.x, key.y, key.w, key.h, 8 * U, bg);
        if (focus) b.roundRectBorder(key.x - 2, key.y - 2, key.w + 4, key.h + 4, 9 * U, 2.5f, col::pink1);
        const float cx = key.x + key.w / 2, cy = key.y + key.h / 2;
        c.fonts->semibold.drawMid(b, cx, cy, key.label.size() == 1 ? 24 * U : 18 * U, col::white, key.label.c_str(),
                                  gfx::Align::Center);
    }
    // Aide manette.
    float hx = fx + fw;
    auto hint = [&](Pad p, const char *label) {
        const float w = c.fonts->regular.measure(14 * U, label);
        hx -= w;
        c.fonts->regular.drawMid(b, hx, top + 28 * U, 14 * U, pal().content2, label);
        hx -= 18 * U;
        padGlyph(c, p, hx, top + 28 * U, 18 * U);
        hx -= 26 * U;
    };
    hint(Pad::Circle, "annuler");
    hint(Pad::R1, "valider");
    hint(Pad::Triangle, "espace");
    hint(Pad::Square, "effacer");
    b.popTransform();
}

}  // namespace lz

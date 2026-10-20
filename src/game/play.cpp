// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/play.h"
#include "game/storyboard.h"
#include "core/i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/anim.h"
#include "core/log.h"

namespace game {
namespace {

constexpr float kPi = 3.14159265358979f;

double difficultyRange(double d, double lo, double mid, double hi) {
    if (d > 5) return mid + (hi - mid) * (d - 5) / 5;
    if (d < 5) return mid - (mid - lo) * (5 - d) / 5;
    return mid;
}

uint32_t rgbaf(float r, float g, float b, float a) {
    auto c = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return gfx::rgba(c(r), c(g), c(b), c(a));
}

uint32_t scaleRgb(uint32_t color, float k) {
    const float r = static_cast<float>(color & 255) / 255.0f, g = static_cast<float>((color >> 8) & 255) / 255.0f,
                b = static_cast<float>((color >> 16) & 255) / 255.0f;
    return rgbaf(r * k, g * k, b * k, static_cast<float>(color >> 24) / 255.0f);
}

// Couleurs de combo par défaut de lazer (quand la map n'en définit pas).
const uint32_t kDefaultCombo[] = {gfx::rgba(255, 192, 0), gfx::rgba(0, 202, 0), gfx::rgba(18, 124, 255),
                                  gfx::rgba(242, 24, 57)};

}  // namespace

float Mods::scoreMultiplier() const {
    float m = 1;
    if (noFail) m *= 0.5f;
    if (easy) m *= 0.5f;
    if (hardRock) m *= 1.06f;
    if (hidden) m *= 1.06f;
    if (doubleTime || nightcore) m *= 1.12f;
    if (halfTime) m *= 0.3f;
    return m;
}

std::string Mods::label() const {
    std::string s;
    if (easy) s += "EZ ";
    if (noFail) s += "NF ";
    if (halfTime) s += "HT ";
    if (hidden) s += "HD ";
    if (nightcore) s += "NC ";
    else if (doubleTime) s += "DT ";
    if (autoplay) s += "AUTO ";
    if (hardRock) s += "HR ";
    if (!s.empty()) s.pop_back();
    return s;
}

bool Play::start(Context &ctx, Beatmap map, const Mods &mods, const gfx::Texture *background) {
    map_ = std::move(map);
    mods_ = mods;
    background_ = background;
    skin_ = ctx.skin && ctx.settings->skin == 2 ? ctx.skin : nullptr;
    cs_ = map_.cs;
    ar_ = map_.ar;
    od_ = map_.od;
    hpSetting_ = map_.hp;
    if (mods.easy) {
        cs_ *= 0.5f;
        ar_ *= 0.5f;
        od_ *= 0.5f;
        hpSetting_ *= 0.5f;
    }
    if (mods.hardRock) {
        cs_ = std::min(10.0f, cs_ * 1.3f);
        ar_ = std::min(10.0f, ar_ * 1.4f);
        od_ = std::min(10.0f, od_ * 1.4f);
        hpSetting_ = std::min(10.0f, hpSetting_ * 1.4f);
        // Hard Rock retourne la map verticalement.
        for (HitObject &o : map_.objects) {
            o.pos.y = 384 - o.pos.y;
            for (Vec2 &p : o.controlPoints) p.y = 384 - p.y;
        }
    }
    map_.cs = cs_;
    map_.ar = ar_;
    prepareBeatmap(map_);

    // Aire de jeu : l'espace 640x480 d'osu! remplit la hauteur de l'écran.
    scale_ = kVH / 480.0f;
    fieldX_ = (kVW - 512.0f * scale_) / 2.0f;
    fieldY_ = (kVH - 384.0f * scale_) / 2.0f + 8.0f * scale_;
    radius_ = circleRadius(cs_) * scale_;
    preempt_ = preemptMs(ar_);
    fadeIn_ = fadeInMs(ar_);
    w300_ = hitWindow300(od_);
    w100_ = hitWindow100(od_);
    w50_ = hitWindow50(od_);

    state_.assign(map_.objects.size(), State{});
    for (size_t i = 0; i < map_.objects.size(); ++i) {
        const HitObject &o = map_.objects[i];
        State &s = state_[i];
        if (o.type == kSlider) {
            for (int span = 0; span < o.slides; ++span) {
                for (const SliderTick &tick : o.ticks) {
                    const double within = span % 2 == 0 ? tick.progress : 1.0 - tick.progress;
                    s.events.push_back({o.time + (span + within) * o.spanDuration, 0, span});
                }
                if (span + 1 < o.slides) s.events.push_back({o.time + (span + 1) * o.spanDuration, 1, span});
            }
            // La fin est vérifiée un peu avant la vraie fin (règle d'osu!stable).
            s.events.push_back({std::max(o.time + (o.endTime - o.time) / 2, o.endTime - 36), 2, o.slides - 1});
            std::sort(s.events.begin(), s.events.end(), [](const SliderEvent &a, const SliderEvent &b) { return a.time < b.time; });
            s.partsTotal = 1 + static_cast<int>(s.events.size());
        } else if (o.type == kSpinner) {
            const double seconds = (o.endTime - o.time) / 1000.0;
            s.required = static_cast<float>(std::max(0.0, std::floor(seconds * difficultyRange(od_, 1.5, 2.5, 3.75))));
        }
    }
    if (map_.objects.empty()) return false;
    firstObject_ = map_.objects.front().time;
    lastObject_ = 0;
    for (const HitObject &o : map_.objects) lastObject_ = std::max(lastObject_, o.endTime);
    const double drainSec = std::max(1.0, (lastObject_ - firstObject_) / 1000.0);
    difficultyMultiplier_ = std::round((map_.hp + map_.cs + map_.od +
                                        std::clamp(static_cast<double>(map_.objects.size()) / drainSec * 8, 0.0, 16.0)) /
                                       38.0 * 5.0);

    // Départ : au moins 1,5 s avant l'apparition du premier objet.
    startTime_ = std::min({0.0, firstObject_ - preempt_ - 1500.0, -static_cast<double>(map_.audioLeadIn)});
    ctx.music->setLoop(false, 0);
    ctx.music->setVolume(ctx.settings->musicVolume);
    ctx.music->setRate(mods_.rate(), !mods_.nightcore);
    ctx.music->play(startTime_ / 1000.0);

    sliderTarget_.destroy();
    sliderTarget_.create(plat::width() > 0 ? plat::width() : 1920, plat::height() > 0 ? plat::height() : 1080);

    score_ = 0;
    displayScore_ = 0;
    combo_ = maxCombo_ = 0;
    n300_ = n100_ = n50_ = nMiss_ = 0;
    hp_ = displayHp_ = 1;
    errors_.clear();
    popups_.clear();
    errorMarks_.clear();
    trail_.clear();
    firstActive_ = 0;
    paused_ = finished_ = failed_ = false;
    failTime_ = endTime_ = -1;
    lastT_ = startTime_;
    lastWall_ = ctx.now;
    result_ = PlayResult{};
    timed_ = computeTimedDifficulty(map_, difficultyMods());
    displayPp_ = 0;
    LOG("play: %s - %s [%s] AR%.1f OD%.1f CS%.1f HP%.1f mods='%s' depart=%.0fms", map_.artist.c_str(),
        map_.title.c_str(), map_.version.c_str(), ar_, od_, cs_, hpSetting_, mods_.label().c_str(), startTime_);
    return true;
}

void Play::stop(Context &ctx) {
    ctx.music->pause();
    sliderTarget_.destroy();
}

// Le décalage audio est une latence en temps réel : en DT il couvre plus de temps de morceau.
double Play::gameTime(Context &ctx) const {
    return ctx.music->time(*ctx.mixer) * 1000.0 - ctx.settings->offsetMs * mods_.rate();
}

double Play::progress() const {
    if (lastObject_ <= firstObject_) return 0;
    return std::clamp((lastT_ - firstObject_) / (lastObject_ - firstObject_), 0.0, 1.0);
}

bool Play::canSkip() const { return !paused_ && firstObject_ - preempt_ - lastT_ > 3000; }

void Play::skip(Context &ctx) {
    if (!canSkip()) return;
    const double target = firstObject_ - preempt_ - 1200;
    ctx.music->play((target + ctx.settings->offsetMs * mods_.rate()) / 1000.0);
    LOG("play: intro passee -> %.0f ms", target);
}

void Play::setPaused(Context &ctx, bool paused) {
    if (paused == paused_ || finished_) return;
    paused_ = paused;
    if (paused)
        ctx.music->pause();
    else
        ctx.music->resume();
}

uint32_t Play::comboColor(int index) const {
    if (!map_.comboColors.empty()) return map_.comboColors[static_cast<size_t>(index) % map_.comboColors.size()];
    if (skin_ && !skin_->comboColours.empty()) return skin_->comboColours[static_cast<size_t>(index) % skin_->comboColours.size()];
    return kDefaultCombo[index % 4];
}

void Play::addScore(int value, bool comboIncrement) {
    if (comboIncrement) {
        ++combo_;
        maxCombo_ = std::max(maxCombo_, combo_);
        comboPopTime_ = lastWall_;
    }
    const double comboBonus = std::max(0, combo_ - 1) * difficultyMultiplier_ * mods_.scoreMultiplier() / 25.0;
    score_ += value + static_cast<long long>(value * comboBonus);
}

void Play::comboBreak(Context &ctx) {
    if (combo_ >= 20) ctx.play(ctx.sfx->comboBreak, ctx.settings->effectsVolume);
    combo_ = 0;
}

void Play::playHitSound(Context &ctx, const HitObject &o, int bits, float x) {
    const int set = std::clamp(o.sampleSet - 1, 0, 2);
    const int addSet = o.additionSet > 0 ? std::clamp(o.additionSet - 1, 0, 2) : set;
    const float vol = std::clamp(static_cast<float>(o.volume) / 100.0f, 0.05f, 1.0f) * 0.9f;
    const float pan = (x / kVW - 0.5f) * 0.6f;  // léger panoramique comme dans osu!
    ctx.mixer->play(&ctx.sfx->hit[set][0], vol, pan);
    if (bits & 2) ctx.mixer->play(&ctx.sfx->hit[addSet][1], vol, pan);
    if (bits & 4) ctx.mixer->play(&ctx.sfx->hit[addSet][2], vol, pan);
    if (bits & 8) ctx.mixer->play(&ctx.sfx->hit[addSet][3], vol, pan);
}

void Play::judge(Context &ctx, size_t index, int result, double now, bool breaksCombo) {
    State &s = state_[index];
    const HitObject &o = map_.objects[index];
    s.done = true;
    s.result = result;
    s.doneAt = now;
    switch (result) {
    case 300: ++n300_; break;
    case 100: ++n100_; break;
    case 50: ++n50_; break;
    default: ++nMiss_; break;
    }
    if (result == 0) {
        if (breaksCombo) comboBreak(ctx);
        hp_ -= 0.05f + 0.012f * hpSetting_;
    } else {
        hp_ += result == 300 ? 0.035f : (result == 100 ? 0.012f : 0.0f);
        addScore(result, o.type != kSlider);
    }
    hp_ = std::clamp(hp_, 0.0f, 1.0f);
    const Vec2 p = o.type == kSlider ? o.endPosition() : o.pos;
    if (result != 300 && !(ctx.settings->missesOnly && result != 0)) popups_.push_back(Popup{toScreenX(p.x), toScreenY(p.y), now, result});
}

void Play::handlePress(Context &ctx, double t) {
    // "Notelock" : seul le premier objet non frappé peut l'être.
    for (size_t i = firstActive_; i < map_.objects.size(); ++i) {
        const HitObject &o = map_.objects[i];
        State &s = state_[i];
        if (o.type == kSpinner || s.headDone || s.done) continue;
        const float dx = cursorX_ - toScreenX(o.pos.x), dy = cursorY_ - toScreenY(o.pos.y);
        if (dx * dx + dy * dy > radius_ * radius_) return;  // à côté : la frappe ne compte pas
        const double diff = t - o.time;
        if (diff < -400) return;  // bien trop tôt : ignorée
        const double a = std::fabs(diff);
        int result = a <= w300_ ? 300 : (a <= w100_ ? 100 : (a <= w50_ ? 50 : 0));
        s.headDone = true;
        s.headResult = result;
        s.headAt = lastWall_;
        s.errorMs = static_cast<float>(diff);
        if (result > 0) {
            errors_.push_back(static_cast<float>(diff));
            errorMarks_.push_back({static_cast<float>(diff), lastWall_});
            playHitSound(ctx, o, o.type == kSlider && !o.edgeSounds.empty() ? o.edgeSounds[0] : o.hitSound,
                         toScreenX(o.pos.x));
        }
        if (o.type == kCircle) {
            judge(ctx, i, result, lastWall_);
        } else {  // tête de slider : 30 points, la note finale vient à la fin
            if (result > 0) {
                ++s.partsHit;
                addScore(30, true);
                hp_ = std::min(1.0f, hp_ + 0.01f);
            } else {
                comboBreak(ctx);
            }
        }
        return;
    }
}

void Play::updateSlider(Context &ctx, size_t i, double t) {
    const HitObject &o = map_.objects[i];
    State &s = state_[i];
    // Suivi : touche enfoncée et curseur dans le cercle de suivi (agrandi une fois accroché).
    const Vec2 ball = o.ballPosition(std::clamp(t, o.time, o.endTime));
    const float dx = cursorX_ - toScreenX(ball.x), dy = cursorY_ - toScreenY(ball.y);
    const float follow = s.tracking ? radius_ * 2.4f : radius_;
    s.tracking = t >= o.time && held_ && dx * dx + dy * dy <= follow * follow;

    while (s.nextEvent < s.events.size() && t >= s.events[s.nextEvent].time) {
        const SliderEvent &e = s.events[s.nextEvent++];
        if (s.tracking) {
            ++s.partsHit;
            if (e.kind == 0) {
                addScore(10, true);
                ctx.mixer->play(&ctx.sfx->sliderTick, 0.6f);
            } else {
                addScore(30, true);
                const size_t edge = static_cast<size_t>(e.span + 1);
                playHitSound(ctx, o, edge < o.edgeSounds.size() ? o.edgeSounds[edge] : o.hitSound, toScreenX(ball.x));
            }
            hp_ = std::min(1.0f, hp_ + 0.01f);
        } else if (e.kind != 2) {
            comboBreak(ctx);  // tick ou retour manqué : le combo casse (pas la fin)
        }
    }
    if (t >= o.endTime && !s.done) {
        if (!s.headDone) {  // tête jamais frappée
            s.headDone = true;
            s.headResult = 0;
        }
        const double frac = static_cast<double>(s.partsHit) / s.partsTotal;
        const int result = frac >= 1.0 ? 300 : (frac >= 0.5 ? 100 : (frac > 0 ? 50 : 0));
        judge(ctx, i, result, lastWall_, false);
        if (result > 0) addScore(0, true);
    }
}

void Play::updateSpinner(Context &ctx, size_t i, double t, double dtMs, const UiInput &in) {
    const HitObject &o = map_.objects[i];
    State &s = state_[i];
    if (t < o.time) return;
    // Angle : direction du stick si on le tourne, sinon position du curseur autour du centre.
    float angle = 0;
    bool valid = false;
    if (in.stickActive) {
        angle = in.stickAngle;
        valid = true;
    } else if (held_) {
        angle = std::atan2(cursorY_ - toScreenY(192), cursorX_ - toScreenX(256));
        valid = true;
    }
    if (valid && s.angleValid && t <= o.endTime) {
        float d = angle - s.lastAngle;
        while (d > kPi) d -= 2 * kPi;
        while (d < -kPi) d += 2 * kPi;
        const float cap = static_cast<float>(dtMs) * 0.05f;  // ~477 tours/min au maximum
        d = std::clamp(d, -cap, cap);
        const int before = static_cast<int>(std::fabs(s.rotation) / (2 * kPi));
        s.rotation += d;
        const int after = static_cast<int>(std::fabs(s.rotation) / (2 * kPi));
        if (after > before) {
            addScore(100, false);
            if (static_cast<float>(after) > s.required) {
                score_ += 1000;
                ctx.mixer->play(&ctx.sfx->spinnerBonus, 0.6f);
                s.bonusShown = after;
            }
            hp_ = std::min(1.0f, hp_ + 0.01f);
        }
        const float rpmNow = dtMs > 0 ? std::fabs(d) / (2 * kPi) / static_cast<float>(dtMs) * 60000.0f : 0;
        s.rpm = anim::approach(s.rpm, rpmNow, static_cast<float>(dtMs) / 1000.0f, 0.15f);
    }
    s.lastAngle = angle;
    s.angleValid = valid;
    if (t >= o.endTime && !s.done) {
        const float spins = std::fabs(s.rotation) / (2 * kPi);
        const int result = spins >= s.required ? 300
                           : spins >= s.required * 0.75f ? 100
                           : spins >= s.required * 0.25f ? 50 : 0;
        judge(ctx, i, result, lastWall_);
        if (result > 0) playHitSound(ctx, o, o.hitSound, kVW / 2);
    }
}

void Play::update(Context &ctx, const UiInput &in) {
    lastWall_ = ctx.now;
    cursorX_ = in.cursorX;
    cursorY_ = in.cursorY;
    if (paused_ || finished_) return;

    const double t = gameTime(ctx);
    const double dtMs = std::clamp(t - lastT_, 0.0, 100.0);
    lastT_ = t;
    held_ = in.hitHeld;

    if (failed_) {
        // Ralentissement du son puis fin de partie.
        const float k = static_cast<float>((ctx.now - failTime_) / 1.5);
        ctx.music->setVolume(ctx.settings->musicVolume * std::max(0.0f, 1 - k));
        if (k >= 1.3f) {
            finished_ = true;
            ctx.music->pause();
            finishResult();
        }
        return;
    }

    if (mods_.autoplay) {
        autoplay(ctx, t);
    } else {
        for (int p = 0; p < in.hitPresses; ++p) handlePress(ctx, t);
        keyCount_[0] += in.key1Presses;
        keyCount_[1] += in.key2Presses;
        keysHeld_ = in.hitKeysHeld;
    }

    for (size_t i = firstActive_; i < map_.objects.size(); ++i) {
        const HitObject &o = map_.objects[i];
        State &s = state_[i];
        if (o.time - preempt_ > t) break;
        if (s.done) continue;
        if (o.type == kCircle) {
            if (t - o.time > w50_) judge(ctx, i, 0, ctx.now);
        } else if (o.type == kSlider) {
            if (!s.headDone && t - o.time > w50_) {
                s.headDone = true;
                s.headResult = 0;
                s.headAt = ctx.now;
                comboBreak(ctx);
            }
            updateSlider(ctx, i, t);
        } else {
            updateSpinner(ctx, i, t, dtMs, in);
        }
    }
    while (firstActive_ < state_.size() && state_[firstActive_].done) ++firstActive_;

    // Vie : diminue avec le temps hors pauses de la map.
    bool inBreak = t < firstObject_ || t > lastObject_;
    for (const auto &b : map_.breaks)
        if (t >= b.first && t <= b.second) inBreak = true;
    if (!inBreak) hp_ -= static_cast<float>(dtMs) * 0.0000075f * (1.0f + hpSetting_);
    hp_ = std::clamp(hp_, 0.0f, 1.0f);
    if (hp_ <= 0 && !mods_.noFail && !mods_.autoplay && !failed_) {
        failed_ = true;
        failTime_ = ctx.now;
        ctx.play(ctx.sfx->comboBreak, ctx.settings->effectsVolume);
        LOG("play: echec a %.0f ms", t);
        return;
    }

    if (firstActive_ >= state_.size() && t > lastObject_ + 800) {
        finished_ = true;
        finishResult();
    } else if (ctx.music->ended() && firstActive_ >= state_.size()) {
        finished_ = true;
        finishResult();
    }
}

void Play::autoplay(Context &ctx, double t) {
    // Frappe parfaite de tout objet dont l'instant est passé.
    for (size_t i = firstActive_; i < map_.objects.size(); ++i) {
        const HitObject &o = map_.objects[i];
        if (o.time > t) break;
        if (o.type == kSpinner || state_[i].headDone || state_[i].done) continue;
        cursorX_ = toScreenX(o.pos.x);
        cursorY_ = toScreenY(o.pos.y);
        handlePress(ctx, o.time);
    }
    // Curseur : suit la bille des sliders, tourne sur les spinners, glisse entre les objets.
    held_ = false;
    const HitObject *prev = nullptr;
    const HitObject *next = nullptr;
    for (size_t i = firstActive_ > 0 ? firstActive_ - 1 : 0; i < map_.objects.size(); ++i) {
        const HitObject &o = map_.objects[i];
        if (o.time <= t) {
            prev = &o;
            continue;
        }
        next = &o;
        break;
    }
    if (prev && t <= prev->endTime && prev->type != kCircle) {
        held_ = true;
        if (prev->type == kSlider) {
            const Vec2 p = prev->ballPosition(t);
            cursorX_ = toScreenX(p.x);
            cursorY_ = toScreenY(p.y);
        } else {
            const float a = static_cast<float>(t) * 0.045f;
            cursorX_ = toScreenX(256) + std::cos(a) * 120;
            cursorY_ = toScreenY(192) + std::sin(a) * 120;
        }
    } else if (next) {
        const Vec2 from = prev ? (prev->type == kSpinner ? Vec2{256, 192} : prev->endPosition()) : Vec2{256, 192};
        const double t0 = prev ? prev->endTime : next->time - 1000;
        const float k = anim::inOutCubic(static_cast<float>((t - t0) / std::max(1.0, next->time - t0)));
        cursorX_ = toScreenX(from.x + (next->pos.x - from.x) * k);
        cursorY_ = toScreenY(from.y + (next->pos.y - from.y) * k);
    }
    (void)ctx;
}

DifficultyMods Play::difficultyMods() const {
    DifficultyMods dm;
    dm.rate = mods_.rate();
    dm.hardRock = mods_.hardRock;
    dm.easy = mods_.easy;
    dm.hidden = mods_.hidden;
    return dm;
}

// PP de la partie en cours : difficulté de la map jusqu'au dernier objet joué.
double Play::livePp() const {
    const int judged = n300_ + n100_ + n50_ + nMiss_;
    if (judged <= 0 || timed_.empty()) return 0;
    const DifficultyAttributes &a = timed_[static_cast<size_t>(std::min<int>(judged, static_cast<int>(timed_.size())) - 1)];
    ScoreStats s;
    s.n300 = n300_;
    s.n100 = n100_;
    s.n50 = n50_;
    s.nMiss = nMiss_;
    s.maxCombo = maxCombo_;
    return computePp(a, s, difficultyMods(), mods_.noFail);
}

void Play::finishResult() {
    PlayResult &r = result_;
    r.score = score_;
    r.maxCombo = maxCombo_;
    r.n300 = n300_;
    r.n100 = n100_;
    r.n50 = n50_;
    r.nMiss = nMiss_;
    r.failed = failed_;
    r.completed = !failed_;
    r.mods = mods_;
    r.pp = livePp();
    r.stars = timed_.empty() ? 0 : timed_.back().stars;
    const int total = n300_ + n100_ + n50_ + nMiss_;
    r.accuracy = total > 0 ? (300.0 * n300_ + 100.0 * n100_ + 50.0 * n50_) / (300.0 * total) : 1.0;
    double mean = 0;
    for (float e : errors_) mean += e;
    if (!errors_.empty()) mean /= static_cast<double>(errors_.size());
    double var = 0;
    for (float e : errors_) var += (e - mean) * (e - mean);
    // En temps réel (comme lazer) : en DT, 10 ms de morceau ne durent que 6,7 ms.
    r.meanError = mean / mods_.rate();
    r.unstableRate = errors_.size() > 1 ? std::sqrt(var / static_cast<double>(errors_.size() - 1)) * 10.0 / mods_.rate() : 0;
    const double p300 = total > 0 ? static_cast<double>(n300_) / total : 1;
    const double p50 = total > 0 ? static_cast<double>(n50_) / total : 0;
    if (failed_) r.rank = "F";
    else if (r.accuracy >= 1.0) r.rank = mods_.hidden ? "SS+" : "SS";
    else if (p300 > 0.9 && p50 < 0.01 && nMiss_ == 0) r.rank = mods_.hidden ? "S+" : "S";
    else if ((p300 > 0.8 && nMiss_ == 0) || p300 > 0.9) r.rank = "A";
    else if ((p300 > 0.7 && nMiss_ == 0) || p300 > 0.8) r.rank = "B";
    else if (p300 > 0.6) r.rank = "C";
    else r.rank = "D";
    LOG("play: fin score=%lld acc=%.2f%% combo=%d 300=%d 100=%d 50=%d rate=%d UR=%.1f moyenne=%+.1fms rang=%s",
        r.score, r.accuracy * 100, r.maxCombo, n300_, n100_, n50_, nMiss_, r.unstableRate, r.meanError, r.rank.c_str());
}

// --- Rendu ------------------------------------------------------------------------------------

void Play::renderSliderBody(Context &ctx, const HitObject &o, float alpha, uint32_t color) {
    gfx::SpriteBatch &b = *ctx.batch;
    if (!sliderTarget_.fbo || o.path.size() < 2) return;
    b.setTarget(&sliderTarget_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    // Le corps : une suite de disques le long du chemin, bord blanc puis intérieur.
    const float total = o.cumLength.back();
    const float stepOsu = std::max(1.5f, radius_ / scale_ * 0.1f);
    const int count = std::min(4000, static_cast<int>(total / stepOsu) + 2);
    std::vector<Vec2> pts;
    pts.reserve(static_cast<size_t>(count));
    for (int k = 0; k < count; ++k) pts.push_back(o.positionAt(static_cast<float>(k) / static_cast<float>(count - 1)));
    const bool flat = ctx.settings->skin == 0 && !skin_;
    const uint32_t border = skin_ ? (skin_->sliderBorder | 0xFF000000u) : gfx::rgba(255, 255, 255);
    for (const Vec2 &p : pts) b.centered(ctx.tex->disc, toScreenX(p.x), toScreenY(p.y), radius_ * 2.0f, border);
    if (skin_) {
        // Corps : couleur imposée par le skin ou couleur du combo, plus claire au centre.
        const uint32_t track = skin_->hasTrackOverride ? (skin_->sliderTrackOverride | 0xFF000000u) : color;
        for (const Vec2 &p : pts) b.centered(ctx.tex->disc, toScreenX(p.x), toScreenY(p.y), radius_ * 1.76f, scaleRgb(track, 0.55f));
        for (const Vec2 &p : pts) b.centered(ctx.tex->disc, toScreenX(p.x), toScreenY(p.y), radius_ * 1.2f, scaleRgb(track, 0.75f));
    } else if (flat) {
        // Épuré : corps presque noir, à peine teinté, bord blanc fin.
        const uint32_t inner = scaleRgb(color, 0.12f);
        for (const Vec2 &p : pts) b.centered(ctx.tex->disc, toScreenX(p.x), toScreenY(p.y), radius_ * 1.84f, inner);
    } else {
        const uint32_t inner = scaleRgb(color, 0.32f);
        for (const Vec2 &p : pts) b.centered(ctx.tex->disc, toScreenX(p.x), toScreenY(p.y), radius_ * 1.76f, inner);
        // Liseré clair au centre, façon lazer.
        const uint32_t centre = scaleRgb(color, 0.45f);
        for (const Vec2 &p : pts) b.centered(ctx.tex->disc, toScreenX(p.x), toScreenY(p.y), radius_ * 0.9f, centre);
    }
    b.setTarget(nullptr);
    b.setBlend(gfx::Blend::Premultiplied);
    const uint8_t a = static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 0.92f * 255.0f);
    b.quad(sliderTarget_.color, 0, 0, kVW, kVH, gfx::rgba(a, a, a, a), 0, 0, 1, 1);
    b.setBlend(gfx::Blend::Alpha);
}

// --- Skin osu! ---------------------------------------------------------------
// Échelles reprises d'osu!stable : un pixel d'une image 1x vaut radius/64 pour
// les objets (128 px = diamètre), et hauteur d'écran/768 pour le curseur.

void Play::skinSprite(Context &ctx, const SkinSprite &sp, float cx, float cy, float scale, uint32_t color, float angle) const {
    if (!sp.ok()) return;
    const float w = sp.w() * scale, h = sp.h() * scale;
    if (angle != 0)
        ctx.batch->rotated(sp.tex, cx, cy, w, h, angle, color);
    else
        ctx.batch->quad(sp.tex, cx - w / 2, cy - h / 2, w, h, color);
}

void Play::skinNumber(Context &ctx, int number, float cx, float cy, float scale, uint32_t color) const {
    const std::string digits = std::to_string(number);
    float total = 0;
    for (char ch : digits) total += skin_->digits[ch - '0'].w() * scale;
    const float overlap = static_cast<float>(skin_->hitCircleOverlap) * scale;
    total -= overlap * static_cast<float>(digits.size() - 1);
    float x = cx - total / 2;
    for (char ch : digits) {
        const SkinSprite &d = skin_->digits[ch - '0'];
        if (!d.ok()) continue;
        ctx.batch->quad(d.tex, x, cy - d.h() * scale / 2, d.w() * scale, d.h() * scale, color);
        x += d.w() * scale - overlap;
    }
}

void Play::renderSkinnedObject(Context &ctx, size_t i, double t, float alpha, uint32_t color) {
    gfx::SpriteBatch &b = *ctx.batch;
    const Skin &sk = *skin_;
    const HitObject &o = map_.objects[i];
    const State &s = state_[i];
    const double now = ctx.now;
    const float k = radius_ / 64.0f;  // pixels virtuels par pixel d'image 1x
    const uint32_t white = gfx::rgba(255, 255, 255);

    if (o.type == kSpinner) {
        if (s.done && now - s.doneAt > 0.3) return;
        const float fade = s.done ? 1 - static_cast<float>((now - s.doneAt) / 0.3) : anim::clamp01(static_cast<float>((t - o.time + 300) / 300));
        const float cx = toScreenX(256), cy = toScreenY(192);
        const float sp = kVH / 768.0f;
        const float remain = anim::clamp01(static_cast<float>((o.endTime - t) / std::max(1.0, o.endTime - o.time)));
        const uint32_t w = gfx::withAlpha(white, fade);
        if (sk.spinnerTop.ok()) {
            skinSprite(ctx, sk.spinnerBottom, cx, cy, sp, w, s.rotation * 0.2f);
            skinSprite(ctx, sk.spinnerTop, cx, cy, sp, w, s.rotation * 0.5f);
            skinSprite(ctx, sk.spinnerMiddle2, cx, cy, sp, w);
            skinSprite(ctx, sk.spinnerMiddle, cx, cy, sp, gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.8f));
        } else {
            skinSprite(ctx, sk.spinnerBackground, cx, cy, sp, w);
            skinSprite(ctx, sk.spinnerCircle, cx, cy, sp, w, s.rotation);
            skinSprite(ctx, sk.spinnerApproach, cx, cy, sp * (0.05f + remain), w);
        }
        if (s.required > 0 && std::fabs(s.rotation) / (2 * kPi) >= s.required) skinSprite(ctx, sk.spinnerClear, cx, cy - 200, sp, w);
        else if (t < o.time + 800) skinSprite(ctx, sk.spinnerSpin, cx, cy + 200, sp, w);
        return;
    }

    const float hx = toScreenX(o.pos.x), hy = toScreenY(o.pos.y);
    if (o.type == kSlider) {
        float bodyAlpha = alpha;
        if (s.done) bodyAlpha *= ctx.settings->instafade ? 0 : 1 - anim::clamp01(static_cast<float>((now - s.doneAt) / 0.25));
        if (mods_.hidden) bodyAlpha *= 1 - anim::clamp01(static_cast<float>((t - o.time) / std::max(1.0, o.endTime - o.time)));
        if (bodyAlpha > 0.01f) {
            renderSliderBody(ctx, o, bodyAlpha, color);
            for (size_t e = s.nextEvent; e < s.events.size(); ++e) {
                const SliderEvent &ev = s.events[e];
                if (ev.kind != 0 || ev.span != std::max(0, static_cast<int>((t - o.time) / std::max(1.0, o.spanDuration)))) continue;
                const Vec2 p = o.ballPosition(ev.time);
                skinSprite(ctx, sk.sliderscorepoint, toScreenX(p.x), toScreenY(p.y), k, gfx::withAlpha(white, bodyAlpha));
            }
            const int spanNow = std::clamp(static_cast<int>((t - o.time) / std::max(1.0, o.spanDuration)), 0, o.slides - 1);
            if (spanNow < o.slides - 1) {
                const bool atEnd = spanNow % 2 == 0;
                const Vec2 tip = o.positionAt(atEnd ? 1.0f : 0.0f);
                const Vec2 prev = o.positionAt(atEnd ? 0.98f : 0.02f);
                const float ang = std::atan2(prev.y - tip.y, prev.x - tip.x);
                const float pulse = 1.0f + 0.15f * (1 - static_cast<float>(std::fmod(t, 300.0) / 300.0));
                skinSprite(ctx, sk.reversearrow, toScreenX(tip.x), toScreenY(tip.y), k * pulse, gfx::withAlpha(white, bodyAlpha), ang);
            }
            if (t >= o.time && t <= o.endTime && !s.done) {
                const Vec2 ball = o.ballPosition(t);
                const float bx = toScreenX(ball.x), by = toScreenY(ball.y);
                if (s.tracking) skinSprite(ctx, sk.sliderfollowcircle, bx, by, k, white);
                if (!sk.sliderb.empty()) {
                    const size_t frame = static_cast<size_t>((t - o.time) / 16.0) % sk.sliderb.size();
                    const Vec2 ahead = o.ballPosition(t + 8);
                    const float ang = std::atan2(ahead.y - ball.y, ahead.x - ball.x);
                    skinSprite(ctx, sk.sliderb[frame], bx, by, k, white, ang);
                }
            }
        }
    }

    const bool headPending = !s.headDone && !(o.type == kCircle && s.done);
    const bool sliderHead = o.type == kSlider && sk.sliderstartcircle.ok();
    const SkinSprite &circle = sliderHead ? sk.sliderstartcircle : sk.hitcircle;
    const SkinSprite &overlay = sliderHead && sk.sliderstartcircleoverlay.ok() ? sk.sliderstartcircleoverlay : sk.hitcircleoverlay;
    if (headPending && alpha > 0) {
        skinSprite(ctx, circle, hx, hy, k, gfx::withAlpha(color, alpha));
        if (!sk.overlayAboveNumber) skinSprite(ctx, overlay, hx, hy, k, gfx::withAlpha(white, alpha));
        skinNumber(ctx, o.comboNumber, hx, hy, k * 0.8f, gfx::withAlpha(white, alpha));
        if (sk.overlayAboveNumber) skinSprite(ctx, overlay, hx, hy, k, gfx::withAlpha(white, alpha));
        if (!mods_.hidden && t < o.time) {
            const float p = anim::clamp01(static_cast<float>((t - (o.time - preempt_)) / preempt_));
            skinSprite(ctx, sk.approachcircle, hx, hy, k * (1.0f + 3.0f * (1.0f - p)), gfx::withAlpha(color, std::min(1.0f, alpha * 1.2f)));
        }
    } else {
        // Frappe : le cercle grossit (x1,4) et s'efface en 240 ms, comme stable.
        const int res = o.type == kCircle ? s.result : s.headResult;
        const double at = o.type == kCircle ? s.doneAt : s.headAt;
        const float e = static_cast<float>((now - at) / 0.24);
        if (res > 0 && e >= 0 && e < 1 && !ctx.settings->instafade) {
            const float sc = k * (1 + 0.4f * anim::outQuint(e));
            skinSprite(ctx, circle, hx, hy, sc, gfx::withAlpha(color, 1 - e));
            skinSprite(ctx, overlay, hx, hy, sc, gfx::withAlpha(white, 1 - e));
        }
    }
}

void Play::renderSkinnedCursor(Context &ctx) {
    const Skin &sk = *skin_;
    const float sp = kVH / 768.0f;
    while (trail_.size() > 10) trail_.pop_back();
    if (sk.cursortrail.ok()) {
        for (size_t k = trail_.size(); k-- > 1;) {
            const float a = 1.0f - static_cast<float>(k) / static_cast<float>(trail_.size());
            skinSprite(ctx, sk.cursortrail, trail_[k].first, trail_[k].second, sp, gfx::withAlpha(gfx::rgba(255, 255, 255), a));
        }
    }
    const float grow = held_ && sk.cursorExpand ? 1.15f : 1.0f;
    const float angle = sk.cursorRotate ? static_cast<float>(std::fmod(ctx.now, 10.0) / 10.0 * 2 * kPi) : 0;
    skinSprite(ctx, sk.cursor, cursorX_, cursorY_, sp * grow, gfx::rgba(255, 255, 255), angle);
    skinSprite(ctx, sk.cursormiddle, cursorX_, cursorY_, sp, gfx::rgba(255, 255, 255));
}

void Play::render(Context &ctx) {
    gfx::SpriteBatch &b = *ctx.batch;
    const double t = paused_ || failed_ ? lastT_ : gameTime(ctx);
    const double now = ctx.now;

    // Fond : image de la map assombrie.
    if (background_ && background_->id) {
        const float bw = static_cast<float>(background_->width), bh = static_cast<float>(background_->height);
        const float k = std::max(kVW / bw, kVH / bh);
        b.quad(*background_, (kVW - bw * k) / 2, (kVH - bh * k) / 2, bw * k, bh * k, gfx::rgba(255, 255, 255));
    }
    // Sans image fournie, c'est le fond global du jeu (déjà assombri) qui sert.
    if (background_) {
        float dim = ctx.settings->backgroundDim;
        if (t < firstObject_ - preempt_) dim *= 0.85f;
        b.rect(0, 0, kVW, kVH, gfx::rgba(0, 0, 0, static_cast<uint8_t>(dim * 255)));
    }

    // Storyboard (assombri comme le fond), puis les objets par-dessus.
    if (storyboard_) {
        storyboard_->drawBelow(b, t);
        b.rect(0, 0, kVW, kVH, gfx::rgba(0, 0, 0, static_cast<uint8_t>(ctx.settings->backgroundDim * 255)));
    }

    // Objets : du plus tardif au plus proche (les plus proches au-dessus).
    int lastVisible = static_cast<int>(firstActive_);
    while (lastVisible < static_cast<int>(map_.objects.size()) &&
           map_.objects[static_cast<size_t>(lastVisible)].time - preempt_ <= t)
        ++lastVisible;
    int firstVisible = static_cast<int>(firstActive_);
    while (firstVisible > 0 && now - state_[static_cast<size_t>(firstVisible - 1)].doneAt < 0.5) --firstVisible;

    const bool flat = ctx.settings->skin == 0;
    for (int idx = lastVisible - 1; idx >= firstVisible; --idx) {
        const size_t i = static_cast<size_t>(idx);
        const HitObject &o = map_.objects[i];
        const State &s = state_[i];
        const uint32_t color = comboColor(o.comboIndex);
        const float appear = static_cast<float>((t - (o.time - preempt_)) / fadeIn_);
        float alpha = anim::clamp01(appear);
        if (mods_.hidden) {
            const double fadeOutStart = o.time - preempt_ + fadeIn_;
            const float out = static_cast<float>((t - fadeOutStart) / (preempt_ * 0.3));
            if (out > 0 && o.type == kCircle) alpha *= 1 - anim::clamp01(out);
        }

        if (skin_) {
            renderSkinnedObject(ctx, i, t, alpha, color);
            continue;
        }
        if (o.type == kSpinner) {
            if (s.done && now - s.doneAt > 0.3) continue;
            const float fade = s.done ? 1 - static_cast<float>((now - s.doneAt) / 0.3) : anim::clamp01(static_cast<float>((t - o.time + 300) / 300));
            const float cx = toScreenX(256), cy = toScreenY(192);
            const float prog = s.required > 0 ? anim::clamp01(std::fabs(s.rotation) / (2 * kPi) / s.required) : 1;
            const float remain = anim::clamp01(static_cast<float>((o.endTime - t) / std::max(1.0, o.endTime - o.time)));
            if (flat) {
                // Épuré : anneau qui se referme, arc de progression, point central.
                b.ring(cx, cy, 15 + 300 * remain, 3, gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.8f));
                b.ring(cx, cy, 240, 10, gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.12f));
                b.arc(cx, cy, 240, 10, 0, prog, gfx::Corners::solid(gfx::withAlpha(prog >= 1 ? gfx::rgba(140, 255, 160) : gfx::rgba(255, 255, 255), fade)));
                const float ang = s.rotation;
                b.circle(cx + std::cos(ang) * 225, cy + std::sin(ang) * 225, 9, gfx::withAlpha(gfx::rgba(255, 255, 255), fade));
                b.circle(cx, cy, 8, gfx::withAlpha(gfx::rgba(255, 255, 255), fade));
                char rpm[48];
                snprintf(rpm, sizeof(rpm), i18n::tr("%.0f tr/min"), s.rpm);
                ctx.fonts->semibold.draw(b, cx, cy + 290, 26, gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.8f), rpm, gfx::Align::Center);
                if (s.bonusShown > 0 && static_cast<float>(s.bonusShown) > s.required) {
                    snprintf(rpm, sizeof(rpm), "+%d", (s.bonusShown - static_cast<int>(s.required)) * 1000);
                    ctx.fonts->semibold.draw(b, cx, cy - 120, 48, gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.9f), rpm, gfx::Align::Center);
                }
                continue;
            }
            b.centered(ctx.tex->spinner, cx, cy, 520, gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.35f));
            b.rotated(ctx.tex->ringThick, cx, cy, 520 * (0.55f + 0.45f * prog), 520 * (0.55f + 0.45f * prog), s.rotation,
                      gfx::withAlpha(prog >= 1 ? gfx::rgba(140, 255, 160) : gfx::rgba(120, 200, 255), fade));
            for (int k = 0; k < 3; ++k) {
                const float a = s.rotation + static_cast<float>(k) * 2 * kPi / 3;
                b.rotated(ctx.tex->triangle, cx + std::cos(a) * 200, cy + std::sin(a) * 200, 36, 36, a + kPi / 2,
                          gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.8f));
            }
            b.centered(ctx.tex->ring, cx, cy, 30 + 600 * remain, gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.7f));
            char line[48];
            snprintf(line, sizeof(line), i18n::tr("%.0f tr/min"), s.rpm);
            ctx.fonts->semibold.draw(b, cx, cy + 290, 30, gfx::withAlpha(gfx::rgba(255, 255, 255), fade), line, gfx::Align::Center);
            if (s.bonusShown > 0 && static_cast<float>(s.bonusShown) > s.required) {
                snprintf(line, sizeof(line), "+%d", (s.bonusShown - static_cast<int>(s.required)) * 1000);
                ctx.fonts->bold.draw(b, cx, cy - 40, 64, gfx::withAlpha(gfx::rgba(255, 230, 140), fade * 0.9f), line, gfx::Align::Center);
            } else if (t < o.time + 800) {
                ctx.fonts->bold.draw(b, cx, cy - 30, 48, gfx::withAlpha(gfx::rgba(255, 255, 255), fade * 0.9f), "Tourne !", gfx::Align::Center);
            }
            continue;
        }

        const float hx = toScreenX(o.pos.x), hy = toScreenY(o.pos.y);
        if (o.type == kSlider) {
            float bodyAlpha = alpha;
            if (s.done) bodyAlpha *= ctx.settings->instafade ? 0 : 1 - anim::clamp01(static_cast<float>((now - s.doneAt) / 0.25));
            if (mods_.hidden) bodyAlpha *= 1 - anim::clamp01(static_cast<float>((t - o.time) / std::max(1.0, o.endTime - o.time)));
            if (bodyAlpha > 0.01f) {
                renderSliderBody(ctx, o, bodyAlpha, color);
                // Ticks restants.
                for (size_t e = s.nextEvent; e < s.events.size(); ++e) {
                    const SliderEvent &ev = s.events[e];
                    if (ev.kind != 0 || ev.span != std::max(0, static_cast<int>((t - o.time) / std::max(1.0, o.spanDuration))))
                        continue;
                    const Vec2 p = o.ballPosition(ev.time);
                    b.centered(ctx.tex->disc, toScreenX(p.x), toScreenY(p.y), radius_ * 0.28f, gfx::withAlpha(gfx::rgba(255, 255, 255), bodyAlpha));
                }
                // Flèche de retour à l'extrémité active.
                const int spanNow = std::clamp(static_cast<int>((t - o.time) / std::max(1.0, o.spanDuration)), 0, o.slides - 1);
                if (spanNow < o.slides - 1) {
                    const bool atEnd = spanNow % 2 == 0;
                    const Vec2 tip = o.positionAt(atEnd ? 1.0f : 0.0f);
                    const Vec2 prev = o.positionAt(atEnd ? 0.98f : 0.02f);
                    const float ang = std::atan2(prev.y - tip.y, prev.x - tip.x);
                    const float pulse = 1.0f + 0.1f * static_cast<float>(std::fmod(t, 300.0) / 300.0);
                    b.rotated(ctx.tex->arrow, toScreenX(tip.x), toScreenY(tip.y), radius_ * 1.2f * pulse, radius_ * 1.2f * pulse,
                              ang, gfx::withAlpha(gfx::rgba(255, 255, 255), bodyAlpha));
                }
                // Bille et cercle de suivi.
                if (t >= o.time && t <= o.endTime && !s.done) {
                    const Vec2 ball = o.ballPosition(t);
                    const float bx = toScreenX(ball.x), by = toScreenY(ball.y);
                    if (flat) {
                        if (s.tracking) b.ring(bx, by, radius_ * 2.2f, radius_ * 0.07f, gfx::withAlpha(gfx::rgba(255, 255, 255), 0.8f));
                        b.ring(bx, by, radius_ * 0.78f, radius_ * 0.16f, gfx::rgba(255, 255, 255));
                    } else {
                        if (s.tracking) {
                            b.centered(ctx.tex->ring, bx, by, radius_ * 4.8f, gfx::withAlpha(gfx::rgba(255, 255, 255), 0.85f));
                            b.centered(ctx.tex->glow, bx, by, radius_ * 4.0f, gfx::withAlpha(color, 0.35f));
                        }
                        b.centered(ctx.tex->disc, bx, by, radius_ * 1.7f, gfx::rgba(255, 255, 255));
                        b.centered(ctx.tex->disc, bx, by, radius_ * 1.4f, color);
                    }
                }
            }
        }

        // Cercle (ou tête de slider).
        const bool headPending = !s.headDone && !(o.type == kCircle && s.done);
        if (headPending && alpha > 0 && flat) {
            // Épuré (façon WhiteCat) : disque sombre, liseré blanc fin, chiffre discret.
            b.circle(hx, hy, radius_, gfx::withAlpha(scaleRgb(color, 0.14f), alpha * 0.95f));
            b.ring(hx, hy, radius_, radius_ * 0.09f, gfx::withAlpha(gfx::rgba(255, 255, 255), alpha));
            char num[8];
            snprintf(num, sizeof(num), "%d", o.comboNumber);
            ctx.fonts->semibold.drawMid(b, hx, hy, radius_ * 0.9f, gfx::withAlpha(gfx::rgba(255, 255, 255), alpha), num,
                                        gfx::Align::Center);
            if (!mods_.hidden && t < o.time) {
                const float p = anim::clamp01(static_cast<float>((t - (o.time - preempt_)) / preempt_));
                const float r = radius_ * (1.0f + 2.5f * (1.0f - p));
                b.ring(hx, hy, r, std::max(2.0f, radius_ * 0.06f), gfx::withAlpha(scaleRgb(color, 1.6f), std::min(1.0f, alpha * 1.1f)));
            }
        } else if (headPending && alpha > 0) {
            // Style "triangles" de lazer : disque coloré, motif de triangles qui tourne, bord blanc.
            b.centered(ctx.tex->disc, hx, hy, radius_ * 2.0f, gfx::withAlpha(scaleRgb(color, 0.78f), alpha));
            b.rotated(ctx.tex->circleTriangles, hx, hy, radius_ * 1.86f, radius_ * 1.86f,
                      static_cast<float>(now) * 0.4f + static_cast<float>(o.comboNumber), gfx::withAlpha(scaleRgb(color, 1.25f), alpha));
            b.centered(ctx.tex->ringThick, hx, hy, radius_ * 2.0f, gfx::withAlpha(gfx::rgba(255, 255, 255), alpha));
            char num[8];
            snprintf(num, sizeof(num), "%d", o.comboNumber);
            ctx.fonts->bold.drawMid(b, hx, hy, radius_ * 1.15f, gfx::withAlpha(gfx::rgba(255, 255, 255), alpha), num,
                                    gfx::Align::Center);
            if (!mods_.hidden && t < o.time) {
                const float p = anim::clamp01(static_cast<float>((t - (o.time - preempt_)) / preempt_));
                const float size = radius_ * 2.0f * (1.0f + 3.0f * (1.0f - p));
                b.centered(ctx.tex->ring, hx, hy, size, gfx::withAlpha(color, std::min(1.0f, alpha * 1.2f)));
            }
        } else {
            // Animation de frappe : le cercle grossit et s'efface.
            const int res = o.type == kCircle ? s.result : s.headResult;
            const double at = o.type == kCircle ? s.doneAt : s.headAt;
            const float k = static_cast<float>((now - at) / 0.24);
            if (ctx.settings->instafade && res > 0) {
                // Instafade : plus rien à dessiner.
            } else if (flat && res > 0 && k < 1 && k >= 0) {
                // Épuré : le cercle grossit un peu et s'efface vite.
                const float e = anim::outQuint(k);
                b.ring(hx, hy, radius_ * (1 + 0.35f * e), radius_ * 0.09f, gfx::withAlpha(gfx::rgba(255, 255, 255), (1 - k) * 0.9f));
                b.circle(hx, hy, radius_ * (1 + 0.35f * e) * 0.92f, gfx::withAlpha(scaleRgb(color, 0.5f), (1 - k) * 0.35f));
            } else if (res > 0 && k < 1 && k >= 0) {
                const float e = anim::outCubic(k);
                b.setBlend(gfx::Blend::Additive);
                b.centered(ctx.tex->glow, hx, hy, radius_ * 3.0f * (1 + e * 0.6f), gfx::withAlpha(color, (1 - k) * 0.6f));
                b.centered(ctx.tex->disc, hx, hy, radius_ * 2.0f * (1 + e * 0.3f), gfx::withAlpha(gfx::rgba(255, 255, 255), (1 - k) * 0.5f));
                // Éclats de triangles projetés vers l'extérieur.
                for (int p = 0; p < 7; ++p) {
                    const float ang = static_cast<float>(p) * 0.8976f + static_cast<float>(o.comboNumber) * 1.3f;
                    const float dist = radius_ * (1.0f + 1.6f * e);
                    b.rotated(ctx.tex->triangle, hx + std::cos(ang) * dist, hy + std::sin(ang) * dist, radius_ * 0.45f, radius_ * 0.4f,
                              ang + 1.5708f, gfx::withAlpha(scaleRgb(color, 1.3f), (1 - k) * 0.9f));
                }
                b.setBlend(gfx::Blend::Alpha);
                b.centered(ctx.tex->disc, hx, hy, radius_ * 2.0f * (1 + e * 0.5f), gfx::withAlpha(scaleRgb(color, 0.85f), 1 - k));
                b.centered(ctx.tex->ringThick, hx, hy, radius_ * 2.0f * (1 + e * 0.5f), gfx::withAlpha(gfx::rgba(255, 255, 255), 1 - k));
            } else if (res == 0 && k < 0.6f && k >= 0) {
                b.centered(ctx.tex->disc, hx, hy + k * 20, radius_ * 2.0f, gfx::withAlpha(scaleRgb(color, 0.5f), 0.6f - k));
            }
        }
    }

    // Jugements.
    for (const Popup &p : popups_) {
        const float k = static_cast<float>((now - p.time) / 0.6);
        if (k >= 1) continue;
        if (skin_) {
            const SkinSprite &sp = p.result == 0 ? skin_->hit0 : p.result == 100 ? skin_->hit100 : skin_->hit50;
            if (sp.ok()) {
                // Petit rebond puis fondu, à l'échelle de l'aire de jeu.
                const float sc = scale_ * 0.42f * (1 + 0.15f * (1 - anim::outQuint(std::min(1.0f, k * 5))));
                const float w = sp.w() * sc, h = sp.h() * sc;
                b.quad(sp.tex, p.x - w / 2, p.y - h / 2 + (p.result == 0 ? k * 20 : 0), w, h,
                       gfx::withAlpha(gfx::rgba(255, 255, 255), 1 - k * k));
                continue;
            }
        }
        if (flat) {
            // Épuré : petits chiffres, croix rouge pour un raté.
            const uint32_t c = p.result == 0 ? gfx::rgba(255, 70, 80) : (p.result == 100 ? gfx::rgba(120, 230, 120) : gfx::rgba(110, 180, 255));
            const float a = 1 - k * k;
            if (p.result == 0) {
                const float r = radius_ * 0.35f * (1 + 0.3f * k);
                b.line(p.x - r, p.y - r + k * 20, p.x + r, p.y + r + k * 20, radius_ * 0.12f, gfx::withAlpha(c, a));
                b.line(p.x + r, p.y - r + k * 20, p.x - r, p.y + r + k * 20, radius_ * 0.12f, gfx::withAlpha(c, a));
            } else {
                ctx.fonts->semibold.draw(b, p.x, p.y - radius_ * 0.3f - k * 10, radius_ * 0.55f, gfx::withAlpha(c, a),
                                         p.result == 100 ? "100" : "50", gfx::Align::Center);
            }
            continue;
        }
        const char *text = p.result == 0 ? "Miss" : (p.result == 100 ? "Ok" : "Meh");
        const uint32_t c = p.result == 0 ? gfx::rgba(237, 17, 33) : (p.result == 100 ? gfx::rgba(179, 217, 68) : gfx::rgba(255, 204, 34));
        const float size = (p.result == 0 ? 52.0f : 40.0f) * (1 + 0.25f * anim::outBack(std::min(1.0f, k * 4)) - 0.25f);
        ctx.fonts->bold.drawShadow(b, p.x, p.y - size * 0.6f + k * (p.result == 0 ? 30 : -10), size, gfx::withAlpha(c, 1 - k * k), text,
                                   gfx::Align::Center);
    }
    while (!popups_.empty() && now - popups_.front().time > 0.6) popups_.pop_front();

    if (storyboard_) storyboard_->drawAbove(b, t);
    renderHud(ctx, t);

    // Curseur et traînée.
    trail_.emplace_front(cursorX_, cursorY_);
    if (!ctx.settings->cursorTrail) trail_.resize(1);
    // Taille du curseur réglable : tout le dessin du curseur est mis à l'échelle autour de lui.
    b.pushScaleAround(cursorX_, cursorY_, ctx.settings->cursorSize);
    if (skin_ && skin_->cursor.ok()) {
        renderSkinnedCursor(ctx);
    } else if (flat) {
        // Traînée continue (segments arrondis qui s'amincissent), petit curseur blanc.
        while (trail_.size() > 24) trail_.pop_back();
        for (size_t k = trail_.size(); k-- > 1;) {
            const float a = 1.0f - static_cast<float>(k) / static_cast<float>(trail_.size());
            const auto &p0 = trail_[k], &p1 = trail_[k - 1];
            const float w = 4 + 10 * a;
            b.line(p0.first, p0.second, p1.first, p1.second, w, gfx::withAlpha(gfx::rgba(255, 255, 255), a * 0.45f));
            b.circle(p1.first, p1.second, w * 0.5f, gfx::withAlpha(gfx::rgba(255, 255, 255), a * 0.45f));
        }
        b.circle(cursorX_, cursorY_, held_ ? 15.0f : 12.0f, gfx::rgba(0, 0, 0, 90), 3);
        b.circle(cursorX_, cursorY_, held_ ? 12.0f : 10.0f, gfx::rgba(255, 255, 255));
        b.ring(cursorX_, cursorY_, held_ ? 12.0f : 10.0f, 2.5f, gfx::rgba(255, 140, 190));
    } else {
    while (trail_.size() > 14) trail_.pop_back();
    for (size_t k = trail_.size(); k-- > 1;) {
        const float a = 1.0f - static_cast<float>(k) / static_cast<float>(trail_.size());
        b.centered(ctx.tex->glow, trail_[k].first, trail_[k].second, 44 * a + 8, gfx::withAlpha(gfx::rgba(255, 180, 220), a * 0.35f));
    }
    b.centered(ctx.tex->glow, cursorX_, cursorY_, 70, gfx::rgba(255, 150, 210, 160));
    b.centered(ctx.tex->disc, cursorX_, cursorY_, held_ ? 30.0f : 24.0f, gfx::rgba(255, 255, 255));
    }
    b.popTransform();

    if (failed_) {
        const float k = anim::clamp01(static_cast<float>((now - failTime_) / 1.0));
        b.rect(0, 0, kVW, kVH, gfx::rgba(40, 0, 0, static_cast<uint8_t>(k * 140)));
        ctx.fonts->bold.drawShadow(b, kVW / 2, kVH / 2 - 60, 96, gfx::withAlpha(gfx::rgba(255, 120, 120), k), "Échec", gfx::Align::Center);
    }
}

void Play::renderHud(Context &ctx, double t) {
    if (!ctx.settings->showHud) return;
    gfx::SpriteBatch &b = *ctx.batch;
    const float dt = ctx.dt;
    displayScore_ = anim::approach(static_cast<float>(displayScore_), static_cast<float>(score_), dt, 0.06f);
    displayHp_ = anim::approach(displayHp_, hp_, dt, 0.08f);
    char line[64];

    // Score et précision (haut droite).
    snprintf(line, sizeof(line), "%08lld", static_cast<long long>(std::llround(displayScore_)));
    ctx.fonts->bold.drawShadow(b, kVW - 30, 18, 64, gfx::rgba(255, 255, 255), line, gfx::Align::Right);
    const int total = n300_ + n100_ + n50_ + nMiss_;
    const double acc = total > 0 ? (300.0 * n300_ + 100.0 * n100_ + 50.0 * n50_) / (300.0 * total) * 100.0 : 100.0;
    snprintf(line, sizeof(line), "%.2f%%", acc);
    ctx.fonts->semibold.drawShadow(b, kVW - 30, 92, 36, gfx::rgba(255, 255, 255), line, gfx::Align::Right);
    float hy = 136;
    if (!mods_.label().empty()) {
        ctx.fonts->semibold.drawShadow(b, kVW - 30, hy, 26, gfx::rgba(255, 210, 120), mods_.label().c_str(), gfx::Align::Right);
        hy += 34;
    }
    // Compteur de PP (option) : sous la précision.
    if (ctx.settings->showPpCounter) {
        displayPp_ = anim::approach(displayPp_, static_cast<float>(livePp()), dt, 0.15f);
        snprintf(line, sizeof(line), "%.0fpp", displayPp_);
        ctx.fonts->semibold.drawShadow(b, kVW - 30, hy, 30, gfx::rgba(255, 255, 255), line, gfx::Align::Right);
    }

    // Combo (bas gauche) avec un petit rebond.
    if (combo_ > 0) {
        const float pop = 1.0f + 0.25f * (1 - anim::outCubic(static_cast<float>((ctx.now - comboPopTime_) / 0.18)));
        snprintf(line, sizeof(line), "%dx", combo_);
        const float size = 72 * pop;
        ctx.fonts->bold.drawShadow(b, 30, kVH - 30 - size, size, gfx::rgba(255, 255, 255), line);
    }

    // Vie (haut gauche).
    b.rect(30, 30, 600, 12, gfx::rgba(0, 0, 0, 120));
    const uint32_t hpColor = displayHp_ < 0.3f ? gfx::rgba(237, 60, 60) : gfx::rgba(255, 102, 170);
    b.setBlend(gfx::Blend::Additive);
    b.centered(ctx.tex->glow, 30 + 600 * displayHp_, 36, 60, gfx::withAlpha(hpColor, 0.8f));
    b.setBlend(gfx::Blend::Alpha);
    b.rect(30, 30, 600 * displayHp_, 12, hpColor);

    // Progression du morceau (en bas, rose comme dans lazer).
    b.rect(0, kVH - 6, kVW, 6, gfx::rgba(0, 0, 0, 100));
    b.rect(0, kVH - 6, kVW * static_cast<float>(progress()), 6, gfx::rgba(255, 102, 170, 220));

    // Barre d'erreur de frappe (bas centre).
    if (ctx.settings->showHitErrorBar) {
        const float cx = kVW / 2, y = kVH - 40, k = 1.6f;
        b.rect(cx - static_cast<float>(w50_) * k, y, static_cast<float>(w50_) * 2 * k, 8, gfx::rgba(255, 190, 90, 110));
        b.rect(cx - static_cast<float>(w100_) * k, y, static_cast<float>(w100_) * 2 * k, 8, gfx::rgba(120, 230, 120, 140));
        b.rect(cx - static_cast<float>(w300_) * k, y, static_cast<float>(w300_) * 2 * k, 8, gfx::rgba(100, 190, 255, 170));
        b.rect(cx - 1.5f, y - 14, 3, 36, gfx::rgba(255, 255, 255));
        for (const ErrorMark &m : errorMarks_) {
            const float a = 1 - anim::clamp01(static_cast<float>((ctx.now - m.time) / 4.0));
            b.rect(cx + m.ms * k - 2, y - 10, 4, 28, gfx::withAlpha(gfx::rgba(255, 255, 255), a * 0.8f));
        }
        while (!errorMarks_.empty() && ctx.now - errorMarks_.front().time > 4.0) errorMarks_.pop_front();
        if (ctx.settings->showUr && errors_.size() >= 2) {
            // Taux d'instabilité : écart type des erreurs de frappe x 10, ramené à la vitesse normale.
            double mean = 0, var = 0;
            for (float e : errors_) mean += e;
            mean /= static_cast<double>(errors_.size());
            for (float e : errors_) var += (e - mean) * (e - mean);
            snprintf(line, sizeof(line), "%.2f UR", std::sqrt(var / static_cast<double>(errors_.size())) * 10 / mods_.rate());
            ctx.fonts->semibold.drawShadow(b, cx, y + 16, 22, gfx::rgba(255, 255, 255, 220), line, gfx::Align::Center);
        }
    }

    // Compteur de touches (droite, milieu).
    if (ctx.settings->keyOverlay) {
        for (int k = 0; k < 2; ++k) {
            const bool on = (keysHeld_ >> k) & 1u;
            const float w = 72, h = 64, x = kVW - w - 16, y = kVH / 2 - h - 4 + static_cast<float>(k) * (h + 8);
            b.roundRect(x, y, w, h, 10, on ? gfx::rgba(255, 102, 170, 220) : gfx::rgba(0, 0, 0, 140));
            snprintf(line, sizeof(line), "%d", keyCount_[k]);
            ctx.fonts->semibold.drawMid(b, x + w / 2, y + h / 2, keyCount_[k] >= 1000 ? 20 : 26, gfx::rgba(255, 255, 255),
                                        keyCount_[k] ? line : (k ? "K2" : "K1"), gfx::Align::Center);
        }
    }

    // Passer l'intro.
    if (canSkip()) {
        const float pulse = 0.7f + 0.3f * std::sin(static_cast<float>(ctx.now) * 4);
        ctx.fonts->semibold.drawShadow(b, kVW - 40, kVH - 90, 34, gfx::withAlpha(gfx::rgba(255, 255, 255), pulse),
                                       "Triangle : passer l'intro →", gfx::Align::Right);
    }
    (void)t;
}

}  // namespace game

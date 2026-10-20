// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/difficulty.h"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNormRadius = 50, kMinDelta = 25;
constexpr double kAssumedSliderRadius = kNormRadius * 1.8, kMaxSliderRadius = kNormRadius * 2.4;
constexpr double kSectionLength = 400, kDifficultyMultiplier = 0.0675;

double lerp(double a, double b, double t) { return a + (b - a) * t; }
double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Vec {
    double x = 0, y = 0;
};
Vec sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
double len(Vec v) { return std::sqrt(v.x * v.x + v.y * v.y); }

// Objet préparé pour l'analyse (équivalent d'OsuDifficultyHitObject).
struct DiffObject {
    double time = 0, endTime = 0;      // ms, à la vitesse de jeu
    double deltaTime = 0, strainTime = 0;
    bool slider = false, spinner = false;
    Vec pos, lazyEnd;
    double travelDistance = 0, travelTime = 0, lazyTravelTime = 0;
    double lazyJumpDistance = 0, minJumpDistance = 0, minJumpTime = 0;
    double angle = -1;                 // -1 : pas d'angle
};

// Courbe de stack d'osu! : décalage d'un objet empilé (coordonnées osu!).
Vec stackedPos(Vec2 p, int stackHeight, double radius) {
    const double off = stackHeight * radius * 0.1;
    return {p.x - off, p.y - off};
}

std::vector<DiffObject> prepare(const Beatmap &map, double rate, double radius) {
    double scaling = kNormRadius / radius;
    if (radius < 30) scaling *= 1 + std::min(30 - radius, 5.0) / 50;
    std::vector<DiffObject> out;
    out.reserve(map.objects.size());
    for (const HitObject &o : map.objects) {
        DiffObject d;
        d.time = o.time / rate;
        d.endTime = o.endTime / rate;
        d.slider = o.type & kSlider;
        d.spinner = o.type & kSpinner;
        d.pos = stackedPos(o.pos, o.stackHeight, radius);
        d.lazyEnd = d.pos;
        if (d.slider) {
            // Trajet « paresseux » du curseur : il ne bouge que s'il sort du cercle de suivi.
            struct Nested {
                double time;
                Vec pos;
            };
            std::vector<Nested> nested;
            const double span = o.spanDuration;
            for (int s = 0; s < o.slides; ++s) {
                for (const SliderTick &t : o.ticks) {
                    const double off = t.time - o.time;
                    const float p = s % 2 == 0 ? t.progress : 1 - t.progress;
                    const double tt = o.time + s * span + (s % 2 == 0 ? off : span - off);
                    const Vec2 q = o.positionAt(p);
                    nested.push_back({tt, stackedPos(q, o.stackHeight, radius)});
                }
                if (s < o.slides - 1) {
                    const Vec2 q = o.positionAt(s % 2 == 0 ? 1.0f : 0.0f);
                    nested.push_back({o.time + (s + 1) * span, stackedPos(q, o.stackHeight, radius)});
                }
            }
            // Fin (« legacy last tick ») 36 ms avant la fin du slider.
            const double tailTime = std::max(o.time + (o.endTime - o.time) / 2, o.endTime - 36);
            const Vec2 endPos = o.slides % 2 == 1 ? o.positionAt(1) : o.positionAt(0);
            nested.push_back({tailTime, stackedPos(endPos, o.stackHeight, radius)});
            std::sort(nested.begin(), nested.end(), [](const Nested &a, const Nested &b) { return a.time < b.time; });
            Vec cursor = d.pos;
            double travel = 0;
            for (size_t i = 0; i < nested.size(); ++i) {
                Vec movement = sub(nested[i].pos, cursor);
                double moveLen = scaling * len(movement);
                const double required = kAssumedSliderRadius;
                if (moveLen > required) {
                    const double k = (moveLen - required) / moveLen;
                    cursor = {cursor.x + movement.x * k, cursor.y + movement.y * k};
                    travel += moveLen - required;
                }
            }
            d.lazyEnd = cursor;
            d.lazyTravelTime = (tailTime - o.time) / rate;
            d.travelDistance = travel * std::pow(1 + (o.slides - 1) / 2.5, 1 / 2.5);
            d.travelTime = std::max(d.lazyTravelTime, kMinDelta);
        }
        out.push_back(d);
    }
    for (size_t i = 1; i < out.size(); ++i) {
        DiffObject &c = out[i];
        const DiffObject &last = out[i - 1];
        c.deltaTime = c.time - last.time;
        c.strainTime = std::max(c.deltaTime, kMinDelta);
        if (c.spinner || last.spinner) continue;
        const Vec lastCursor = last.slider ? last.lazyEnd : last.pos;
        c.lazyJumpDistance = len(sub(c.pos, lastCursor)) * scaling;
        c.minJumpTime = c.strainTime;
        c.minJumpDistance = c.lazyJumpDistance;
        if (last.slider) {
            const double lastTravelTime = std::max(last.lazyTravelTime, kMinDelta);
            c.minJumpTime = std::max(c.strainTime - lastTravelTime, kMinDelta);
            const HitObject &lo = map.objects[i - 1];
            const Vec2 tail = lo.slides % 2 == 1 ? lo.positionAt(1) : lo.positionAt(0);
            const double tailJump = len(sub(c.pos, stackedPos(tail, lo.stackHeight, radius))) * scaling;
            c.minJumpDistance = std::max(0.0, std::min(c.lazyJumpDistance - (kMaxSliderRadius - kAssumedSliderRadius),
                                                       tailJump - kMaxSliderRadius));
        }
        if (i >= 2) {
            const DiffObject &ll = out[i - 2];
            if (!ll.spinner) {
                const Vec llCursor = ll.slider ? ll.lazyEnd : ll.pos;
                const Vec v1 = sub(llCursor, last.pos), v2 = sub(c.pos, lastCursor);
                const double dot = v1.x * v2.x + v1.y * v2.y, det = v1.x * v2.y - v1.y * v2.x;
                c.angle = std::fabs(std::atan2(det, dot));
            }
        }
    }
    return out;
}

double wideAngleBonus(double a) { return std::pow(std::sin(3.0 / 4 * (std::min(5.0 / 6 * kPi, std::max(kPi / 6, a)) - kPi / 6)), 2); }
double acuteAngleBonus(double a) { return 1 - wideAngleBonus(a); }

double aimEvaluate(const std::vector<DiffObject> &v, size_t i, bool withSliders) {
    if (i < 2) return 0;
    const DiffObject &c = v[i], &last = v[i - 1], &ll = v[i - 2];
    if (c.spinner || last.spinner) return 0;
    double currVelocity = c.lazyJumpDistance / c.strainTime;
    if (last.slider && withSliders) {
        const double travelVelocity = last.travelDistance / last.travelTime;
        const double movementVelocity = c.minJumpDistance / c.minJumpTime;
        currVelocity = std::max(currVelocity, movementVelocity + travelVelocity);
    }
    double prevVelocity = last.lazyJumpDistance / last.strainTime;
    if (ll.slider && withSliders) {
        const double travelVelocity = ll.travelDistance / ll.travelTime;
        const double movementVelocity = last.minJumpDistance / std::max(last.minJumpTime, 1.0);
        prevVelocity = std::max(prevVelocity, movementVelocity + travelVelocity);
    }
    double wide = 0, acute = 0, sliderBonus = 0, velocityChange = 0;
    double strain = currVelocity;
    if (std::max(c.strainTime, last.strainTime) < 1.25 * std::min(c.strainTime, last.strainTime) && c.angle >= 0 &&
        last.angle >= 0) {
        const double angleBonus = std::min(currVelocity, prevVelocity);
        wide = wideAngleBonus(c.angle);
        acute = acuteAngleBonus(c.angle);
        if (c.strainTime > 100) {
            acute = 0;
        } else {
            acute *= acuteAngleBonus(last.angle) * std::min(angleBonus, 125 / c.strainTime) *
                     std::pow(std::sin(kPi / 2 * std::min(1.0, (100 - c.strainTime) / 25)), 2) *
                     std::pow(std::sin(kPi / 2 * (clamp(c.lazyJumpDistance, 50, 100) - 50) / 50), 2);
        }
        wide *= angleBonus * (1 - std::min(wide, std::pow(wideAngleBonus(last.angle), 3)));
        const double llAngle = ll.angle >= 0 ? ll.angle : 0;
        acute *= 0.5 + 0.5 * (1 - std::min(acute, std::pow(acuteAngleBonus(llAngle), 3)));
    }
    if (std::max(prevVelocity, currVelocity) != 0) {
        const double pv = (last.lazyJumpDistance + ll.travelDistance) / last.strainTime;
        const double cv = (c.lazyJumpDistance + last.travelDistance) / c.strainTime;
        const double distRatio = std::pow(std::sin(kPi / 2 * std::fabs(pv - cv) / std::max(pv, cv)), 2);
        const double overlap = std::min(125 / std::min(c.strainTime, last.strainTime), std::fabs(pv - cv));
        velocityChange = overlap * distRatio *
                         std::pow(std::min(c.strainTime, last.strainTime) / std::max(c.strainTime, last.strainTime), 2);
    }
    if (last.slider) sliderBonus = last.travelDistance / last.travelTime;
    strain += std::max(acute * 1.95, wide * 1.5 + velocityChange * 0.75);
    if (withSliders) strain += sliderBonus * 1.35;
    return strain;
}

double speedEvaluate(const std::vector<DiffObject> &v, size_t i, double greatWindow) {
    if (i < 1 || v[i].spinner) return 0;
    const DiffObject &c = v[i];
    double strainTime = c.strainTime;
    // Double frappe : objet suivant presque collé (même rythme partagé à deux mains).
    double doubletapness = 1;
    if (i + 1 < v.size()) {
        const double currDelta = std::max(1.0, c.deltaTime), nextDelta = std::max(1.0, v[i + 1].deltaTime);
        const double deltaDiff = std::fabs(nextDelta - currDelta);
        const double speedRatio = currDelta / std::max(currDelta, deltaDiff);
        const double windowRatio = std::pow(std::min(1.0, currDelta / greatWindow), 2);
        doubletapness = std::pow(speedRatio, 1 - windowRatio);
    }
    strainTime /= clamp((strainTime / greatWindow) / 0.93, 0.92, 1);
    double speedBonus = 0;
    if (strainTime < 75) speedBonus = 0.75 * std::pow((75 - strainTime) / 40, 2);
    const double travel = v[i - 1].travelDistance;
    const double distance = std::min(125.0, travel + c.minJumpDistance);
    const double distanceBonus = std::pow(distance / 125, 3.95) * 0.94;
    return (1 + speedBonus + distanceBonus) * doubletapness / strainTime;
}

// Somme pondérée des pics de difficulté (OsuStrainSkill) ; pics des 10 premières
// sections réduits pour ne pas sur-noter les passages courts.
double difficultyValue(std::vector<double> peaks) {
    std::sort(peaks.begin(), peaks.end(), std::greater<double>());
    for (size_t i = 0; i < std::min<size_t>(10, peaks.size()); ++i) {
        const double scale = std::log10(lerp(1, 10, clamp(static_cast<double>(i) / 10, 0, 1)));
        peaks[i] *= lerp(0.75, 1, scale);
    }
    std::sort(peaks.begin(), peaks.end(), std::greater<double>());
    double total = 0, weight = 1;
    for (double p : peaks) {
        if (p <= 0) break;
        total += p * weight;
        weight *= 0.9;
    }
    return total;
}

// Accumulateur de « strain » d'une compétence, avec ses pics par section de 400 ms.
struct Skill {
    double multiplier, decayBase;
    double current = 0, sectionPeak = 0, sectionEnd = -1;
    std::vector<double> peaks;
    double decay(double ms) const { return std::pow(decayBase, ms / 1000); }
    void process(double time, double strainTime, double value) {
        if (sectionEnd < 0) sectionEnd = std::ceil(time / kSectionLength) * kSectionLength;
        while (time > sectionEnd) {
            peaks.push_back(sectionPeak);
            // Nouvelle section : le pic part de la valeur décrue au début de la section.
            sectionPeak = current * decay(sectionEnd - (time - strainTime));
            sectionEnd += kSectionLength;
        }
        current *= decay(strainTime);
        current += value * multiplier;
        sectionPeak = std::max(sectionPeak, current);
    }
    double value() const {
        std::vector<double> all = peaks;
        all.push_back(sectionPeak);
        return difficultyValue(all);
    }
};

double adjustedOd(double od, double rate) {
    const double great = (80 - 6 * od) / rate;
    return (80 - great) / 6;
}
double adjustedAr(double ar, double rate) {
    double preempt = ar < 5 ? 1200 + 600 * (5 - ar) / 5 : 1200 - 750 * (ar - 5) / 5;
    preempt /= rate;
    return preempt > 1200 ? 5 - (preempt - 1200) / 600 * 5 : 5 + (1200 - preempt) / 750 * 5;
}

double starsFrom(double aim, double speed) {
    const double baseAim = std::pow(5 * std::max(1.0, aim / kDifficultyMultiplier) - 4, 3) / 100000;
    const double baseSpeed = std::pow(5 * std::max(1.0, speed / kDifficultyMultiplier) - 4, 3) / 100000;
    const double base = std::pow(std::pow(baseAim, 1.1) + std::pow(baseSpeed, 1.1), 1 / 1.1);
    return base > 1e-5 ? std::cbrt(1.15) * 0.027 * (std::cbrt(100000 / std::pow(2, 1 / 1.1) * base) + 4) : 0;
}

int comboOf(const HitObject &o) {
    if (!(o.type & kSlider)) return 1;
    return 1 + static_cast<int>(o.ticks.size()) * o.slides + (o.slides - 1) + 1;
}

}  // namespace

std::vector<DifficultyAttributes> computeTimedDifficulty(const Beatmap &map, const DifficultyMods &mods) {
    std::vector<DifficultyAttributes> out;
    if (map.objects.empty()) return out;
    double cs = map.cs, od = map.od, ar = map.ar < 0 ? map.od : map.ar;
    if (mods.hardRock) cs = std::min(10.0, cs * 1.3), od = std::min(10.0, od * 1.4), ar = std::min(10.0, ar * 1.4);
    if (mods.easy) cs *= 0.5, od *= 0.5, ar *= 0.5;
    const double radius = 54.4 - 4.48 * cs;
    const double greatWindow = (80 - 6 * od) / mods.rate;
    const std::vector<DiffObject> objs = prepare(map, mods.rate, radius);

    Skill aim{25.18, 0.15}, aimNoSliders{25.18, 0.15}, speed{1430, 0.3};
    std::vector<double> speedStrains;
    double maxSpeedStrain = 0;
    DifficultyAttributes base;
    base.od = adjustedOd(od, mods.rate);
    base.ar = adjustedAr(ar, mods.rate);
    out.reserve(objs.size());
    for (size_t i = 0; i < objs.size(); ++i) {
        const HitObject &o = map.objects[i];
        base.maxCombo += comboOf(o);
        if (o.type & kSlider) ++base.sliders;
        else if (o.type & kSpinner) ++base.spinners;
        else ++base.circles;
        if (i > 0) {
            aim.process(objs[i].time, objs[i].strainTime, aimEvaluate(objs, i, true));
            aimNoSliders.process(objs[i].time, objs[i].strainTime, aimEvaluate(objs, i, false));
            speed.process(objs[i].time, objs[i].strainTime, speedEvaluate(objs, i, greatWindow));
            speedStrains.push_back(speed.current);
            maxSpeedStrain = std::max(maxSpeedStrain, speed.current);
        }
        DifficultyAttributes a = base;
        const double aimValue = aim.value(), aimNs = aimNoSliders.value();
        a.aim = std::sqrt(aimValue) * kDifficultyMultiplier;
        a.speed = std::sqrt(speed.value()) * kDifficultyMultiplier;
        a.sliderFactor = aimValue > 0 ? std::sqrt(aimNs) / std::sqrt(aimValue) : 1;
        double notes = 0;
        if (maxSpeedStrain > 0)
            for (double s : speedStrains) notes += 1 / (1 + std::exp(-(s / maxSpeedStrain * 12 - 6)));
        a.speedNoteCount = notes;
        a.stars = starsFrom(a.aim, a.speed);
        out.push_back(a);
    }
    return out;
}

DifficultyAttributes computeDifficulty(const Beatmap &map, const DifficultyMods &mods) {
    // La dernière entrée des attributs « au fil de la map » est celle de la map entière.
    // (Le compte des notes de vitesse est refait à chaque objet : coût quadratique,
    // négligeable pour quelques milliers d'objets.)
    const std::vector<DifficultyAttributes> timed = computeTimedDifficulty(map, mods);
    return timed.empty() ? DifficultyAttributes{} : timed.back();
}

double computePp(const DifficultyAttributes &a, const ScoreStats &s, const DifficultyMods &mods, bool noFail) {
    const int total = s.n300 + s.n100 + s.n50 + s.nMiss;
    if (total <= 0) return 0;
    const double accuracy = (s.n300 * 6.0 + s.n100 * 2.0 + s.n50) / (total * 6.0);
    double multiplier = 1.15;
    if (noFail) multiplier *= std::max(0.9, 1 - 0.02 * s.nMiss);
    // Ratés « effectifs » : un combo cassé sur slider compte aussi.
    double effMiss = s.nMiss;
    if (a.sliders > 0) {
        const double threshold = a.maxCombo - 0.1 * a.sliders;
        if (s.maxCombo < threshold) effMiss = std::max(effMiss, threshold / std::max(1, s.maxCombo));
    }
    effMiss = std::min(effMiss, static_cast<double>(total));
    const double lengthBonus = 0.95 + 0.4 * std::min(1.0, total / 2000.0) + (total > 2000 ? std::log10(total / 2000.0) * 0.5 : 0);

    // Aim.
    double aimDiff = a.aim;
    if (a.sliders > 0) {
        const double difficultSliders = a.sliders * 0.15;
        const double dropped = clamp(std::min<double>(s.n100 + s.n50 + s.nMiss, a.maxCombo - s.maxCombo), 0, difficultSliders);
        aimDiff *= (1 - a.sliderFactor) * std::pow(1 - dropped / difficultSliders, 3) + a.sliderFactor;
    }
    double aimValue = std::pow(5 * std::max(1.0, aimDiff / kDifficultyMultiplier) - 4, 3) / 100000 * lengthBonus;
    if (effMiss > 0) aimValue *= 0.97 * std::pow(1 - std::pow(effMiss / total, 0.775), effMiss);
    double arFactor = 0;
    if (a.ar > 10.33) arFactor = 0.3 * (a.ar - 10.33);
    else if (a.ar < 8) arFactor = 0.05 * (8 - a.ar);
    aimValue *= 1 + arFactor * lengthBonus;
    if (mods.hidden) aimValue *= 1 + 0.04 * (12 - a.ar);
    aimValue *= accuracy * (0.98 + a.od * a.od / 2500);

    // Vitesse.
    double speedValue = std::pow(5 * std::max(1.0, a.speed / kDifficultyMultiplier) - 4, 3) / 100000 * lengthBonus;
    if (effMiss > 0) speedValue *= 0.97 * std::pow(1 - std::pow(effMiss / total, 0.775), std::pow(effMiss, 0.875));
    speedValue *= 1 + (a.ar > 10.33 ? 0.3 * (a.ar - 10.33) : 0) * lengthBonus;
    if (mods.hidden) speedValue *= 1 + 0.04 * (12 - a.ar);
    double relevantAcc = 0;
    if (a.speedNoteCount > 0) {
        const double diff = std::max(0.0, total - a.speedNoteCount);
        const double r300 = std::max(0.0, s.n300 - diff);
        const double r100 = std::max(0.0, s.n100 - std::max(0.0, diff - s.n300));
        const double r50 = std::max(0.0, s.n50 - std::max(0.0, diff - s.n300 - s.n100));
        relevantAcc = (r300 * 6 + r100 * 2 + r50) / (a.speedNoteCount * 6);
    }
    speedValue *= (0.95 + a.od * a.od / 750) * std::pow((accuracy + relevantAcc) / 2, (14.5 - std::max(a.od, 8.0)) / 2);
    speedValue *= std::pow(0.99, s.n50 < total / 500.0 ? 0 : s.n50 - total / 500.0);

    // Précision (sur les cercles seulement, comme le score v1).
    double accValue = 0;
    if (a.circles > 0) {
        const double better = std::max(0.0, ((s.n300 - (total - a.circles)) * 6.0 + s.n100 * 2.0 + s.n50) / (a.circles * 6.0));
        accValue = std::pow(1.52163, a.od) * std::pow(better, 24) * 2.83 * std::min(1.15, std::pow(a.circles / 1000.0, 0.3));
        if (mods.hidden) accValue *= 1.08;
    }
    return std::pow(std::pow(aimValue, 1.1) + std::pow(speedValue, 1.1) + std::pow(accValue, 1.1), 1 / 1.1) * multiplier;
}

}  // namespace game

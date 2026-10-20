// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/storyboard.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/context.h"
#include "core/fsutil.h"
#include "core/log.h"

namespace game {
namespace {


// Courbes d'osu! (numéros 0 à 34).
float easeOsu(int e, float t) {
    const float pi = 3.14159265f;
    auto outBounce = [](float x) {
        if (x < 1 / 2.75f) return 7.5625f * x * x;
        if (x < 2 / 2.75f) return x -= 1.5f / 2.75f, 7.5625f * x * x + 0.75f;
        if (x < 2.5f / 2.75f) return x -= 2.25f / 2.75f, 7.5625f * x * x + 0.9375f;
        return x -= 2.625f / 2.75f, 7.5625f * x * x + 0.984375f;
    };
    auto inOut = [](float x, float (*in)(float)) { return x < 0.5f ? in(2 * x) / 2 : 1 - in(2 - 2 * x) / 2; };
    switch (e) {
    case 1: case 4: return t * (2 - t);
    case 2: case 3: return t * t;
    case 5: return inOut(t, [](float x) { return x * x; });
    case 6: return t * t * t;
    case 7: return 1 - std::pow(1 - t, 3.0f);
    case 8: return inOut(t, [](float x) { return x * x * x; });
    case 9: return std::pow(t, 4.0f);
    case 10: return 1 - std::pow(1 - t, 4.0f);
    case 11: return inOut(t, [](float x) { return x * x * x * x; });
    case 12: return std::pow(t, 5.0f);
    case 13: return 1 - std::pow(1 - t, 5.0f);
    case 14: return inOut(t, [](float x) { return x * x * x * x * x; });
    case 15: return 1 - std::cos(t * pi / 2);
    case 16: return std::sin(t * pi / 2);
    case 17: return 0.5f - 0.5f * std::cos(t * pi);
    case 18: return t <= 0 ? 0 : std::pow(2.0f, 10 * (t - 1));
    case 19: return t >= 1 ? 1 : 1 - std::pow(2.0f, -10 * t);
    case 20: return inOut(t, [](float x) { return x <= 0 ? 0.0f : std::pow(2.0f, 10 * (x - 1)); });
    case 21: return 1 - std::sqrt(std::max(0.0f, 1 - t * t));
    case 22: return std::sqrt(std::max(0.0f, 1 - (t - 1) * (t - 1)));
    case 23: return inOut(t, [](float x) { return 1 - std::sqrt(std::max(0.0f, 1 - x * x)); });
    case 24: return t <= 0 || t >= 1 ? t : -std::pow(2.0f, 10 * (t - 1)) * std::sin((t - 1.075f) * 2 * pi / 0.3f);
    case 25: case 26: case 27: case 28:
        return t <= 0 || t >= 1 ? t : std::pow(2.0f, -10 * t) * std::sin((t - 0.075f) * 2 * pi / 0.3f) + 1;
    case 29: return t * t * (2.70158f * t - 1.70158f);
    case 30: return (t - 1) * (t - 1) * (2.70158f * (t - 1) + 1.70158f) + 1;
    case 31: return inOut(t, [](float x) { return x * x * (2.70158f * x - 1.70158f); });
    case 32: return 1 - outBounce(1 - t);
    case 33: return outBounce(t);
    case 34: return t < 0.5f ? (1 - outBounce(1 - 2 * t)) / 2 : (1 + outBounce(2 * t - 1)) / 2;
    default: return t;
    }
}

std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char ch : s) {
        if (ch == '"') quoted = !quoted;
        else if (ch == ',' && !quoted) out.push_back(cur), cur.clear();
        else cur += ch;
    }
    out.push_back(cur);
    return out;
}

int layerOf(const std::string &s) {
    static const char *names[] = {"Background", "Fail", "Pass", "Foreground", "Overlay"};
    for (int i = 0; i < 5; ++i)
        if (s == names[i]) return i;
    return std::atoi(s.c_str());
}

int originOf(const std::string &s) {
    static const char *names[] = {"TopLeft",     "Centre", "CentreLeft", "TopRight",   "BottomCentre",
                                  "TopCentre",   "Custom", "CentreRight", "BottomLeft", "BottomRight"};
    for (int i = 0; i < 10; ++i)
        if (s == names[i]) return i;
    return std::atoi(s.c_str());
}

void originXY(int o, float &ox, float &oy) {
    static const float t[10][2] = {{0, 0}, {.5f, .5f}, {0, .5f}, {1, 0}, {.5f, 1}, {.5f, 0}, {0, 0}, {1, .5f}, {0, 1}, {1, 1}};
    ox = t[o >= 0 && o < 10 ? o : 1][0];
    oy = t[o >= 0 && o < 10 ? o : 1][1];
}

std::string normPath(std::string p) {
    for (char &c : p)
        if (c == '\\') c = '/';
    while (!p.empty() && (p.front() == ' ' || p.front() == '"')) p.erase(p.begin());
    while (!p.empty() && (p.back() == ' ' || p.back() == '"' || p.back() == '\r')) p.pop_back();
    return p;
}

}  // namespace

Storyboard::~Storyboard() { unload(); }

void Storyboard::unload() {
    if (loaderStarted_) loader_.stop();
    loaderStarted_ = false;
    for (gfx::Texture &t : textures_) gfx::destroyTexture(t);
    textures_.clear();
    texByPath_.clear();
    pending_.clear();
    elements_.clear();
    usedFiles_.clear();
    files_.clear();
}

bool Storyboard::load(const std::string &folder, const std::string &osuFile) {
    unload();
    // Fichiers du dossier (insensible à la casse : les storyboards viennent de Windows).
    std::vector<std::string> dirs{""};
    for (size_t d = 0; d < dirs.size() && d < 64; ++d) {
        std::vector<fsu::Entry> entries;
        if (!fsu::listDir(folder + (dirs[d].empty() ? "" : "/" + dirs[d]), entries)) continue;
        for (const fsu::Entry &e : entries) {
            const std::string rel = dirs[d].empty() ? e.name : dirs[d] + "/" + e.name;
            if (e.isDir) dirs.push_back(rel);
            else files_[fsu::toLower(rel)] = rel;
        }
    }
    std::string text;
    if (fsu::readText(folder + "/" + osuFile, text)) parseEvents(text, folder);
    for (const auto &f : files_) {
        if (fsu::extensionLower(f.second) != "osb") continue;
        std::string osb;
        if (fsu::readText(folder + "/" + f.second, osb)) parseEvents(osb, folder);
    }
    // Les éléments sans aucune commande n'apparaissent jamais.
    elements_.erase(std::remove_if(elements_.begin(), elements_.end(), [](const Element &e) { return e.end < e.start; }),
                    elements_.end());
    LOG("storyboard: %s : %zu elements, %zu images (%zu fichiers)", folder.c_str(), elements_.size(), textures_.size(),
        files_.size());
    return !elements_.empty();
}

bool Storyboard::usesFile(const std::string &file) const {
    const std::string f = fsu::toLower(normPath(file));
    return std::find(usedFiles_.begin(), usedFiles_.end(), f) != usedFiles_.end();
}

int Storyboard::texture(const std::string &relPath, const std::string &folder) {
    const std::string key = fsu::toLower(normPath(relPath));
    auto it = texByPath_.find(key);
    if (it != texByPath_.end()) return it->second;
    const int index = static_cast<int>(textures_.size());
    textures_.push_back({});
    texByPath_[key] = index;
    usedFiles_.push_back(key);
    auto f = files_.find(key);
    if (f == files_.end()) return index;  // image absente : élément invisible
    if (!loaderStarted_) {
        loader_.start(2);
        loaderStarted_ = true;
    }
    pending_[loader_.request(folder + "/" + f->second, 4096)] = index;
    return index;
}

void Storyboard::update() {
    if (!loaderStarted_ || pending_.empty()) return;
    for (const gfx::ImageLoader::Done &d : loader_.poll(4)) {
        auto it = pending_.find(d.ticket);
        if (it == pending_.end()) continue;
        if (d.ok) textures_[static_cast<size_t>(it->second)] = d.texture;
        pending_.erase(it);
    }
}

void Storyboard::parseEvents(const std::string &text, const std::string &folder) {
    std::vector<std::pair<std::string, std::string>> vars;
    std::string section;
    Element *el = nullptr;
    // Boucle en cours : commandes relatives, répétées à la fin du bloc.
    bool inLoop = false, inTrigger = false;
    double loopStart = 0;
    int loopCount = 1;
    std::vector<std::pair<int, Cmd>> loopCmds;
    auto flushLoop = [&]() {
        if (!inLoop || !el) return;
        inLoop = false;
        double len = 0;
        for (auto &pc : loopCmds) len = std::max(len, pc.second.t1);
        for (int i = 0; i < std::max(1, loopCount); ++i)
            for (auto pc : loopCmds) {
                pc.second.t0 += loopStart + i * len;
                pc.second.t1 += loopStart + i * len;
                el->cmds[pc.first].push_back(pc.second);
                el->start = std::min(el->start, pc.second.t0);
                el->end = std::max(el->end, pc.second.t1);
            }
        loopCmds.clear();
    };
    size_t pos = 0;
    while (pos < text.size()) {
        size_t endl = text.find('\n', pos);
        if (endl == std::string::npos) endl = text.size();
        std::string line = text.substr(pos, endl - pos);
        pos = endl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.compare(0, 2, "//") == 0) continue;
        if (line[0] == '[') {
            flushLoop();
            section = line;
            el = nullptr;
            continue;
        }
        if (section == "[Variables]") {
            const size_t eq = line.find('=');
            if (eq != std::string::npos && line[0] == '$') vars.push_back({line.substr(0, eq), line.substr(eq + 1)});
            std::sort(vars.begin(), vars.end(), [](auto &a, auto &b) { return a.first.size() > b.first.size(); });
            continue;
        }
        if (section != "[Events]") continue;
        for (const auto &v : vars)
            for (size_t p = line.find(v.first); p != std::string::npos; p = line.find(v.first, p + v.second.size()))
                line.replace(p, v.first.size(), v.second);
        int depth = 0;
        while (depth < static_cast<int>(line.size()) && (line[depth] == ' ' || line[depth] == '_')) ++depth;
        const std::vector<std::string> f = split(line.substr(depth));
        if (depth == 0) {
            flushLoop();
            el = nullptr;
            inTrigger = false;
            const std::string &kind = f[0];
            const bool sprite = kind == "Sprite" || kind == "4";
            const bool anim = kind == "Animation" || kind == "6";
            if ((!sprite && !anim) || f.size() < 6) continue;
            Element e;
            e.layer = layerOf(f[1]);
            e.origin = originOf(f[2]);
            e.x = static_cast<float>(std::atof(f[4].c_str()));
            e.y = static_cast<float>(std::atof(f[5].c_str()));
            const std::string path = normPath(f[3]);
            if (anim && f.size() >= 8) {
                const int frames = std::clamp(std::atoi(f[6].c_str()), 1, 512);
                e.frameDelay = std::atof(f[7].c_str());
                e.loopForever = !(f.size() >= 9 && (f[8] == "LoopOnce" || f[8] == "1"));
                const size_t dot = path.find_last_of('.');
                for (int i = 0; i < frames; ++i) {
                    const std::string p = dot == std::string::npos ? path + std::to_string(i)
                                                                   : path.substr(0, dot) + std::to_string(i) + path.substr(dot);
                    e.frames.push_back(texture(p, folder));
                }
            } else {
                e.frames.push_back(texture(path, folder));
            }
            elements_.push_back(std::move(e));
            el = &elements_.back();
            continue;
        }
        if (!el || f.empty()) continue;
        if (depth == 1) {
            flushLoop();
            inTrigger = false;
        }
        const std::string &ev = f[0];
        if (ev == "L" && f.size() >= 3) {
            inLoop = true;
            loopStart = std::atof(f[1].c_str());
            loopCount = std::atoi(f[2].c_str());
            continue;
        }
        if (ev == "T") {
            inTrigger = true;  // déclencheurs : non gérés
            continue;
        }
        if (inTrigger || f.size() < 4) continue;
        int prop, n;
        if (ev == "F") prop = Fade, n = 1;
        else if (ev == "M") prop = Move, n = 2;
        else if (ev == "MX") prop = MoveX, n = 1;
        else if (ev == "MY") prop = MoveY, n = 1;
        else if (ev == "S") prop = Scale, n = 1;
        else if (ev == "V") prop = VScale, n = 2;
        else if (ev == "R") prop = Rotate, n = 1;
        else if (ev == "C") prop = Colour, n = 3;
        else if (ev == "P") prop = Param, n = 1;
        else continue;
        const int easing = std::atoi(f[1].c_str());
        const double t0 = std::atof(f[2].c_str());
        const double t1 = f[3].empty() ? t0 : std::atof(f[3].c_str());
        std::vector<float> params;
        for (size_t i = 4; i < f.size(); ++i) {
            if (prop == Param) params.push_back(f[i] == "H" ? 0.f : f[i] == "V" ? 1.f : 2.f);
            else params.push_back(static_cast<float>(std::atof(f[i].c_str())));
        }
        if (static_cast<int>(params.size()) < n) continue;
        const int segments = std::max(1, static_cast<int>(params.size()) / n - 1);
        const double dur = t1 - t0;
        for (int s = 0; s < segments; ++s) {
            Cmd c{};
            c.easing = static_cast<uint8_t>(easing);
            c.t0 = t0 + dur * s;
            c.t1 = t1 + dur * s;
            const size_t from = static_cast<size_t>(s * n);
            const size_t to = static_cast<int>(params.size()) >= (s + 2) * n ? from + static_cast<size_t>(n) : from;
            for (int k = 0; k < n; ++k) {
                c.a[k] = params[from + static_cast<size_t>(k)];
                c.b[k] = params[to + static_cast<size_t>(k)];
            }
            if (inLoop && depth >= 2) {
                loopCmds.push_back({prop, c});
            } else {
                el->cmds[prop].push_back(c);
                el->start = std::min(el->start, c.t0);
                el->end = std::max(el->end, c.t1);
            }
        }
    }
    flushLoop();
    for (Element &e : elements_)
        for (auto &v : e.cmds)
            std::stable_sort(v.begin(), v.end(), [](const Cmd &a, const Cmd &b) { return a.t0 < b.t0; });
}

bool Storyboard::value(const std::vector<Cmd> &cmds, double t, int n, float *out) {
    if (cmds.empty()) return false;
    if (t < cmds.front().t0) {
        for (int k = 0; k < n; ++k) out[k] = cmds.front().a[k];
        return true;
    }
    auto it = std::upper_bound(cmds.begin(), cmds.end(), t, [](double v, const Cmd &c) { return v < c.t0; });
    const Cmd &c = *(it - 1);
    float p = 1;
    if (t < c.t1 && c.t1 > c.t0) p = easeOsu(c.easing, static_cast<float>((t - c.t0) / (c.t1 - c.t0)));
    for (int k = 0; k < n; ++k) out[k] = c.a[k] + (c.b[k] - c.a[k]) * p;
    return true;
}

void Storyboard::draw(gfx::SpriteBatch &b, double t, bool overlay) const {
    // Espace 640 x 480 (854 de large en 16:9), centré et mis à l'échelle de l'écran.
    const float s = kVH / 480, x0 = (kVW - 640 * s) / 2;
    for (const Element &e : elements_) {
        if ((e.layer == 4) != overlay || e.layer == 1) continue;  // pas de couche « échec »
        if (t < e.start || t > e.end) continue;
        float fade = 1, pos[2] = {e.x, e.y}, sc = 1, vs[2] = {1, 1}, rot = 0, col[3] = {255, 255, 255};
        value(e.cmds[Fade], t, 1, &fade);
        if (fade <= 0.001f) continue;
        value(e.cmds[Move], t, 2, pos);
        value(e.cmds[MoveX], t, 1, &pos[0]);
        value(e.cmds[MoveY], t, 1, &pos[1]);
        value(e.cmds[Scale], t, 1, &sc);
        value(e.cmds[VScale], t, 2, vs);
        value(e.cmds[Rotate], t, 1, &rot);
        value(e.cmds[Colour], t, 3, col);
        bool flipH = false, flipV = false, additive = false;
        for (const Cmd &c : e.cmds[Param])
            if (t >= c.t0 && (t <= c.t1 || c.t0 == c.t1)) {
                const int k = static_cast<int>(c.a[0]);
                (k == 0 ? flipH : k == 1 ? flipV : additive) = true;
            }
        int frame = 0;
        if (e.frames.size() > 1 && e.frameDelay > 0) {
            frame = static_cast<int>((t - e.start) / e.frameDelay);
            frame = e.loopForever ? frame % static_cast<int>(e.frames.size()) : std::min(frame, static_cast<int>(e.frames.size()) - 1);
            frame = std::max(0, frame);
        }
        const gfx::Texture &tex = textures_[static_cast<size_t>(e.frames[static_cast<size_t>(frame)])];
        if (!tex.id) continue;
        float w = static_cast<float>(tex.width) * s * sc * vs[0], h = static_cast<float>(tex.height) * s * sc * vs[1];
        if (w < 0) w = -w, flipH = !flipH;
        if (h < 0) h = -h, flipV = !flipV;
        float ox, oy;
        originXY(e.origin, ox, oy);
        const float cx = x0 + pos[0] * s, cy = pos[1] * s;
        const float cs = std::cos(rot), sn = std::sin(rot);
        float lx[4] = {-ox * w, (1 - ox) * w, (1 - ox) * w, -ox * w}, ly[4] = {-oy * h, -oy * h, (1 - oy) * h, (1 - oy) * h};
        if (flipH) std::swap(lx[0], lx[1]), std::swap(lx[2], lx[3]);
        if (flipV) std::swap(ly[0], ly[3]), std::swap(ly[1], ly[2]);
        float xy[8];
        for (int i = 0; i < 4; ++i) {
            xy[i * 2] = cx + lx[i] * cs - ly[i] * sn;
            xy[i * 2 + 1] = cy + lx[i] * sn + ly[i] * cs;
        }
        const auto ch = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); };
        const uint32_t color = gfx::rgba(ch(col[0]), ch(col[1]), ch(col[2]), ch(fade * 255));
        if (additive) b.setBlend(gfx::Blend::Additive);
        b.quad4(tex, xy, color);
        if (additive) b.setBlend(gfx::Blend::Alpha);
    }
}

}  // namespace game

// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/skin.h"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/fsutil.h"
#include "core/log.h"
#include "miniz/miniz.h"
#include "render/image.h"

namespace game {
namespace {

constexpr const char *kSkinList = "skins.txt";

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

bool parseColour(const std::string &v, uint32_t &out) {
    int r = 0, g = 0, b = 0;
    if (sscanf(v.c_str(), "%d , %d , %d", &r, &g, &b) != 3 && sscanf(v.c_str(), "%d,%d,%d", &r, &g, &b) != 3) return false;
    out = gfx::rgba(static_cast<uint8_t>(std::clamp(r, 0, 255)), static_cast<uint8_t>(std::clamp(g, 0, 255)),
                    static_cast<uint8_t>(std::clamp(b, 0, 255)));
    return true;
}

std::vector<std::string> readList(const std::string &path) {
    std::vector<std::string> out;
    std::string text;
    if (!fsu::readText(path, text)) return out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

}  // namespace

bool importOsk(const std::string &oskPath, const std::string &skinsDir, std::string &name) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, oskPath.c_str(), 0)) return false;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    // Beaucoup d'archives contiennent un dossier racine : on le retire (repéré par skin.ini).
    std::string prefix;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        std::string n = fsu::toLower(st.m_filename);
        for (char &c : n)
            if (c == '\\') c = '/';
        if (n == "skin.ini") {
            prefix.clear();
            break;
        }
        const size_t at = n.rfind("/skin.ini");
        if (at != std::string::npos && at + 9 == n.size()) prefix = n.substr(0, at + 1);
    }
    std::string base = oskPath.substr(oskPath.find_last_of('/') + 1);
    if (base.size() > 4) base.resize(base.size() - 4);
    name = fsu::sanitizeName(base);
    const std::string dest = skinsDir + "/" + name;
    fsu::makeDirs(dest);
    std::string manifest;
    int written = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        std::string n = fsu::toLower(st.m_filename);
        for (char &c : n)
            if (c == '\\') c = '/';
        if (!prefix.empty()) {
            if (n.compare(0, prefix.size(), prefix) != 0) continue;
            n = n.substr(prefix.size());
        }
        // Seuls les fichiers à la racine du skin servent ; on ignore les sous-dossiers.
        if (n.empty() || n.find('/') != std::string::npos || n.find("..") != std::string::npos) continue;
        if (!mz_zip_reader_extract_to_file(&zip, i, (dest + "/" + n).c_str(), 0)) continue;
        manifest += n + "\n";
        ++written;
    }
    mz_zip_reader_end(&zip);
    fsu::writeFileAtomic(dest + "/.files", manifest.data(), manifest.size());
    LOG("skin: %s importe (%d fichiers)", name.c_str(), written);
    return written > 0;
}

std::vector<std::string> scanSkins(const std::string &skinsDir) {
    fsu::makeDirs(skinsDir);
    std::vector<std::string> names = readList(skinsDir + "/" + kSkinList);
    std::vector<fsu::Entry> entries;
    bool changed = false;
    if (fsu::listDir(skinsDir, entries)) {
        for (const fsu::Entry &e : entries) {
            if (!e.isDir && fsu::extensionLower(e.name) == "osk") {
                std::string name;
                if (importOsk(skinsDir + "/" + e.name, skinsDir, name)) {
                    unlink((skinsDir + "/" + e.name).c_str());
                    if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
                    changed = true;
                }
            } else if (e.isDir && std::find(names.begin(), names.end(), e.name) == names.end()) {
                names.push_back(e.name);
                changed = true;
            }
        }
    }
    // Ne garde que les dossiers qui existent encore.
    names.erase(std::remove_if(names.begin(), names.end(), [&](const std::string &n) { return !fsu::isDir(skinsDir + "/" + n); }),
                names.end());
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) { return fsu::toLower(a) < fsu::toLower(b); });
    std::string text;
    for (const std::string &n : names) text += n + "\n";
    if (changed || true) fsu::writeFileAtomic(skinsDir + "/" + kSkinList, text.data(), text.size());
    LOG("skin: %zu skin(s) dans %s", names.size(), skinsDir.c_str());
    return names;
}

std::string Skin::resolve(const std::string &file) const {
    // Retrouve le nom réel quelle que soit la casse (dossiers copiés à la main).
    const std::string lower = fsu::toLower(file);
    for (const std::string &f : files_)
        if (fsu::toLower(f) == lower) return dir_ + "/" + f;
    return dir_ + "/" + lower;
}

SkinSprite Skin::sprite(const std::string &base) {
    SkinSprite s;
    gfx::ImageData img;
    for (int hd = 1; hd >= 0; --hd) {
        const std::string path = resolve(base + (hd ? "@2x.png" : ".png"));
        if (!gfx::decodeImageFile(path, 2048, img) || img.width <= 0) continue;
        // Une image de 1x1 (ou entièrement transparente) sert souvent à « masquer » un élément.
        s.tex = gfx::createTexture(img.width, img.height, img.rgba.data(), true);
        s.scale = hd ? 0.5f : 1.0f;
        return s;
    }
    return s;
}

void Skin::parseIni(const std::string &text) {
    std::string section;
    size_t pos = 0;
    std::vector<std::pair<int, uint32_t>> combos;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line.rfind("//", 0) == 0) continue;
        if (line[0] == '[') {
            section = line;
            continue;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = trim(line.substr(0, colon)), value = trim(line.substr(colon + 1));
        if (section == "[General]") {
            if (key == "HitCircleOverlayAboveNumber" || key == "HitCircleOverlayAboveNumer") overlayAboveNumber = atoi(value.c_str()) != 0;
            if (key == "CursorExpand") cursorExpand = atoi(value.c_str()) != 0;
            if (key == "CursorCentre") cursorCentre = atoi(value.c_str()) != 0;
            if (key == "CursorRotate") cursorRotate = atoi(value.c_str()) != 0;
        } else if (section == "[Colours]") {
            uint32_t c = 0;
            if (key.rfind("Combo", 0) == 0 && parseColour(value, c)) combos.emplace_back(atoi(key.c_str() + 5), c);
            if (key == "SliderBorder" && parseColour(value, c)) sliderBorder = c;
            if (key == "SliderTrackOverride" && parseColour(value, c)) {
                sliderTrackOverride = c;
                hasTrackOverride = true;
            }
        } else if (section == "[Fonts]") {
            if (key == "HitCirclePrefix") {
                numberPrefix_ = value;
                for (char &ch : numberPrefix_)
                    if (ch == '\\') ch = '/';
            }
            if (key == "HitCircleOverlap") hitCircleOverlap = atoi(value.c_str());
            if (key == "ScorePrefix" || key == "ComboPrefix") {
                std::string &p = key == "ScorePrefix" ? scorePrefix_ : comboPrefix_;
                p = value;
                for (char &ch : p)
                    if (ch == '\\') ch = '/';
            }
            if (key == "ScoreOverlap") ui.scoreOverlap = atoi(value.c_str());
            if (key == "ComboOverlap") ui.comboOverlap = atoi(value.c_str());
        }
    }
    std::sort(combos.begin(), combos.end());
    for (const auto &c : combos) comboColours.push_back(c.second);
}

bool Skin::load(const std::string &dir, const std::string &name) {
    unload();
    dir_ = dir;
    name_ = name;
    std::vector<fsu::Entry> entries;
    if (fsu::listDir(dir, entries)) {
        for (const fsu::Entry &e : entries) files_.push_back(e.name);
    } else {
        for (const std::string &f : readList(dir + "/.files")) files_.push_back(f);
    }
    std::string ini;
    if (fsu::readText(resolve("skin.ini"), ini)) parseIni(ini);
    hitcircle = sprite("hitcircle");
    hitcircleoverlay = sprite("hitcircleoverlay");
    approachcircle = sprite("approachcircle");
    sliderstartcircle = sprite("sliderstartcircle");
    sliderstartcircleoverlay = sprite("sliderstartcircleoverlay");
    reversearrow = sprite("reversearrow");
    sliderfollowcircle = sprite("sliderfollowcircle");
    sliderscorepoint = sprite("sliderscorepoint");
    cursor = sprite("cursor");
    cursortrail = sprite("cursortrail");
    cursormiddle = sprite("cursormiddle");
    spinnerCircle = sprite("spinner-circle");
    spinnerBackground = sprite("spinner-background");
    spinnerApproach = sprite("spinner-approachcircle");
    spinnerTop = sprite("spinner-top");
    spinnerBottom = sprite("spinner-bottom");
    spinnerMiddle = sprite("spinner-middle");
    spinnerMiddle2 = sprite("spinner-middle2");
    spinnerClear = sprite("spinner-clear");
    spinnerSpin = sprite("spinner-spin");
    hit0 = sprite("hit0");
    if (!hit0.ok()) hit0 = sprite("hit0-0");
    hit50 = sprite("hit50");
    if (!hit50.ok()) hit50 = sprite("hit50-0");
    hit100 = sprite("hit100");
    if (!hit100.ok()) hit100 = sprite("hit100-0");
    hit300 = sprite("hit300");
    if (!hit300.ok()) hit300 = sprite("hit300-0");
    // Bille : animée (sliderb0, sliderb1...) ou fixe (sliderb).
    for (int i = 0; i < 60; ++i) {
        SkinSprite s = sprite("sliderb" + std::to_string(i));
        if (!s.ok()) break;
        sliderb.push_back(s);
    }
    if (sliderb.empty()) {
        SkinSprite s = sprite("sliderb");
        if (s.ok()) sliderb.push_back(s);
    }
    for (int i = 0; i < 10; ++i) digits[i] = sprite(numberPrefix_ + "-" + std::to_string(i));
    // Interface (thème classique).
    if (comboPrefix_.empty()) comboPrefix_ = scorePrefix_;
    for (int i = 0; i < 10; ++i) {
        ui.scoreDigits[i] = sprite(scorePrefix_ + "-" + std::to_string(i));
        ui.comboDigits[i] = sprite(comboPrefix_ + "-" + std::to_string(i));
    }
    ui.scoreComma = sprite(scorePrefix_ + "-comma");
    ui.scoreDot = sprite(scorePrefix_ + "-dot");
    ui.scorePercent = sprite(scorePrefix_ + "-percent");
    ui.scoreX = sprite(scorePrefix_ + "-x");
    ui.comboX = sprite(comboPrefix_ + "-x");
    static const char *const kRankNames[8] = {"XH", "SH", "X", "S", "A", "B", "C", "D"};
    for (int i = 0; i < 8; ++i) {
        ui.ranking[i] = sprite(std::string("ranking-") + kRankNames[i]);
        ui.rankingSmall[i] = sprite(std::string("ranking-") + kRankNames[i] + "-small");
    }
    ui.hit300g = sprite("hit300g");
    if (!ui.hit300g.ok()) ui.hit300g = sprite("hit300g-0");
    ui.hit100k = sprite("hit100k");
    if (!ui.hit100k.ok()) ui.hit100k = sprite("hit100k-0");
    ui.pauseContinue = sprite("pause-continue");
    ui.pauseRetry = sprite("pause-retry");
    ui.pauseBack = sprite("pause-back");
    ui.pauseOverlay = sprite("pause-overlay");
    ui.failBackground = sprite("fail-background");
    ui.menuBack = sprite("menu-back");
    if (!ui.menuBack.ok()) ui.menuBack = sprite("menu-back-0");
    ui.selectionMode = sprite("selection-mode");
    ui.selectionModeOver = sprite("selection-mode-over");
    ui.selectionMods = sprite("selection-mods");
    ui.selectionModsOver = sprite("selection-mods-over");
    ui.selectionRandom = sprite("selection-random");
    ui.selectionRandomOver = sprite("selection-random-over");
    ui.selectionOptions = sprite("selection-options");
    ui.selectionOptionsOver = sprite("selection-options-over");
    ui.scorebarBg = sprite("scorebar-bg");
    ui.scorebarColour = sprite("scorebar-colour");
    if (!ui.scorebarColour.ok()) ui.scorebarColour = sprite("scorebar-colour-0");
    ui.rankingPanel = sprite("ranking-panel");
    ui.rankingPerfect = sprite("ranking-perfect");
    ui.rankingTitle = sprite("ranking-title");
    ui.rankingRetry = sprite("ranking-retry");
    ui.rankingReplay = sprite("ranking-replay");
    ui.rankingGraph = sprite("ranking-graph");
    ui.rankingMaxCombo = sprite("ranking-maxcombo");
    ui.rankingAccuracy = sprite("ranking-accuracy");
    loaded_ = hitcircle.ok() || approachcircle.ok() || cursor.ok();
    LOG("skin: %s charge (cercle %d, chiffres %d, curseur %d, %zu couleurs)", name.c_str(), hitcircle.ok(), digits[0].ok(),
        cursor.ok(), comboColours.size());
    return loaded_;
}

std::vector<SkinSprite *> SkinUi::all() {
    std::vector<SkinSprite *> v;
    for (SkinSprite &s : scoreDigits) v.push_back(&s);
    for (SkinSprite &s : comboDigits) v.push_back(&s);
    for (SkinSprite &s : ranking) v.push_back(&s);
    for (SkinSprite &s : rankingSmall) v.push_back(&s);
    for (SkinSprite *s : {&scoreComma, &scoreDot, &scorePercent, &scoreX, &comboX, &hit300g, &hit100k, &pauseContinue, &pauseRetry,
                          &pauseBack, &pauseOverlay, &failBackground, &menuBack, &selectionMode, &selectionModeOver, &selectionMods,
                          &selectionModsOver, &selectionRandom, &selectionRandomOver, &selectionOptions, &selectionOptionsOver,
                          &scorebarBg, &scorebarColour, &rankingPanel, &rankingPerfect, &rankingTitle, &rankingRetry, &rankingReplay,
                          &rankingGraph, &rankingMaxCombo, &rankingAccuracy})
        v.push_back(s);
    return v;
}

int rankIndex(const std::string &rank, bool hidden) {
    if (rank == "SS") return hidden ? 0 : 2;
    if (rank == "S") return hidden ? 1 : 3;
    if (rank == "A") return 4;
    if (rank == "B") return 5;
    if (rank == "C") return 6;
    return 7;
}

void Skin::unload() {
    for (SkinSprite *s : {&hitcircle, &hitcircleoverlay, &approachcircle, &sliderstartcircle, &sliderstartcircleoverlay, &reversearrow,
                          &sliderfollowcircle, &sliderscorepoint, &cursor, &cursortrail, &cursormiddle, &spinnerCircle,
                          &spinnerBackground, &spinnerApproach, &spinnerTop, &spinnerBottom, &spinnerMiddle, &spinnerMiddle2,
                          &spinnerClear, &spinnerSpin, &hit0, &hit50, &hit100, &hit300})
        gfx::destroyTexture(s->tex);
    for (SkinSprite &s : sliderb) gfx::destroyTexture(s.tex);
    sliderb.clear();
    for (SkinSprite &s : digits) gfx::destroyTexture(s.tex);
    for (SkinSprite *s : ui.all()) gfx::destroyTexture(s->tex);
    ui = SkinUi{};
    scorePrefix_ = "score";
    comboPrefix_.clear();
    comboColours.clear();
    files_.clear();
    sliderBorder = 0xFFFFFFFFu;
    hasTrackOverride = false;
    overlayAboveNumber = true;
    hitCircleOverlap = -2;
    numberPrefix_ = "default";
    loaded_ = false;
}

}  // namespace game

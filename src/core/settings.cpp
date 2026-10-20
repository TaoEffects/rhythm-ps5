// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/settings.h"

#include <cstdio>
#include <cstdlib>

#include "core/fsutil.h"
#include "core/log.h"

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

bool Settings::load(const std::string &path) {
    std::string text;
    if (!fsu::readText(path, text, 1 << 20)) return false;
    version = 0;  // un fichier sans version date d'avant les migrations
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        const float f = static_cast<float>(atof(value.c_str()));
        const int i = atoi(value.c_str());
        if (key == "refresh_hz") refreshHz = i == 120 ? 120 : 60;
        else if (key == "background_dim") backgroundDim = clampf(f, 0, 1);
        else if (key == "show_debug") showDebug = i != 0;
        else if (key == "language") language = i;
        else if (key == "storyboard") storyboard = i != 0;
        else if (key == "frame_limit") frameLimit = i;
        else if (key == "show_fps") showFps = i != 0;
        else if (key == "fullscreen") fullscreen = i != 0;
        else if (key == "pp_counter") showPpCounter = i != 0;
        else if (key == "instafade") instafade = i != 0;
        else if (key == "misses_only") missesOnly = i != 0;
        else if (key == "show_ur") showUr = i != 0;
        else if (key == "key_overlay") keyOverlay = i != 0;
        else if (key == "tablet_playfield") tabletPlayfield = i != 0;
        else if (key == "hit_error_bar") showHitErrorBar = i != 0;
        else if (key == "master_volume") masterVolume = clampf(f, 0, 1);
        else if (key == "music_volume") musicVolume = clampf(f, 0, 1);
        else if (key == "effects_volume") effectsVolume = clampf(f, 0, 1);
        else if (key == "offset_ms") offsetMs = i < -300 ? -300 : (i > 300 ? 300 : i);
        else if (key == "cursor_speed") cursorSpeed = clampf(f, 200, 5000);
        else if (key == "cursor_curve") cursorCurve = clampf(f, 1, 3);
        else if (key == "touchpad_absolute") touchpadAbsolute = i != 0;
        else if (key == "last_set") lastSetFolder = value;
        else if (key == "last_difficulty") lastDifficulty = i;
        else if (key == "mirror" && !value.empty()) mirror = value;
        else if (key == "first_run_done") firstRunDone = i != 0;
        else if (key == "skin") skin = i >= 0 && i <= 2 ? i : 0;
        else if (key == "skin_name") skinName = value;
        else if (key == "version") version = i;
        else if (key == "key_hit1") keyHit1 = i;
        else if (key == "key_hit2") keyHit2 = i;
        else if (key == "pad_hit_mask") padHitMask = static_cast<unsigned>(strtoul(value.c_str(), nullptr, 10));
        else if (key == "mouse_buttons_in_game") mouseButtonsInGame = i != 0;
        else if (key == "cursor_size") cursorSize = clampf(f, 0.5f, 2.0f);
        else if (key == "cursor_trail") cursorTrail = i != 0;
        else if (key == "show_hud") showHud = i != 0;
        else if (key == "blur_in_game") blurInGame = i != 0;
        else if (key == "menu_parallax") menuParallax = i != 0;
        else if (key == "intro") introEnabled = i != 0;
        else if (key == "original_metadata") originalMetadata = i != 0;
        else if (key == "ui_scale") uiScale = clampf(f, 0.8f, 1.3f);
        else if (key == "input_mode") inputMode = i >= 0 && i <= 2 ? i : 0;
        else if (key == "ui_theme") uiTheme = 0;
    }
    // Version 2 : 120 Hz et style « triangles » par défaut pour tout le monde.
    if (version < 2) {
        refreshHz = 120;
        if (skin == 0) skin = 1;
        version = 2;
    }
    LOG("settings: charges depuis %s (refresh %d Hz)", path.c_str(), refreshHz);
    return true;
}

bool Settings::save(const std::string &path) const {
    std::string out;
    char line[512];
    auto put = [&](const char *key, const char *fmt, auto value) {
        snprintf(line, sizeof(line), "%s=", key);
        out += line;
        snprintf(line, sizeof(line), fmt, value);
        out += line;
        out += "\n";
    };
    put("version", "%d", version);
    put("refresh_hz", "%d", refreshHz);
    put("background_dim", "%.2f", static_cast<double>(backgroundDim));
    put("show_debug", "%d", showDebug ? 1 : 0);
    put("language", "%d", language);
    put("storyboard", "%d", storyboard ? 1 : 0);
    put("frame_limit", "%d", frameLimit);
    put("show_fps", "%d", showFps ? 1 : 0);
    put("fullscreen", "%d", fullscreen ? 1 : 0);
    put("pp_counter", "%d", showPpCounter ? 1 : 0);
    put("instafade", "%d", instafade ? 1 : 0);
    put("misses_only", "%d", missesOnly ? 1 : 0);
    put("show_ur", "%d", showUr ? 1 : 0);
    put("key_overlay", "%d", keyOverlay ? 1 : 0);
    put("tablet_playfield", "%d", tabletPlayfield ? 1 : 0);
    put("hit_error_bar", "%d", showHitErrorBar ? 1 : 0);
    put("master_volume", "%.2f", static_cast<double>(masterVolume));
    put("music_volume", "%.2f", static_cast<double>(musicVolume));
    put("effects_volume", "%.2f", static_cast<double>(effectsVolume));
    put("offset_ms", "%d", offsetMs);
    put("cursor_speed", "%.0f", static_cast<double>(cursorSpeed));
    put("cursor_curve", "%.2f", static_cast<double>(cursorCurve));
    put("touchpad_absolute", "%d", touchpadAbsolute ? 1 : 0);
    put("last_set", "%s", lastSetFolder.c_str());
    put("last_difficulty", "%d", lastDifficulty);
    put("mirror", "%s", mirror.c_str());
    put("first_run_done", "%d", firstRunDone ? 1 : 0);
    put("skin", "%d", skin);
    put("skin_name", "%s", skinName.c_str());
    put("key_hit1", "%d", keyHit1);
    put("key_hit2", "%d", keyHit2);
    put("pad_hit_mask", "%u", padHitMask);
    put("mouse_buttons_in_game", "%d", mouseButtonsInGame ? 1 : 0);
    put("cursor_size", "%.2f", static_cast<double>(cursorSize));
    put("cursor_trail", "%d", cursorTrail ? 1 : 0);
    put("show_hud", "%d", showHud ? 1 : 0);
    put("blur_in_game", "%d", blurInGame ? 1 : 0);
    put("menu_parallax", "%d", menuParallax ? 1 : 0);
    put("intro", "%d", introEnabled ? 1 : 0);
    put("original_metadata", "%d", originalMetadata ? 1 : 0);
    put("ui_scale", "%.2f", static_cast<double>(uiScale));
    put("input_mode", "%d", inputMode);
    put("ui_theme", "%d", uiTheme);
    return fsu::writeFileAtomic(path, out.data(), out.size());
}

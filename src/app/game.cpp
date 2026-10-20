// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/game.h"
#include "core/i18n.h"

#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/anim.h"
#include "core/fsutil.h"
#include "core/log.h"
#include "core/netlog.h"
#include "core/thread.h"
#include "net/http.h"
#include "render/gl.h"
#include "render/procedural.h"
#include "version.h"

namespace {

// Codes HID du clavier (PC et clavier USB sur PS5).
constexpr int kKeyEnter = 0x28, kKeyEsc = 0x29, kKeyRight = 0x4F, kKeyLeft = 0x50,
              kKeyDown = 0x51, kKeyUp = 0x52, kKeyZ = 0x1D, kKeyX = 0x1B, kKeyF1 = 0x3A, kKeyF2 = 0x3B, kKeyF3 = 0x3C,
              kKeySpace = 0x2C,
              kKeyPgUp = 0x4B, kKeyPgDn = 0x4E, kKeyHome = 0x4A, kKeyEnd = 0x4D;

// Texture procédurale : triangle plein anticrénelé.
gfx::Texture makeTriangle(int size) {
    std::vector<uint8_t> a(static_cast<size_t>(size) * size);
    const float h = static_cast<float>(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float fx = static_cast<float>(x) + 0.5f, fy = static_cast<float>(y) + 0.5f;
            // Distance signée aux 3 côtés d'un triangle pointe en haut.
            const float half = (fy / h) * h * 0.5f;
            const float d1 = fx - (h * 0.5f - half), d2 = (h * 0.5f + half) - fx, d3 = h - fy;
            const float d = std::fmin(std::fmin(d1, d2), d3);
            a[static_cast<size_t>(y) * size + x] = static_cast<uint8_t>(std::clamp(d, 0.0f, 1.0f) * 255);
        }
    return gfx::createAlphaTexture(size, size, a.data(), true);
}

// Chevron (flèche de retour des sliders) pointant vers la droite.
gfx::Texture makeArrow(int size) {
    std::vector<uint8_t> a(static_cast<size_t>(size) * size);
    const float s = static_cast<float>(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) / s - 0.5f, fy = (static_cast<float>(y) + 0.5f) / s - 0.5f;
            // Deux segments épais formant ">".
            const float t = 0.09f;
            auto seg = [&](float ax, float ay, float bx, float by) {
                const float vx = bx - ax, vy = by - ay;
                float k = ((fx - ax) * vx + (fy - ay) * vy) / (vx * vx + vy * vy);
                k = std::clamp(k, 0.0f, 1.0f);
                const float dx = fx - (ax + vx * k), dy = fy - (ay + vy * k);
                return std::sqrt(dx * dx + dy * dy);
            };
            const float d = std::fmin(seg(-0.2f, -0.3f, 0.2f, 0.0f), seg(0.2f, 0.0f, -0.2f, 0.3f));
            a[static_cast<size_t>(y) * size + x] = static_cast<uint8_t>(std::clamp((t - d) * s, 0.0f, 1.0f) * 255);
        }
    return gfx::createAlphaTexture(size, size, a.data(), true);
}

// Disque de spinner : dégradé radial doux.
gfx::Texture makeSpinnerDisc(int size) {
    std::vector<uint8_t> a(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2 - 1;
            const float dy = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2 - 1;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float v = d > 1 ? 0 : std::pow(d, 1.5f) * std::clamp((1 - d) * 40, 0.0f, 1.0f);
            a[static_cast<size_t>(y) * size + x] = static_cast<uint8_t>(v * 255);
        }
    return gfx::createAlphaTexture(size, size, a.data(), true);
}

// Motif de triangles translucides découpé en disque (intérieur des cercles).
gfx::Texture makeCircleTriangles(int size) {
    std::vector<uint8_t> a(static_cast<size_t>(size) * size, 0);
    uint32_t seed = 4242;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };
    const float s = static_cast<float>(size);
    for (int n = 0; n < 34; ++n) {
        const float tw = s * (0.12f + rnd() * 0.3f), th = tw * 0.87f;
        const float tx = rnd() * s - tw / 2, ty = rnd() * s - th / 2;
        const uint8_t v = static_cast<uint8_t>(50 + rnd() * 120);
        for (int y = static_cast<int>(std::max(0.0f, ty)); y < std::min(s, ty + th); ++y) {
            const float fy = (static_cast<float>(y) - ty) / th;  // 0 en haut (pointe), 1 en bas
            const float half = fy * tw / 2;
            for (int x = static_cast<int>(std::max(0.0f, tx + tw / 2 - half)); x < std::min(s, tx + tw / 2 + half); ++x) {
                uint8_t &p = a[static_cast<size_t>(y) * size + x];
                p = std::max(p, v);
            }
        }
    }
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / s * 2 - 1, dy = (static_cast<float>(y) + 0.5f) / s * 2 - 1;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float mask = std::clamp((1 - d) * s * 0.5f, 0.0f, 1.0f);
            uint8_t &p = a[static_cast<size_t>(y) * size + x];
            p = static_cast<uint8_t>(p * mask);
        }
    return gfx::createAlphaTexture(size, size, a.data(), true);
}

float deadzone(float v, float dz, float curve) {
    const float a = std::fabs(v);
    if (a <= dz) return 0;
    return std::copysign(std::pow((a - dz) / (1 - dz), curve), v);
}

}  // namespace

void Context::saveSettings() const { settings->save(dataDir + "/settings.cfg"); }

void Context::applyVolumes() const {
    mixer->setMasterGain(settings->masterVolume);
    mixer->setEffectsGain(settings->effectsVolume);
    music->setVolume(settings->musicVolume);
}

bool Game::init() {
    ctx.batch = &batch_;
    ctx.fonts = &fonts_;
    ctx.tex = &tex_;
    ctx.mixer = &mixer_;
    ctx.music = &music_;
    ctx.sfx = &sfx_;
    ctx.settings = &settings_;
    ctx.library = &library_;
    ctx.images = &images_;
    ctx.dataDir = plat::writableDir();
    settings_.load(ctx.dataDir + "/settings.cfg");
    lz::setUiScale(settings_.uiScale);
    i18n::setLang(i18n::resolve(settings_.language, plat::systemLanguage()));

    // Dossier des maps : /data est inscriptible sur les consoles jailbreakées ;
    // sinon on se rabat sur /download0.
#if defined(__PROSPERO__)
    ctx.songsDir = "/data/rhythm-ps5/songs";
    if (!fsu::makeDirs(ctx.songsDir) || !fsu::isDir(ctx.songsDir)) ctx.songsDir = ctx.dataDir + "/songs";
#else
    const char *env = getenv("RHYTHM_SONGS");
    ctx.songsDir = env && *env ? env : ctx.dataDir + "/songs";
#endif
    fsu::makeDirs(ctx.songsDir);
    LOG("game: maps dans %s", ctx.songsDir.c_str());

    if (!batch_.init()) return false;
    const std::string fonts = std::string(plat::assetsDir()) + "/fonts/";
    // Outfit (OFL) ; Inter complète les symboles qu'Outfit n'a pas (★, ♪, ✕...).
    const std::string fallback = fonts + "Inter-Regular.ttf";
    fonts_.light.load(fonts + "Outfit-Light.ttf", fallback);
    fonts_.regular.load(fonts + "Outfit-Regular.ttf", fallback);
    fonts_.semibold.load(fonts + "Outfit-SemiBold.ttf", fallback);
    fonts_.bold.load(fonts + "Outfit-Bold.ttf", fallback);
    fonts_.icons.load(fonts + "FontAwesome-Solid.ttf", "", true);
    // Caractères japonais (et chinois) : créés à la demande depuis Noto Sans JP.
    if (cjk_.load(fonts + "NotoSansJP-Medium.ttf", 48, fonts_.regular.ascentPx()))
        for (gfx::Font *f : {&fonts_.light, &fonts_.regular, &fonts_.semibold, &fonts_.bold}) f->setDynamicFallback(&cjk_);
    tex_.disc = gfx::makeHitCircle(256);
    {
        // Disque plein uni (le "hit circle" procédural a un bord blanc).
        std::vector<uint8_t> a(256 * 256);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                const float dx = (static_cast<float>(x) + 0.5f) / 128.0f - 1, dy = (static_cast<float>(y) + 0.5f) / 128.0f - 1;
                const float d = std::sqrt(dx * dx + dy * dy);
                a[static_cast<size_t>(y) * 256 + x] = static_cast<uint8_t>(std::clamp((1 - d) * 128.0f, 0.0f, 1.0f) * 255);
            }
        gfx::destroyTexture(tex_.disc);
        tex_.disc = gfx::createAlphaTexture(256, 256, a.data(), true);
    }
    tex_.ring = gfx::makeRing(256, 0.035f);
    tex_.ringThick = gfx::makeRing(256, 0.12f);
    tex_.glow = gfx::makeSoftDot(128);
    tex_.arrow = makeArrow(128);
    tex_.triangle = makeTriangle(128);
    tex_.spinner = makeSpinnerDisc(512);
    tex_.circleTriangles = makeCircleTriangles(256);
    LOG("game: ressources graphiques pretes (gl 0x%x)", glGetError());

    sfx_.generate();
    // Sons personnels (par ex. ceux d'un skin) déposés dans le dossier « sons », à
    // côté du dossier des maps (/data/rhythm-ps5/sons sur la console).
    {
        const size_t slash = ctx.songsDir.find_last_of('/');
        const std::string base = slash == std::string::npos ? std::string(".") : ctx.songsDir.substr(0, slash);
        soundsDir_ = base + "/sons";
        skinsDir_ = base + "/skins";
        fsu::makeDirs(soundsDir_);
        sfx_.loadOverrides(soundsDir_);
        // Skins osu! déposés dans « skins » (dossiers ou archives .osk).
        skinNames = game::scanSkins(skinsDir_);
        applySkin();
    }
    mixer_.setMusic(&music_);
    audioOk_ = plat::audioStart(audio::Mixer::callback, &mixer_, 256);
    ctx.applyVolumes();

    library_.setRoot(ctx.songsDir);
    library_.beginScan();
    images_.start(3, ctx.dataDir + "/cache");
    mirror.start(settings_.mirror, ctx.songsDir);
    loadScores();
    {
        // Piste du lancement : « triangles » (cYsmix), celle de l'intro de lazer.
        // Elle n'est pas fournie avec le jeu (licence CC BY-NC des ressources
        // osu!) : on la récupère une fois depuis le dépôt public ppy/osu-resources.
        const size_t slash = ctx.songsDir.find_last_of('/');
        const std::string base = slash == std::string::npos ? std::string(".") : ctx.songsDir.substr(0, slash);
        const std::string dir = base + "/intro";
        fsu::makeDirs(dir);
        introTrackPath_ = dir + "/triangles.mp3";
        if (fsu::exists(introTrackPath_)) {
            introTrackState_.store(2);
        } else {
            introTrackState_.store(1);
            introTrackThreadStarted_ = thr::start(introTrackThread_, introTrackMain, this, "intro");
            if (introTrackThreadStarted_) pthread_detach(introTrackThread_);
            else introTrackState_.store(3);
        }
    }
    goTo(makeIntro());
    return true;
}

void Game::applySkin() {
    skin_.unload();
    ctx.skin = nullptr;
    sfx_.generate();
    sfx_.loadOverrides(soundsDir_);
    if (settings_.skin == 2 && !settings_.skinName.empty()) {
        const std::string dir = skinsDir_ + "/" + settings_.skinName;
        if (skin_.load(dir, settings_.skinName)) {
            ctx.skin = &skin_;
            sfx_.loadOverrides(dir);
        } else {
            toast(i18n::tr("Skin illisible : ") + settings_.skinName);
        }
    }
}

void Game::shutdown() {
    if (screen_) screen_->leave();
    screen_.reset();
    leaving_.clear();
    overlay_.reset();
    overlayLeaving_.reset();
    mixer_.setMusic(nullptr);
    plat::audioStop();
    music_.unload();
    mirror.stop();
    images_.stop();
    ctx.saveSettings();
    for (gfx::Texture *t : {&tex_.disc, &tex_.ring, &tex_.ringThick, &tex_.glow, &tex_.arrow, &tex_.triangle, &tex_.spinner, &tex_.circleTriangles,
                            &bgCurrent_, &bgPrevious_, &bgBlurCurrent_, &bgBlurPrevious_})
        gfx::destroyTexture(*t);
    for (auto &kv : thumbs_) gfx::destroyTexture(kv.second.tex);
    thumbs_.clear();
    cjk_.shutdown();
    fonts_.light.shutdown();
    fonts_.icons.shutdown();
    fonts_.regular.shutdown();
    fonts_.semibold.shutdown();
    fonts_.bold.shutdown();
    batch_.shutdown();
}

void Game::goTo(std::unique_ptr<Screen> next, bool forward) {
    if (screen_) {
        screen_->forward = forward;
        const double dur = screen_->leave();
        // Gardé au moins jusqu'à l'image suivante : goTo peut être appelé depuis
        // l'update de l'écran lui-même.
        leaving_.push_back({std::move(screen_), ctx.now * 1000 + std::max(dur, 1.0), !forward});
    }
    screen_ = std::move(next);
    screen_->forward = forward;
    screen_->enter();
}

void Game::openOverlay(std::unique_ptr<Screen> overlay) {
    if (overlay_) return;
    overlay_ = std::move(overlay);
    overlay_->forward = true;
    overlay_->enter();
}

void Game::closeOverlay() {
    if (!overlay_) return;
    overlay_->forward = false;
    overlayLeaveUntil_ = ctx.now * 1000 + std::max(1.0, overlay_->leave());
    overlayLeaving_ = std::move(overlay_);
}

// Téléchargements terminés : ajout à la bibliothèque (thread principal), quel que
// soit l'écran affiché.
void Game::handleDownloads() {
    for (const auto &d : mirror.downloads()) {
        const int phase = d->phase.load();
        if (phase < 3) continue;
        if (std::find(handledDownloads_.begin(), handledDownloads_.end(), d.get()) != handledDownloads_.end()) continue;
        handledDownloads_.push_back(d.get());
        if (phase == 3) {
            if (library_.addFolder(d->folder)) {
                if (!d->quiet) {
                    ctx.play(sfx_.uiDownloaded);
                    toast(i18n::tr("Installée : ") + d->label);
                }
                const int idx = library_.findSet(d->folder);
                if (idx >= 0 && selectedSet < 0) select(idx, 0, true);
            }
        } else if (!d->quiet) {
            ctx.play(sfx_.uiError);
            toast(i18n::tr("Échec : ") + d->error);
        }
    }
}

void Game::handleToolbar(const UiInput &in) {
    switch (toolbar.update(ctx, in)) {
    case lz::Toolbar::Settings:
        if (!overlay_) openOverlay(makeOptions());
        break;
    case lz::Toolbar::Download:
        if (!overlay_) openOverlay(makeDownload());
        break;
    case lz::Toolbar::Home:
        if (overlay_) closeOverlay();
        goTo(makeMainMenu(), false);
        break;
    default: break;
    }
}

const game::SetInfo *Game::currentSet() const {
    const auto &sets = library_.sets();
    if (selectedSet < 0 || selectedSet >= static_cast<int>(sets.size())) return nullptr;
    return &sets[static_cast<size_t>(selectedSet)];
}

const game::DiffInfo *Game::currentDiff() const {
    const game::SetInfo *s = currentSet();
    if (!s || s->diffs.empty()) return nullptr;
    return &s->diffs[static_cast<size_t>(std::clamp(selectedDiff, 0, static_cast<int>(s->diffs.size()) - 1))];
}

void Game::select(int set, int diff, bool preview) {
    const bool setChanged = set != selectedSet;
    selectedSet = set;
    selectedDiff = diff;
    const game::SetInfo *s = currentSet();
    const game::DiffInfo *d = currentDiff();
    if (!s || !d) return;
    settings_.lastSetFolder = s->folder;
    settings_.lastDifficulty = diff;
    const std::string bg = d->background.empty() ? "" : s->path(ctx.songsDir) + "/" + d->background;
    if (bg != bgPath_) {
        bgPath_ = bg;
        // Version nette (fond de menu, sélection) et version floutée (chargement, résultats).
        bgTicket_ = bg.empty() ? 0 : images_.request(bg, 1920, 0);
        bgBlurTicket_ = bg.empty() ? 0 : images_.request(bg, 480, 3);
        if (bg.empty()) {
            gfx::destroyTexture(bgPrevious_);
            gfx::destroyTexture(bgBlurPrevious_);
            bgPrevious_ = bgCurrent_;
            bgBlurPrevious_ = bgBlurCurrent_;
            bgCurrent_ = bgBlurCurrent_ = gfx::Texture{};
            bgFadeStart_ = bgBlurFadeStart_ = ctx.now;
        }
    }
    if (preview && (setChanged || previewAudio_ != s->path(ctx.songsDir) + "/" + d->audioFile)) playPreview();
}

void *Game::introTrackMain(void *self) {
    Game &g = *static_cast<Game *>(self);
    const std::string osz = g.introTrackPath_ + ".osz.part";
    const net::Response r =
        net::get("https://raw.githubusercontent.com/ppy/osu-resources/master/osu.Game.Resources/Tracks/triangles.osz",
                 16u << 20);
    bool ok = r.ok() && fsu::writeFileAtomic(osz, r.body.data(), r.body.size());
    if (ok) {
        const std::string tmpDir = g.introTrackPath_ + ".d";
        std::string error;
        ok = net::extractOsz(osz, tmpDir, error) && rename((tmpDir + "/audio.mp3").c_str(), g.introTrackPath_.c_str()) == 0;
        fsu::removeFile(tmpDir + "/audio.mp3");
        std::vector<fsu::Entry> left;
        if (fsu::listDir(tmpDir, left))
            for (const fsu::Entry &e : left) fsu::removeFile(tmpDir + "/" + e.name);
        rmdir(tmpDir.c_str());
        if (!ok) LOG("intro: extraction impossible (%s)", error.c_str());
    } else {
        LOG("intro: telechargement de la piste impossible (HTTP %d %s)", r.status, r.error.c_str());
    }
    fsu::removeFile(osz);
    g.introTrackState_.store(ok ? 2 : 3);
    if (ok) LOG("intro: piste triangles installee");
    return nullptr;
}

bool Game::playIntroTrack() {
    if (introTrackState_.load() != 2) return false;
    if (!music_.load(introTrackPath_)) {
        introTrackState_.store(3);
        return false;
    }
    previewAudio_ = introTrackPath_;
    music_.setLoop(false, 0);
    music_.setVolume(settings_.musicVolume);
    music_.play(0);
    introTrackPlaying_ = true;
    return true;
}

void Game::playPreview() {
    const game::SetInfo *s = currentSet();
    const game::DiffInfo *d = currentDiff();
    if (!s || !d) return;
    const std::string path = s->path(ctx.songsDir) + "/" + d->audioFile;
    if (path != previewAudio_ || !music_.loaded()) {
        if (!music_.load(path)) {
            previewAudio_.clear();
            return;
        }
        previewAudio_ = path;
    }
    const double start = d->previewTime >= 0 ? d->previewTime / 1000.0 : d->lengthMs * 0.4 / 1000.0;
    music_.setLoop(true, start);
    music_.setVolume(settings_.musicVolume);
    music_.play(start);
}

const gfx::Texture *Game::thumbnail(int setIndex) {
    const auto &sets = library_.sets();
    if (setIndex < 0 || setIndex >= static_cast<int>(sets.size())) return nullptr;
    const game::SetInfo &s = sets[static_cast<size_t>(setIndex)];
    Thumb &t = thumbs_[s.folder];
    t.lastUse = ctx.now;
    if (t.tex.id) return &t.tex;
    if (!t.ticket && !t.failed && !s.diffs.empty() && !s.diffs[0].background.empty()) {
        t.ticket = images_.request(s.path(ctx.songsDir) + "/" + s.diffs[0].background, 960, 0);
        thumbTickets_[t.ticket] = s.folder;
    }
    return nullptr;
}

void Game::updateBackgroundLoads() {
    for (const gfx::ImageLoader::Done &done : images_.poll(3)) {
        auto th = thumbTickets_.find(done.ticket);
        if (th != thumbTickets_.end()) {
            Thumb &t = thumbs_[th->second];
            t.ticket = 0;
            if (done.ok) t.tex = done.texture;
            else t.failed = false;  // abandonnée (défilement rapide) : on redemandera
            thumbTickets_.erase(th);
            continue;
        }
        if (done.ticket == bgBlurTicket_) {
            gfx::destroyTexture(bgBlurPrevious_);
            bgBlurPrevious_ = bgBlurCurrent_;
            bgBlurCurrent_ = done.ok ? done.texture : gfx::Texture{};
            bgBlurFadeStart_ = ctx.now;
            continue;
        }
        if (done.ticket != bgTicket_) {
            gfx::Texture t = done.texture;
            gfx::destroyTexture(t);
            continue;
        }
        gfx::destroyTexture(bgPrevious_);
        bgPrevious_ = bgCurrent_;
        bgCurrent_ = done.ok ? done.texture : gfx::Texture{};
        bgFadeStart_ = ctx.now;
    }
}

void Game::setBackground(float dim, float blur, double dur, anim::Ease e) {
    const double t = ctx.now * 1000;
    if (bgDim_.target() != dim) bgDim_.go(t, dim, dur, e);
    if (bgBlur_.target() != blur) bgBlur_.go(t, blur, dur, e);
}

void Game::drawBackground() {
    gfx::SpriteBatch &b = batch_;
    const double t = ctx.now * 1000;
    // Fond par défaut (aucune map) : simple dégradé sombre.
    b.roundRectGradient(0, 0, kVW, kVH, 0, gfx::Corners::vertical(lz::hex(0x2a1f3d), lz::hex(0x120e1c)));
    if (!backgroundVisible) return;
    // Parallaxe légère suivant la souris (comme lazer).
    const UiInput &in = lastInput_;
    const float px = in.mouseActive ? (in.mouseX / kVW - 0.5f) * 2 : 0, py = in.mouseActive ? (in.mouseY / kVH - 0.5f) * 2 : 0;
    const float amount = settings_.menuParallax ? 0.02f * backgroundParallax : 0;
    auto draw = [&](const gfx::Texture &tex, float alpha) {
        if (!tex.id || alpha <= 0.001f) return;
        const float bw = static_cast<float>(tex.width), bh = static_cast<float>(tex.height);
        const float sc = std::fmax(kVW / bw, kVH / bh) * (1 + amount);
        b.quad(tex, (kVW - bw * sc) / 2 - px * kVW * amount / 2, (kVH - bh * sc) / 2 - py * kVH * amount / 2, bw * sc, bh * sc,
               gfx::withAlpha(gfx::rgba(255, 255, 255), alpha));
    };
    const float k = anim::clamp01(static_cast<float>((ctx.now - bgFadeStart_) / 0.8));
    const float kb = anim::clamp01(static_cast<float>((ctx.now - bgBlurFadeStart_) / 0.8));
    const float blur = bgBlur_.at(t);
    const bool haveSharp = bgCurrent_.id || bgPrevious_.id;
    // Version nette en dessous, version floutée par-dessus selon le flou demandé.
    if (blur < 1 || !bgBlurCurrent_.id) {
        draw(bgPrevious_, 1 - k);
        draw(bgCurrent_, bgPrevious_.id ? 1.0f : k);
    }
    if (blur > 0 || !haveSharp) {
        const float a = haveSharp ? blur : 1;
        draw(bgBlurPrevious_, (1 - kb) * a);
        draw(bgBlurCurrent_, (bgBlurPrevious_.id ? 1.0f : kb) * a);
    }
    b.rect(0, 0, kVW, kVH, gfx::rgba(0, 0, 0, static_cast<uint8_t>(std::clamp(bgDim_.at(t), 0.0f, 1.0f) * 255)));
}

namespace {
// Clavier virtuel du jeu : toujours sur PS5 ; sur ordinateur avec RHYTHM_OSK=1 (tests).
bool useOsk() {
#if defined(__PROSPERO__)
    return true;
#else
    const char *env = getenv("RHYTHM_OSK");
    return env && *env == '1';
#endif
}
}  // namespace

bool Game::textInputSupported() const { return useOsk() || plat::textInputSupported(); }

bool Game::startTextInput(const char *title, const std::string &initial) {
    if (!useOsk()) return plat::startTextInput(title, initial);
    osk_.open(ctx, i18n::tr(title), initial);
    return true;
}

int Game::pollTextInput(std::string &out) { return useOsk() ? osk_.poll(out) : plat::pollTextInput(out); }

void Game::toast(const std::string &text) {
    lastToast = text;
    toastTime = ctx.now;
}

std::string Game::diffKey(const game::SetInfo &set, const game::DiffInfo &diff) { return set.folder + "/" + diff.file; }

std::vector<LocalScore> Game::scoresFor(const std::string &key) const {
    std::vector<LocalScore> out;
    for (const LocalScore &s : scores_)
        if (s.key == key) out.push_back(s);
    std::sort(out.begin(), out.end(), [](const LocalScore &a, const LocalScore &b) { return a.score > b.score; });
    return out;
}

void Game::addScore(const LocalScore &s) {
    scores_.push_back(s);
    saveScores();
}

void Game::loadScores() {
    std::string text;
    if (!fsu::readText(ctx.dataDir + "/scores.txt", text)) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        std::vector<std::string> f;
        size_t s = 0;
        for (size_t i = 0; i <= line.size(); ++i)
            if (i == line.size() || line[i] == '\t') {
                f.push_back(line.substr(s, i - s));
                s = i + 1;
            }
        if (f.size() < 11) continue;
        LocalScore sc;
        sc.key = f[0];
        sc.score = atoll(f[1].c_str());
        sc.accuracy = atof(f[2].c_str());
        sc.maxCombo = atoi(f[3].c_str());
        sc.n300 = atoi(f[4].c_str());
        sc.n100 = atoi(f[5].c_str());
        sc.n50 = atoi(f[6].c_str());
        sc.nMiss = atoi(f[7].c_str());
        sc.rank = f[8];
        sc.mods = f[9];
        sc.date = atoll(f[10].c_str());
        scores_.push_back(sc);
    }
    LOG("game: %zu scores locaux", scores_.size());
}

void Game::saveScores() const {
    std::string text;
    char buf[256];
    for (const LocalScore &s : scores_) {
        snprintf(buf, sizeof(buf), "\t%lld\t%.6f\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%lld\n", s.score, s.accuracy, s.maxCombo, s.n300,
                 s.n100, s.n50, s.nMiss, s.rank.c_str(), s.mods.c_str(), s.date);
        text += s.key + buf;
    }
    fsu::writeFileAtomic(ctx.dataDir + "/scores.txt", text.data(), text.size());
}

UiInput Game::buildInput(const plat::InputState &raw) {
    UiInput in;
    in.raw = &raw;
    const plat::PadState &p = raw.pad, &pp = prev_.pad;
    auto padPressed = [&](uint32_t bit) { return (p.buttons & bit) && !(pp.buttons & bit); };
    auto keyPressed = [&](int k) { return raw.keyboard.keys[k] && !prev_.keyboard.keys[k]; };

    // Navigation avec répétition automatique (croix directionnelle, stick ou flèches).
    int dir = 0;
    if ((p.buttons & plat::kPadUp) || raw.keyboard.keys[kKeyUp] || p.ly < -0.6f) dir = 1;
    else if ((p.buttons & plat::kPadDown) || raw.keyboard.keys[kKeyDown] || p.ly > 0.6f) dir = 2;
    else if ((p.buttons & plat::kPadLeft) || raw.keyboard.keys[kKeyLeft] || p.lx < -0.6f) dir = 3;
    else if ((p.buttons & plat::kPadRight) || raw.keyboard.keys[kKeyRight] || p.lx > 0.6f) dir = 4;
    bool fire = false;
    if (dir != repeatDir_) {
        fire = dir != 0;
        repeatNext_ = ctx.now + 0.32;
    } else if (dir != 0 && ctx.now >= repeatNext_) {
        fire = true;
        repeatNext_ = ctx.now + 0.065;
    }
    repeatDir_ = dir;
    if (fire) {
        in.up = dir == 1;
        in.down = dir == 2;
        in.left = dir == 3;
        in.right = dir == 4;
    }
    in.confirm = padPressed(plat::kPadCross) || keyPressed(kKeyEnter);
    // En jeu, Rond est une touche de frappe : seule Échap (ou Options) met en pause.
    in.back = (padPressed(plat::kPadCircle) && !(screen_ && screen_->gameplay())) || keyPressed(kKeyEsc);
    // Raccourcis clavier façon lazer : F1 mods, F2 hasard, F3 options, Espace passer.
    in.options = padPressed(plat::kPadOptions) || keyPressed(kKeyF3);
    in.extra = padPressed(plat::kPadTriangle) || keyPressed(kKeyF2) || keyPressed(kKeySpace);
    in.extra2 = padPressed(plat::kPadSquare) || keyPressed(kKeyF1);
    in.touchpadClick = padPressed(plat::kPadTouchpad);
    in.mouseRightClick = (raw.mouse.buttons & 2u) && !(prev_.mouse.buttons & 2u);
    in.wheel = raw.mouse.wheel;
    in.l1 = padPressed(plat::kPadL1) || keyPressed(kKeyPgUp);
    in.r1 = padPressed(plat::kPadR1) || keyPressed(kKeyPgDn);
    in.l2 = padPressed(plat::kPadL2) || keyPressed(kKeyHome);
    in.r2 = padPressed(plat::kPadR2) || keyPressed(kKeyEnd);

    // Frappes (jeu).
    // Boutons et touches de frappe : réglables dans les options.
    const uint32_t hitMask = settings_.padHitMask ? settings_.padHitMask : Settings::defaultPadHitMask();
    const int k1 = std::clamp(settings_.keyHit1, 4, 231), k2 = std::clamp(settings_.keyHit2, 4, 231);
    const uint32_t mouseMask = settings_.mouseButtonsInGame ? 3u : 0u;
    for (uint32_t b = p.buttons & ~pp.buttons & hitMask; b; b &= b - 1) ++in.hitPresses;
    in.hitPresses += keyPressed(k1) + (k2 != k1 ? keyPressed(k2) : 0);
    in.hitPresses += ((raw.mouse.buttons & 1u & mouseMask) && !(prev_.mouse.buttons & 1u)) +
                     ((raw.mouse.buttons & 2u & mouseMask) && !(prev_.mouse.buttons & 2u));
    in.hitHeld = (p.buttons & hitMask) || raw.keyboard.keys[k1] || raw.keyboard.keys[k2] || (raw.mouse.buttons & mouseMask);
    in.mouseClick = (raw.mouse.buttons & 1u) && !(prev_.mouse.buttons & 1u);

    // Curseur : souris (absolu), pavé tactile (absolu ou relatif), sticks (relatif).
    const float dt = ctx.dt;
    if (raw.mouse.connected && (raw.mouse.x != lastMouseX_ || raw.mouse.y != lastMouseY_)) {
        if (lastMouseX_ >= 0) {
            cursorX_ = raw.mouse.x * kVW / static_cast<float>(std::max(1, plat::width()));
            cursorY_ = raw.mouse.y * kVH / static_cast<float>(std::max(1, plat::height()));
            pointerX_ = cursorX_;
            pointerY_ = cursorY_;
            in.mouseMoved = true;
            mouseActive_ = true;
            padPointer_ = false;
        }
        lastMouseX_ = raw.mouse.x;
        lastMouseY_ = raw.mouse.y;
    }
    if (p.touchCount > 0) {
        const float tx = p.touchX[0], ty = p.touchY[0];
        if (settings_.touchpadAbsolute) {
            // Tout le pavé = l'aire de jeu agrandie de 10 % (pas de lissage : réactivité maximale).
            const float fw = 512 * kVH / 480 * 1.1f, fh = 384 * kVH / 480 * 1.1f;
            cursorX_ = kVW / 2 - fw / 2 + tx / 1920.0f * fw;
            cursorY_ = kVH / 2 + 8 * kVH / 480 - fh / 2 + ty / 1080.0f * fh;
        } else if (touchWasDown_) {
            cursorX_ += (tx - touchLastX_) * 1.6f;
            cursorY_ += (ty - touchLastY_) * 1.6f;
        }
        touchLastX_ = tx;
        touchLastY_ = ty;
        touchWasDown_ = true;
    } else {
        touchWasDown_ = false;
        const float c = settings_.cursorCurve;
        const float vx = deadzone(p.lx, 0.12f, c) + 0.3f * deadzone(p.rx, 0.12f, c);
        const float vy = deadzone(p.ly, 0.12f, c) + 0.3f * deadzone(p.ry, 0.12f, c);
        cursorX_ += vx * settings_.cursorSpeed * dt;
        cursorY_ += vy * settings_.cursorSpeed * dt;
    }
    cursorX_ = std::clamp(cursorX_, 0.0f, kVW);
    cursorY_ = std::clamp(cursorY_, 0.0f, kVH);
    in.cursorX = cursorX_;
    in.cursorY = cursorY_;
    const float mag = std::sqrt(p.lx * p.lx + p.ly * p.ly);
    in.stickActive = mag > 0.6f;
    in.stickAngle = std::atan2(p.ly, p.lx);
    // Hors partie, le stick droit déplace un pointeur (comme une souris) : tout ce
    // qui se clique (mods, au hasard, options, logo, onglets, recherche...) devient
    // accessible ; Croix clique tant que le pointeur est affiché.
    const bool menus = !(screen_ && screen_->gameplay()) || overlay_;
    if (menus) {
        const float px = deadzone(p.rx, 0.15f, 1.6f), py = deadzone(p.ry, 0.15f, 1.6f);
        if (px != 0 || py != 0) {
            if (!mouseActive_) {
                // Réapparaît là où il était (ou au centre au premier usage).
                if (pointerX_ < 0) pointerX_ = kVW / 2, pointerY_ = kVH / 2;
            }
            pointerX_ = std::clamp(pointerX_ + px * 1500 * lz::U * dt, 0.0f, kVW);
            pointerY_ = std::clamp(pointerY_ + py * 1500 * lz::U * dt, 0.0f, kVH);
            mouseActive_ = true;
            padPointer_ = true;
            in.mouseMoved = true;
        }
        // Le pavé tactile glisse aussi le pointeur (déplacement relatif, comme un pavé
        // de portable) ; un appui sur le pavé clique.
        if (p.touchCount > 0) {
            const float tx = static_cast<float>(p.touchX[0]), ty = static_cast<float>(p.touchY[0]);
            if (ptrTouchDown_ && (tx != ptrTouchX_ || ty != ptrTouchY_)) {
                if (pointerX_ < 0) pointerX_ = kVW / 2, pointerY_ = kVH / 2;
                const float k = kVW / 1920.0f * 1.4f;
                pointerX_ = std::clamp(pointerX_ + (tx - ptrTouchX_) * k, 0.0f, kVW);
                pointerY_ = std::clamp(pointerY_ + (ty - ptrTouchY_) * k, 0.0f, kVH);
                mouseActive_ = true;
                padPointer_ = true;
                in.mouseMoved = true;
            }
            ptrTouchX_ = tx;
            ptrTouchY_ = ty;
            ptrTouchDown_ = true;
        } else {
            ptrTouchDown_ = false;
        }
        if (padPointer_ && mouseActive_ && (padPressed(plat::kPadCross) || in.touchpadClick)) {
            in.mouseClick = true;
            in.confirm = false;
            in.touchpadClick = false;
        }
    } else {
        padPointer_ = false;
    }
    const uint32_t newButtons = p.buttons & ~pp.buttons & ~(padPointer_ ? plat::kPadCross | plat::kPadTouchpad : 0u);
    in.anyKey = newButtons != 0;
    for (int k = 4; k < 232 && !in.anyKey; ++k) in.anyKey = keyPressed(k);
    // Pointeur : actif dès que la souris (ou le stick droit) bouge ou clique,
    // masqué dès qu'on navigue à la croix, au stick gauche ou aux flèches.
    if (in.mouseClick || in.wheel != 0) mouseActive_ = true;
    if (dir != 0 || in.anyKey) mouseActive_ = false, padPointer_ = false;
    if (pointerX_ < 0) pointerX_ = kVW / 2, pointerY_ = kVH / 2;
    in.mouseX = pointerX_;
    in.mouseY = pointerY_;
    in.mouseActive = mouseActive_ && (raw.mouse.connected || padPointer_);
    if (in.mouseClick && (raw.mouse.connected || padPointer_)) mouseActive_ = true, in.mouseActive = true;
    prev_ = raw;
    return in;
}

void Game::reportSwap(double swapMs) {
    perfSwap_ += swapMs;
    perfSwapMax_ = std::max(perfSwapMax_, swapMs);
}

void Game::frame(const plat::InputState &raw, double now) {
    const double frameStart = plat::nowSeconds();
    ctx.dt = lastFrame_ < 0 ? 1.0f / 60.0f : static_cast<float>(std::min(0.1, now - lastFrame_));
    lastFrame_ = now;
    ctx.now = now;
    fpsAccum_ += ctx.dt;
    if (++fpsFrames_ >= 30) {
        fps_ = static_cast<float>(fpsFrames_ / fpsAccum_);
        fpsAccum_ = 0;
        fpsFrames_ = 0;
    }
    if (raw.quitRequested) exit_ = true;

    // Piste du lancement finie (ou remplacée) : on enchaîne sur la map choisie.
    if (introTrackPlaying_ && music_.path() != introTrackPath_) introTrackPlaying_ = false;
    if (introTrackPlaying_ && music_.ended()) {
        introTrackPlaying_ = false;
        previewAudio_.clear();
        if (!screen_ || !screen_->gameplay()) playPreview();
    }

    // Battement de la musique en cours (BPM et origine de la difficulté choisie ;
    // « triangles » : 160 BPM, premier temps à 3011,74 ms).
    const game::DiffInfo *beatDiff = introTrackPlaying_ ? nullptr : currentDiff();
    const double bpm = introTrackPlaying_ ? 160.0 : beatDiff ? beatDiff->bpm : 0;
    if (bpm > 1 && music_.playing()) {
        ctx.beatLength = 60000.0 / bpm;
        const double t = music_.time(mixer_) * 1000.0 - (introTrackPlaying_ ? 3011.74 : beatDiff->beatOffset);
        const double beats = t / ctx.beatLength;
        const long idx = static_cast<long>(std::floor(beats));
        ctx.beatProgress = static_cast<float>(beats - std::floor(beats));
        if (idx != ctx.beatIndex) ctx.beatIndex = idx;
        ctx.beatIndexEarly = static_cast<long>(std::floor((t + 60.0) / ctx.beatLength));
        ctx.beatPulse = std::pow(1.0f - ctx.beatProgress, 3.0f);
    } else {
        ctx.beatPulse = anim::approach(ctx.beatPulse, 0, ctx.dt, 0.1f);
    }

    ctx.trackRunning = music_.playing();
    ctx.amplitude = ctx.trackRunning ? std::min(1.0f, music_.level() * 2.5f) : 0;

    // Balayage de la bibliothèque, quelques dossiers par image.
    if (library_.scanning() && library_.scanStep(6)) {
        const int set = library_.findSet(settings_.lastSetFolder);
        if (set >= 0)
            select(set, settings_.lastDifficulty, false);
        else if (!library_.sets().empty())
            select(static_cast<int>(rand() % library_.sets().size()), 0, false);
    }
    updateBackgroundLoads();
    handleDownloads();
    // Vignettes inutilisées depuis 20 s : libérées (mémoire GPU).
    for (auto it = thumbs_.begin(); it != thumbs_.end();) {
        if (it->second.tex.id && now - it->second.lastUse > 20) {
            gfx::destroyTexture(it->second.tex);
            it = thumbs_.erase(it);
        } else {
            ++it;
        }
    }

    const UiInput in = buildInput(raw);
    lastInput_ = in;

    // Écrans qui finissent leur animation de sortie.
    const double nowMs = now * 1000;
    leaving_.erase(std::remove_if(leaving_.begin(), leaving_.end(), [&](const Leaving &l) { return nowMs >= l.until; }),
                   leaving_.end());
    if (overlayLeaving_ && nowMs >= overlayLeaveUntil_) overlayLeaving_.reset();
    logo.update(ctx, in);
    if (osk_.isOpen()) {
        // Clavier virtuel ouvert : il prend les entrées ; l'écran ne fait que l'attendre.
        osk_.update(ctx, in);
        const UiInput idle{};
        if (overlay_) overlay_->update(idle);
        else if (screen_) screen_->update(idle);
    } else if (overlay_) {
        overlay_->update(in);
    } else {
        if (screen_ && !screen_->gameplay()) handleToolbar(in);
        if (screen_) screen_->update(in);
    }

    // Rendu dans la cible MSAA (recréée si la taille change), résolue à la fin.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const double renderStart = plat::nowSeconds();
    perfUpdate_ += (renderStart - frameStart) * 1000;
    batch_.begin(kVW, kVH, plat::width(), plat::height());
    drawBackground();
    for (Leaving &l : leaving_)
        if (!l.above) l.screen->render();
    if (screen_) screen_->render();
    for (Leaving &l : leaving_)
        if (l.above) l.screen->render();
    logo.draw(ctx);
    if (overlayLeaving_) overlayLeaving_->render();
    if (overlay_) overlay_->render();
    toolbar.draw(ctx);
    osk_.draw(ctx);
    // Notification temporaire.
    const float tk = static_cast<float>(now - toastTime);
    if (tk < 3.0f && !lastToast.empty()) {
        const float a = std::min(1.0f, std::min(tk * 6, (3.0f - tk) * 3));
        const float w = fonts_.semibold.measure(28, lastToast.c_str()) + 60;
        const float x = kVW - w - 20 + (1 - anim::outQuint(std::min(1.0f, tk * 3))) * (w + 40);
        batch_.shadow(x, 80, w, 64, 10, 12, gfx::rgba(0, 0, 0, static_cast<uint8_t>(90 * a)));
        batch_.roundRect(x, 80, w, 64, 10, gfx::withAlpha(lz::hex(0x22282a), a * 0.97f));
        batch_.roundRect(x, 80, 8, 64, 4, gfx::withAlpha(lz::col::pink1, a));
        fonts_.semibold.draw(batch_, x + 32, 97, 28, gfx::withAlpha(lz::col::white, a), lastToast.c_str());
    }
    if (screen_ && (!screen_->gameplay() || overlay_) && in.mouseActive) lz::cursor(ctx, in.mouseX, in.mouseY, (raw.mouse.buttons & 1u) != 0);
    if (settings_.showDebug) {
        char line[160];
        const gfx::BatchStats &bs = batch_.lastFrame();
        snprintf(line, sizeof(line), "%.0f FPS  %d appels  %d quads  %s:%u%s", fps_, bs.drawCalls, bs.quads, netlog::localAddress(),
                 netlog::port(), netlog::clientConnected() ? " (PC)" : "");
        fonts_.regular.drawShadow(batch_, 12, kVH - 30, 20, gfx::rgba(200, 255, 200), line);
    }
    const double submitStart = plat::nowSeconds();
    batch_.end();
    const double frameEnd = plat::nowSeconds();
    perfBuild_ += (submitStart - renderStart) * 1000;
    perfSubmit_ += (frameEnd - submitStart) * 1000;
    perfFrameMax_ = std::max(perfFrameMax_, (frameEnd - frameStart) * 1000);
    ++perfFrames_;
    // Toutes les 3 s, le détail du temps passé par image (dans le log réseau).
    if (now - perfLast_ >= 3.0 && perfFrames_ > 0) {
        const double n = perfFrames_;
        const gfx::BatchStats &bs = batch_.lastFrame();
        LOG("perf: %.1f img/s | maj %.2f ms, construction %.2f ms, envoi GPU %.2f ms, swap %.2f ms (max image %.1f, max swap %.1f) | "
            "%d appels, %d envois, %d quads, %d Hz",
            n / (now - perfLast_), perfUpdate_ / n, perfBuild_ / n, perfSubmit_ / n, perfSwap_ / n, perfFrameMax_, perfSwapMax_,
            bs.drawCalls, bs.flushes, bs.quads, plat::refreshHz());
        perfLast_ = now;
        perfFrames_ = 0;
        perfUpdate_ = perfBuild_ = perfSubmit_ = perfSwap_ = perfFrameMax_ = perfSwapMax_ = 0;
    }
}

# rhythm-ps5

A rhythm game for **jailbroken PS5** (homebrew) that plays osu!standard beatmaps, with a
controller-first interface inspired by osu!lazer.

> **Unofficial project**, not affiliated with ppy or osu!. It does not use the osu! name or
> logo as its brand, ships no official assets and **never sends anything** to the official
> servers: scores stay on your console.
>
> Free project, built **with the help of an AI** (Claude) and tested by hand on a real console.

**Alpha version**: expect bugs.

## Features

- osu!standard gameplay: circles, sliders, spinners, score, accuracy, health.
- Mods: Easy, No Fail, Half Time, Hard Rock, Double Time, Nightcore, Hidden, Auto.
  DT / HT keep the original pitch.
- lazer-style interface: intro, main menu, song select, mods, pause, results, options.
- In-game beatmap downloads (community mirror osu.direct), search with the console's
  on-screen keyboard, original-language metadata (Japanese...).
- Storyboards.
- osu! skins (.osk) and two built-in styles (clean, triangles).
- Controller (sticks, touchpad as a pointer in menus), USB keyboard and mouse; remappable keys.
- English and French (follows the console language).
- Star rating and pp computed locally (osu!lazer algorithms), live pp counter.
- 60 / 120 Hz on PS5; V-Sync or frame limiter on desktop. Local scores, automatic audio offset correction.

## Installation (PS5)

1. Download `PPSA99731.zip` from the [releases](../../releases).
2. Copy the `PPSA99731` folder to the console (over FTP, e.g. into `/data/homebrew/`) and
   register it with a folder-app manager (ShadowMountPlus, etc.).
3. Launch it. On first launch, a setup wizard offers the tutorial and starter beatmaps.

Folders on the console:

| | |
| --- | --- |
| Beatmaps | `/data/rhythm-ps5/songs` (folders or `.osz`) |
| Skins | `/data/rhythm-ps5/skins` (folders or `.osk`) |
| Custom sounds | `/data/rhythm-ps5/sons` |

## Installation (Windows / macOS)

- **Windows (x64):** run `rhythm-ps5-setup.exe` from the [releases](../../releases). It installs
  for your user (no admin rights), adds shortcuts, and later updates are offered in-game
  (Options > Misc).
- **macOS (Apple Silicon):** open the `.dmg`, drag rhythm-ps5 to Applications, then right-click >
  Open the first time (the app is not signed by Apple).

On desktop: fullscreen by default (Alt+Enter to toggle), Alt + mouse wheel for volume.

## Controls (controller)

| | |
| --- | --- |
| Cross | select |
| Circle | back |
| D-pad / left stick | navigate |
| Right stick / touchpad | pointer in menus (Cross or touchpad press to click) |
| Square | mods (song select), search (downloads) |
| Triangle | random beatmap |
| L1 / R1 | change page or sort |
| Options | options |

In-game hit keys can be changed in the options.

## Building

```
bash ps5/build.sh                       # PS5 package -> build/ps5/dist/PPSA99731.zip
cmake -S . -B build/pc -G Ninja && ninja -C build/pc && ./build/pc/rhythm-ps5   # PC (SDL2)
```

## License

GPL-3.0-or-later (see `LICENSE`). Built on
[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl),
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (GPL-3.0+) and
[font8x8](https://github.com/dhepper/font8x8) (public domain); fonts
[Outfit](https://github.com/Outfitio/Outfit-Fonts), [Inter](https://rsms.me/inter/) and
[Noto Sans JP](https://fonts.google.com/noto/specimen/Noto+Sans+JP) (OFL); icons
[Font Awesome Free](https://fontawesome.com) (font OFL, icons CC BY 4.0). The interface
reproduces the behaviour of [osu!lazer](https://github.com/ppy/osu) (MIT) from its code, without
using any of its assets. Sony API structures come from the
[ps5-payload-dev SDL fork](https://github.com/ps5-payload-dev/SDL).

Startup music: "triangles" by cYsmix, from [osu-resources](https://github.com/ppy/osu-resources)
(© ppy Pty Ltd, [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/)). It is **not**
included in this repository or in the package: the game downloads it once from that public
repository on first launch and keeps it in `/data/rhythm-ps5/intro`.

---

## Français

Jeu de rythme pour **PS5 jailbreakée** (homebrew) compatible avec les beatmaps osu!standard,
avec une interface pensée pour la manette et inspirée d'osu!lazer.

Projet **non officiel**, sans lien avec ppy ni osu! : ni nom ni logo osu! comme marque, aucun
asset officiel, et rien n'est envoyé aux serveurs officiels (les scores restent sur la console).
Projet gratuit, développé **avec l'aide d'une IA** (Claude) et testé à la main sur console.
Version alpha.

**Installation :** télécharger `PPSA99731.zip` dans les [releases](../../releases), copier le
dossier `PPSA99731` sur la console et l'enregistrer avec un gestionnaire d'applis en dossier
(ShadowMountPlus, etc.). Le jeu est en français si la console l'est, sinon en anglais.

Maps : `/data/rhythm-ps5/songs` · Skins : `/data/rhythm-ps5/skins` · Sons :
`/data/rhythm-ps5/sons`.

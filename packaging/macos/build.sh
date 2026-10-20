#!/usr/bin/env bash
# Version macOS (Apple Silicon) : rhythm-ps5.app dans un .dmg.
# À lancer sur un Mac avec Homebrew (curl, ninja, cmake, pkg-config, dylibbundler).
# SDL2 est compilée en statique depuis ses sources : la « sdl2 » de Homebrew n'est
# qu'une couche de compatibilité qui charge SDL3 au démarrage.
# Sortie : build/macos/rhythm-ps5-macos-arm64.dmg
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out="$root/build/macos"
deps="$root/.deps/macos"
sdl_version=2.30.9
export MACOSX_DEPLOYMENT_TARGET=11.0
mkdir -p "$out" "$deps"
if [[ ! -f $deps/sdl/lib/libSDL2.a ]]; then
    curl -fsSL -o "$deps/sdl.tgz" "https://github.com/libsdl-org/SDL/releases/download/release-$sdl_version/SDL2-$sdl_version.tar.gz"
    tar xzf "$deps/sdl.tgz" -C "$deps"
    cmake -S "$deps/SDL2-$sdl_version" -B "$deps/sdl-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$deps/sdl"
    cmake --build "$deps/sdl-build"
    cmake --install "$deps/sdl-build"
fi
export PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
cmake -S "$root" -B "$out/cmake" -G Ninja -DCMAKE_BUILD_TYPE=Release -DRHYTHM_SDL2_STATIC=ON \
    -DCMAKE_PREFIX_PATH="$deps/sdl"
ninja -k 0 -C "$out/cmake" rhythm-ps5

app="$out/dmg/rhythm-ps5.app"
rm -rf "$out/dmg" "$out/rhythm-ps5-macos-arm64.dmg"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Frameworks"
cp "$out/cmake/rhythm-ps5" "$app/Contents/MacOS/"
cp "$root/packaging/macos/Info.plist" "$app/Contents/"
cp -R "$root/ps5/assets" "$app/Contents/Resources/assets"
# Icône : PNG 512 px -> .icns.
iconset="$out/AppIcon.iconset"
rm -rf "$iconset" && mkdir -p "$iconset"
for s in 16 32 64 128 256 512; do
    sips -z $s $s "$root/ps5/sce_sys/icon0.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null
done
cp "$iconset/icon_32x32.png" "$iconset/icon_16x16@2x.png"
cp "$iconset/icon_64x64.png" "$iconset/icon_32x32@2x.png"
cp "$iconset/icon_256x256.png" "$iconset/icon_128x128@2x.png"
cp "$iconset/icon_512x512.png" "$iconset/icon_256x256@2x.png"
rm "$iconset/icon_64x64.png"
iconutil -c icns "$iconset" -o "$app/Contents/Resources/AppIcon.icns"
# curl et ses dépendances (Homebrew) copiés dans l'app.
dylibbundler -od -b -x "$app/Contents/MacOS/rhythm-ps5" -d "$app/Contents/Frameworks" -p @executable_path/../Frameworks
# Signature ad hoc (obligatoire sur Apple Silicon après modification des bibliothèques).
codesign --force --deep --sign - "$app"
ln -s /Applications "$out/dmg/Applications"
hdiutil create -volname "rhythm-ps5" -srcfolder "$out/dmg" -ov -format UDZO "$out/rhythm-ps5-macos-arm64.dmg"
echo "-> $out/rhythm-ps5-macos-arm64.dmg"

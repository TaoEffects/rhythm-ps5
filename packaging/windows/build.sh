#!/usr/bin/env bash
# Version Windows (x86-64), compilée depuis Linux avec MinGW-w64.
# Prérequis : g++-mingw-w64-x86-64-posix, curl, unzip, zip.
# Sortie : build/windows/rhythm-ps5-windows.zip
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out="$root/build/windows"
deps="$root/.deps/windows"
sdl_version=2.30.9
mkdir -p "$out/obj" "$deps"
cd "$deps"
[[ -d SDL2-$sdl_version ]] || { curl -fsSL -o sdl.tgz "https://github.com/libsdl-org/SDL/releases/download/release-$sdl_version/SDL2-devel-$sdl_version-mingw.tar.gz"; tar xzf sdl.tgz; }
if ! ls -d curl-*-win64-mingw >/dev/null 2>&1; then
    curl -fsSL -o curl.zip "https://curl.se/windows/latest.cgi?p=win64-mingw.zip"
    unzip -q -o curl.zip
fi
sdl="$deps/SDL2-$sdl_version/x86_64-w64-mingw32"
curl_dir=$(ls -d "$deps"/curl-*-win64-mingw | head -1)
cxx=x86_64-w64-mingw32-g++-posix
cc=x86_64-w64-mingw32-gcc-posix
flags=(-O2 -I"$root/src" -I"$root/third_party" -I"$root/third_party/font8x8" -I"$sdl/include/SDL2" -I"$curl_dir/include"
       '-DRHYTHM_ASSETS_DIR="assets"')
cd "$root"
objects=()
for f in $(git ls-files 'src/*.cpp' | grep -v platform_ps5); do
    o="$out/obj/$(echo "$f" | tr / _).o"
    "$cxx" -std=gnu++20 "${flags[@]}" -c "$f" -o "$o"
    objects+=("$o")
done
for f in third_party/font8x8/font8x8.c third_party/impl.c third_party/miniz/*.c; do
    o="$out/obj/$(echo "$f" | tr / _).o"
    "$cc" -O2 -I"$root/third_party" -I"$root/third_party/font8x8" -c "$f" -o "$o"
    objects+=("$o")
done
(cd packaging/windows && x86_64-w64-mingw32-windres rhythm.rc -O coff -o "$out/obj/rhythm.res.o")
objects+=("$out/obj/rhythm.res.o")
"$cxx" -o "$out/rhythm-ps5.exe" "${objects[@]}" -L"$sdl/lib" -L"$curl_dir/lib" -lSDL2 -lcurl -lopengl32 -lshell32 -mwindows \
    -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread -Wl,-Bdynamic
pkg="$out/rhythm-ps5-windows"
rm -rf "$pkg" "$out/rhythm-ps5-windows.zip"
mkdir -p "$pkg"
cp "$out/rhythm-ps5.exe" "$sdl/bin/SDL2.dll" "$curl_dir/bin/libcurl-x64.dll" LICENSE README.md "$pkg/"
cp -r ps5/assets "$pkg/assets"
(cd "$out" && zip -qr rhythm-ps5-windows.zip rhythm-ps5-windows)
echo "-> $out/rhythm-ps5-windows.zip"
# Installateur (un seul .exe) si NSIS est présent.
if command -v makensis >/dev/null; then
    version=$(sed -n 's/^#define RHYTHM_VERSION "\([0-9.]*\).*/\1/p' src/version.h)
    (cd packaging/windows && makensis -V2 -DVERSION="$version" -DSRC="$pkg" -DOUT="$out/rhythm-ps5-setup.exe" installer.nsi)
    echo "-> $out/rhythm-ps5-setup.exe"
fi

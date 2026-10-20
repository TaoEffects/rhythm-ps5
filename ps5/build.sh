#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build PS5 : produit un dossier d'application native (titre installé), comme
# l'exige ps5-opengl (une appli OpenGL ne se lance PAS via un chargeur d'ELF).
#
#   bash ps5/build.sh            # la fréquence (60/120 Hz) se règle dans les options du jeu
#
# Résultat : build/ps5/dist/PPSA99731/ (à copier dans /data/homebrew/ sur la PS5)
#            build/ps5/dist/PPSA99731.zip
#            build/ps5/dist/rhythm-ps5-symbols.elf (pour décoder un plantage)
#
# Toutes les dépendances sont téléchargées une fois dans .deps/ et vérifiées :
#  - ps5-native-app-boilerplate (commit épinglé) : SDK payload v0.42, outils FSELF ;
#  - ps5-opengl (commit épinglé) : fichiers d'intégration native-app/ ;
#  - SDK ps5-opengl 1.0.1 précompilé (somme SHA-256 épinglée).
# Prérequis (Ubuntu/WSL) : clang-18 lld-18 llvm-18 make ninja-build python3 curl
#                          wget unzip git.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
deps="$root/.deps"
out="$root/build/ps5"

boilerplate_repo=https://github.com/blackbearreloaded/ps5-native-app-boilerplate
boilerplate_commit=b8dc4a3fe2e05e826b037bd68824751c7468a6d7
opengl_commit=db752da4c89380af228497b5b6852163a4e61069
opengl_raw="https://raw.githubusercontent.com/blackbearreloaded/ps5-opengl/$opengl_commit"
sdk_version=1.0.1
sdk_url="https://github.com/blackbearreloaded/ps5-opengl/releases/download/v$sdk_version/ps5-opengl-sdk-$sdk_version.tar.gz"
sdk_sha256=aaa2e8957f55e1fc0b654dcb35a36e585f7e635d63952da40a28be7f64fde741

title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/ps5/sce_sys/param.json")
compiler=$(command -v "${PS5_CLANG:-clang-18}") || { echo "clang-18 introuvable" >&2; exit 2; }
linker=$(command -v ld.lld-18 || command -v ld.lld) || { echo "ld.lld introuvable" >&2; exit 2; }
step() { printf '\n==> %s\n' "$*"; }

mkdir -p "$deps" "$out"

# --- 1. Boilerplate + SDK payload ---------------------------------------------------
template="$deps/ps5-native-app-boilerplate"
if [[ ! -d $template/.git ]]; then
    step "Téléchargement du boilerplate natif"
    git clone -q "$boilerplate_repo" "$template"
fi
if [[ $(git -C "$template" rev-parse HEAD) != "$boilerplate_commit" ]]; then
    git -C "$template" fetch -q origin "$boilerplate_commit" || git -C "$template" fetch -q origin
    git -C "$template" checkout -q --detach "$boilerplate_commit"
fi
if [[ ! -x $template/.deps/native/ps5-payload-sdk/bin/prospero-lld ]]; then
    step "SDK payload PS5 (épinglé et vérifié par le boilerplate)"
    (cd "$template" && bash tools/setup-native-dependencies.sh)
fi
payload_sdk="$template/.deps/native/ps5-payload-sdk"

# --- 2. Fichiers d'intégration ps5-opengl --------------------------------------------
glue="$deps/ps5-opengl-$opengl_commit"
if [[ ! -f $glue/.complete ]]; then
    step "Fichiers native-app de ps5-opengl ($opengl_commit)"
    mkdir -p "$glue/native-app" "$glue/tools"
    for f in runtime_shims.c app_heap.c ps5-pie.ld app-symbols.map agc_link_stub.c agc_driver_link_stub.c; do
        curl -fsSL -o "$glue/native-app/$f" "$opengl_raw/native-app/$f"
    done
    curl -fsSL -o "$glue/tools/native-display-metadata.py" "$opengl_raw/tools/native-display-metadata.py"
    touch "$glue/.complete"
fi

# --- 3. SDK ps5-opengl précompilé -----------------------------------------------------
sdk_dir="$deps/ps5-opengl-sdk-$sdk_version"
if [[ ! -f $sdk_dir/.complete ]]; then
    step "SDK ps5-opengl $sdk_version"
    archive="$deps/ps5-opengl-sdk-$sdk_version.tar.gz"
    [[ -f $archive ]] || curl -fL --retry 4 -o "$archive" "$sdk_url"
    echo "$sdk_sha256  $archive" | sha256sum --check --strict
    rm -rf "$sdk_dir"
    tar -xzf "$archive" -C "$deps"
    touch "$sdk_dir/.complete"
fi
gl_prefix="$sdk_dir/sdk"
(cd "$gl_prefix" && sha256sum --check --strict --quiet manifest.sha256)

# --- 4. Compilation de l'application ---------------------------------------------------
step "Compilation (clang-18, cible x86_64-sie-ps5)"
obj="$out/obj"
rm -rf "$obj"
mkdir -p "$obj"
git_rev=$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo inconnue)
[[ -z $(git -C "$root" status --porcelain 2>/dev/null) ]] || git_rev="$git_rev+modifs"
common=(-O2 -g -fPIC -ffunction-sections -fdata-sections -fno-omit-frame-pointer -Wall -Wextra
        -DGL_GLEXT_PROTOTYPES=1 "-DRHYTHM_GIT_REV=\"$git_rev\""
        -I"$gl_prefix/include" -I"$root/src" -I"$root/third_party" -I"$root/third_party/font8x8")
# Toutes les sources du jeu (sauf la plateforme PC) + bibliothèques tierces en C.
mapfile -t sources < <(cd "$root" && find src -name '*.cpp' ! -name 'platform_sdl2.cpp' | sort)
sources+=(third_party/font8x8/font8x8.c third_party/impl.c third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c
          third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
objects=()
for source in "${sources[@]}"; do
    object="$obj/$(echo "$source" | tr '/' '_').o"
    if [[ $source == *.c ]]; then std=(-std=c11); else std=(-std=c++20 -fno-exceptions -fno-rtti); fi
    PS5_PAYLOAD_SDK="$payload_sdk" PS5_CLANG="$compiler" sh "$template/tooling/prospero-clang18" \
        "${std[@]}" "${common[@]}" -c "$root/$source" -o "$object"
    objects+=("$object")
done
"$linker" -r -o "$out/rhythm-ps5.o" "${objects[@]}"

# --- 5. Assemblage du dossier d'application -------------------------------------------
# Même procédure que tools/build-native-test-app.sh de ps5-opengl : copie du
# boilerplate, tas applicatif de ps5-opengl (malloc enveloppé), script de liens
# ps5-opengl, puis édition de liens avec le runtime OpenGL et libc++.
step "Assemblage du titre $title_id"
app="$out/app"
rm -rf "$app"
mkdir -p "$app"
cp "$template/Makefile" "$app/Makefile"
for directory in runtime tooling tools; do
    mkdir -p "$app/$directory"
    cp -a "$template/$directory/." "$app/$directory/"
done
mkdir -p "$app/sce_sys" "$app/assets" "$app/src" "$app/include" "$app/vendor"
cp "$root/ps5/sce_sys/param.json" "$root/ps5/sce_sys/icon0.png" "$app/sce_sys/"
cp -a "$root/ps5/assets/." "$app/assets/"
# Toujours déclarer la capacité 120 Hz : le choix 60/120 se fait dans les options.
python3 "$glue/tools/native-display-metadata.py" "$app/sce_sys/param.json" --fps 120

heap_source="$app/tooling/native/sce_module_writer.cpp"
heap_default='write_u64(result.data, result.heap_size, std::numeric_limits<std::uint64_t>::max());'
[[ $(grep -Fc "$heap_default" "$heap_source") == 1 ]] || { echo "boilerplate inattendu (heap)" >&2; exit 1; }
sed -i "s/$heap_default/write_u64(result.data, result.heap_size, 0x10000000ULL);/" "$heap_source"
link_script="$app/tools/build.sh"
[[ $(grep -Fc -- '--eh-frame-hdr \' "$link_script") == 1 ]] || { echo "boilerplate inattendu (liens)" >&2; exit 1; }
sed -i 's/--eh-frame-hdr \\/--eh-frame-hdr --wrap=malloc --wrap=calloc --wrap=realloc --wrap=free --wrap=posix_memalign --wrap=malloc_usable_size \\/' \
    "$link_script"
cp "$template/tooling/native/ps5-pie.ld" "$app/tooling/native/ps5-pie-base.ld"
cp "$glue/native-app/ps5-pie.ld" "$glue/native-app/app-symbols.map" "$app/tooling/native/"
cp "$glue/native-app/runtime_shims.c" "$glue/native-app/app_heap.c" "$app/src/"
for headers in EGL GL KHR; do cp -a "$gl_prefix/include/$headers" "$app/include/"; done

compiler_runtime="$("$compiler" --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"
static_libraries=(
    "$gl_prefix/lib/libPS5OpenGLCore33.a"
    "$payload_sdk/target/lib/libScePad.so"
    "$payload_sdk/target/lib/libSceUserService.so"
    "$payload_sdk/target/lib/libSceAudioOut.so"
    "$payload_sdk/target/lib/libSceKeyboard.so"
    "$payload_sdk/target/lib/libSceImeDialog.so"
    "$payload_sdk/target/lib/libSceSystemService.so"
    "$payload_sdk/target/lib/libSceNet.so"
    "$payload_sdk/target/lib/libSceSsl.so"
    "$payload_sdk/target/lib/libSceHttp.so"
    "$payload_sdk/target/lib/libunwind.a"
    "$payload_sdk/target/lib/libc++abi.a"
    "$payload_sdk/target/lib/libc++.a"
    "$compiler_runtime"
)
for library in "${static_libraries[@]}"; do [[ -s $library ]] || { echo "manquant : $library" >&2; exit 1; }; done
{
    printf 'SEARCH_DIR("%s")\n' "$payload_sdk/target/lib"
    printf 'SEARCH_DIR("%s")\n' "$gl_prefix/lib"
    printf 'EXTERN(ps5_agc_gate2_run)\n'
    printf 'GROUP (\n'
    printf '  "%s"\n' "$out/rhythm-ps5.o" "${static_libraries[@]}"
    printf ')\n'
} > "$app/vendor/librhythm_group.a"
printf 'APP_INCLUDE_PATHS = include\nAPP_STATIC_ARCHIVES = vendor/librhythm_group.a\nUSE_CCACHE = 0\n' > "$app/.env"

mkdir -p "$app/.deps"
cp -a "$template/.deps/native" "$app/.deps/native"
app_sdk="$app/.deps/native/ps5-payload-sdk"
mkdir -p "$app/build/native-imports"
for stub in agc_link_stub:libSceAgc agc_driver_link_stub:libSceAgcDriver; do
    source=${stub%%:*}
    module=${stub##*:}
    PS5_PAYLOAD_SDK="$app_sdk" PS5_CLANG="$compiler" sh "$app/tooling/prospero-clang18" \
        -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections \
        -c "$glue/native-app/$source.c" -o "$app/build/native-imports/$source.o"
    "$app_sdk/bin/prospero-lld" --shared -soname "$module.prx" \
        -o "$app_sdk/target/lib/$module.so" "$app/build/native-imports/$source.o"
done

make -C "$app" --no-print-directory app

# --- 6. Sorties -------------------------------------------------------------------------
dist="$out/dist"
rm -rf "$dist"
mkdir -p "$dist"
cp -a "$app/dist/$title_id" "$dist/$title_id"
cp "$app/dist/$title_id.zip" "$dist/"
cp "$app/build/llvm-pie.elf" "$dist/rhythm-ps5-symbols.elf"
step "Terminé"
printf 'Dossier d appli : %s\n' "$dist/$title_id"
printf 'Archive         : %s\n' "$dist/$title_id.zip"
printf 'Symboles        : %s\n' "$dist/rhythm-ps5-symbols.elf"
sha256sum "$dist/$title_id/eboot.bin"

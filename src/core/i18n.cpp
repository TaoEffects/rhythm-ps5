// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/i18n.h"

#include <cstring>
#include <string_view>
#include <unordered_map>

namespace i18n {
namespace {

Lang g_lang = Lang::French;

struct Entry {
    const char *fr, *en;
};
const Entry kEnglish[] = {
#include "core/i18n_en.inc"
};

const Entry kJapanese[] = {
#include "core/i18n_ja.inc"
};

using Table = std::unordered_map<std::string_view, const char *>;

template <size_t N>
Table makeTable(const Entry (&entries)[N]) {
    Table m;
    for (const Entry &e : entries) m.emplace(e.fr, e.en);
    return m;
}

const Table &english() {
    static const Table map = makeTable(kEnglish);
    return map;
}

const Table &japanese() {
    static const Table map = makeTable(kJapanese);
    return map;
}

}  // namespace

void setLang(Lang lang) { g_lang = lang; }
Lang lang() { return g_lang; }

Lang resolve(int setting, const char *systemLang) {
    if (setting == 1) return Lang::French;
    if (setting == 2) return Lang::English;
    if (setting == 3) return Lang::Japanese;
    if (systemLang && !strncmp(systemLang, "fr", 2)) return Lang::French;
    if (systemLang && !strncmp(systemLang, "ja", 2)) return Lang::Japanese;
    return Lang::English;
}

const char *tr(const char *french) {
    if (g_lang == Lang::French || !french || !*french) return french;
    const std::string_view key(french);
    if (g_lang == Lang::Japanese) {
        // Texte sans traduction japonaise : l'anglais, à défaut le français.
        const auto it = japanese().find(key);
        if (it != japanese().end()) return it->second;
    }
    const auto it = english().find(key);
    return it == english().end() ? french : it->second;
}

std::string tr(const std::string &french) {
    if (g_lang == Lang::French) return french;
    return tr(french.c_str());
}

}  // namespace i18n

// SPDX-License-Identifier: GPL-3.0-or-later
// Langue de l'interface. Les textes sont écrits en français dans le code ; en
// anglais ou en japonais, le moteur de texte remplace chaque texte connu par sa
// traduction au moment de l'afficher (tables i18n_en.inc et i18n_ja.inc). Les textes construits avec des
// valeurs passent par tr() sur leur modèle (« Essais : %d »...).
#pragma once

#include <string>

namespace i18n {

enum class Lang { French, English, Japanese };

void setLang(Lang lang);
Lang lang();
// Réglage (0 auto, 1 français, 2 anglais, 3 japonais) + langue de la console -> langue effective.
Lang resolve(int setting, const char *systemLang);

// Traduction d'un texte français (renvoie le texte tel quel s'il est inconnu).
const char *tr(const char *french);
std::string tr(const std::string &french);

}  // namespace i18n

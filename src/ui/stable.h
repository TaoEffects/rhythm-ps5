// SPDX-License-Identifier: GPL-3.0-or-later
// Thème « stable » : menus disposés comme ceux d'osu!stable (menu principal,
// sélection de map, classement, pause), dessinés avec nos propres formes. Les
// valeurs de mise en page s'appuient sur McOsu (GPL-3), qui reproduit stable.
#pragma once

#include <memory>

#include "app/game.h"

std::unique_ptr<Screen> makeStableMainMenu(Game &g);
std::unique_ptr<Screen> makeStableSongSelect(Game &g);
std::unique_ptr<Screen> makeStableResults(Game &g, const game::PlayResult &r);

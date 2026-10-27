// SPDX-License-Identifier: GPL-3.0-or-later
// Briques communes du thème classique (façon osu!stable) : unité de mise en page,
// couleurs, nombres et lettres de rang (images du skin s'il en a, sinon dessinés),
// bouton retour, curseur.
#pragma once

#include <string>
#include <vector>

#include "app/context.h"
#include "ui/lz.h"

struct LocalScore;

namespace st {

using lz::ButtonFx;
using lz::Tween;
using anim::Ease;

// stable dispose ses écrans sur une hauteur de référence de 768 px.
inline float S() { return kVH / 768.0f; }

namespace col {
constexpr uint32_t white = lz::hex(0xffffff), black = lz::hex(0x000000);
// Carrousel : sets en rose, difficultés du set ouvert en bleu, difficulté choisie en blanc.
constexpr uint32_t setPanel = lz::hex(0xeb4999), diffPanel = lz::hex(0x0078ff), activePanel = lz::hex(0xffffff);
constexpr uint32_t back = lz::hex(0xee3399);
constexpr uint32_t yellow = lz::hex(0xffcc22);
}  // namespace col

// Couleur d'une lettre de rang dessinée (SS argent si Hidden).
uint32_t rankColour(const std::string &rank, bool silver);

// Nombre avec les chiffres « score » du skin, sinon la police du jeu. Renvoie la largeur.
// align : 0 gauche, 1 centre, 2 droite. suffix : "x", "%" ou vide.
float number(Context &c, const std::string &text, float x, float y, float h, uint32_t color, int align, bool combo = false);
// Lettre de rang (images ranking-X du skin, sinon dessinée) centrée en (cx, cy), hauteur h.
void rankLetter(Context &c, const std::string &rank, bool silver, float cx, float cy, float h, float alpha = 1, bool small = false);

// Bouton « retour » du coin bas gauche (menu-back). fx.hover anime l'avancée.
void backButton(Context &c, ButtonFx &fx, float alpha = 1);
float backButtonW();
float backButtonH();
bool backHovered(const UiInput &in);

// Curseur du menu (curseur du skin s'il en a un).
void cursor(Context &c, float x, float y, bool pressed);

// Statistiques du joueur local, calculées comme celles d'osu! à partir des scores
// enregistrés : pp pondérés (meilleur score par difficulté, x 0,95 à chaque rang),
// précision pondérée de la même façon, niveau d'après le score total.
struct PlayerStats {
    double pp = 0, accuracy = 0;
    long long totalScore = 0;
    int level = 1, plays = 0;
    float levelProgress = 0;
};
PlayerStats playerStats(const std::vector<::LocalScore> &scores);
// Carte du joueur (avatar, nom, performance, précision, niveau) dans le rectangle donné.
void userCard(Context &c, const PlayerStats &s, float x, float y, float w, float h, float hover = 0, float alpha = 1);

// Score avec séparateurs de milliers.
std::string grouped(long long v);

}  // namespace st

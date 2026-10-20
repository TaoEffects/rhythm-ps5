// SPDX-License-Identifier: GPL-3.0-or-later
// Clavier virtuel du jeu (PS5) : panneau en bas de l'écran, dans le style de
// l'interface. Croix directionnelle / stick pour choisir une touche, Croix pour
// taper, Carré efface, Triangle met une espace, R1 valide, Rond annule ; le
// pointeur (stick droit, pavé tactile) fonctionne aussi.
#pragma once

#include <string>

#include "app/context.h"
#include "core/anim.h"

namespace lz {

class OnScreenKeyboard {
public:
    void open(Context &c, const std::string &title, const std::string &initial);
    bool isOpen() const { return open_; }
    // Résultat : 0 en cours, 1 validé (texte dans out), -1 annulé.
    int poll(std::string &out);
    void update(Context &c, const UiInput &in);
    void draw(Context &c) const;

private:
    struct Key {
        float x, y, w, h;
        int row, col;
        std::string label;
    };
    void layout(const Context &c, Key *keys, int &count) const;
    void press(Context &c, const std::string &label);
    void finish(int result);

    bool open_ = false;
    int result_ = 0;
    std::string title_, text_;
    int row_ = 1, col_ = 0;
    anim::Tween slide_{0};
};

}  // namespace lz

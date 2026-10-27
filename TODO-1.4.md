# À faire pour la 1.4 (correctifs et optimisation)

Rien n'est publié avant ton feu vert.

## Paramètres
- [x] Clic sur les icônes de section : la liste défile jusqu'à la section (corrigé localement, pas encore publié).
- [ ] Vérifier le reste à la souris et à la manette : retour, curseurs, assignation des touches.

## Relecture et nettoyage du code (~20 000 lignes)
- [ ] Compiler avec plus d'avertissements (-Wall -Wextra, clang -Wunused-member-function / -Wunused-private-field) et supprimer ce qui n'est pas utilisé : fonctions, membres, includes, réglages morts. Exemple déjà repéré : variable `col` inutilisée dans stable_menu.cpp.
- [ ] Retirer le code commenté, les instructions qui ne font rien et les restes de débogage.
- [ ] Mettre en commun ce qui est dupliqué entre les deux sélections de map (lazer et stable) : filtre de recherche, saisie au clavier, règles d'incompatibilité des mods (à placer dans game::Mods).
- [ ] Mettre à jour les commentaires périmés et rendre cohérentes les façons de faire (unités U / K / S, conversions de couleurs).
- [ ] Relire écran par écran : menu, sélection, partie, résultats, options, téléchargement, assistant de premier lancement.

## Accès aux fichiers du jeu
- [ ] PC / Mac : boutons « Ouvrir le dossier » (maps, skins, données) dans les options, via l'explorateur / Finder / xdg-open.
- [ ] Vérifier où se trouvent les données sur chaque système et les afficher dans les options. Envisager un emplacement plus visible (Documents).
- [ ] PS5 : afficher clairement les chemins (/data/rhythm-ps5/...) et expliquer comment y accéder (FTP).

## Ajout de skins (et de maps)
- [ ] PC / Mac : glisser-déposer d'un .osk ou d'un .osz sur la fenêtre, puis import automatique.
- [ ] Relancer la détection du dossier skins à l'ouverture des options (sans redémarrer), avec un bouton « Actualiser ».
- [ ] Importer aussi les .osk et .osz déposés dans le dossier des maps.
- [ ] Mesurer le temps de chargement d'un skin (beaucoup d'images) et ne charger les éléments d'interface que si le thème classique est actif.

## Changement de thème
- [ ] Bascule immédiate, sans fermer les options, avec un fondu (sortie / entrée).
- [ ] Recharger les polices sans saccade et vérifier qu'aucun texte n'utilise l'ancienne police pendant une image.
- [ ] Remettre le logo, la barre d'outils et le fond dans un état cohérent pour le nouveau thème (position, taille, visualiseur).
- [ ] Options et Téléchargement en style classique quand ce thème est actif.

## Optimisation
- [ ] Mesurer le temps par image sur chaque écran (journal perf) et sur PS5 en 120 Hz.
- [ ] Sélection de map : éviter de recopier les scores à chaque image (scoresFor renvoie une copie). Mettre en cache la vignette et le rang par bouton.
- [ ] HUD : calculer l'UR de façon incrémentale au lieu de reparcourir toutes les erreurs à chaque image.
- [ ] Stats du joueur (pp pondérés) : les recalculer seulement quand un score est ajouté.

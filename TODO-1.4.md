# À faire pour la 1.4 (correctifs et optimisation)

Rien n'est publié avant ton feu vert.

## Mentions de la console sur PC / Mac
- [ ] Options > Langue : « Automatique (langue de la console) » apparaît aussi sur ordinateur → « langue du système ».
- [ ] Assistant de premier lancement : « tes scores restent sur la console » → « sur cet appareil » hors PS5.
- [ ] Repasser sur tous les textes (FR, EN, JA), dont les astuces et l'aide des options, et le README.

## Paramètres
- [x] Clic sur les icônes de section : la liste défile jusqu'à la section (corrigé localement, pas encore publié).
- [ ] Vérifier le reste à la souris et à la manette : retour, curseurs, assignation des touches.

## Thème moderne : zones cliquables
- [ ] Revoir la zone cliquable des boutons : elle ne couvre pas tout le bouton dessiné (pied de page de la sélection : Mods, Au hasard, Options ; à vérifier aussi sur les boutons inclinés du menu, le retour, la pause et les résultats). Faire correspondre la zone à la forme inclinée.

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

## Musique
- [ ] Fondu entre deux morceaux (changement de map, suivant / précédent) trop lent : le raccourcir et vérifier qu'il reste propre.

## Raccourcis clavier (PC / Mac)
- [ ] Ajouter les raccourcis d'osu! qui manquent : Ctrl+O (options), Ctrl+D (téléchargement), F2 au hasard / Maj+F2 retour arrière, Ctrl+Entrée (Auto), F1 mods, Échap, raccourcis du menu (P jouer, O options, E/Q quitter), Alt+molette volume, F7/F8 et lecteur de musique (Z X C V B), F12 capture, ajustement du décalage en jeu (+ / -), touche de pause, touche « recommencer rapidement » (` maintenue)...
- [ ] Vérifier lesquels existent déjà, puis les rendre tous reprogrammables dans Options > Commandes (liste des raccourcis, saisie de la nouvelle touche, rétablir les valeurs par défaut).

## Écran de résultats (thème moderne)
- [ ] Le refaire : aujourd'hui très lazer (anneau au centre) et mal fini. Viser un entre-deux lazer / stable : moderne mais lisible d'un coup d'œil, avec l'espace de l'écran utilisé.
  - Grande lettre de rang et score bien visibles.
  - Jugements (300 / 100 / 50 / ratés), combo, précision, pp, UR et courbe de vie lisibles sans chercher.
  - Animations d'arrivée soignées.
- [ ] Le maquetter et comparer avec stable et lazer avant de le finaliser.

## Personnalisation des skins
- [ ] Options de skin à la manière d'osu! : ignorer les couleurs de combo de la map (couleurs du skin seulement), ignorer les sons de la map (hitsounds du skin seulement), ignorer le skin de la map, choix de la couleur des combos, taille du curseur et de la traînée par skin, réglages des sliders (épaisseur du contour, couleur), etc.

## Premier lancement : profil « classique » ou « pro »
- [ ] Dans l'assistant de départ, choisir entre « classique » (réglages par défaut) et « pro ». Le profil « pro » met le fond noir au maximum, l'instafade, les ratés seuls, l'UR, le compteur de touches, la vidéo et le storyboard coupés, etc.
- [ ] Profil « pro » : proposer de télécharger des skins « pro » connus (WhiteCat, Rafis, mrekk...). À vérifier avant : chaque skin appartient à son auteur, il faut une source et une autorisation de redistribution (lien officiel de l'auteur, sinon simple lien vers la page du skin plutôt qu'un téléchargement intégré).

## Rendu des skins osu! en jeu
- [ ] Revoir l'implémentation complète des skins : interface en jeu (score, combo, barre de vie, précision, jugements) avec les images du skin, et les effets (éclatement des cercles, lumières de frappe, sliders, spinners, curseur et traînée), en comparant avec stable.

## Paramètres : animations
- [ ] Animer les éléments qui coulissent : interrupteurs, curseurs, valeurs des choix, ouverture des sections.
- [ ] Repérer ce qui change encore sans animation (survol, sélection, passage d'une section à l'autre, assignation d'une touche) et l'animer.

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

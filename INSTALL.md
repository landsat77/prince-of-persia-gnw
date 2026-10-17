# Prince of Persia — installation

Requires Retro-Go SD **v2.0.0-rc1 or later** on the Game & Watch.

The game data is not included: you need the files of the DOS game
(`PRINCE.DAT`, `KID.DAT`, `LEVELS.DAT`, `VDUNGEON.DAT`, ...) or the `data`
folder of SDLPoP.

1. Build the data pack (Python 3, plus `pip install Pillow` for SDLPoP's
   PNG data folders):

       python3 pop_pack.py PrinceOfPersia.pop /path/to/PRINCE

   Keep the `pop_render` folder next to `pop_pack.py`: it renders the music
   once (pack of about 9 MB, music at 11 kHz, smooth game). Options:
   `--music-rate 22050` (full quality, 17 MB), `--music-codec adpcm`
   (smaller, slight hiss), `--no-music` (1.2 MB, music synthesized live,
   slower).

2. Copy to the SD card, side by side, with the same name:

       /homebrews/PrinceOfPersia.bin
       /homebrews/PrinceOfPersia.pop

3. Start "Prince of Persia" from the Homebrew tab. The first start copies the
   pack to the external flash ("Caching game").

Controls: D-pad = move, A = action (grab, pick up, strike), B = jump,
GAME = pause, TIME = remaining time, PAUSE/SET = Retro-Go menu (savestates,
music, sound, bug fixes, restart level).

---

# Prince of Persia — installation (français)

Nécessite Retro-Go SD **v2.0.0-rc1 ou plus récent** sur la Game & Watch.

Les données du jeu ne sont pas fournies : il faut les fichiers du jeu DOS
(`PRINCE.DAT`, `KID.DAT`, `LEVELS.DAT`, `VDUNGEON.DAT`...) ou le dossier
`data` de SDLPoP.

1. Créer le pack de données (Python 3, et `pip install Pillow` pour les
   dossiers PNG de SDLPoP) :

       python3 pop_pack.py PrinceOfPersia.pop /chemin/vers/PRINCE

   Laisser le dossier `pop_render` à côté de `pop_pack.py` : il pré-calcule
   la musique (pack d'environ 9 Mo, musique en 11 kHz, jeu fluide). Options :
   `--music-rate 22050` (qualité complète, 17 Mo), `--music-codec adpcm`
   (plus petit, léger souffle), `--no-music` (1,2 Mo, musique synthétisée en
   direct, plus lent).

2. Copier sur la carte SD, côte à côte, avec le même nom :

       /homebrews/PrinceOfPersia.bin
       /homebrews/PrinceOfPersia.pop

3. Lancer « Prince of Persia » depuis l'onglet Homebrew. Au premier
   lancement, le pack est copié dans la flash externe (« Caching game »).

Commandes : croix = déplacements, A = action (s'accrocher, ramasser, frapper),
B = saut, GAME = pause, TIME = temps restant, PAUSE/SET = menu Retro-Go
(sauvegardes, musique, son, corrections, recommencer le niveau).

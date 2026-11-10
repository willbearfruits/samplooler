// The factory songs (pixel.h): each a sound for the core (its name and settings) and its notes as text (inst_song.c
// has the text's rules). They are original tunes written for PIXEL, or old ones long out of copyright.
#include "pixel.h"

const preset_t PX_PRESETS[PX_NSONGS] = {
    { "FIRST LEVEL", "tmpo=150 key =9 scal=1 cons=3" },
    { "EMPTY", "tmpo=120 key =0 scal=0 cons=3 lech=0" },
};

const char *const PX_SONG_TEXT[PX_NSONGS] = {
    // FIRST LEVEL: A minor, the four chords going round
    "AL: 5 . 1' . 3' 2' 1' . 5 . 1' . 7 - - .\n"
    "AB: 1 . 1' . 1 . 1' . 1 . 1' . 1 . 1' 1\n"
    "AC: I . . . . . . . . . . . . . . .\n"
    "AK: x...x...x...x...\n"
    "AS: ....x.......x...\n"
    "AH: x.x.x.x.x.x.x.x.\n"
    "BL: 6 . 1' . 3' 2' 1' . 6 . 4 . 6! - - .\n"
    "BB: 6, . 6 . 6, . 6 . 6, . 6 . 6, . 6 6\n"
    "BC: VI . . . . . . . . . . . . . . .\n"
    "BK: x...x...x...x...\n"
    "BS: ....x.......x...\n"
    "BH: x.x.x.x.x.x.x.x.\n"
    "CL: 5 . 7 . 3' 2' 7 . 5 . 3 . 5 - ~7 -\n"
    "CB: 3 . 3' . 3 . 3' . 3 . 3' . 3 . 3' 3\n"
    "CC: III . . . . . . . . . . . . . . .\n"
    "CK: x...x...x...x...\n"
    "CS: ....x.......x...\n"
    "CH: x.x.x.x.x.x.x.x.\n"
    "CO: ..............x.\n"
    "DL: 7 . 2' . 4' 3' 2' . 7 . 5 . 7! - 2' -\n"
    "DB: 7, . 7 . 7, . 7 . 7, . 7 . 5 . 7 2'\n"
    "DC: VII . . . . . . . V7 . . . . . . .\n"
    "DK: x...x...x...x.x.\n"
    "DS: ....x.......x.xx\n"
    "DH: x.x.x.x.x.x.....\n"
    "DT: ............X.x.\n"
    "ORDER: A B C D A B C D\n",
    // EMPTY: one bar of nothing, to write in
    "ORDER: A\n",
};

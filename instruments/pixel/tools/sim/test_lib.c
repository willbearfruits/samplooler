// The songs keep their notes (inst_lib.c): the core's ways with a sound (the factory list, MY EDIT, SAVE AS, REVERT,
// switching off and on, RESET ALL) all carry the notes along, though the core knows nothing of them.
#include "sim.h"
#include "pixel.h"
#include <string.h>

void sim_menu_goto(int gi, int k);
void sim_menu_close(void);
bool sim_menu_param(int p);
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
void lib_test_flush(void);
extern bool px_store_fail;

static void program(int n) { front_midi(0xC0, (uint8_t)n, 0); sim_run(0.1f); }
static void save_as(int k)
{
    sim_menu_goto(0, 1); sim_tap();
    for (int i = 0; i < 8; i++) sim_turn(-1);
    for (int i = 0; i < k; i++) sim_turn(1);
    sim_tap(); sim_menu_close(); sim_run(0.1f);
}
static void revert(void) { sim_menu_goto(0, 2); sim_tap(); sim_menu_close(); sim_run(0.1f); }
static void browse_first(void) { sim_menu_goto(0, 0); sim_tap(); for (int i = 0; i < 70; i++) sim_turn(-1); sim_run(0.1f); }
static void browse(int d) { sim_turn(d); sim_run(0.2f); }
// the board switched off and on: what the flash held (the params, the sound's number, the stores) comes back
static void power_cycle(void) { sim_menu_close(); lib_test_flush(); front_init(true, false, true); sim_run(0.3f); }
static void edit(int step, uint8_t cell) { px_song.pat[0].lead[step] = cell; px_notes_edited(); sim_run(0.05f); }
static uint8_t note_at(int step) { return px_song.pat[0].lead[step]; }

void test_library(void)
{
    section("pixel: the songs keep their notes");
    int nrev = px_param_nrev();
    sim_menu_close(); px_stop(); px_store_fail = false;
    program(0);
    uint8_t first = note_at(0);
    check(meta.sound == 0 && first != 0 && px_song.order_n == 8 && pv[nrev] == 0.0f, "the first factory song: its notes, eight bars, unedited");
    check(!front_status()->edited, "a factory song starts without the edit mark");

    // an edit marks the sound; another song chosen, it waits as MY EDIT with its notes
    edit(1, 23);
    check(front_status()->edited && pv[nrev] != 0.0f && lib_notes_edited(), "a changed note marks the song as edited");
    browse_first(); browse(2);                                      // (MY EDIT, the first song, EMPTY)
    check(meta.sound == 1 && px_song.order_n == 1 && note_at(0) == 0 && note_at(1) == 0, "another song chosen (EMPTY): its own notes");
    browse_first();
    check(meta.sound == -1 && note_at(0) == first && note_at(1) == 23, "MY EDIT brings the edited notes back");
    sim_menu_close();

    // SAVE AS: the notes go with the user song
    save_as(1);
    check(meta.sound == 101 && !front_status()->edited && !lib_notes_edited(), "SAVE AS USER 2: saved, no edit mark");
    program(0);
    check(note_at(1) != 23 && note_at(0) == first, "the factory song again: as it was written");
    program(PX_NSONGS + 1);
    check(meta.sound == 101 && note_at(1) == 23, "USER 2: the notes that were saved");

    // REVERT: the edit goes, the saved notes return
    edit(2, 30); edit(1, 0);
    check(note_at(2) == 30 && front_status()->edited, "USER 2 edited further");
    revert();
    check(note_at(2) != 30 && note_at(1) == 23 && !lib_notes_edited(), "REVERT: back to the saved notes");

    // saved again over itself, after another change
    edit(3, 31);
    save_as(1);
    program(0); program(PX_NSONGS + 1);
    check(note_at(3) == 31 && note_at(1) == 23, "saved over itself: the newer notes are the saved ones");

    // off and on
    power_cycle();
    check(meta.sound == 101 && note_at(3) == 31 && note_at(1) == 23, "off and on: the user song and its notes");
    edit(4, 33);
    power_cycle();
    check(meta.sound == 101 && note_at(4) == 33 && front_status()->edited, "off and on with an edit not saved: the edit is still there, marked");
    revert();
    check(note_at(4) != 33 && note_at(3) == 31, "and REVERT still finds the saved notes");
    program(0); edit(5, 35);
    power_cycle();
    check(meta.sound == 0 && note_at(5) == 35 && note_at(0) == first, "off and on with an edited factory song: the edit is there");
    browse_first(); browse(2); browse_first();                      // to EMPTY and back: it is MY EDIT now
    sim_menu_close();
    power_cycle();
    check(meta.sound == -1 && note_at(5) == 35, "off and on while on MY EDIT: its notes");

    // an edit, then off before anything was written: the song comes back as it was saved (a few seconds are lost,
    // as with any setting)
    program(PX_NSONGS + 1);
    px_song.pat[0].lead[6] = 40; px_notes_edited();
    front_init(true, false, true); sim_run(0.3f);
    check(meta.sound == 101 && note_at(6) != 40 && note_at(3) == 31, "off within a moment of an edit: the saved notes, nothing broken");

    // the store is full: the save says so and the notes stay in hand
    program(0); edit(7, 41);
    px_store_fail = true;
    save_as(2);
    check(note_at(7) == 41 && front_status()->edited, "no room in the store: the notes are still in hand and the song stays marked");
    px_store_fail = false;
    save_as(2);
    program(0); program(PX_NSONGS + 2);
    check(meta.sound == 102 && note_at(7) == 41, "saved again once there is room: USER 3 has them");

    // the working notes are written by themselves, 3 s after the last change at the earliest
    program(0); edit(8, 42);
    sim_run(31.0f);
    front_init(true, false, true); sim_run(0.3f);
    check(note_at(8) == 42, "left alone half a minute, an edit is in the store without being asked");

    // RESET ALL: the user songs go, the first song is as written
    sim_menu_param(CP(CP_RESET)); sim_tap(); sim_turn(1); sim_tap(); sim_run(0.3f);
    sim_menu_close();
    check(meta.sound == 0 && meta.user_mask == 0 && note_at(0) == first && note_at(8) != 42 && pv[nrev] == 0.0f, "RESET ALL: no user songs, the first song as it was written");
    sim_run(0.5f);
}

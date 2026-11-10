// (a first stand-in: the editor comes next)
#include "pixel.h"
bool edit_active(void) { return false; }
bool edit_hold(void) { return false; }
void edit_tick(const ctl_t *c) { (void)c; }
void edit_draw(const view_t *v) { (void)v; }
void edit_action(int what) { (void)what; }
int  edit_pattern(void) { return px_pos.pattern; }

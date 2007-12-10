/* curses attributes for the currently focused window */
#define ATTR_SELECTED COLOR(COLOR_BLACK,COLOR_RED)
/* curses attributes for normal (not selected) windows */
#define ATTR_NORMAL   A_NORMAL
/* printf format string for the window title %s is replaced by the title */
#define TITLE "[ %s ]"
/* master width factor [0.1 .. 0.9] */
#define MWFACT  0.5	
/* defines how often all window's are repainted (in milliseconds) */
#define REDRAW_TIMEOUT  50 

#include "tile.c"
#include "grid.c"
#include "bstack.c"

Layout layouts[] = {
	{ "[]=", tile },
	{ "+++", grid },
	{ "TTT", bstack },
};

#define MOD CTRL('a')

Key keys[] = {
	{ MOD, 'q', quit , NULL },
	{ MOD, 'c', create , "/bin/sh --login" },
	{ MOD, 'j', focusnext , NULL },
	{ MOD, 'k', focusprev , NULL },
	{ MOD, 't', setlayout , "[]=" },
	{ MOD, 'g', setlayout , "+++" },
	{ MOD, 'b', setlayout , "TTT" },
	{ MOD, ' ', setlayout , NULL },
	{ MOD, 'h', setmwfact , "-0.05" },
	{ MOD, 'l', setmwfact , "+0.05" },
	{ MOD, 'z', zoom , NULL },
	{ MOD, '0', focusn, "0" },
	{ MOD, '1', focusn, "1" },
	{ MOD, '2', focusn, "2" },
	{ MOD, '3', focusn, "3" },
	{ MOD, '4', focusn, "4" },
	{ MOD, '5', focusn, "5" },
	{ MOD, '6', focusn, "6" },
	{ MOD, '7', focusn, "7" },
	{ MOD, '8', focusn, "8" },
	{ MOD, '9', focusn, "9" },
};

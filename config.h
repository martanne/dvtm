/* curses attributes for the currently focused window */
#define ATTR_SELECTED   COLOR(COLOR_BLACK,COLOR_RED)
/* curses attributes for normal (not selected) windows */
#define ATTR_NORMAL     A_NORMAL
/* curses attributes for the status bar */
#define BAR_ATTR        COLOR(COLOR_BLACK,COLOR_RED)
/* true if the statusbar text should be right aligned, 
 * set to false if you prefer it left aligned */
#define BAR_ALIGN_RIGHT true
/* separator between window title and window number */
#define SEPARATOR " | "
/* printf format string for the window title, first %s 
 * is replaced by the title, second %s is replaced by
 * the SEPARATOR, %d stands for the window number */
#define TITLE "[%s%s#%d]"
/* master width factor [0.1 .. 0.9] */
#define MWFACT  0.5	

#include "tile.c"
#include "grid.c"
#include "bstack.c"
#include "fullscreen.c"

Layout layouts[] = {
	{ "[]=", tile },
	{ "+++", grid },
	{ "TTT", bstack },
	{ "[ ]", fullscreen },
};

#define MOD CTRL('g')

Key keys[] = {
	{ MOD, 'q', quit , NULL },
	{ MOD, 'c', create , "/bin/sh --login" },
	{ MOD, 'j', focusnext , NULL },
	{ MOD, 'u', focusnextnm , NULL },
	{ MOD, 'i', focusprevnm , NULL },
	{ MOD, 'k', focusprev , NULL },
	{ MOD, 't', setlayout , "[]=" },
	{ MOD, 'g', setlayout , "+++" },
	{ MOD, 'b', setlayout , "TTT" },
	{ MOD, 'f', setlayout , "[ ]" },
	{ MOD, ' ', setlayout , NULL },
	{ MOD, 'h', setmwfact , "-0.05" },
	{ MOD, 'l', setmwfact , "+0.05" },
	{ MOD, 'n', toggleminimize , NULL },
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

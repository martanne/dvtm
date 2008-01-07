/* curses attributes for the currently focused window */
/* valid curses attributes are listed below they can be ORed
 *
 * A_NORMAL        Normal display (no highlight)
 * A_STANDOUT      Best highlighting mode of the terminal.
 * A_UNDERLINE     Underlining
 * A_REVERSE       Reverse video
 * A_BLINK         Blinking
 * A_DIM           Half bright
 * A_BOLD          Extra bright or bold
 * A_PROTECT       Protected mode
 * A_INVIS         Invisible or blank mode
 * COLOR(fg,bg)    Color where fg and bg are one of:
 *
 *   COLOR_BLACK
 *   COLOR_RED
 *   COLOR_GREEN
 *   COLOR_YELLOW
 *   COLOR_BLUE
 *   COLOR_MAGENTA
 *   COLOR_CYAN
 *   COLOR_WHITE
 */
#define ATTR_SELECTED   COLOR(COLOR_RED,COLOR_BLACK)
/* curses attributes for normal (not selected) windows */
#define ATTR_NORMAL     A_NORMAL
/* status bar (command line option -s) position */
#define BARPOS		BarTop /* BarBot, BarOff */
/* curses attributes for the status bar */
#define BAR_ATTR        COLOR(COLOR_RED,COLOR_BLACK)
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
	{ MOD, 'c', create , SHELL },
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
	{ MOD, 's', togglebar, NULL },
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

/* possible values for the mouse buttons are listed below:
 *
 * BUTTON1_PRESSED          mouse button 1 down
 * BUTTON1_RELEASED         mouse button 1 up
 * BUTTON1_CLICKED          mouse button 1 clicked
 * BUTTON1_DOUBLE_CLICKED   mouse button 1 double clicked
 * BUTTON1_TRIPLE_CLICKED   mouse button 1 triple clicked
 * BUTTON2_PRESSED          mouse button 2 down
 * BUTTON2_RELEASED         mouse button 2 up
 * BUTTON2_CLICKED          mouse button 2 clicked
 * BUTTON2_DOUBLE_CLICKED   mouse button 2 double clicked
 * BUTTON2_TRIPLE_CLICKED   mouse button 2 triple clicked
 * BUTTON3_PRESSED          mouse button 3 down
 * BUTTON3_RELEASED         mouse button 3 up
 * BUTTON3_CLICKED          mouse button 3 clicked
 * BUTTON3_DOUBLE_CLICKED   mouse button 3 double clicked
 * BUTTON3_TRIPLE_CLICKED   mouse button 3 triple clicked
 * BUTTON4_PRESSED          mouse button 4 down
 * BUTTON4_RELEASED         mouse button 4 up
 * BUTTON4_CLICKED          mouse button 4 clicked
 * BUTTON4_DOUBLE_CLICKED   mouse button 4 double clicked
 * BUTTON4_TRIPLE_CLICKED   mouse button 4 triple clicked
 * BUTTON_SHIFT             shift was down during button state change
 * BUTTON_CTRL              control was down during button state change
 * BUTTON_ALT               alt was down during button state change
 * ALL_MOUSE_EVENTS         report all button state changes
 * REPORT_MOUSE_POSITION    report mouse movement
 */

Button buttons[] = {
	{ BUTTON1_CLICKED, mouse_focus, NULL },
	{ BUTTON1_DOUBLE_CLICKED, mouse_fullscreen, NULL },
	{ BUTTON1_TRIPLE_CLICKED, mouse_zoom, NULL },
	{ BUTTON2_CLICKED, mouse_minimize, NULL },
};

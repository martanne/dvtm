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
 *
 */
#define BLUE            (COLORS==256 ? 68 : COLOR_BLUE)

#define SELECTED_ATTR   A_NORMAL
#define SELECTED_FG     BLUE
#define SELECTED_BG     -1
/* curses attributes for normal (not selected) windows */
#define NORMAL_ATTR     A_NORMAL
#define NORMAL_FG       -1
#define NORMAL_BG       -1
/* status bar (command line option -s) position */
#define BAR_POS		BAR_TOP /* BAR_BOTTOM, BAR_OFF */
/* curses attributes for the status bar */
#define BAR_ATTR        A_NORMAL
#define BAR_FG          BLUE
#define BAR_BG          -1
/* determines whether the statusbar text should be right or left aligned */
#define BAR_ALIGN       ALIGN_RIGHT
/* separator between window title and window number */
#define SEPARATOR " | "
/* printf format string for the window title, first %s
 * is replaced by the title, second %s is replaced by
 * the SEPARATOR, %d stands for the window number */
#define TITLE "[%s%s#%d]"
/* master width factor [0.1 .. 0.9] */
#define MFACT 0.5
/* scroll back buffer size in lines */
#define SCROLL_HISTORY 500

#include "tile.c"
#include "grid.c"
#include "bstack.c"
#include "fullscreen.c"

/* by default the first layout entry is used */
Layout layouts[] = {
	{ "[]=", tile },
	{ "+++", grid },
	{ "TTT", bstack },
	{ "[ ]", fullscreen },
};

#define MOD CTRL('g')

/* you can at most specifiy MAX_ARGS (2) number of arguments */
Key keys[] = {
	{ MOD, 'c',       { create,         { NULL }                    } },
	{ MOD, 'x',       { killclient,     { NULL }                    } },
	{ MOD, 'j',       { focusnext,      { NULL }                    } },
	{ MOD, 'u',       { focusnextnm,    { NULL }                    } },
	{ MOD, 'i',       { focusprevnm,    { NULL }                    } },
	{ MOD, 'k',       { focusprev,      { NULL }                    } },
	{ MOD, 't',       { setlayout,      { "[]=" }                   } },
	{ MOD, 'g',       { setlayout,      { "+++" }                   } },
	{ MOD, 'b',       { setlayout,      { "TTT" }                   } },
	{ MOD, 'm',       { setlayout,      { "[ ]" }                   } },
	{ MOD, ' ',       { setlayout,      { NULL }                    } },
	{ MOD, 'h',       { setmfact,       { "-0.05" }                 } },
	{ MOD, 'l',       { setmfact,       { "+0.05" }                 } },
	{ MOD, '.',       { toggleminimize, { NULL }                    } },
	{ MOD, 's',       { togglebar,      { NULL }                    } },
	{ MOD, 'M',       { togglemouse,    { NULL }                    } },
	{ MOD, '\n',      { zoom ,          { NULL }                    } },
	{ MOD, '1',       { focusn,         { "1" }                     } },
	{ MOD, '2',       { focusn,         { "2" }                     } },
	{ MOD, '3',       { focusn,         { "3" }                     } },
	{ MOD, '4',       { focusn,         { "4" }                     } },
	{ MOD, '5',       { focusn,         { "5" }                     } },
	{ MOD, '6',       { focusn,         { "6" }                     } },
	{ MOD, '7',       { focusn,         { "7" }                     } },
	{ MOD, '8',       { focusn,         { "8" }                     } },
	{ MOD, '9',       { focusn,         { "9" }                     } },
	{ MOD, 'q',       { quit,           { NULL }                    } },
	{ MOD, 'G',       { escapekey,      { NULL }                    } },
	{ MOD, 'a',       { togglerunall,   { NULL }                    } },
	{ MOD, 'r',       { redraw,         { NULL }                    } },
	{ MOD, 'X',       { lock,           { NULL }                    } },
	{ MOD, 'B',       { togglebell,     { NULL }                    } },
	{ MOD, KEY_PPAGE, { scrollback,     { "-1" }                    } },
	{ MOD, KEY_NPAGE, { scrollback,     { "1"  }                    } },
	{ MOD, '?',       { create,         { "man dvtm", "dvtm help" } } },
};

static const ColorRule colorrules[] = {
	/* title attrs     fgcolor      bgcolor */
#if 0
	{ "ssh", A_NORMAL, COLOR_BLACK, 224      },
#endif
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

#ifdef NCURSES_MOUSE_VERSION
# define CONFIG_MOUSE /* compile in mouse support if we build against ncurses */
#endif

#define ENABLE_MOUSE true /* whether to enable mouse events by default */

#ifdef CONFIG_MOUSE
Button buttons[] = {
	{ BUTTON1_CLICKED,        { mouse_focus,      { NULL  } } },
	{ BUTTON1_DOUBLE_CLICKED, { mouse_fullscreen, { "[ ]" } } },
	{ BUTTON2_CLICKED,        { mouse_zoom,       { NULL  } } },
	{ BUTTON3_CLICKED,        { mouse_minimize,   { NULL  } } },
};
#endif /* CONFIG_MOUSE */

Cmd commands[] = {
	{ "create", { create,	{ NULL } } },
};

/* gets executed when dvtm is started */
Action actions[] = {
	{ create, { NULL } },
};

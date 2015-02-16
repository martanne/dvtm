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
 */

enum {
	DEFAULT,
	BLUE,
};

static Color colors[] = {
	[DEFAULT] = { .fg = -1,         .bg = -1, .fg256 = -1, .bg256 = -1, },
	[BLUE]    = { .fg = COLOR_BLUE, .bg = -1, .fg256 = 68, .bg256 = -1, },
};

#define COLOR(c)        COLOR_PAIR(colors[c].pair)
/* curses attributes for the currently focused window */
#define SELECTED_ATTR   (COLOR(BLUE) | A_NORMAL)
/* curses attributes for normal (not selected) windows */
#define NORMAL_ATTR     (COLOR(DEFAULT) | A_NORMAL)
/* curses attributes for a window with pending urgent flag */
#define URGENT_ATTR     NORMAL_ATTR
/* curses attributes for the status bar */
#define BAR_ATTR        (COLOR(BLUE) | A_NORMAL)
/* status bar (command line option -s) position */
#define BAR_POS         BAR_TOP /* BAR_BOTTOM, BAR_OFF */
/* whether status bar should be hidden if only one client exists */
#define BAR_AUTOHIDE    true
/* master width factor [0.1 .. 0.9] */
#define MFACT 0.5
/* number of clients in master area */
#define NMASTER 1
/* scroll back buffer size in lines */
#define SCROLL_HISTORY 500
/* printf format string for the tag in the status bar */
#define TAG_SYMBOL   "[%s]"
/* curses attributes for the currently selected tags */
#define TAG_SEL      (COLOR(BLUE) | A_BOLD)
/* curses attributes for not selected tags which contain no windows */
#define TAG_NORMAL   (COLOR(DEFAULT) | A_NORMAL)
/* curses attributes for not selected tags which contain windows */
#define TAG_OCCUPIED (COLOR(BLUE) | A_NORMAL)
/* curses attributes for not selected tags which with urgent windows */
#define TAG_URGENT (COLOR(BLUE) | A_NORMAL | A_BLINK)

const char tags[][8] = { "1", "2", "3", "4", "5" };

#include "tile.c"
#include "grid.c"
#include "bstack.c"
#include "fullscreen.c"

/* by default the first layout entry is used */
static Layout layouts[] = {
	{ "[]=", tile },
	{ "+++", grid },
	{ "TTT", bstack },
	{ "[ ]", fullscreen },
};

#define MOD  CTRL('g')

/* you can at most specifiy MAX_ARGS (3) number of arguments */
static KeyBinding bindings[] = {
	{ { MOD, 'c',          }, { create,         { NULL }                    } },
	{ { MOD, 'C',          }, { create,         { NULL, NULL, "$CWD" }      } },
	{ { MOD, 'x',          }, { killclient,     { NULL }                    } },
	{ { MOD, 'j',          }, { focusnext,      { NULL }                    } },
	{ { MOD, 'J',          }, { focusnextnm,    { NULL }                    } },
	{ { MOD, 'K',          }, { focusprevnm,    { NULL }                    } },
	{ { MOD, 'k',          }, { focusprev,      { NULL }                    } },
	{ { MOD, 'f',          }, { setlayout,      { "[]=" }                   } },
	{ { MOD, 'g',          }, { setlayout,      { "+++" }                   } },
	{ { MOD, 'b',          }, { setlayout,      { "TTT" }                   } },
	{ { MOD, 'm',          }, { setlayout,      { "[ ]" }                   } },
	{ { MOD, ' ',          }, { setlayout,      { NULL }                    } },
	{ { MOD, 'i',          }, { incnmaster,     { "+1" }                    } },
	{ { MOD, 'd',          }, { incnmaster,     { "-1" }                    } },
	{ { MOD, 'h',          }, { setmfact,       { "-0.05" }                 } },
	{ { MOD, 'l',          }, { setmfact,       { "+0.05" }                 } },
	{ { MOD, '.',          }, { toggleminimize, { NULL }                    } },
	{ { MOD, 's',          }, { togglebar,      { NULL }                    } },
	{ { MOD, 'S',          }, { togglebarpos,   { NULL }                    } },
	{ { MOD, 'M',          }, { togglemouse,    { NULL }                    } },
	{ { MOD, '\n',         }, { zoom ,          { NULL }                    } },
	{ { MOD, '\r',         }, { zoom ,          { NULL }                    } },
	{ { MOD, '1',          }, { focusn,         { "1" }                     } },
	{ { MOD, '2',          }, { focusn,         { "2" }                     } },
	{ { MOD, '3',          }, { focusn,         { "3" }                     } },
	{ { MOD, '4',          }, { focusn,         { "4" }                     } },
	{ { MOD, '5',          }, { focusn,         { "5" }                     } },
	{ { MOD, '6',          }, { focusn,         { "6" }                     } },
	{ { MOD, '7',          }, { focusn,         { "7" }                     } },
	{ { MOD, '8',          }, { focusn,         { "8" }                     } },
	{ { MOD, '9',          }, { focusn,         { "9" }                     } },
	{ { MOD, '\t',         }, { focuslast,      { NULL }                    } },
	{ { MOD, 'q',          }, { quit,           { NULL }                    } },
	{ { MOD, 'a',          }, { togglerunall,   { NULL }                    } },
	{ { MOD, CTRL('L'),    }, { redraw,         { NULL }                    } },
	{ { MOD, 'r',          }, { redraw,         { NULL }                    } },
	{ { MOD, 'e',          }, { copymode,       { NULL }                    } },
	{ { MOD, '/',          }, { copymode,       { "/" }                     } },
	{ { MOD, 'p',          }, { paste,          { NULL }                    } },
	{ { MOD, KEY_PPAGE,    }, { scrollback,     { "-1" }                    } },
	{ { MOD, KEY_NPAGE,    }, { scrollback,     { "1"  }                    } },
	{ { MOD, '?',          }, { create,         { "man dvtm", "dvtm help" } } },
	{ { MOD, MOD,          }, { send,           { (const char []){MOD, 0} } } },
	{ { KEY_SPREVIOUS,     }, { scrollback,     { "-1" }                    } },
	{ { KEY_SNEXT,         }, { scrollback,     { "1"  }                    } },
	{ { MOD, '0',          }, { view,           { NULL }                    } },
	{ { MOD, KEY_F(1),     }, { view,           { tags[0] }                 } },
	{ { MOD, KEY_F(2),     }, { view,           { tags[1] }                 } },
	{ { MOD, KEY_F(3),     }, { view,           { tags[2] }                 } },
	{ { MOD, KEY_F(4),     }, { view,           { tags[3] }                 } },
	{ { MOD, KEY_F(5),     }, { view,           { tags[4] }                 } },
	{ { MOD, 'v', '0'      }, { view,           { NULL }                    } },
	{ { MOD, 'v', '1'      }, { view,           { tags[0] }                 } },
	{ { MOD, 'v', '2'      }, { view,           { tags[1] }                 } },
	{ { MOD, 'v', '3'      }, { view,           { tags[2] }                 } },
	{ { MOD, 'v', '4'      }, { view,           { tags[3] }                 } },
	{ { MOD, 'v', '5'      }, { view,           { tags[4] }                 } },
	{ { MOD, 'v', '\t',    }, { viewprevtag,    { NULL }                    } },
	{ { MOD, 't', '0'      }, { tag,            { NULL }                    } },
	{ { MOD, 't', '1'      }, { tag,            { tags[0] }                 } },
	{ { MOD, 't', '2'      }, { tag,            { tags[1] }                 } },
	{ { MOD, 't', '3'      }, { tag,            { tags[2] }                 } },
	{ { MOD, 't', '4'      }, { tag,            { tags[3] }                 } },
	{ { MOD, 't', '5'      }, { tag,            { tags[4] }                 } },
	{ { MOD, 'V', '1'      }, { toggleview,     { tags[0] }                 } },
	{ { MOD, 'V', '2'      }, { toggleview,     { tags[1] }                 } },
	{ { MOD, 'V', '3'      }, { toggleview,     { tags[2] }                 } },
	{ { MOD, 'V', '4'      }, { toggleview,     { tags[3] }                 } },
	{ { MOD, 'V', '5'      }, { toggleview,     { tags[4] }                 } },
	{ { MOD, 'T', '1'      }, { toggletag,      { tags[0] }                 } },
	{ { MOD, 'T', '2'      }, { toggletag,      { tags[1] }                 } },
	{ { MOD, 'T', '3'      }, { toggletag,      { tags[2] }                 } },
	{ { MOD, 'T', '4'      }, { toggletag,      { tags[3] }                 } },
	{ { MOD, 'T', '5'      }, { toggletag,      { tags[4] }                 } },
};

static const ColorRule colorrules[] = {
	{ "", A_NORMAL, &colors[DEFAULT] }, /* default */
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
static Button buttons[] = {
	{ BUTTON1_CLICKED,        { mouse_focus,      { NULL  } } },
	{ BUTTON1_DOUBLE_CLICKED, { mouse_fullscreen, { "[ ]" } } },
	{ BUTTON2_CLICKED,        { mouse_zoom,       { NULL  } } },
	{ BUTTON3_CLICKED,        { mouse_minimize,   { NULL  } } },
};
#endif /* CONFIG_MOUSE */

static Cmd commands[] = {
	{ "create", { create,	{ NULL } } },
};

/* gets executed when dvtm is started */
static Action actions[] = {
	{ create, { NULL } },
};

static char const * const keytable[] = {
	/* add your custom key escape sequences */
};

/* editor to use for copy mode. If neither of DVTM_EDITOR, EDITOR and PAGER is
 * set the first entry is chosen. Otherwise the array is consulted for supported
 * options. A %d in argv is replaced by the line number at which the file should
 * be opened. If filter is true the editor is expected to work even if stdout is
 * redirected (i.e. not a terminal).
 */
static Editor editors[] = {
	{ .name = "vis",         .argv = { "vis", "+%d", "-", NULL  }, .filter = true  },
	{ .name = "sandy",       .argv = { "sandy", "-d", "-", NULL }, .filter = true  },
	{ .name = "dvtm-editor", .argv = { "dvtm-editor", "-", NULL }, .filter = true  },
	{ .name = "vim",         .argv = { "vim", "+%d", "-", NULL  }, .filter = false },
	{ .name = "less",        .argv = { "less", "+%d", NULL      }, .filter = false },
	{ .name = "more",        .argv = { "more", "+%d", NULL      }, .filter = false },
};

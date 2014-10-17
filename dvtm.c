/*
 * The initial "port" of dwm to curses was done by
 *
 * © 2007-2014 Marc André Tanner <mat at brain-dump dot org>
 *
 * It is highly inspired by the original X11 dwm and
 * reuses some code of it which is mostly
 *
 * © 2006-2007 Anselm R. Garbe <garbeam at gmail dot com>
 *
 * See LICENSE for details.
 */

#include <sys/select.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <curses.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <pwd.h>
#if defined __CYGWIN__ || defined __sun
# include <termios.h>
#endif
#include "vt.h"

#ifdef PDCURSES
int ESCDELAY;
#endif

#ifndef NCURSES_REENTRANT
# define set_escdelay(d) (ESCDELAY = (d))
#endif

typedef struct {
	float mfact;
	int history;
	int w;
	int h;
	bool need_resize;
} Screen;

typedef struct {
	const char *symbol;
	void (*arrange)(void);
} Layout;

typedef struct Client Client;
struct Client {
	WINDOW *window;
	Vt *term;
	Vt *editor, *app;
	int editor_fds[2];
	volatile sig_atomic_t editor_died;
	const char *cmd;
	char title[255];
	int order;
	pid_t pid;
	unsigned short int id;
	unsigned short int x;
	unsigned short int y;
	unsigned short int w;
	unsigned short int h;
	bool has_title_line;
	bool minimized;
	volatile sig_atomic_t died;
	Client *next;
	Client *prev;
};

typedef struct {
	short fg;
	short bg;
	short fg256;
	short bg256;
	short pair;
} Color;

typedef struct {
	const char *title;
	attr_t attrs;
	Color *color;
} ColorRule;

#define ALT(k)      ((k) + (161 - 'a'))
#if defined CTRL && defined _AIX
  #undef CTRL
#endif
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

#define MAX_ARGS 3

typedef struct {
	void (*cmd)(const char *args[]);
	/* needed to avoid an error about initialization
	 * of nested flexible array members */
	const char *args[MAX_ARGS];
} Action;

#define MAX_KEYS 3

typedef unsigned int KeyCombo[MAX_KEYS];

typedef struct {
	KeyCombo keys;
	Action action;
} KeyBinding;

typedef struct {
	mmask_t mask;
	Action action;
} Button;

typedef struct {
	const char *name;
	Action action;
} Cmd;

enum { BAR_TOP, BAR_BOTTOM, BAR_OFF };
enum { ALIGN_LEFT, ALIGN_RIGHT };

typedef struct {
	int fd;
	int pos;
	unsigned short int h;
	unsigned short int y;
	char text[512];
	const char *file;
} StatusBar;

typedef struct {
	int fd;
	const char *file;
	unsigned short int id;
} CmdFifo;

typedef struct {
	char *data;
	size_t len;
	size_t size;
} Register;

#define countof(arr) (sizeof(arr) / sizeof((arr)[0]))
#define sstrlen(str) (sizeof(str) - 1)
#define max(x, y) ((x) > (y) ? (x) : (y))

#ifdef NDEBUG
 #define debug(format, args...)
#else
 #define debug eprint
#endif

/* commands for use by keybindings */
static void create(const char *args[]);
static void copymode(const char *args[]);
static void focusn(const char *args[]);
static void focusnext(const char *args[]);
static void focusnextnm(const char *args[]);
static void focusprev(const char *args[]);
static void focusprevnm(const char *args[]);
static void focuslast(const char *args[]);
static void killclient(const char *args[]);
static void paste(const char *args[]);
static void quit(const char *args[]);
static void redraw(const char *args[]);
static void scrollback(const char *args[]);
static void send(const char *args[]);
static void setlayout(const char *args[]);
static void setmfact(const char *args[]);
static void startup(const char *args[]);
static void togglebar(const char *args[]);
static void togglebell(const char *key[]);
static void toggleminimize(const char *args[]);
static void togglemouse(const char *args[]);
static void togglerunall(const char *args[]);
static void zoom(const char *args[]);

/* commands for use by mouse bindings */
static void mouse_focus(const char *args[]);
static void mouse_fullscreen(const char *args[]);
static void mouse_minimize(const char *args[]);
static void mouse_zoom(const char *args[]);

/* functions and variables available to layouts via config.h */
static void resize(Client *c, int x, int y, int w, int h);
extern Screen screen;
static unsigned int waw, wah, wax, way;
static Client *clients = NULL;
static char *title;
#define COLOR(c) COLOR_PAIR(colors[c].pair)
#define NOMOD ERR

#include "config.h"

/* global variables */
static const char *dvtm_name = "dvtm";
Screen screen = { MFACT, SCROLL_HISTORY };
static Client *sel = NULL;
static Client *lastsel = NULL;
static Client *msel = NULL;
static bool mouse_events_enabled = ENABLE_MOUSE;
static Layout *layout = layouts;
static StatusBar bar = { -1, BAR_POS, 1 };
static CmdFifo cmdfifo = { -1 };
static const char *shell;
static Register copyreg;
static volatile sig_atomic_t running = true;
static bool runinall = false;

static void
eprint(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
}

static void
error(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static bool
isarrange(void (*func)()) {
	return func == layout->arrange;
}

static bool
is_content_visible(Client *c) {
	if (!c)
		return false;
	if (isarrange(fullscreen))
		return sel == c;
	return !c->minimized;
}

static void
drawbar(void) {
	wchar_t wbuf[sizeof bar.text];
	int x, y, w, maxwidth = screen.w - 2;
	if (bar.pos == BAR_OFF)
		return;
	getyx(stdscr, y, x);
	attrset(BAR_ATTR);
	mvaddch(bar.y, 0, '[');
	if (mbstowcs(wbuf, bar.text, sizeof bar.text) == (size_t)-1)
		return;
	if ((w = wcswidth(wbuf, maxwidth)) == -1)
		return;
	if (BAR_ALIGN == ALIGN_RIGHT) {
		for (int i = 0; i + w < maxwidth; i++)
			addch(' ');
	}
	addstr(bar.text);
	if (BAR_ALIGN == ALIGN_LEFT) {
		for (; w < maxwidth; w++)
			addch(' ');
	}
	mvaddch(bar.y, screen.w - 1, ']');
	attrset(NORMAL_ATTR);
	move(y, x);
	wnoutrefresh(stdscr);
}

static int
show_border(void) {
	return (bar.fd != -1 && bar.pos != BAR_OFF) || (clients && clients->next);
}

static void
draw_border(Client *c) {
	if (!show_border())
		return;
	char t = '\0';
	int x, y, maxlen;

	wattrset(c->window, (sel == c || (runinall && !c->minimized)) ? SELECTED_ATTR : NORMAL_ATTR);
	getyx(c->window, y, x);
	mvwhline(c->window, 0, 0, ACS_HLINE, c->w);
	maxlen = c->w - (2 + sstrlen(TITLE) - sstrlen("%s%sd")  + sstrlen(SEPARATOR) + 2);
	if (maxlen < 0)
		maxlen = 0;
	if ((size_t)maxlen < sizeof(c->title)) {
		t = c->title[maxlen];
		c->title[maxlen] = '\0';
	}

	mvwprintw(c->window, 0, 2, TITLE,
	          *c->title ? c->title : "",
	          *c->title ? SEPARATOR : "",
	          c->order);
	if (t)
		c->title[maxlen] = t;
	wmove(c->window, y, x);
}

static void
draw_content(Client *c) {
	vt_draw(c->term, c->window, c->has_title_line, 0);
}

static void
draw(Client *c) {
	if (is_content_visible(c)) {
		redrawwin(c->window);
		draw_content(c);
	}
	if (!isarrange(fullscreen) || sel == c)
		draw_border(c);
	wnoutrefresh(c->window);
}

static void
draw_all(void) {
	if (!isarrange(fullscreen)) {
		for (Client *c = clients; c; c = c->next) {
			if (c == sel)
				continue;
			draw(c);
		}
	}
	/* as a last step the selected window is redrawn,
	 * this has the effect that the cursor position is
	 * accurate
	 */
	if (sel)
		draw(sel);
}

static void
arrange(void) {
	int m = 0;
	for (Client *c = clients; c; c = c->next)
		if (c->minimized)
			m++;
	erase();
	drawbar();
	attrset(NORMAL_ATTR);
	if (m && !isarrange(fullscreen))
		wah--;
	layout->arrange();
	if (m && !isarrange(fullscreen)) {
		int nw = waw / m, nx = wax;
		for (Client *c = clients; c; c = c->next) {
			if (c->minimized) {
				resize(c, nx, way+wah, nw, 1);
				nx += nw;
			}
		}
		wah++;
	}
	wnoutrefresh(stdscr);
	draw_all();
}

static void
attach(Client *c) {
	if (clients)
		clients->prev = c;
	c->next = clients;
	c->prev = NULL;
	clients = c;
	for (int o = 1; c; c = c->next, o++)
		c->order = o;
}

static void
attachafter(Client *c, Client *a) { /* attach c after a */
	if (c == a)
		return;
	if (!a)
		for (a = clients; a && a->next; a = a->next);

	if (a) {
		if (a->next)
			a->next->prev = c;
		c->next = a->next;
		c->prev = a;
		a->next = c;
		for (int o = a->order; c; c = c->next)
			c->order = ++o;
	}
}

static void
detach(Client *c) {
	Client *d;
	if (c->prev)
		c->prev->next = c->next;
	if (c->next) {
		c->next->prev = c->prev;
		for (d = c->next; d; d = d->next)
			--d->order;
	}
	if (c == clients)
		clients = c->next;
	c->next = c->prev = NULL;
}

static void
settitle(Client *c) {
	char *term, *t = title;
	if (!t && sel == c && *c->title)
		t = c->title;
	if (t && (term = getenv("TERM")) && !strstr(term, "linux"))
		printf("\033]0;%s\007", t);
}

static void
focus(Client *c) {
	Client *tmp = sel;
	if (sel == c)
		return;
	lastsel = sel;
	sel = c;
	settitle(c);
	if (tmp && !isarrange(fullscreen)) {
		draw_border(tmp);
		wnoutrefresh(tmp->window);
	}
	if (isarrange(fullscreen)) {
		draw(c);
	} else {
		draw_border(c);
		wnoutrefresh(c->window);
	}
	curs_set(!c->minimized && vt_cursor(c->term));
}

static void
applycolorrules(Client *c) {
	const ColorRule *r = colorrules;
	short fg = r->color->fg, bg = r->color->bg;
	attr_t attrs = r->attrs;

	for (unsigned int i = 1; i < countof(colorrules); i++) {
		r = &colorrules[i];
		if (strstr(c->title, r->title)) {
			attrs = r->attrs;
			fg = r->color->fg;
			bg = r->color->bg;
			break;
		}
	}

	vt_set_default_colors(c->term, attrs, fg, bg);
}

static void
term_event_handler(Vt *term, int event, void *event_data) {
	Client *c = (Client *)vt_get_data(term);
	switch (event) {
	case VT_EVENT_TITLE:
		if (event_data)
			strncpy(c->title, event_data, sizeof(c->title) - 1);
		c->title[event_data ? sizeof(c->title) - 1 : 0] = '\0';
		settitle(c);
		if (!isarrange(fullscreen) || sel == c)
			draw_border(c);
		applycolorrules(c);
		break;
	}
}

static void
move_client(Client *c, int x, int y) {
	if (c->x == x && c->y == y)
		return;
	debug("moving, x: %d y: %d\n", x, y);
	if (mvwin(c->window, y, x) == ERR) {
		eprint("error moving, x: %d y: %d\n", x, y);
	} else {
		c->x = x;
		c->y = y;
	}
}

static void
resize_client(Client *c, int w, int h) {
	bool has_title_line = show_border();
	bool resize_window = c->w != w || c->h != h;
	if (resize_window) {
		debug("resizing, w: %d h: %d\n", w, h);
		if (wresize(c->window, h, w) == ERR) {
			eprint("error resizing, w: %d h: %d\n", w, h);
		} else {
			c->w = w;
			c->h = h;
		}
	}
	if (resize_window || c->has_title_line != has_title_line) {
		c->has_title_line = has_title_line;
		vt_resize(c->app, h - has_title_line, w);
		if (c->editor)
			vt_resize(c->editor, h - has_title_line, w);
	}
}

static void
resize(Client *c, int x, int y, int w, int h) {
	resize_client(c, w, h);
	move_client(c, x, y);
}

static Client*
get_client_by_coord(unsigned int x, unsigned int y) {
	if (y < way || y >= wah)
		return NULL;
	if (isarrange(fullscreen))
		return sel;
	for (Client *c = clients; c; c = c->next) {
		if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) {
			debug("mouse event, x: %d y: %d client: %d\n", x, y, c->order);
			return c;
		}
	}
	return NULL;
}

static void
sigchld_handler(int sig) {
	int errsv = errno;
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
		if (pid == -1) {
			if (errno == ECHILD) {
				/* no more child processes */
				break;
			}
			eprint("waitpid: %s\n", strerror(errno));
			break;
		}

		debug("child with pid %d died\n", pid);

		for (Client *c = clients; c; c = c->next) {
			if (c->pid == pid) {
				c->died = true;
				break;
			}
			if (c->editor && vt_pid_get(c->editor) == pid) {
				c->editor_died = true;
				break;
			}
		}
	}

	errno = errsv;
}

static void
sigwinch_handler(int sig) {
	screen.need_resize = true;
}

static void
sigterm_handler(int sig) {
	running = false;
}

static void
updatebarpos(void) {
	bar.y = 0;
	wax = 0;
	way = 0;
	wah = screen.h;
	waw = screen.w;
	if (bar.fd == -1)
		return;
	if (bar.pos == BAR_TOP) {
		wah -= bar.h;
		way += bar.h;
	} else if (bar.pos == BAR_BOTTOM) {
		wah -= bar.h;
		bar.y = wah;
	}
}

static void
resize_screen(void) {
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) == -1) {
		getmaxyx(stdscr, screen.h, screen.w);
	} else {
		screen.w = ws.ws_col;
		screen.h = ws.ws_row;
	}

	debug("resize_screen(), w: %d h: %d\n", screen.w, screen.h);

	resizeterm(screen.h, screen.w);
	wresize(stdscr, screen.h, screen.w);
	updatebarpos();
	clear();
	arrange();
}

static KeyBinding*
keybinding(KeyCombo keys) {
	unsigned int keycount = 0;
	while (keycount < MAX_KEYS && keys[keycount])
		keycount++;
	for (unsigned int b = 0; b < countof(bindings); b++) {
		for (unsigned int k = 0; k < keycount; k++) {
			if (keys[k] != bindings[b].keys[k])
				break;
			if (k == keycount - 1)
				return &bindings[b];
		}
	}
	return NULL;
}

static void
keypress(int code) {
	unsigned int len = 1;
	char buf[8] = { '\e' };

	if (code == '\e') {
		/* pass characters following escape to the underlying app */
		nodelay(stdscr, TRUE);
		for (int t; len < sizeof(buf) && (t = getch()) != ERR; len++)
			buf[len] = t;
		nodelay(stdscr, FALSE);
	}

	for (Client *c = runinall ? clients : sel; c; c = c->next) {
		if (is_content_visible(c)) {
			if (code == '\e')
				vt_write(c->term, buf, len);
			else
				vt_keypress(c->term, code);
		}
		if (!runinall)
			break;
	}
}

static void
mouse_setup(void) {
#ifdef CONFIG_MOUSE
	mmask_t mask = 0;

	if (mouse_events_enabled) {
		mask = BUTTON1_CLICKED | BUTTON2_CLICKED;
		for (unsigned int i = 0; i < countof(buttons); i++)
			mask |= buttons[i].mask;
	}
	mousemask(mask, NULL);
#endif /* CONFIG_MOUSE */
}

static bool
checkshell(const char *shell) {
	if (shell == NULL || *shell == '\0' || *shell != '/')
		return false;
	if (!strcmp(strrchr(shell, '/')+1, dvtm_name))
		return false;
	if (access(shell, X_OK))
		return false;
	return true;
}

static const char *
getshell(void) {
	const char *shell = getenv("SHELL");
	struct passwd *pw;

	if (checkshell(shell))
		return shell;
	if ((pw = getpwuid(getuid())) && checkshell(pw->pw_shell))
		return pw->pw_shell;
	return "/bin/sh";
}

static void
setup(void) {
	shell = getshell();
	setlocale(LC_CTYPE, "");
	initscr();
	start_color();
	noecho();
	keypad(stdscr, TRUE);
	mouse_setup();
	raw();
	vt_init();
	vt_set_keytable(keytable, countof(keytable));
	for (unsigned int i = 0; i < countof(colors); i++) {
		if (COLORS == 256) {
			if (colors[i].fg256)
				colors[i].fg = colors[i].fg256;
			if (colors[i].bg256)
				colors[i].bg = colors[i].bg256;
		}
		colors[i].pair = vt_color_reserve(colors[i].fg, colors[i].bg);
	}
	resize_screen();
	struct sigaction sa;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigwinch_handler;
	sigaction(SIGWINCH, &sa, NULL);
	sa.sa_handler = sigchld_handler;
	sigaction(SIGCHLD, &sa, NULL);
	sa.sa_handler = sigterm_handler;
	sigaction(SIGTERM, &sa, NULL);
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
}

static void
destroy(Client *c) {
	if (sel == c)
		focusnextnm(NULL);
	detach(c);
	if (sel == c) {
		if (clients) {
			focus(clients);
			toggleminimize(NULL);
		} else {
			sel = NULL;
		}
	}
	if (lastsel == c)
		lastsel = NULL;
	werase(c->window);
	wnoutrefresh(c->window);
	vt_destroy(c->term);
	delwin(c->window);
	if (!clients && countof(actions)) {
		if (!strcmp(c->cmd, shell))
			quit(NULL);
		else
			create(NULL);
	}
	free(c);
	arrange();
}

static void
cleanup(void) {
	while (clients)
		destroy(clients);
	vt_shutdown();
	endwin();
	free(copyreg.data);
	if (bar.fd > 0)
		close(bar.fd);
	if (bar.file)
		unlink(bar.file);
	if (cmdfifo.fd > 0)
		close(cmdfifo.fd);
	if (cmdfifo.file)
		unlink(cmdfifo.file);
}

static char *getcwd_by_pid(Client *c) {
	if (!c)
		return NULL;
	char buf[32];
	snprintf(buf, sizeof buf, "/proc/%d/cwd", c->pid);
	return realpath(buf, NULL);
}

/* commands for use by keybindings */
static void
create(const char *args[]) {
	Client *c = calloc(1, sizeof(Client));
	if (!c)
		return;
	const char *cmd = (args && args[0]) ? args[0] : shell;
	const char *pargs[] = { "/bin/sh", "-c", cmd, NULL };
	c->id = ++cmdfifo.id;
	char buf[8], *cwd = NULL;
	snprintf(buf, sizeof buf, "%d", c->id);
	const char *env[] = {
		"DVTM", VERSION,
		"DVTM_WINDOW_ID", buf,
		NULL
	};

	if (!(c->window = newwin(wah, waw, way, wax))) {
		free(c);
		return;
	}

	c->has_title_line = show_border();
	c->term = c->app = vt_create(screen.h - c->has_title_line, screen.w, screen.history);
	if (!c->term) {
		delwin(c->window);
		free(c);
		return;
	}

	c->cmd = cmd;
	if (args && args[1]) {
		strncpy(c->title, args[1], sizeof(c->title) - 1);
		c->title[sizeof(c->title) - 1] = '\0';
	}
	if (args && args[2])
		cwd = !strcmp(args[2], "$CWD") ? getcwd_by_pid(sel) : (char*)args[2];
	c->pid = vt_forkpty(c->term, "/bin/sh", pargs, cwd, env, NULL, NULL);
	if (args && args[2] && !strcmp(args[2], "$CWD"))
		free(cwd);
	vt_set_data(c->term, c);
	vt_set_event_handler(c->term, term_event_handler);
	c->w = screen.w;
	c->h = screen.h;
	c->x = wax;
	c->y = way;
	c->order = 0;
	c->minimized = false;
	debug("client with pid %d forked\n", c->pid);
	attach(c);
	focus(c);
	arrange();
}

static void
copymode(const char *args[]) {
	if (!sel || sel->editor)
		return;
	if (!(sel->editor = vt_create(sel->h, sel->w, 0)))
		return;

	char *ed = getenv("DVTM_EDITOR");
	const char **argv = NULL;
	if (!ed && !(ed = getenv("EDITOR"))) {
		ed = editor;
		argv = editor_args;
	}
	if (!argv)
		argv = (const char*[]){ ed, "-", NULL };

	const char *cwd = NULL;
	const char *env[] = { "DVTM", VERSION, NULL };
	int *to = &sel->editor_fds[0], *from = &sel->editor_fds[1];

	if (vt_forkpty(sel->editor, ed, argv, cwd, env, to, from) < 0) {
		vt_destroy(sel->editor);
		sel->editor = NULL;
		return;
	}

	sel->term = sel->editor;

	if (sel->editor_fds[0] != -1) {
		char *buf = NULL;
		size_t len = vt_content_get(sel->app, &buf);
		char *cur = buf;
		while (len > 0) {
			ssize_t res = write(sel->editor_fds[0], cur, len);
			if (res < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				break;
			}
			cur += res;
			len -= res;
		}
		free(buf);
		close(sel->editor_fds[0]);
	}

	if (args[0])
		vt_write(sel->editor, args[0], strlen(args[0]));
}

static void
focusn(const char *args[]) {
	for (Client *c = clients; c; c = c->next) {
		if (c->order == atoi(args[0])) {
			focus(c);
			if (c->minimized)
				toggleminimize(NULL);
			return;
		}
	}
}

static void
focusnext(const char *args[]) {
	if (!sel)
		return;
	Client *c = sel->next;
	if (!c)
		c = clients;
	if (c)
		focus(c);
}

static void
focusnextnm(const char *args[]) {
	if (!sel)
		return;
	Client *c = sel;
	do {
		c = c->next;
		if (!c)
			c = clients;
	} while (c->minimized && c != sel);
	focus(c);
}

static void
focusprev(const char *args[]) {
	if (!sel)
		return;
	Client *c = sel->prev;
	if (!c)
		for (c = clients; c && c->next; c = c->next);
	if (c)
		focus(c);
}

static void
focusprevnm(const char *args[]) {
	if (!sel)
		return;
	Client *c = sel;
	do {
		c = c->prev;
		if (!c)
			for (c = clients; c && c->next; c = c->next);
	} while (c->minimized && c != sel);
	focus(c);
}

static void
focuslast(const char *args[]) {
	if (lastsel)
		focus(lastsel);
}

static void
killclient(const char *args[]) {
	if (!sel)
		return;
	debug("killing client with pid: %d\n", sel->pid);
	kill(-sel->pid, SIGKILL);
}

static void
paste(const char *args[]) {
	if (sel && copyreg.data)
		vt_write(sel->term, copyreg.data, copyreg.len);
}

static void
quit(const char *args[]) {
	cleanup();
	exit(EXIT_SUCCESS);
}

static void
redraw(const char *args[]) {
	for (Client *c = clients; c; c = c->next) {
		if (!c->minimized) {
			vt_dirty(c->term);
			wclear(c->window);
			wnoutrefresh(c->window);
		}
	}
	resize_screen();
}

static void
scrollback(const char *args[]) {
	if (!is_content_visible(sel))
		return;

	if (!args[0] || atoi(args[0]) < 0)
		vt_scroll(sel->term, -sel->h/2);
	else
		vt_scroll(sel->term,  sel->h/2);

	draw(sel);
	curs_set(vt_cursor(sel->term));
}

static void
send(const char *args[]) {
	if (sel && args && args[0])
		vt_write(sel->term, args[0], strlen(args[0]));
}

static void
setlayout(const char *args[]) {
	unsigned int i;

	if (!args || !args[0]) {
		if (++layout == &layouts[countof(layouts)])
			layout = &layouts[0];
	} else {
		for (i = 0; i < countof(layouts); i++)
			if (!strcmp(args[0], layouts[i].symbol))
				break;
		if (i == countof(layouts))
			return;
		layout = &layouts[i];
	}
	arrange();
}

static void
setmfact(const char *args[]) {
	float delta;

	if (isarrange(fullscreen) || isarrange(grid))
		return;
	/* arg handling, manipulate mfact */
	if (args[0] == NULL) {
		screen.mfact = MFACT;
	} else if (1 == sscanf(args[0], "%f", &delta)) {
		if (args[0][0] == '+' || args[0][0] == '-')
			screen.mfact += delta;
		else
			screen.mfact = delta;
		if (screen.mfact < 0.1)
			screen.mfact = 0.1;
		else if (screen.mfact > 0.9)
			screen.mfact = 0.9;
	}
	arrange();
}

static void
startup(const char *args[]) {
	for (unsigned int i = 0; i < countof(actions); i++)
		actions[i].cmd(actions[i].args);
}

static void
togglebar(const char *args[]) {
	if (bar.pos == BAR_OFF)
		bar.pos = (BAR_POS == BAR_OFF) ? BAR_TOP : BAR_POS;
	else
		bar.pos = BAR_OFF;
	updatebarpos();
	redraw(NULL);
}

static void
togglebell(const char *args[]) {
	vt_togglebell(sel->term);
}

static void
toggleminimize(const char *args[]) {
	Client *c, *m;
	unsigned int n;
	if (!sel)
		return;
	/* the last window can't be minimized */
	if (!sel->minimized) {
		for (n = 0, c = clients; c; c = c->next)
			if (!c->minimized)
				n++;
		if (n == 1)
			return;
	}
	sel->minimized = !sel->minimized;
	m = sel;
	/* check whether the master client was minimized */
	if (sel == clients && sel->minimized) {
		c = sel->next;
		detach(c);
		attach(c);
		focus(c);
		detach(m);
		for (; c && c->next && !c->next->minimized; c = c->next);
		attachafter(m, c);
	} else if (m->minimized) {
		/* non master window got minimized move it above all other
		 * minimized ones */
		focusnextnm(NULL);
		detach(m);
		for (c = clients; c && c->next && !c->next->minimized; c = c->next);
		attachafter(m, c);
	} else { /* window is no longer minimized, move it to the master area */
		vt_dirty(m->term);
		detach(m);
		attach(m);
	}
	arrange();
}

static void
togglemouse(const char *args[]) {
	mouse_events_enabled = !mouse_events_enabled;
	mouse_setup();
}

static void
togglerunall(const char *args[]) {
	runinall = !runinall;
	draw_all();
}

static void
zoom(const char *args[]) {
	Client *c;

	if (!sel)
		return;
	if (args && args[0])
		focusn(args);
	if ((c = sel) == clients)
		if (!(c = c->next))
			return;
	detach(c);
	attach(c);
	focus(c);
	if (c->minimized)
		toggleminimize(NULL);
	arrange();
}

/* commands for use by mouse bindings */
static void
mouse_focus(const char *args[]) {
	focus(msel);
	if (msel->minimized)
		toggleminimize(NULL);
}

static void
mouse_fullscreen(const char *args[]) {
	mouse_focus(NULL);
	if (isarrange(fullscreen))
		setlayout(NULL);
	else
		setlayout(args);
}

static void
mouse_minimize(const char *args[]) {
	focus(msel);
	toggleminimize(NULL);
}

static void
mouse_zoom(const char *args[]) {
	focus(msel);
	zoom(NULL);
}

static Cmd *
get_cmd_by_name(const char *name) {
	for (unsigned int i = 0; i < countof(commands); i++) {
		if (!strcmp(name, commands[i].name))
			return &commands[i];
	}
	return NULL;
}

static void
handle_cmdfifo(void) {
	int r;
	char *p, *s, cmdbuf[512], c;
	Cmd *cmd;
	switch (r = read(cmdfifo.fd, cmdbuf, sizeof cmdbuf - 1)) {
	case -1:
	case 0:
		cmdfifo.fd = -1;
		break;
	default:
		cmdbuf[r] = '\0';
		p = cmdbuf;
		while (*p) {
			/* find the command name */
			for (; *p == ' ' || *p == '\n'; p++);
			for (s = p; *p && *p != ' ' && *p != '\n'; p++);
			if ((c = *p))
				*p++ = '\0';
			if (*s && (cmd = get_cmd_by_name(s)) != NULL) {
				bool quote = false;
				int argc = 0;
				const char *args[MAX_ARGS], *arg;
				memset(args, 0, sizeof(args));
				/* if arguments were specified in config.h ignore the one given via
				 * the named pipe and thus skip everything until we find a new line
				 */
				if (cmd->action.args[0] || c == '\n') {
					debug("execute %s", s);
					cmd->action.cmd(cmd->action.args);
					while (*p && *p != '\n')
						p++;
					continue;
				}
				/* no arguments were given in config.h so we parse the command line */
				while (*p == ' ')
					p++;
				arg = p;
				for (; (c = *p); p++) {
					switch (*p) {
					case '\\':
						/* remove the escape character '\\' move every
						 * following character to the left by one position
						 */
						switch (p[1]) {
							case '\\':
							case '\'':
							case '\"': {
								char *t = p+1;
								do {
									t[-1] = *t;
								} while (*t++);
							}
						}
						break;
					case '\'':
					case '\"':
						quote = !quote;
						break;
					case ' ':
						if (!quote) {
					case '\n':
							/* remove trailing quote if there is one */
							if (*(p - 1) == '\'' || *(p - 1) == '\"')
								*(p - 1) = '\0';
							*p++ = '\0';
							/* remove leading quote if there is one */
							if (*arg == '\'' || *arg == '\"')
								arg++;
							if (argc < MAX_ARGS)
								args[argc++] = arg;

							while (*p == ' ')
								++p;
							arg = p--;
						}
						break;
					}

					if (c == '\n' || *p == '\n') {
						if (!*p)
							p++;
						debug("execute %s", s);
						for(int i = 0; i < argc; i++)
							debug(" %s", args[i]);
						debug("\n");
						cmd->action.cmd(args);
						break;
					}
				}
			}
		}
	}
}

static void
handle_mouse(void) {
#ifdef CONFIG_MOUSE
	MEVENT event;
	unsigned int i;
	if (getmouse(&event) != OK)
		return;
	msel = get_client_by_coord(event.x, event.y);

	if (!msel)
		return;

	debug("mouse x:%d y:%d cx:%d cy:%d mask:%d\n", event.x, event.y, event.x - msel->x, event.y - msel->y, event.bstate);

	vt_mouse(msel->term, event.x - msel->x, event.y - msel->y, event.bstate);

	for (i = 0; i < countof(buttons); i++) {
		if (event.bstate & buttons[i].mask)
			buttons[i].action.cmd(buttons[i].action.args);
	}

	msel = NULL;
#endif /* CONFIG_MOUSE */
}

static void
handle_statusbar(void) {
	char *p;
	int r;
	switch (r = read(bar.fd, bar.text, sizeof bar.text - 1)) {
		case -1:
			strncpy(bar.text, strerror(errno), sizeof bar.text - 1);
			bar.text[sizeof bar.text - 1] = '\0';
			bar.fd = -1;
			break;
		case 0:
			bar.fd = -1;
			break;
		default:
			bar.text[r] = '\0';
			p = bar.text + r - 1;
			for (; p >= bar.text && *p == '\n'; *p-- = '\0');
			for (; p >= bar.text && *p != '\n'; --p);
			if (p >= bar.text)
				memmove(bar.text, p + 1, strlen(p));
			drawbar();
	}
}

static void
handle_editor(Client *c) {
	if (!copyreg.data && (copyreg.data = malloc(screen.history)))
		copyreg.size = screen.history;
	copyreg.len = 0;
	while (copyreg.len < copyreg.size) {
		ssize_t len = read(c->editor_fds[1], copyreg.data + copyreg.len, copyreg.size - copyreg.len);
		if (len == -1) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (len == 0)
			break;
		copyreg.len += len;
		if (copyreg.len == copyreg.size) {
			copyreg.size *= 2;
			if (!(copyreg.data = realloc(copyreg.data, copyreg.size))) {
				copyreg.size = 0;
				copyreg.len = 0;
			}
		}
	}
	c->editor_died = false;
	vt_destroy(c->editor);
	c->editor = NULL;
	c->term = c->app;
	vt_dirty(c->term);
	draw_content(c);
	wnoutrefresh(c->window);
}

static int
open_or_create_fifo(const char *name, const char **name_created) {
	struct stat info;
	int fd;

	do {
		if ((fd = open(name, O_RDWR|O_NONBLOCK)) == -1) {
			if (errno == ENOENT && !mkfifo(name, S_IRUSR|S_IWUSR)) {
				*name_created = name;
				continue;
			}
			error("%s\n", strerror(errno));
		}
	} while (fd == -1);

	if (fstat(fd, &info) == -1)
		error("%s\n", strerror(errno));
	if (!S_ISFIFO(info.st_mode))
		error("%s is not a named pipe\n", name);
	return fd;
}

static void
usage(void) {
	cleanup();
	eprint("usage: dvtm [-v] [-M] [-m mod] [-d delay] [-h lines] [-t title] "
	       "[-s status-fifo] [-c cmd-fifo] [cmd...]\n");
	exit(EXIT_FAILURE);
}

static bool
parse_args(int argc, char *argv[]) {
	bool init = false;
	const char *name = argv[0];

	if (name && (name = strrchr(name, '/')))
		dvtm_name = name + 1;
	if (!getenv("ESCDELAY"))
		set_escdelay(100);
	for (int arg = 1; arg < argc; arg++) {
		if (argv[arg][0] != '-') {
			const char *args[] = { argv[arg], NULL, NULL };
			if (!init) {
				setup();
				init = true;
			}
			create(args);
			continue;
		}
		if (argv[arg][1] != 'v' && argv[arg][1] != 'M' && (arg + 1) >= argc)
			usage();
		switch (argv[arg][1]) {
			case 'v':
				puts("dvtm-"VERSION" © 2007-2014 Marc André Tanner");
				exit(EXIT_SUCCESS);
			case 'M':
				mouse_events_enabled = !mouse_events_enabled;
				break;
			case 'm': {
				char *mod = argv[++arg];
				if (mod[0] == '^' && mod[1])
					*mod = CTRL(mod[1]);
				for (unsigned int b = 0; b < countof(bindings); b++)
					if (bindings[b].keys[0] == MOD)
						bindings[b].keys[0] = *mod;
				break;
			}
			case 'd':
				set_escdelay(atoi(argv[++arg]));
				if (ESCDELAY < 50)
					set_escdelay(50);
				else if (ESCDELAY > 1000)
					set_escdelay(1000);
				break;
			case 'h':
				screen.history = atoi(argv[++arg]);
				break;
			case 't':
				title = argv[++arg];
				break;
			case 's':
				bar.fd = open_or_create_fifo(argv[++arg], &bar.file);
				updatebarpos();
				break;
			case 'c': {
				const char *fifo;
				cmdfifo.fd = open_or_create_fifo(argv[++arg], &cmdfifo.file);
				if (!(fifo = realpath(argv[arg], NULL)))
					error("%s\n", strerror(errno));
				setenv("DVTM_CMD_FIFO", fifo, 1);
				break;
			}
			default:
				usage();
		}
	}
	return init;
}

int
main(int argc, char *argv[]) {
	KeyCombo keys;
	unsigned int key_index = 0;
	memset(keys, 0, sizeof(keys));
	sigset_t emptyset, blockset;

	if (!parse_args(argc, argv)) {
		setup();
		startup(NULL);
	}

	sigemptyset(&emptyset);
	sigemptyset(&blockset);
	sigaddset(&blockset, SIGWINCH);
	sigaddset(&blockset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &blockset, NULL);

	while (running) {
		int r, nfds = 0;
		fd_set rd;

		if (screen.need_resize) {
			resize_screen();
			screen.need_resize = false;
		}

		FD_ZERO(&rd);
		FD_SET(STDIN_FILENO, &rd);

		if (cmdfifo.fd != -1) {
			FD_SET(cmdfifo.fd, &rd);
			nfds = cmdfifo.fd;
		}

		if (bar.fd != -1) {
			FD_SET(bar.fd, &rd);
			nfds = max(nfds, bar.fd);
		}

		for (Client *c = clients; c; ) {
			if (c->editor && c->editor_died)
				handle_editor(c);
			if (!c->editor && c->died) {
				Client *t = c->next;
				destroy(c);
				c = t;
				continue;
			}
			int pty = c->editor ? vt_getpty(c->editor) : vt_getpty(c->app);
			FD_SET(pty, &rd);
			nfds = max(nfds, pty);
			c = c->next;
		}

		doupdate();
		r = pselect(nfds + 1, &rd, NULL, NULL, NULL, &emptyset);

		if (r == -1 && errno == EINTR)
			continue;

		if (r < 0) {
			perror("select()");
			exit(EXIT_FAILURE);
		}

		if (FD_ISSET(STDIN_FILENO, &rd)) {
			int code = getch();
			if (code >= 0) {
				keys[key_index++] = code;
				KeyBinding *binding = NULL;
				if (code == KEY_MOUSE) {
					handle_mouse();
				} else if ((binding = keybinding(keys))) {
					unsigned int key_length = 0;
					while (key_length < MAX_KEYS && binding->keys[key_length])
						key_length++;
					if (key_index == key_length) {
						binding->action.cmd(binding->action.args);
						key_index = 0;
						memset(keys, 0, sizeof(keys));
					}
				} else {
					key_index = 0;
					memset(keys, 0, sizeof(keys));
					keypress(code);
				}
			}
			if (r == 1) /* no data available on pty's */
				continue;
		}

		if (cmdfifo.fd != -1 && FD_ISSET(cmdfifo.fd, &rd))
			handle_cmdfifo();

		if (bar.fd != -1 && FD_ISSET(bar.fd, &rd))
			handle_statusbar();

		for (Client *c = clients; c; c = c->next) {
			bool ed = c->editor && FD_ISSET(vt_getpty(c->editor), &rd);
			bool vt = FD_ISSET(vt_getpty(c->app), &rd);

			if (ed && vt_process(c->editor) < 0 && errno == EIO) {
				c->editor_died = true;
				continue;
			} else if (vt && vt_process(c->term) < 0 && errno == EIO) {
				c->died = true;
				continue;
			}

			if ((ed || vt) && c != sel && is_content_visible(c)) {
				draw_content(c);
				wnoutrefresh(c->window);
			}
		}

		if (is_content_visible(sel)) {
			draw_content(sel);
			curs_set(vt_cursor(sel->term));
			wnoutrefresh(sel->window);
		}
	}

	cleanup();
	return 0;
}

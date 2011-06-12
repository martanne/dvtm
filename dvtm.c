/*
 * The initial "port" of dwm to curses was done by
 * (c) 2007-2011 Marc Andre Tanner <mat at brain-dump dot org>
 *
 * It is highly inspired by the original X11 dwm and
 * reuses some code of it which is mostly
 *
 * (c) 2006-2007 Anselm R. Garbe <garbeam at gmail dot com>
 *
 * See LICENSE for details.
 */

#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef _AIX
# include <fcntl.h>
#else
# include <sys/fcntl.h>
#endif
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ncurses.h>
#include <stdio.h>
#include <signal.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#ifdef __CYGWIN__
# include <termios.h>
#endif
#include "madtty.h"

typedef struct {
	double mfact;
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
	madtty_t *term;
	const char *cmd;
	char title[256];
	uint8_t order;
	pid_t pid;
	int pty;
	unsigned short int id;
	unsigned short int x;
	unsigned short int y;
	unsigned short int w;
	unsigned short int h;
	bool minimized;
	bool died;
	Client *next;
	Client *prev;
};

typedef struct {
	const char *title;
	unsigned attrs;
	short fg;
	short bg;
} ColorRule;

#define ALT(k)      ((k) + (161 - 'a'))
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

#define MAX_ARGS 2

typedef struct {
	void (*cmd)(const char *args[]);
	/* needed to avoid an error about initialization
	 * of nested flexible array members */
	const char *args[MAX_ARGS + 1];
} Action;

typedef struct {
	unsigned int mod;
	unsigned int code;
	Action action;
} Key;

typedef struct {
	mmask_t mask;
	Action action;
} Button;

typedef struct {
	const char *name;
	Action action;
} Cmd;

enum BarPos { BarTop, BarBot, BarOff };
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

#define countof(arr) (sizeof(arr) / sizeof((arr)[0]))
#define sstrlen(str) (sizeof(str) - 1)
#define max(x, y) ((x) > (y) ? (x) : (y))

#ifdef NDEBUG
 #define debug(format, args...)
#else
 #define debug eprint
#endif

/* commands for use by keybindings */
static void quit(const char *args[]);
static void create(const char *args[]);
static void startup(const char *args[]);
static void escapekey(const char *args[]);
static void killclient(const char *args[]);
static void focusn(const char *args[]);
static void focusnext(const char *args[]);
static void focusnextnm(const char *args[]);
static void focusprev(const char *args[]);
static void focusprevnm(const char *args[]);
static void togglebell(const char *key[]);
static void toggleminimize(const char *args[]);
static void setmfact(const char *args[]);
static void setlayout(const char *args[]);
static void scrollback(const char *args[]);
static void redraw(const char *args[]);
static void zoom(const char *args[]);
static void lock(const char *key[]);
static void togglerunall(const char *args[]);

static void togglebar(const char *args[]);

static void mouse_focus(const char *args[]);
static void mouse_fullscreen(const char *args[]);
static void mouse_minimize(const char *args[]);
static void mouse_zoom(const char *args[]);
static void mouse_toggle();

static void clear_workspace();
static void draw(Client *c);
static void draw_all(bool border);
static void draw_border(Client *c);
static void resize(Client *c, int x, int y, int w, int h);
static void resize_screen();
static void eprint(const char *errstr, ...);
static bool isarrange(void (*func)());
static void arrange();
static void focus(Client *c);
static void keypress(int code);

static unsigned int waw, wah, wax, way;
static Client *clients = NULL;
extern Screen screen;

#include "config.h"

Screen screen = { MFACT, SCROLL_HISTORY };
static Client *sel = NULL;
static Layout *layout = layouts;
static StatusBar bar = { -1, BARPOS, 1 };
static CmdFifo cmdfifo = { -1 };
static const char *shell;
static bool running = true;
static bool runinall = false;

#include "mouse.c"
#include "cmdfifo.c"
#include "statusbar.c"

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

static void
attach(Client *c) {
	uint8_t order;
	if (clients)
		clients->prev = c;
	c->next = clients;
	c->prev = NULL;
	clients = c;
	for (order = 1; c; c = c->next, order++)
		c->order = order;
}

static void
attachafter(Client *c, Client *a) { /* attach c after a */
	uint8_t o;
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
		for (o = a->order; c; c = c->next)
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
arrange() {
	clear_workspace();
	attrset(NORMAL_ATTR);
	color_set(madtty_color_get(NORMAL_FG, NORMAL_BG), NULL);
	layout->arrange();
	wnoutrefresh(stdscr);
	draw_all(true);
}

static bool
isarrange(void (*func)()) {
	return func == layout->arrange;
}

static void
focus(Client *c) {
	Client *tmp = sel;
	if (sel == c)
		return;
	sel = c;
	if (tmp) {
		draw_border(tmp);
		wrefresh(tmp->window);
	}
	if (isarrange(fullscreen))
		redrawwin(c->window);
	draw_border(c);
	wrefresh(c->window);
}

static void
focusn(const char *args[]) {
	Client *c;

	for (c = clients; c; c = c->next) {
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
	Client *c;

	if (!sel)
		return;

	c = sel->next;
	if (!c)
		c = clients;
	if (c)
		focus(c);
}

static void
focusnextnm(const char *args[]) {
	Client *c;

	if (!sel)
		return;
	c = sel;
	do {
		c = c->next;
		if (!c)
			c = clients;
	} while (c->minimized && c != sel);
	focus(c);
}

static void
focusprev(const char *args[]) {
	Client *c;

	if (!sel)
		return;
	c = sel->prev;
	if (!c)
		for (c = clients; c && c->next; c = c->next);
	if (c)
		focus(c);
}

static void
focusprevnm(const char *args[]) {
	Client *c;

	if (!sel)
		return;
	c = sel;
	do {
		c = c->prev;
		if (!c)
			for (c = clients; c && c->next; c = c->next);
	} while (c->minimized && c != sel);
	focus(c);
}

static void
zoom(const char *args[]) {
	Client *c;

	if (!sel)
		return;
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

static void
togglebell(const char *args[]) {
	madtty_togglebell(sel->term);
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
		madtty_dirty(m->term);
		detach(m);
		attach(m);
	}
	arrange();
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
	double delta;

	if (isarrange(fullscreen) || isarrange(grid))
		return;
	/* arg handling, manipulate mfact */
	if (args[0] == NULL)
		screen.mfact = MFACT;
	else if (1 == sscanf(args[0], "%lf", &delta)) {
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
scrollback(const char *args[]) {
	if (!sel) return;

	if (!args[0] || atoi(args[0]) < 0)
		madtty_scroll(sel->term, -sel->h/2);
	else
		madtty_scroll(sel->term,  sel->h/2);

	draw(sel);
}

static void
redraw(const char *args[]) {
	wrefresh(curscr);
	resize_screen();
	draw_all(true);
}

static void
draw_border(Client *c) {
	char *s, t = '\0';
	int x, y, o;
	if (sel == c) {
		wattrset(c->window, SELECTED_ATTR);
		wcolor_set(c->window, madtty_color_get(SELECTED_FG, SELECTED_BG), NULL);
	} else {
		wattrset(c->window, NORMAL_ATTR);
		wcolor_set(c->window, madtty_color_get(NORMAL_FG, NORMAL_BG), NULL);
	}
	getyx(c->window, y, x);
	curs_set(0);
	mvwhline(c->window, 0, 0, ACS_HLINE, c->w);
	o = c->w - (4 + sstrlen(TITLE) - 5  + sstrlen(SEPARATOR));
	if (o < 0)
		o = 0;
	if ((size_t)o < sizeof(c->title)) {
		t = *(s = &c->title[o]);
		*s = '\0';
	}
	mvwprintw(c->window, 0, 2, TITLE,
	          *c->title ? c->title : "",
	          *c->title ? SEPARATOR : "",
	          c->order);
	if (t)
		*s = t;
	wmove(c->window, y, x);
	if (!c->minimized)
		curs_set(madtty_cursor(c->term));
}

static void
draw_content(Client *c) {
	if (!c->minimized || isarrange(fullscreen)) {
		madtty_draw(c->term, c->window, 1, 0);
		if (c != sel)
			curs_set(0);
	}
}

static void
draw(Client *c) {
	draw_content(c);
	draw_border(c);
	wrefresh(c->window);
}

static void
clear_workspace() {
	for (unsigned int y = 0; y < wah; y++)
		mvhline(way + y, 0, ' ', waw);
	wnoutrefresh(stdscr);
}

static void
draw_all(bool border) {
	Client *c;
	curs_set(0);
	for (c = clients; c; c = c->next) {
		redrawwin(c->window);
		if (c == sel)
			continue;
		draw_content(c);
		if (border)
			draw_border(c);
		wnoutrefresh(c->window);
	}
	/* as a last step the selected window is redrawn,
	 * this has the effect that the cursor position is
	 * accurate
	 */
	refresh();
	if (sel) {
		draw_content(sel);
		if (border)
			draw_border(sel);
		wrefresh(sel->window);
	}
}

static void
escapekey(const char *args[]) {
	int key;
	if ((key = getch()) >= 0) {
		debug("escaping key `%c'\n", key);
		keypress(CTRL(key));
	}
}

/*
 * Lock the screen until the correct password is entered.
 * The password can either be specified in config.h which is
 * not recommended because `strings dvtm` will contain it. If
 * no password is specified in the configuration file it is read
 * from the keyboard before the screen is locked.
 *
 * NOTE: this function doesn't handle the input from clients. All
 *       foreground operations are temporarily suspended since the
 *       function doesn't return.
 */
static void
lock(const char *args[]) {
	size_t len = 0, i = 0;
	char buf[16], *pass = buf;
	int c;

	erase();
	curs_set(0);

	if (args && args[0]) {
		len = strlen(args[0]);
		pass = (char *)args[0];
	} else {
		mvprintw(LINES / 2, COLS / 2 - 7, "Enter password");
		while (len < sizeof buf && (c = getch()) != '\n')
			if (c != ERR)
				buf[len++] = c;
	}

	mvprintw(LINES / 2, COLS / 2 - 7, "Screen locked!");

	while (i != len) {
		for(i = 0; i < len; i++) {
			if (getch() != pass[i])
				break;
		}
	}

	arrange();
}

static void
togglerunall(const char *args[]) {
	runinall = !runinall;
}

static void
killclient(const char *args[]) {
	if (!sel)
		return;
	debug("killing client with pid: %d\n", sel->pid);
	kill(-sel->pid, SIGKILL);
}

static void
applycolorrules(madtty_t *term, char *title) {
	unsigned int i;
	unsigned attrs = A_NORMAL;
	short fg = -1, bg = -1;
	const ColorRule *r;

	for (i = 0; i < countof(colorrules); i++) {
		r = &colorrules[i];
		if (strstr(title, r->title)) {
			attrs = r->attrs;
			fg = r->fg;
			bg = r->bg;
			break;
		}
	}
	madtty_set_default_colors(term, attrs, fg, bg);
}

static int
title_escape_seq_handler(madtty_t *term, char *es) {
	Client *c;
	unsigned int l;
	if (es[0] != ']' || (es[1] && (es[1] < '0' || es[1] > '9')) || (es[2] && es[2] != ';'))
		return MADTTY_HANDLER_NOWAY;
	if ((l = strlen(es)) < 3 || es[l - 1] != '\07')
		return MADTTY_HANDLER_NOTYET;
	es[l - 1] = '\0';
	c = (Client *)madtty_get_data(term);
	strncpy(c->title, es + 3, sizeof(c->title));
	draw_border(c);
	debug("window title: %s\n", c->title);
	applycolorrules(term, c->title);
	return MADTTY_HANDLER_OK;
}

static void
create(const char *args[]) {
	Client *c = calloc(sizeof(Client), 1);
	if (!c)
		return;
	const char *cmd = (args && args[0]) ? args[0] : shell;
	const char *pargs[] = { "/bin/sh", "-c", cmd, NULL };
	c->id = ++cmdfifo.id;
	char buf[8];
	snprintf(buf, sizeof buf, "%d", c->id);
	const char *env[] = {
		"DVTM", VERSION,
		"DVTM_WINDOW_ID", buf,
		NULL
	};

	c->window = newwin(wah, waw, way, wax);
	c->term = madtty_create(screen.h - 1, screen.w, screen.history);
	c->cmd = cmd;
	if (args && args[1])
		strncpy(c->title, args[1], sizeof(c->title));
	c->pid = madtty_forkpty(c->term, "/bin/sh", pargs, env, &c->pty);
	madtty_set_data(c->term, c);
	madtty_set_handler(c->term, title_escape_seq_handler);
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
destroy(Client *c) {
	if (sel == c)
		focusnextnm(NULL);
	detach(c);
	if (sel == c) {
		if (clients) {
			focus(clients);
			toggleminimize(NULL);
		} else
			sel = NULL;
	}
	werase(c->window);
	wrefresh(c->window);
	madtty_destroy(c->term);
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
move_client(Client *c, int x, int y) {
	if (c->x == x && c->y == y)
		return;
	debug("moving, x: %d y: %d\n", x, y);
	if (mvwin(c->window, y, x) == ERR)
		eprint("error moving, x: %d y: %d\n", x, y);
	else {
		c->x = x;
		c->y = y;
	}
}

static void
resize_client(Client *c, int w, int h) {
	if (c->w == w && c->h == h)
		return;
	debug("resizing, w: %d h: %d\n", w, h);
	if (wresize(c->window, h, w) == ERR)
		eprint("error resizing, w: %d h: %d\n", w, h);
	else {
		c->w = w;
		c->h = h;
	}
	madtty_resize(c->term, h - 1, w);
}

static void
resize(Client *c, int x, int y, int w, int h) {
	resize_client(c, w, h);
	move_client(c, x, y);
}

static bool
is_modifier(unsigned int mod) {
	unsigned int i;
	for (i = 0; i < countof(keys); i++) {
		if (keys[i].mod == mod)
			return true;
	}
	return false;
}

static Key*
keybinding(unsigned int mod, unsigned int code) {
	unsigned int i;
	for (i = 0; i < countof(keys); i++) {
		if (keys[i].mod == mod && keys[i].code == code)
			return &keys[i];
	}
	return NULL;
}

static Client*
get_client_by_pid(pid_t pid) {
	Client *c;
	for (c = clients; c; c = c->next) {
		if (c->pid == pid)
			return c;
	}
	return NULL;
}

static void
sigchld_handler(int sig) {
	int errsv = errno;
	int status;
	pid_t pid;
	Client *c;

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
		if ((c = get_client_by_pid(pid)))
			c->died = true;
	}

	signal(SIGCHLD, sigchld_handler);

	errno = errsv;
}

static void
sigwinch_handler(int sig) {
	signal(SIGWINCH, sigwinch_handler);
	screen.need_resize = true;
}

static void
sigterm_handler(int sig) {
	running = false;
}

static void
resize_screen() {
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) == -1)
		return;

	screen.w = ws.ws_col;
	screen.h = ws.ws_row;

	debug("resize_screen(), w: %d h: %d\n", screen.w, screen.h);

	resizeterm(screen.h, screen.w);
	wresize(stdscr, screen.h, screen.w);
	wrefresh(curscr);
	refresh();

	waw = screen.w;
	wah = screen.h;
	updatebarpos();
	drawbar();
	arrange();
}

static void
startup(const char *args[]) {
	for (unsigned int i = 0; i < countof(actions); i++)
		actions[i].cmd(actions[i].args);
}

static void
setup() {
	if (!(shell = getenv("SHELL")))
		shell = "/bin/sh";
	setlocale(LC_CTYPE, "");
	initscr();
	start_color();
	noecho();
	keypad(stdscr, TRUE);
	mouse_setup();
	raw();
	madtty_init();
	getmaxyx(stdscr, screen.h, screen.w);
	resize_screen();
	signal(SIGWINCH, sigwinch_handler);
	signal(SIGCHLD, sigchld_handler);
	signal(SIGTERM, sigterm_handler);
}

static void
cleanup() {
	endwin();
	if (bar.fd > 0)
		close(bar.fd);
	if (bar.file)
		unlink(bar.file);
	if (cmdfifo.fd > 0)
		close(cmdfifo.fd);
	if (cmdfifo.file)
		unlink(cmdfifo.file);
}

static void
quit(const char *args[]) {
	cleanup();
	exit(EXIT_SUCCESS);
}

static void
usage() {
	cleanup();
	eprint("usage: dvtm [-v] [-m mod] [-d escdelay] [-h n] "
		"[-s status-fifo] "
		"[-c cmd-fifo] "
		"[cmd...]\n");
	exit(EXIT_FAILURE);
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

static bool
parse_args(int argc, char *argv[]) {
	int arg;
	bool init = false;

	if (!getenv("ESCDELAY"))
		ESCDELAY = 100;
	for (arg = 1; arg < argc; arg++) {
		if (argv[arg][0] != '-') {
			const char *args[] = { argv[arg], NULL };
			if (!init) {
				setup();
				init = true;
			}
			create(args);
			continue;
		}
		if (argv[arg][1] != 'v' && (arg + 1) >= argc)
			usage();
		switch (argv[arg][1]) {
			case 'v':
				puts("dvtm-"VERSION" (c) 2007-2011 Marc Andre Tanner");
				exit(EXIT_SUCCESS);
			case 'm': {
				char *mod = argv[++arg];
				if (mod[0] == '^' && mod[1])
					*mod = CTRL(mod[1]);
				for (unsigned int i = 0; i < countof(keys); i++)
					keys[i].mod = *mod;
				break;
			}
			case 'd':
				ESCDELAY = atoi(argv[++arg]);
				if (ESCDELAY < 50)
					ESCDELAY = 50;
				else if (ESCDELAY > 1000)
					ESCDELAY = 1000;
				break;
			case 'h':
				screen.history = atoi(argv[++arg]);
				break;
			case 's':
				bar.fd = open_or_create_fifo(argv[++arg], &bar.file);
				updatebarpos();
				break;
			case 'c': {
				const char *fifo;
				cmdfifo.fd = open_or_create_fifo(argv[++arg], &cmdfifo.file);
				if (!(fifo = get_realpath(argv[arg])))
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

void
keypress(int code) {
	Client *c;
	unsigned int len = 1;
	char buf[8] = { '\e' };

	if (code == '\e') {
		/* pass characters following escape to the underlying app */
		nodelay(stdscr, TRUE);
		for (int t; len < sizeof(buf) && (t = getch()) != ERR; len++)
			buf[len] = t;
		nodelay(stdscr, FALSE);
	}

	for (c = runinall ? clients : sel; c; c = c->next) {
		if (!c->minimized || isarrange(fullscreen)) {
			if (code == '\e')
				madtty_write(c->term, buf, len);
			else
				madtty_keypress(c->term, code);
		}
		if (!runinall)
			break;
	}
}

int
main(int argc, char *argv[]) {
	if (!parse_args(argc, argv)) {
		setup();
		startup(NULL);
	}

	while (running) {
		Client *c, *t;
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

		for (c = clients; c; ) {
			if (c->died) {
				t = c->next;
				destroy(c);
				c = t;
				continue;
			}
			FD_SET(c->pty, &rd);
			nfds = max(nfds, c->pty);
			c = c->next;
		}
		r = select(nfds + 1, &rd, NULL, NULL, NULL);

		if (r == -1 && errno == EINTR)
			continue;

		if (r < 0) {
			perror("select()");
			exit(EXIT_FAILURE);
		}

		if (FD_ISSET(STDIN_FILENO, &rd)) {
			int code = getch();
			Key *key;
			if (code >= 0) {
				if (code == KEY_MOUSE) {
					handle_mouse();
				} else if (is_modifier(code)) {
					int mod = code;
					code = getch();
					if (code >= 0) {
						if (code == mod)
							keypress(code);
						else if ((key = keybinding(mod, code)))
							key->action.cmd(key->action.args);
					}
				} else if ((key = keybinding(0, code))) {
					key->action.cmd(key->action.args);
				} else {
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

		for (c = clients; c; ) {
			if (FD_ISSET(c->pty, &rd)) {
				if (madtty_process(c->term) < 0 && errno == EIO) {
					/* client probably terminated */
					t = c->next;
					destroy(c);
					c = t;
					continue;
				}
				if (c != sel) {
					draw_content(c);
					if (!isarrange(fullscreen))
						wnoutrefresh(c->window);
				}
			}
			c = c->next;
		}

		if (sel) {
			draw_content(sel);
			wnoutrefresh(sel->window);
		}
		doupdate();
	}

	cleanup();
	return 0;
}

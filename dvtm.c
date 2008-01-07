/*
 * The initial "port" of dwm to curses was done by
 * (c) 2007 Marc Andre Tanner <mat at brain-dump dot org>
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
#include <sys/fcntl.h>
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
#include "madtty.h"

typedef struct {
	const char *symbol;
	void (*arrange)(void);
} Layout;

typedef struct Client Client;
struct Client {
	WINDOW *window;
	madtty_t *term;
	const char *cmd;
	const char *title;
	uint8_t order;
	pid_t pid;
	int pty;
	short int x;
	short int y;
	short int w;
	short int h;
	bool minimized;
	Client *next;
	Client *prev;
};

#define ALT(k)      ((k) + (161 - 'a'))
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

typedef struct {
	unsigned int mod;
	unsigned int code;
	void (*action)(const char *arg);
	const char *arg;
} Key;

typedef struct {
	mmask_t mask;
	void (*action)(const char *arg);
	const char *arg;
} Button;

enum { BarTop, BarBot, BarOff };

#define COLOR(fg, bg) madtty_color_pair(fg, bg)
#define countof(arr) (sizeof (arr) / sizeof((arr)[0]))
#define max(x, y) ((x) > (y) ? (x) : (y))

#ifdef __linux__
# define SHELL "/bin/sh --login"
#else
# define SHELL "/bin/sh"
#endif

#ifdef DEBUG
 #define debug eprint
#else
 #define debug(format, args...)
#endif

/* commands for use by keybindings */
void quit(const char *arg);
void create(const char *cmd);
void killclient(const char *arg);
void focusn(const char *arg);
void focusnext(const char *arg);
void focusnextnm(const char *arg);
void focusprev(const char *arg);
void focusprevnm(const char *arg);
void toggleminimize(const char *arg);
void togglebar(const char *arg);
void setmwfact(const char *arg);
void setlayout(const char *arg);
void zoom(const char *arg);
/* special mouse related commands */
void mouse_focus(const char *arg);
void mouse_fullscreen(const char *arg);
void mouse_minimize(const char *arg);
void mouse_zoom(const char *arg);

void draw_all(bool border);
void draw_border(Client *c);
void drawbar();
void resize(Client* c, int x, int y, int w, int h);

unsigned int bh = 1, by, waw, wah, wax, way;
Client *clients = NULL;
extern double mwfact;

#include "config.h"

Client *sel = NULL;
Client *msel = NULL;
double mwfact = MWFACT;
Client *client_killed = NULL;
int statusfd = -1;
char stext[512];
int barpos = BARPOS;
unsigned int ltidx = 0;
bool need_screen_resize = false;
int width, height;
bool running = true;

void
eprint(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
}

void
attach(Client *c) {
	uint8_t order;
	if(clients)
		clients->prev = c;
	c->next = clients;
	c->prev = NULL;
	clients = c;
	for(order = 1; c; c = c->next, order++)
		c->order = order;
}

void
attachafter(Client *c, Client *a){ /* attach c after a */
	uint8_t o;
	if(c == a)
		return;
	if(!a)
		for(a = clients; a && a->next; a = a->next);

	if(a){
		if(a->next)
			a->next->prev = c;
		c->next = a->next;
		c->prev = a;
		a->next = c;
		for(o = a->order; c; c = c->next)
			c->order = ++o;
	}
}

void
detach(Client *c) {
	Client *d;
	if(c->prev)
		c->prev->next = c->next;
	if(c->next){
		c->next->prev = c->prev;
		for (d = c->next; d; d = d->next)
			--d->order;
	}
	if(c == clients)
		clients = c->next;
	c->next = c->prev = NULL;
}

void
arrange(){
	layouts[ltidx].arrange();
	draw_all(true);
}

bool
isarrange(void (*func)()){
	return func == layouts[ltidx].arrange;
}

void
focus(Client *c){
	Client *tmp = sel;
	if(sel == c)
		return;
	sel = c;
	if(tmp){
		draw_border(tmp);
		wrefresh(tmp->window);
	}
	draw_border(c);
	wrefresh(c->window);
}

void
focusn(const char *arg){
	Client *c;

	for(c = clients; c; c = c->next){
		if (c->order == atoi(arg)){
			focus(c);
			if(c->minimized)
				toggleminimize(NULL);
			return;
		}
	}
}

void
focusnext(const char *arg) {
	Client *c;

	if(!sel)
		return;

	c = sel->next;
	if(!c)
		c = clients;
	if(c)
		focus(c);
}

void
focusnextnm(const char *arg) {
	Client *c;

	if(!sel)
		return;
	c = sel;
	do {
		c = c->next;
		if(!c)
			c = clients;
	} while(c->minimized && c != sel);
	focus(c);
}

void
focusprev(const char *arg){
	Client *c;

	if(!sel)
		return;
	c = sel->prev;
	if(!c)
		for(c = clients; c && c->next; c = c->next);
	if(c)
		focus(c);
}

void
focusprevnm(const char *arg){
	Client *c;

	if(!sel)
		return;
	c = sel;
	do {
		c = c->prev;
		if(!c)
			for(c = clients; c && c->next; c = c->next);
	} while(c->minimized && c != sel);
	focus(c);
}

void
zoom(const char *arg) {
	Client *c;

	if(!sel)
		return;
	if((c = sel) == clients)
		if(!(c = c->next))
			return;
	detach(c);
	attach(c);
	focus(c);
	if(c->minimized)
		toggleminimize(NULL);
	arrange();
}

void
toggleminimize(const char *arg){
	Client *c, *m;
	unsigned int n;
	if(!sel)
		return;
	/* the last window can't be minimized */
	if(!sel->minimized){
		for(n = 0, c = clients; c; c = c->next)
			if(!c->minimized)
				n++;
		if(n == 1)
			return;
	}
	sel->minimized = !sel->minimized;
	m = sel;
	/* check whether the master client was minimized */
	if(sel == clients && sel->minimized){
		c = sel->next;
		detach(c);
		attach(c);
		focus(c);
		detach(m);
		for(; c && c->next && !c->next->minimized; c = c->next);
		attachafter(m, c);
	} else if(m->minimized){
		/* non master window got minimized move it above all other
		 * minimized ones */
		focusnextnm(NULL);
		detach(m);
		for(c = clients; c && c->next && !c->next->minimized; c = c->next);
		attachafter(m, c);
	} else { /* window is no longer minimized, move it to the master area */
		detach(m);
		attach(m);
	}
	arrange();
}

void
updatebarpos(void) {
	by = 0;
	wax = 0;
	way = 0;
	waw = width;
	wah = height;
	if(statusfd == -1)
		return;
	if(barpos == BarTop){
		wah -= bh;
		way += bh;
	} else if(barpos == BarBot){
		wah -= bh;
		by = wah;
	}
}

void
togglebar(const char *arg) {
	if(barpos == BarOff)
		barpos = (BARPOS == BarOff) ? BarTop : BARPOS;
	else
		barpos = BarOff;
	updatebarpos();
	arrange();
	drawbar();
}

void
setlayout(const char *arg) {
	unsigned int i;

	if(!arg) {
		if(++ltidx == countof(layouts))
			ltidx = 0;
	} else {
		for(i = 0; i < countof(layouts); i++)
			if(!strcmp(arg, layouts[i].symbol))
				break;
		if(i == countof(layouts))
			return;
		ltidx = i;
	}
	arrange();
}

void
setmwfact(const char *arg) {
	double delta;

	if(!isarrange(tile) && !isarrange(bstack))
		return;
	/* arg handling, manipulate mwfact */
	if(arg == NULL)
		mwfact = MWFACT;
	else if(1 == sscanf(arg, "%lf", &delta)) {
		if(arg[0] == '+' || arg[0] == '-')
			mwfact += delta;
		else
			mwfact = delta;
		if(mwfact < 0.1)
			mwfact = 0.1;
		else if(mwfact > 0.9)
			mwfact = 0.9;
	}
	arrange();
}

void
draw_border(Client *c){
	int x, y;
	if(sel == c)
		wattron(c->window, ATTR_SELECTED);
	else
		wattrset(c->window, ATTR_NORMAL);
	if(c->minimized && !isarrange(fullscreen))
		mvwhline(c->window, 0, 0, ACS_HLINE, c->w);
	else
		box(c->window, 0, 0);
	curs_set(0);
	getyx(c->window, y, x);
	mvwprintw(c->window, 0, 2, TITLE,
	          c->title ? c->title : "",
	          c->title ? SEPARATOR : "",
		  c->order);
	wmove(c->window, y, x);
	curs_set(1);
}

void
draw_content(Client *c){
	if(!c->minimized || isarrange(fullscreen))
		madtty_draw(c->term, c->window, 1, 1);
}

void
draw(Client *c){
	draw_content(c);
	draw_border(c);
	wrefresh(c->window);
}

void
draw_all(bool border){
	Client *c;
	curs_set(0);
	for(c = clients; c; c = c->next){
		if(c == sel)
			continue;
		draw_content(c);
		if(border)
			draw_border(c);
		wnoutrefresh(c->window);
	}
	/* as a last step the selected window is redrawn,
	 * this has the effect that the cursor position is
	 * accurate
	 */
	if(sel){
		draw_content(sel);
		if(border)
			draw_border(sel);
		wnoutrefresh(sel->window);
	}
	if(!sel || !sel->minimized)
		curs_set(1);
	doupdate();
	if(sel && isarrange(fullscreen))
		redrawwin(sel->window);
}

void
drawbar(){
	int s, l;
	if(barpos == BarOff || !*stext)
		return;
	curs_set(0);
	attrset(BAR_ATTR);
	mvaddch(by, 0, '[');
	stext[width - 2] = '\0';
	l = strlen(stext);
	if(BAR_ALIGN_RIGHT)
		for(s = 0; s + l < width - 2; s++)
			addch(' ');
	else
		for(; l < width - 2; l++)
			stext[l] = ' ';
	addstr(stext);
	addch(']');
	attrset(ATTR_NORMAL);
	if(sel)
		curs_set(1);
	refresh();
}

void
killclient(const char *arg){
	if(!sel)
		return;
	debug("killing client with pid: %d\n", sel->pid);
	kill(-sel->pid, SIGKILL);
}

void
create(const char *cmd){
	const char *args[] = { "/bin/sh", "-c", cmd, NULL };
	Client *c = malloc(sizeof(Client));
	c->window = newwin(wah, waw, way, wax);
	c->term = madtty_create(height-2, width-2);
	c->cmd = cmd;
	c->title = cmd;
	c->pid = madtty_forkpty(c->term, "/bin/sh", args, &c->pty);
	c->w = width;
	c->h = height;
	c->x = wax;
	c->y = way;
	c->order = 0;
	c->minimized = false;
	debug("client with pid %d forked\n", c->pid);
	attach(c);
	focus(c);
	arrange();
}

void
destroy(Client *c){
	if(sel == c)
		focusnextnm(NULL);
	detach(c);
	if(sel == c){
		if(clients){
			focus(clients);
			toggleminimize(NULL);
		} else
			sel = NULL;
	}
	werase(c->window);
	wrefresh(c->window);
	madtty_destroy(c->term);
	delwin(c->window);
	free(c);
	arrange();
}

void
move_client(Client *c, int x, int y){
	if(c->x == x && c->y == y)
		return;
	debug("moving, x: %d y: %d\n", x, y);
	if(mvwin(c->window, y, x) == ERR)
		eprint("error moving, x: %d y: %d\n", x, y);
	else {
		c->x = x;
		c->y = y;
	}
}

void
resize_client(Client *c, int w, int h){
	if(c->w == w && c->h == h)
		return;
	debug("resizing, w: %d h: %d\n", w, h);
	if(wresize(c->window, h, w) == ERR)
		eprint("error resizing, w: %d h: %d\n", w, h);
	else {
		c->w = w;
		c->h = h;
	}
	madtty_resize(c->term, h-2, w-2);
}

void
resize(Client *c, int x, int y, int w, int h){
	resize_client(c, w, h);
	move_client(c, x, y);
}

bool
is_modifier(unsigned int mod){
	unsigned int i;
	for(i = 0; i < countof(keys); i++){
		if(keys[i].mod == mod)
			return true;
	}
	return false;
}

Key*
keybinding(unsigned int mod, unsigned int code){
	unsigned int i;
	for(i = 0; i < countof(keys); i++){
		if(keys[i].mod == mod && keys[i].code == code)
			return &keys[i];
	}
	return NULL;
}

void
mouse_focus(const char *arg){
	focus(msel);
	if(msel->minimized)
		toggleminimize(NULL);
}

void
mouse_fullscreen(const char *arg){
	mouse_focus(NULL);
	if(isarrange(fullscreen))
		setlayout(NULL);
	else
		setlayout("[ ]");
}

void
mouse_minimize(const char *arg){
	focus(msel);
	toggleminimize(NULL);
}

void
mouse_zoom(const char *arg){
	focus(msel);
	zoom(NULL);
}

Client*
get_client_by_coord(int x, int y){
	Client *c;
	if(y < way || y >= wah)
		return NULL;
	if(isarrange(fullscreen))
		return sel;
	for(c = clients; c; c = c->next){
		if(x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h){
			debug("mouse event, x: %d y: %d client: %d\n", x, y, c->order);
			return c;
		}
	}
	return NULL;
}

void
handle_mouse(){
	MEVENT event;
	unsigned int i;
	if(getmouse(&event) != OK)
		return;
	msel = get_client_by_coord(event.x, event.y);
	if(!msel)
		return;
	for(i = 0; i < countof(buttons); i++)
		if(event.bstate & buttons[i].mask)
			buttons[i].action(buttons[i].arg);
	msel = NULL;
}

Client*
get_client_by_pid(pid_t pid){
	Client *c;
	for(c = clients; c; c = c->next){
		if(c->pid == pid)
			return c;
	}
	return NULL;
}

void
sigchld_handler(int sig){
	int child_status;
	signal(SIGCHLD, sigchld_handler);
	pid_t pid = wait(&child_status);
	debug("child with pid %d died\n", pid);
	client_killed = get_client_by_pid(pid);
}

void
sigwinch_handler(int sig){
	struct winsize ws;
	signal(SIGWINCH, sigwinch_handler);
	if(ioctl(0, TIOCGWINSZ, &ws) == -1)
		return;

	width = ws.ws_col;
	height = ws.ws_row;
	need_screen_resize = true;
}

void
sigterm_handler(int sig){
	running = false;
}

void
resize_screen(){
	debug("resize_screen(), w: %d h: %d\n", width, height);
	if(need_screen_resize){
	#if defined(__OpenBSD__) || defined(__NetBSD__)
		resizeterm(height, width);
	#else
		resize_term(height, width);
	#endif
		wresize(stdscr, height, width);
		wrefresh(curscr);
		refresh();
	}
	updatebarpos();
	drawbar();
	arrange();
	need_screen_resize = false;
}

void
setup(){
	int i;
	mmask_t mask;
	setlocale(LC_CTYPE,"");
	initscr();
	start_color();
	noecho();
   	keypad(stdscr, TRUE);
	for(i = 0, mask = 0; i < countof(buttons); i++)
		mask |= buttons[i].mask;
	mousemask(mask, NULL);
	raw();
	madtty_init_colors();
	madtty_init_vt100_graphics();
	getmaxyx(stdscr, height, width);
	resize_screen();
	signal(SIGWINCH, sigwinch_handler);
	signal(SIGCHLD, sigchld_handler);
	signal(SIGTERM, sigterm_handler);
}

void
cleanup(){
	endwin();
	if(statusfd > 0)
		close(statusfd);
}

void
quit(const char *arg){
	cleanup();
	exit(EXIT_SUCCESS);
}

void
usage(){
	cleanup();
	eprint("usage: dvtm [-v] [-m mod] [-s status]\n");
	exit(EXIT_FAILURE);
}

void
parse_args(int argc, char **argv){
	int arg;
	for(arg = 1; arg < argc; arg++){
		if(argv[arg][0] != '-')
			usage();
		switch(argv[arg][1]){
			case 'v':
				puts("dvtm-"VERSION" (c) 2007 Marc Andre Tanner");
				exit(EXIT_SUCCESS);
			case 'm':
				if(++arg >= argc)
					usage();
				unsigned int i;
				char *mod = argv[arg];
				if(mod[0] == '^' && mod[1])
					*mod = CTRL(mod[1]);
				for(i = 0; i < countof(keys); i++)
					keys[i].mod = *mod;
				break;
			case 's':
				if(++arg >= argc)
					usage();
				struct stat info;
				if((statusfd = open(argv[arg], O_RDONLY|O_NONBLOCK)) == - 1 ||
				    fstat(statusfd, &info) == -1){
					perror("status");
					exit(EXIT_FAILURE);
				}
				if(!S_ISFIFO(info.st_mode)){
					eprint("%s is not a named pipe.\n", argv[arg]);
					exit(EXIT_FAILURE);
				}
				updatebarpos();
				break;
			default:
				usage();
		}
	}
}

int
main(int argc, char *argv[]) {
	parse_args(argc, argv);
	setup();
	while(running){
		Client *c;
		int r, nfds = 0;
		fd_set rd;

		if(need_screen_resize)
			resize_screen();

		if(client_killed){
			destroy(client_killed);
			client_killed = NULL;
		}

		FD_ZERO(&rd);
		FD_SET(STDIN_FILENO, &rd);

		if(statusfd != -1){
			FD_SET(statusfd, &rd);
			nfds = max(nfds, statusfd);
		}

		for(c = clients; c; c = c->next){
			FD_SET(c->pty, &rd);
			nfds = max(nfds, c->pty);
		}
		r = select(nfds + 1, &rd, NULL, NULL, NULL);

		if(r == -1 && errno == EINTR)
			continue;

		if(r < 0){
			perror("select()");
			exit(EXIT_FAILURE);
		}

		if(FD_ISSET(STDIN_FILENO, &rd)){
			int code = getch();
			Key *key;
			if(code >= 0){
				if(code == KEY_MOUSE){
					handle_mouse();
				} else if(is_modifier(code)){
					int mod = code;
					code = getch();
					if(code >= 0){
						if(code == mod)
							goto keypress;
						if((key = keybinding(mod, code)))
							key->action(key->arg);
					}
				} else if((key = keybinding(0, code))){
					key->action(key->arg);
				} else {
			keypress:
					if(sel && (!sel->minimized || isarrange(fullscreen))){
						madtty_keypress(sel->term, code);
						if(r == 1){
							draw_content(sel);
							wrefresh(sel->window);
						}
					}
				}
			}
			if(r == 1) /* no data available on pty's */
				continue;
		}

		if(statusfd != -1 && FD_ISSET(statusfd, &rd)){
			char *p;
			switch(r = read(statusfd, stext, sizeof stext - 1)) {
				case -1:
					strncpy(stext, strerror(errno), sizeof stext - 1);
					stext[sizeof stext - 1] = '\0';
					statusfd = -1;
					break;
				case 0:
					statusfd = -1;
					break;
				default:
					stext[r] = '\0'; p = stext + strlen(stext) - 1;
					for(; p >= stext && *p == '\n'; *p-- = '\0');
					for(; p >= stext && *p != '\n'; --p);
					if(p > stext)
						strncpy(stext, p + 1, sizeof stext);
					drawbar();
				}
		}

		for(c = clients; c; c = c->next){
			if(FD_ISSET(c->pty, &rd)){
				madtty_process(c->term);
				if(c != sel){
					draw_content(c);
					wnoutrefresh(c->window);
				}
			}
		}

		if(sel) {
			draw_content(sel);
			wnoutrefresh(sel->window);
		}
		doupdate();
		if(sel && isarrange(fullscreen))
			redrawwin(sel->window);
	}

	cleanup();
	return 0;
}

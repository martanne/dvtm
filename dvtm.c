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

#include <ncurses.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <rote/rote.h>
#include <stdbool.h>

typedef struct {
	const char *symbol;
	void (*arrange)(void);
} Layout;

typedef struct Client Client;
struct Client {
	WINDOW *window;
	RoteTerm *term;
	const char *cmd;
	const char *title;
	pid_t pid;
	short int x;
	short int y;
	short int w;
	short int h;
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

#define COLOR(bg,fg) COLOR_PAIR(bg * 8 + 7 - fg)
#define countof(arr) (sizeof (arr) / sizeof((arr)[0])) 

#ifdef DEBUG
 #define debug eprint
#else
 #define debug(format,args...)
#endif

void arrange();
void draw(Client *c);
void draw_all(bool border);
void draw_border(Client *c);
void draw_content(Client *c);
void attach(Client *c);
void detach(Client *c);
void zoom(const char *arg);
void sigwinch_handler(int sig);
void sigchld_handler(int sig);
void resize_screen();
void cleanup();
void setup();
void quit(const char *arg);
void create(const char *cmd);
void destroy(Client *c);
void resize(Client* c,int x,int y,int w,int h);
void resize_client(Client* c,int w,int h);
void move_client(Client *c,int x, int y);
void focus(Client *c);
void focusn(const char *arg);
void focusnext(const char *arg);
void focusprev(const char *arg);
bool isarrange(void (*func)());
void setmwfact(const char *arg);
void setlayout(const char *arg);
void eprint(const char *errstr, ...);
Client* get_client_by_pid(pid_t pid);

unsigned int waw,wah,wax = 0,way = 0;
double mwfact = 0.5;
Client *clients = NULL;

#include "config.h"

Client *sel = NULL;
/* should probably be a linked list? */
Client *client_killed = NULL;

unsigned int ltidx = 0;
bool need_screen_resize = false;

int width,height;

void
attach(Client *c) {
	if(clients)
		clients->prev = c;
	c->next = clients;
	c->prev = NULL;
	clients = c;
}

void
detach(Client *c) {
	if(c->prev)
		c->prev->next = c->next;
	if(c->next)
		c->next->prev = c->prev;
	if(c == clients)
		clients = c->next;
	c->next = c->prev = NULL;
}

void
zoom(const char *arg) {
	Client *c;

	if(!sel)
		return;
	if((c = sel) == clients)
		if(!(c =c->next))
			return;
	detach(c);
	attach(c);
	focus(c);
	arrange();
}

void
focus(Client *c){
	Client *tmp = sel;
	sel = c;
	if(tmp && tmp != c){
		draw_border(tmp);
		wrefresh(tmp->window);
	}
	draw_border(c);
	wrefresh(c->window);
}

void
focusn(const char *arg){
	unsigned int i;
	Client *c;

	for(i = 0, c = clients; c; c = c->next,i++){
		if(i == atoi(arg)){
			focus(c);
			break;
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
focusprev(const char *arg) {
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
arrange(){
	layouts[ltidx].arrange();
	draw_all(true);
}

bool
isarrange(void (*func)()){
	return func == layouts[ltidx].arrange;
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
draw(Client *c){
	draw_content(c);
	draw_border(c);
	wrefresh(c->window);
}

void
draw_border(Client *c){
	if(sel == c)
		wattron(c->window,ATTR_SELECTED);
	else
		wattrset(c->window,ATTR_NORMAL);

	wborder(c->window, 0, 0, 0, 0, 0, 0, 0, 0);
	if(c->title){
		curs_set(0);
		int x,y;
		getyx(c->window,y,x);
		mvwprintw(c->window,0,2, TITLE, c->title);
		wmove(c->window,y,x);
		curs_set(1);
	}
}

void
draw_content(Client *c){
	rote_vt_draw(c->term,c->window,1,1,NULL);
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
	curs_set(1);
	doupdate();
}

void
create(const char *cmd){
	Client *c = malloc(sizeof(Client));
	c->window = newwin(height,width,0,0);
	c->term = rote_vt_create(height-2,width-2);
	c->cmd = cmd;
	c->title = cmd;
	c->pid = rote_vt_forkpty(c->term,cmd);
	c->w = width;
	c->h = height;
	c->x = 0;
	c->y = 0;
	debug("client with pid %d forked\n",c->pid);
	attach(c);
	focus(c);
	arrange();
}

void
destroy(Client *c){
	if(sel == c)
		focusnext(NULL);
	if(sel == c)
		sel = NULL;
	detach(c);
	werase(c->window);
	wrefresh(c->window);
	rote_vt_destroy(c->term);
	delwin(c->window);
	free(c);
	arrange();
}

void
resize(Client *c, int x, int y, int w, int h){
	resize_client(c,w,h);
	move_client(c,x,y);
}

void
resize_client(Client *c,int w, int h){
	if(c->w == w && c->h == h)
		return;
	debug("resizing, w: %d h: %d\n",w,h);
	if(wresize(c->window,h,w) == ERR)
		eprint("error resizing, w: %d h: %d\n",w,h);
	else {
		c->w = w;
		c->h = h;
	}
	rote_vt_resize(c->term,h-2,w-2);
}

void
move_client(Client *c,int x, int y){
	if(c->x == x && c->y == y)
		return;
	debug("moving, x: %d y: %d\n",x,y);
	if(mvwin(c->window,y,x) == ERR)
		eprint("error moving, x: %d y: %d\n",x,y);
	else {
		c->x = x;
		c->y = y;
	}
		
}


int
is_modifier(unsigned int mod){
	unsigned int i;
	for(i = 0; i < countof(keys); i++){
		if(keys[i].mod == mod)
			return 1;
	}
	return 0;
}

Key*
keybinding(unsigned int mod,unsigned int code){
	unsigned int i;
	for(i = 0; i < countof(keys); i++){
		if(keys[i].mod == mod && keys[i].code == code)
			return &keys[i];
	}
	return NULL;
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
	pid_t pid = wait(&child_status);
	debug("child with pid %d died\n",pid);
	client_killed = get_client_by_pid(pid);
}

void 
sigwinch_handler(int sig){
	struct winsize ws;
	if(ioctl(0, TIOCGWINSZ, &ws) == -1)
		return;
	
	width = ws.ws_col;
	height = ws.ws_row;
	need_screen_resize = true;
}

void
resize_screen(){
	debug("resize_screen(), w: %d h: %d\n",width, height);
	if(need_screen_resize){
	#if defined(__OpenBSD__) || defined(__NetBSD__)
		resizeterm(height,width);
	#else
		resize_term(height,width);
	#endif
		wresize(stdscr,height,width);
		wrefresh(curscr);
		refresh();
	}
	waw = width;
	wah = height;
	arrange();
	need_screen_resize = false;
}

void
eprint(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
}

void
setup(){
	initscr();
	start_color();
	noecho();
   	keypad(stdscr, TRUE);
	timeout(REDRAW_TIMEOUT);
	raw();
	/* initialize the color pairs the way rote_vt_draw expects it. You might
	 * initialize them differently, but in that case you would need
	 * to supply a custom conversion function for rote_vt_draw to
	 * call when setting attributes. The idea of this "default" mapping
	 * is to map (fg,bg) to the color pair bg * 8 + 7 - fg. This way,
	 * the pair (white,black) ends up mapped to 0, which means that
	 * it does not need a color pair (since it is the default). Since
	 * there are only 63 available color pairs (and 64 possible fg/bg
	 * combinations), we really have to save 1 pair by assigning no pair
	 * to the combination white/black.
	 */
	int i, j;
	for (i = 0; i < 8; i++) for (j = 0; j < 8; j++)
		if (i != 7 || j != 0)
			init_pair(j*8+7-i, i, j);
	
	getmaxyx(stdscr,height,width);
	resize_screen();
	signal(SIGWINCH,sigwinch_handler);
	signal(SIGCHLD,sigchld_handler);
}

void
cleanup(){
	endwin();
}

void 
quit(const char *arg){
	cleanup();
	exit(EXIT_SUCCESS);
}

int 
main(int argc, char *argv[]) {
	int code;
	if(argc == 2 && !strcmp("-v", argv[1])){
		eprint("dvtm-"VERSION", (c) 2007 Marc Andre Tanner\n");
		exit(EXIT_SUCCESS);
	} else if(argc != 1){
		eprint("usage: dvtm [-v]\n");
		exit(EXIT_FAILURE);
	}
	setup();
	while((code = getch())){
		/* if no key was pressed then just update the screen */
		if(code >= 0){
			if(is_modifier(code)){
				int mod = code;
				do {
					code = getch();
				} while (code < 0);
				Key* key = keybinding(mod,code);
				if(key)
					key->action(key->arg);
			} else if(sel)
				rote_vt_keypress(sel->term, code);
		}

		if(clients)
			draw_all(false);
		if(need_screen_resize)
			resize_screen();
		if(client_killed){
			destroy(client_killed);
			client_killed = NULL;
		}
	}

	return 0;
}

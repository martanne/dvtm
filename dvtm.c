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

#ifdef USE_UTF8
#  include <ncursesw/ncurses.h>
#  include <rote/rotew.h>
#else
#  include <ncurses.h>
#  include <rote/rote.h>
#endif
#include <stdio.h>
#include <signal.h>
#include <locale.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

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
	unsigned char order;
	pid_t pid;
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

#define COLOR(bg,fg) COLOR_PAIR(bg * 8 + 7 - fg)
#define countof(arr) (sizeof (arr) / sizeof((arr)[0])) 
#define max(x,y) ((x) > (y) ? (x) : (y))

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
void drawbar();
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
void focusnextnm(const char *arg);
void focusprev(const char *arg);
void focusprevnm(const char *arg);
void toggleminimize(const char *arg);
bool isarrange(void (*func)());
void setmwfact(const char *arg);
void setlayout(const char *arg);
void eprint(const char *errstr, ...);
Client* get_client_by_pid(pid_t pid);

unsigned int waw,wah,wax = 0,way = 0;
Client *clients = NULL;
extern double mwfact;

#include "config.h"

Client *sel = NULL;
double mwfact = MWFACT;
Client *client_killed = NULL;
int statusfd = -1;
char stext[512];
unsigned int ltidx = 0;
bool need_screen_resize = false;
int width,height;
bool running = true;

void
attach(Client *c) {
	unsigned char order;
	if(clients)
		clients->prev = c;
	c->next = clients;
	c->prev = NULL;
	clients = c;
	for(order = 1; c; c = c->next,order++)
		c->order = order;
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
attachafter(Client *c, Client *a){ /* attach c after a */
	unsigned int o;
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
toggleminimize(const char *arg){
	Client *c,*m;
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
		attachafter(m,c);
	} else if(m->minimized){
		/* non master window got minimized move it above all other 
		 * minimized ones */
		focusnextnm(NULL);
		detach(m);
		for(c = clients; c && c->next && !c->next->minimized; c = c->next);
		attachafter(m,c);
	} else { /* window is no longer minimized, move it to the master area */
		detach(m);
		attach(m);
	}
	arrange();
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
	int x,y;
	if(sel == c)
		wattron(c->window,ATTR_SELECTED);
	else
		wattrset(c->window,ATTR_NORMAL);
	if(c->minimized && !isarrange(fullscreen))
		mvwhline(c->window,0,0,ACS_HLINE,c->w);
	else
		box(c->window,0,0);
	curs_set(0);
	getyx(c->window,y,x);
	mvwprintw(c->window,0,2, TITLE,
	          c->title ? c->title : "",
	          c->title ? SEPARATOR : "",
		  c->order);
	wmove(c->window,y,x);
	curs_set(1);
}

void
draw_content(Client *c){
	if(!c->minimized || isarrange(fullscreen))
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
	if(!sel || !sel->minimized)
		curs_set(1);
	doupdate();
}

void
drawbar(){
	int s,l;
	curs_set(0);
	attrset(BAR_ATTR);
	mvaddch(0,0,'[');
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
create(const char *cmd){
	Client *c = malloc(sizeof(Client));
	c->window = newwin(height,width,way,wax);
	c->term = rote_vt_create(height-2,width-2);
	c->cmd = cmd;
	c->title = cmd;
	c->pid = rote_vt_forkpty(c->term,cmd);
	c->w = width;
	c->h = height;
	c->x = wax;
	c->y = way;
	c->order = 0;
	c->minimized = false;
	debug("client with pid %d forked\n",c->pid);
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
	signal(SIGCHLD,sigchld_handler);
	int child_status;
	pid_t pid = wait(&child_status);
	debug("child with pid %d died\n",pid);
	client_killed = get_client_by_pid(pid);
}

void 
sigwinch_handler(int sig){
	signal(SIGWINCH,sigwinch_handler);
	struct winsize ws;
	if(ioctl(0, TIOCGWINSZ, &ws) == -1)
		return;
	
	width = ws.ws_col;
	height = ws.ws_row;
	need_screen_resize = true;
}

void 
sigterm_handler(int sig){
	signal(SIGTERM,sigterm_handler);
	running = false;
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
	waw = width - wax;
	wah = height - way;
	if(*stext)
		drawbar();
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
	setlocale(LC_ALL,"");
	initscr();
	start_color();
	noecho();
   	keypad(stdscr, TRUE);
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
	signal(SIGTERM,sigterm_handler);
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


int 
main(int argc, char *argv[]) {
	int arg; 
	for(arg = 1; arg < argc; arg++){
		if(argv[arg][0] != '-')
			usage();
		switch(argv[arg][1]){
			case 'v':
				printf("dvtm-"VERSION" (c) 2007 Marc Andre Tanner\n");
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
				if((statusfd = open(argv[arg],O_RDONLY|O_NONBLOCK)) == - 1 ||
				    fstat(statusfd,&info) == -1){
					perror("status");
					exit(EXIT_FAILURE);
				}
				if(!S_ISFIFO(info.st_mode)){
					eprint("%s is not a named pipe.\n",argv[arg]);
					exit(EXIT_FAILURE);
				}
				way = 1;
				break;
			default:
				usage();
		}
	}
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
			FD_SET(statusfd,&rd);
			nfds = max(nfds,statusfd);
		}

		for(c = clients; c; c = c->next){
			FD_SET(rote_vt_get_pty_fd(c->term),&rd);
			nfds = max(nfds,rote_vt_get_pty_fd(c->term));
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
			if(code >= 0){
				if(is_modifier(code)){
					int mod = code;
					code = getch();
					if(code >= 0){
						Key* key = keybinding(mod,code);
						if(key)
							key->action(key->arg);
					}
				} else if(sel && (!sel->minimized || isarrange(fullscreen))){
					rote_vt_keypress(sel->term, code);
					if(r == 1){
						draw_content(sel);
						wrefresh(sel->window);
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
			if(FD_ISSET(rote_vt_get_pty_fd(c->term),&rd)){
				draw_content(c);
				if(c != sel)
					wnoutrefresh(c->window);
			}
		}

		if(sel)
			wnoutrefresh(sel->window);
		doupdate();
	}

	cleanup();
	return 0;
}

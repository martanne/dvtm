/*
 * Copyright © 2004 Bruno T. C. de Oliveira
 * Copyright © 2006 Pierre Habouzit
 * Copyright © 2008-2013 Marc André Tanner
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef VT_H
#define VT_H

#include <curses.h>
#include <stdbool.h>
#include <sys/types.h>

#ifndef NCURSES_MOUSE_VERSION
#define mmask_t unsigned long
#endif

typedef struct Vt Vt;
typedef void (*vt_title_handler_t)(Vt*, const char *title);
typedef void (*vt_urgent_handler_t)(Vt*);

void vt_init(void);
void vt_shutdown(void);

void vt_keytable_set(char const * const keytable_overlay[], int count);
void vt_default_colors_set(Vt*, attr_t attrs, short fg, short bg);
void vt_title_handler_set(Vt*, vt_title_handler_t);
void vt_urgent_handler_set(Vt*, vt_urgent_handler_t);
void vt_data_set(Vt*, void *);
void *vt_data_get(Vt*);

Vt *vt_create(int rows, int cols, int scroll_buf_sz);
void vt_resize(Vt*, int rows, int cols);
void vt_destroy(Vt*);
pid_t vt_forkpty(Vt*, const char *p, const char *argv[], const char *cwd, const char *env[], int *to, int *from);
int vt_pty_get(Vt*);
bool vt_cursor_visible(Vt*);

int vt_process(Vt *);
void vt_keypress(Vt *, int keycode);
ssize_t vt_write(Vt*, const char *buf, size_t len);
void vt_mouse(Vt*, int x, int y, mmask_t mask);
void vt_dirty(Vt*);
void vt_draw(Vt*, WINDOW *win, int startrow, int startcol);
short vt_color_get(Vt*, short fg, short bg);
short vt_color_reserve(short fg, short bg);

void vt_scroll(Vt*, int rows);
void vt_noscroll(Vt*);

pid_t vt_pid_get(Vt*);
size_t vt_content_get(Vt*, char **s, bool colored);
int vt_content_start(Vt*);

#endif /* VT_H */

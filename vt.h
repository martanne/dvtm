/*
 * Copyright © 2004 Bruno T. C. de Oliveira
 * Copyright © 2006 Pierre Habouzit
 * Copyright © 2008-2012 Marc Andre Tanner
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

#ifndef VT_VT_H
#define VT_VT_H

#include <curses.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

#ifndef NCURSES_MOUSE_VERSION
#define mmask_t unsigned long
#endif

enum {
    /* means escape sequence was handled */
    VT_ESCSEQ_HANDLER_OK,
    /* means the escape sequence was not  recognized yet, but
     * there is hope that it still will once more characters
     * arrive (i.e. it is not yet complete).
     *
     * The library will thus continue collecting characters
     * and calling the handler as each character arrives until
     * either OK or NOWAY is returned.
     */
    VT_ESCSEQ_HANDLER_NOTYET,
    /* means the escape sequence was not recognized, and there
     * is no chance that it will even if more characters  are
     * added to it.
     */
    VT_ESCSEQ_HANDLER_NOWAY
};

typedef struct Vt Vt;
typedef int (*vt_escseq_handler_t)(Vt *, char *es);

enum {
	VT_EVENT_TITLE,
	VT_EVENT_COPY_TEXT,
};

typedef void (*vt_event_handler_t)(Vt *, int event, void *data);

void vt_init(void);
void vt_shutdown(void);
void vt_set_escseq_handler(Vt *, vt_escseq_handler_t);
void vt_set_event_handler(Vt *, vt_event_handler_t);
void vt_set_data(Vt *, void *);
void *vt_get_data(Vt *);
void vt_set_default_colors(Vt *, unsigned attrs, short fg, short bg);

Vt *vt_create(int rows, int cols, int scroll_buf_sz);
void vt_resize(Vt *, int rows, int cols);
void vt_destroy(Vt *);
pid_t vt_forkpty(Vt *, const char *, const char *argv[], const char *envp[], int *pty);
int vt_getpty(Vt *);
unsigned vt_cursor(Vt *t);

int vt_process(Vt *);
void vt_keypress(Vt *, int keycode);
int vt_write(Vt *t, const char *buf, int len);
void vt_mouse(Vt *t, int x, int y, mmask_t mask);
void vt_dirty(Vt *t);
void vt_draw(Vt *, WINDOW *win, int startrow, int startcol);
short vt_color_get(Vt *t, short fg, short bg);
short vt_color_reserve(short fg, short bg);

void vt_scroll(Vt *, int rows);
void vt_noscroll(Vt *);

void vt_bell(Vt *, bool bell);
void vt_togglebell(Vt *);

void vt_copymode_enter(Vt *t);
void vt_copymode_leave(Vt *t);
unsigned vt_copymode(Vt *t);
void vt_copymode_keypress(Vt *t, int keycode);

#endif /* VT_VT_H */

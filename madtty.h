/*
    LICENSE INFORMATION:
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License (LGPL) as published by the Free Software Foundation.

    Please refer to the COPYING file for more information.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

    Copyright © 2004 Bruno T. C. de Oliveira
    Copyright © 2006 Pierre Habouzit
 */

#ifndef MADTTY_MADTTY_H
#define MADTTY_MADTTY_H

#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

enum {
    /* means escape sequence was handled */
    MADTTY_HANDLER_OK,
    /* means the escape sequence was not  recognized yet, but
     * there is hope that it still will once more characters
     * arrive (i.e. it is not yet complete).
     *
     * The library will thus continue collecting characters
     * and calling the handler as each character arrives until
     * either OK or NOWAY is returned.
     */
    MADTTY_HANDLER_NOTYET,
    /* means the escape sequence was not recognized, and there
     * is no chance that it will even if more characters  are
     * added to it.
     */
    MADTTY_HANDLER_NOWAY
};

typedef struct madtty_t madtty_t;
typedef int (*madtty_handler_t)(madtty_t *, char *es);

void madtty_init_colors(void);
void madtty_init_vt100_graphics(void);
void madtty_set_handler(madtty_t *, madtty_handler_t);
void madtty_set_data(madtty_t *, void *);
void *madtty_get_data(madtty_t *);

madtty_t *madtty_create(int rows, int cols, int scroll_buf_sz);
void madtty_resize(madtty_t *, int rows, int cols);
void madtty_destroy(madtty_t *);
pid_t madtty_forkpty(madtty_t *, const char *, const char *argv[], const char *envp[], int *pty);
int madtty_getpty(madtty_t *);
unsigned madtty_cursor(madtty_t *t);

int madtty_process(madtty_t *);
void madtty_keypress(madtty_t *, int keycode);
void madtty_keypress_sequence(madtty_t *, const char *seq);
void madtty_dirty(madtty_t *t);
void madtty_draw(madtty_t *, WINDOW *win, int startrow, int startcol);
void madtty_color_set(WINDOW *win, short fg, short bg);

void madtty_scroll(madtty_t *, int rows);
void madtty_noscroll(madtty_t *);

#endif /* MADTTY_MADTTY_H */

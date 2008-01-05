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

typedef struct madtty_t madtty_t;

void madtty_init_colors(void);
void madtty_init_vt100_graphics(void);

int madtty_color_pair(int fg, int bg);

madtty_t *madtty_create(int rows, int cols);
void madtty_resize(madtty_t *, int rows, int cols);
void madtty_destroy(madtty_t *);
pid_t madtty_forkpty(madtty_t *, const char *, const char *argv[], int *pty);
int madtty_getpty(madtty_t *);

int madtty_process(madtty_t *);
void madtty_keypress(madtty_t *, int keycode);
void madtty_draw(madtty_t *, WINDOW *win, int startrow, int startcol);

#endif /* MADTTY_MADTTY_H */

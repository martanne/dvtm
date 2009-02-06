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
    Copyright © 2008 Marc Andre Tanner
 */

#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <wchar.h>
#ifdef __linux__
# include <pty.h>
#elif defined(__FreeBSD__)
# include <libutil.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__)
# include <util.h>
#endif
#ifdef __CYGWIN__
# include <alloca.h>
#endif

#include "madtty.h"

#ifndef NCURSES_ATTR_SHIFT
# define NCURSES_ATTR_SHIFT 8
#endif

#define IS_CONTROL(ch) !((ch) & 0xffffff60UL)

static int has_default, is_utf8, use_palette;

static const unsigned palette_start = 1;
static const unsigned palette_end   = 256;
static unsigned  palette_cur;
static short    *color2palette;
static unsigned  color_hash(short f, short b) { return ((f+1) * (COLORS+1)) + b+1 ; }

enum {
    C0_NUL = 0x00,
            C0_SOH, C0_STX, C0_ETX, C0_EOT, C0_ENQ, C0_ACK, C0_BEL,
    C0_BS , C0_HT , C0_LF , C0_VT , C0_FF , C0_CR , C0_SO , C0_SI ,
    C0_DLE, C0_DC1, C0_DC2, D0_DC3, C0_DC4, C0_NAK, C0_SYN, C0_ETB,
    C0_CAN, C0_EM , C0_SUB, C0_ESC, C0_IS4, C0_IS3, C0_IS2, C0_IS1,
};

enum {
    C1_40 = 0x40,
            C1_41 , C1_BPH, C1_NBH, C1_44 , C1_NEL, C1_SSA, C1_ESA,
    C1_HTS, C1_HTJ, C1_VTS, C1_PLD, C1_PLU, C1_RI , C1_SS2, C1_SS3,
    C1_DCS, C1_PU1, C1_PU2, C1_STS, C1_CCH, C1_MW , C1_SPA, C1_EPA,
    C1_SOS, C1_59 , C1_SCI, C1_CSI, CS_ST , C1_OSC, C1_PM , C1_APC,
};

enum {
    CSI_ICH = 0x40,
             CSI_CUU, CSI_CUD, CSI_CUF, CSI_CUB, CSI_CNL, CSI_CPL, CSI_CHA,
    CSI_CUP, CSI_CHT, CSI_ED , CSI_EL , CSI_IL , CSI_DL , CSI_EF , CSI_EA ,
    CSI_DCH, CSI_SEE, CSI_CPR, CSI_SU , CSI_SD , CSI_NP , CSI_PP , CSI_CTC,
    CSI_ECH, CSI_CVT, CSI_CBT, CSI_SRS, CSI_PTX, CSI_SDS, CSI_SIMD, CSI_5F,
    CSI_HPA, CSI_HPR, CSI_REP, CSI_DA , CSI_VPA, CSI_VPR, CSI_HVP, CSI_TBC,
    CSI_SM , CSI_MC , CSI_HPB, CSI_VPB, CSI_RM , CSI_SGR, CSI_DSR, CSI_DAQ,
    CSI_70 , CSI_71 , CSI_72 , CSI_73 , CSI_74 , CSI_75 , CSI_76 , CSI_77 ,
    CSI_78 , CSI_79 , CSI_7A , CSI_7B , CSI_7C , CSI_7D , CSI_7E , CSI_7F
};

struct madtty_t {
    int   pty;
    pid_t childpid;

    /* flags */
    unsigned seen_input : 1;
    unsigned insert     : 1;
    unsigned escaped    : 1;
    unsigned graphmode  : 1;
    unsigned curshid    : 1;
    unsigned curskeymode: 1;

    /* geometry */
    int rows, cols;
    unsigned curattrs;
    short curfg, curbg;

    /* scrollback buffer */
    struct t_row_t *scroll_buf;
    int    scroll_buf_sz;
    int    scroll_buf_ptr;
    int    scroll_buf_len;
    int    scroll_amount;

    struct t_row_t *lines;
    struct t_row_t *scroll_top;
    struct t_row_t *scroll_bot;

    /* cursor */
    struct t_row_t *curs_row;
    int curs_col, curs_srow, curs_scol;

    /* buffers and parsing state */
    mbstate_t ps;
    char rbuf[BUFSIZ];
    char ebuf[BUFSIZ];
    int  rlen, elen;

    /* custom escape sequence handler */
    madtty_handler_t handler;
    void *data;
};

typedef struct t_row_t {
    wchar_t  *text;
    uint16_t *attr;
    short    *fg;
    short    *bg;
    unsigned dirty : 1;
} t_row_t;

static char const * const keytable[KEY_MAX+1] = {
    ['\n']          = "\r",
    /* for the arrow keys the CSI / SS3 sequences are not stored here
     * because they depend on the current cursor terminal mode
     */
    [KEY_UP]        = "A",
    [KEY_DOWN]      = "B",
    [KEY_RIGHT]     = "C",
    [KEY_LEFT]      = "D",
#ifdef KEY_SUP
    [KEY_SUP]       = "\e[1;2A",
#endif
#ifdef KEY_SDOWN
    [KEY_SDOWN]     = "\e[1;2B",
#endif
    [KEY_SRIGHT]    = "\e[1;2C",
    [KEY_SLEFT]     = "\e[1;2D",
    [KEY_BACKSPACE] = "\177",
    [KEY_IC]        = "\e[2~",
    [KEY_DC]        = "\e[3~",
    [KEY_PPAGE]     = "\e[5~",
    [KEY_NPAGE]     = "\e[6~",
    [KEY_HOME]      = "\e[7~",
    [KEY_END]       = "\e[8~",
    [KEY_SUSPEND]   = "\x1A",  /* Ctrl+Z gets mapped to this */
    [KEY_F(1)]      = "\e[11~",
    [KEY_F(2)]      = "\e[12~",
    [KEY_F(3)]      = "\e[13~",
    [KEY_F(4)]      = "\e[14~",
    [KEY_F(5)]      = "\e[15~",
    [KEY_F(6)]      = "\e[17~",
    [KEY_F(7)]      = "\e[18~",
    [KEY_F(8)]      = "\e[19~",
    [KEY_F(9)]      = "\e[20~",
    [KEY_F(10)]     = "\e[21~",
    [KEY_F(11)]     = "\e[23~",
    [KEY_F(12)]     = "\e[24~",
    [KEY_F(13)]     = "\e[25~",
    [KEY_F(14)]     = "\e[26~",
    [KEY_F(15)]     = "\e[28~",
    [KEY_F(16)]     = "\e[29~",
    [KEY_F(17)]     = "\e[31~",
    [KEY_F(18)]     = "\e[32~",
    [KEY_F(19)]     = "\e[33~",
    [KEY_F(20)]     = "\e[34~",
};

__attribute__((const))
static uint16_t build_attrs(unsigned curattrs)
{
    return ((curattrs & ~A_COLOR) | COLOR_PAIR(curattrs & 0xff))
        >> NCURSES_ATTR_SHIFT;
}

static void t_row_set(t_row_t *row, int start, int len, madtty_t *t)
{
    row->dirty = true;
    wmemset(row->text + start, 0, len);
    attr_t attr = t ? build_attrs(t->curattrs) : 0;
    short  fg   = t ? t->curfg : -1;
    short  bg   = t ? t->curbg : -1;
    for (int i = start; i < len + start; i++) {
        row->attr[i] = attr;
        row->fg  [i] = fg;
        row->bg  [i] = bg;
    }
}

static void t_row_roll(t_row_t *start, t_row_t *end, int count)
{
    int n = end - start;

    count %= n;
    if (count < 0)
        count += n;

    if (count) {
        t_row_t *buf = alloca(count * sizeof(t_row_t));

        memcpy(buf, start, count * sizeof(t_row_t));
        memmove(start, start + count, (n - count) * sizeof(t_row_t));
        memcpy(end - count, buf, count * sizeof(t_row_t));
        for (t_row_t *row = start; row < end; row++) {
            row->dirty = true;
        }
    }
}

static void clamp_cursor_to_bounds(madtty_t *t)
{
    if (t->curs_row < t->lines) {
        t->curs_row = t->lines;
    }
    if (t->curs_row >= t->lines + t->rows) {
        t->curs_row = t->lines + t->rows - 1;
    }

    if (t->curs_col < 0) {
        t->curs_col = 0;
    }
    if (t->curs_col >= t->cols) {
        t->curs_col = t->cols - 1;
    }
}

static void fill_scroll_buf(madtty_t *t, int s)
{
    /* work in screenfuls */
    int ssz = t->scroll_bot - t->scroll_top;
    if (s > ssz) {
	fill_scroll_buf(t, ssz);
	fill_scroll_buf(t, s-ssz);
	return;
    }
    if (s < -ssz) {
	fill_scroll_buf(t, -ssz);
	fill_scroll_buf(t, s + ssz);
	return;
    }

    t->scroll_buf_len += s;
    if (t->scroll_buf_len >= t->scroll_buf_sz)
	t->scroll_buf_len = t->scroll_buf_sz;

    if (s > 0 && t->scroll_buf_sz) {
	for (int i = 0; i < s; i++) {
	    struct t_row_t tmp = t->scroll_top[i];
	    t->scroll_top[i] = t->scroll_buf[t->scroll_buf_ptr];
	    t->scroll_buf[t->scroll_buf_ptr] = tmp;

	    t->scroll_buf_ptr++;
	    if (t->scroll_buf_ptr == t->scroll_buf_sz)
		t->scroll_buf_ptr = 0;
	}
    }
    t_row_roll(t->scroll_top, t->scroll_bot, s);
    if (s < 0 && t->scroll_buf_sz) {
	for (int i = (-s)-1; i >= 0; i--) {
	    t->scroll_buf_ptr--;
	    if (t->scroll_buf_ptr == -1)
		t->scroll_buf_ptr = t->scroll_buf_sz - 1;

	    struct t_row_t tmp = t->scroll_top[i];
	    t->scroll_top[i] = t->scroll_buf[t->scroll_buf_ptr];
	    t->scroll_buf[t->scroll_buf_ptr] = tmp;
	    t->scroll_top[i].dirty = true;
	}
    }
}

static void cursor_line_down(madtty_t *t)
{
    t->curs_row++;
    if (t->curs_row < t->scroll_bot)
        return;

    madtty_noscroll(t);

    t->curs_row = t->scroll_bot - 1;
    fill_scroll_buf(t, 1);
    t_row_set(t->curs_row, 0, t->cols, t);
}

static void new_escape_sequence(madtty_t *t)
{
    t->escaped = true;
    t->elen    = 0;
    t->ebuf[0] = '\0';
}

static void cancel_escape_sequence(madtty_t *t)
{
    t->escaped = false;
    t->elen    = 0;
    t->ebuf[0] = '\0';
}

static bool is_valid_csi_ender(int c)
{
    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c == '@' || c == '`');
}

/* interprets a 'set attribute' (SGR) CSI escape sequence */
static void interpret_csi_SGR(madtty_t *t, int param[], int pcount)
{
    int i;

    if (pcount == 0) {
        /* special case: reset attributes */
        t->curattrs = A_NORMAL;
        t->curfg = t->curbg = -1;
        return;
    }

    for (i = 0; i < pcount; i++) {
        switch (param[i]) {
#define CASE(x, op)  case x: op; break
            CASE(0,  t->curattrs = A_NORMAL; t->curfg = t->curbg = -1);
            CASE(1,  t->curattrs |= A_BOLD);
            CASE(4,  t->curattrs |= A_UNDERLINE);
            CASE(5,  t->curattrs |= A_BLINK);
            CASE(6,  t->curattrs |= A_BLINK);
            CASE(7,  t->curattrs |= A_REVERSE);
            CASE(8,  t->curattrs |= A_INVIS);
            CASE(22, t->curattrs &= ~A_BOLD);
            CASE(24, t->curattrs &= ~A_UNDERLINE);
            CASE(25, t->curattrs &= ~A_BLINK);
            CASE(27, t->curattrs &= ~A_REVERSE);
            CASE(28, t->curattrs &= ~A_INVIS);

          case 30 ... 37: /* fg */
            t->curfg = param[i]-30;
            break;

          case 38:
            assert(param[i+1] == 5);
            t->curfg = param[i+2];
            i+=2;
            break;

          case 39:
            t->curfg = -1;
            break;

          case 40 ... 47: /* bg */
            t->curbg = param[i] - 40;
            break;

          case 48:
            assert(param[i+1] == 5);
            t->curbg = param[i+2];
            i+=2;
            break;

          case 49:
            t->curbg = -1;
            break;

          default:
            break;
        }
    }
}

/* interprets an 'erase display' (ED) escape sequence */
static void interpret_csi_ED(madtty_t *t, int param[], int pcount)
{
    t_row_t *row, *start, *end;

    /* decide range */
    if (pcount && param[0] == 2) {
        start = t->lines;
        end   = t->lines + t->rows;
    } else
    if (pcount && param[0] == 1) {
        start = t->lines;
        end   = t->curs_row;
        t_row_set(t->curs_row, 0, t->curs_col + 1, t);
    } else {
        t_row_set(t->curs_row, t->curs_col,
                     t->cols - t->curs_col, t);
        start = t->curs_row + 1;
        end   = t->lines + t->rows;
    }

    for (row = start; row < end; row++) {
        t_row_set(row, 0, t->cols, t);
    }
}

/* interprets a 'move cursor' (CUP) escape sequence */
static void interpret_csi_CUP(madtty_t *t, int param[], int pcount)
{
    if (pcount == 0) {
        /* special case */
        t->curs_row = t->lines;
        t->curs_col = 0;
        return;
    } else
    if (pcount < 2) {
        return;  /* malformed */
    }

    t->curs_row = t->lines + param[0] - 1;
    t->curs_col = param[1] - 1;

    clamp_cursor_to_bounds(t);
}

/* Interpret the 'relative mode' sequences: CUU, CUD, CUF, CUB, CNL,
 * CPL, CHA, HPR, VPA, VPR, HPA */
static void
interpret_csi_C(madtty_t *t, char verb, int param[], int pcount)
{
    int n = (pcount && param[0] > 0) ? param[0] : 1;

    switch (verb) {
      case 'A':           t->curs_row -= n; break;
      case 'B': case 'e': t->curs_row += n; break;
      case 'C': case 'a': t->curs_col += n; break;
      case 'D':           t->curs_col -= n; break;
      case 'E':           t->curs_row += n; t->curs_col = 0; break;
      case 'F':           t->curs_row -= n; t->curs_col = 0; break;
      case 'G': case '`': t->curs_col  = param[0] - 1; break;
      case 'd':           t->curs_row  = t->lines + param[0] - 1; break;
    }

    clamp_cursor_to_bounds(t);
}

/* Interpret the 'erase line' escape sequence */
static void interpret_csi_EL(madtty_t *t, int param[], int pcount)
{
    switch (pcount ? param[0] : 0) {
      case 1:
        t_row_set(t->curs_row, 0, t->curs_col + 1, t);
        break;
      case 2:
        t_row_set(t->curs_row, 0, t->cols, t);
        break;
      default:
        t_row_set(t->curs_row, t->curs_col, t->cols - t->curs_col, t);
        break;
    }
}

/* Interpret the 'insert blanks' sequence (ICH) */
static void interpret_csi_ICH(madtty_t *t, int param[], int pcount)
{
    t_row_t *row = t->curs_row;
    int n = (pcount && param[0] > 0) ? param[0] : 1;
    int i;

    if (t->curs_col + n > t->cols) {
        n = t->cols - t->curs_col;
    }

    for (i = t->cols - 1; i >= t->curs_col + n; i--) {
        row->text[i] = row->text[i - n];
        row->attr[i] = row->attr[i - n];
        row->bg  [i] = row->bg  [i - n];
        row->fg  [i] = row->fg  [i - n];
    }

    t_row_set(row, t->curs_col, n, t);
}

/* Interpret the 'delete chars' sequence (DCH) */
static void interpret_csi_DCH(madtty_t *t, int param[], int pcount)
{
    t_row_t *row = t->curs_row;
    int n = (pcount && param[0] > 0) ? param[0] : 1;
    int i;

    if (t->curs_col + n > t->cols) {
        n = t->cols - t->curs_col;
    }

    for (i = t->curs_col; i < t->cols - n; i++) {
        row->text[i] = row->text[i + n];
        row->attr[i] = row->attr[i + n];
        row->bg  [i] = row->bg  [i + n];
        row->fg  [i] = row->fg  [i + n];
    }

    t_row_set(row, t->cols - n, n, t);
}

/* Interpret a 'scroll reverse' (SR) */
static void interpret_csi_SR(madtty_t *t)
{
    t_row_roll(t->scroll_top, t->scroll_bot, -1);
    t_row_set(t->scroll_top, 0, t->cols, t);
}

/* Interpret an 'insert line' sequence (IL) */
static void interpret_csi_IL(madtty_t *t, int param[], int pcount)
{
    int n = (pcount && param[0] > 0) ? param[0] : 1;

    if (t->curs_row + n >= t->scroll_bot) {
        for (t_row_t *row = t->curs_row; row < t->scroll_bot; row++) {
            t_row_set(row, 0, t->cols, t);
        }
    } else {
        t_row_roll(t->curs_row, t->scroll_bot, -n);
        for (t_row_t *row = t->curs_row; row < t->curs_row + n; row++) {
            t_row_set(row, 0, t->cols, t);
        }
    }
}

/* Interpret a 'delete line' sequence (DL) */
static void interpret_csi_DL(madtty_t *t, int param[], int pcount)
{
    int n = (pcount && param[0] > 0) ? param[0] : 1;

    if (t->curs_row + n >= t->scroll_bot) {
        for (t_row_t *row = t->curs_row; row < t->scroll_bot; row++) {
            t_row_set(row, 0, t->cols, t);
        }
    } else {
        t_row_roll(t->curs_row, t->scroll_bot, n);
        for (t_row_t *row = t->scroll_bot - n; row < t->scroll_bot; row++) {
            t_row_set(row, 0, t->cols, t);
        }
    }
}

/* Interpret an 'erase characters' (ECH) sequence */
static void interpret_csi_ECH(madtty_t *t, int param[], int pcount)
{
    int n = (pcount && param[0] > 0) ? param[0] : 1;

    if (t->curs_col + n < t->cols) {
        n = t->cols - t->curs_col;
    }
    t_row_set(t->curs_row, t->curs_col, n, t);
}

/* Interpret a 'set scrolling region' (DECSTBM) sequence */
static void interpret_csi_DECSTBM(madtty_t *t, int param[], int pcount)
{
    int new_top, new_bot;

    switch (pcount) {
      case 0:
        t->scroll_top = t->lines;
        t->scroll_bot = t->lines + t->rows;
        break;
      default:
        return; /* malformed */

      case 2:
        new_top = param[0] - 1;
        new_bot = param[1];

        /* clamp to bounds */
        if (new_top < 0)
            new_top = 0;
        if (new_top >= t->rows)
            new_top = t->rows - 1;
        if (new_bot < 0)
            new_bot = 0;
        if (new_bot >= t->rows)
            new_bot = t->rows;

        /* check for range validity */
        if (new_top < new_bot) {
            t->scroll_top = t->lines + new_top;
            t->scroll_bot = t->lines + new_bot;
        }
        break;
    }
}

static void es_interpret_csi(madtty_t *t)
{
    static int csiparam[BUFSIZ];
    int param_count = 0;
    const char *p = t->ebuf + 1;
    char verb = t->ebuf[t->elen - 1];

    p += t->ebuf[1] == '?'; /* CSI private mode */

    /* parse numeric parameters */
    while (isdigit((unsigned char)*p) || *p == ';') {
        if (*p == ';') {
            if (param_count >= (int)sizeof(csiparam))
                return; /* too long! */
            csiparam[param_count++] = 0;
        } else {
            if (param_count == 0) csiparam[param_count++] = 0;
            csiparam[param_count - 1] *= 10;
            csiparam[param_count - 1] += *p - '0';
        }

        p++;
    }

    if (t->ebuf[1] == '?') {
        switch (verb) {
          case 'l':
            if (csiparam[0] == 25)
                t->curshid = true;
            if (csiparam[0] == 1) /* DECCKM: reset ANSI cursor (normal) key mode */
                t->curskeymode = 0;
            if (csiparam[0] == 47) {
                /* use normal screen buffer */
                t->curattrs = A_NORMAL;
                t->curfg = t->curbg = -1;
            }
            break;

          case 'h':
            if (csiparam[0] == 25)
                t->curshid = false;
            if (csiparam[0] == 1) /* DECCKM: set ANSI cursor (application) key mode */
                t->curskeymode = 1;
            if (csiparam[0] == 47) {
                /* use alternate screen buffer */
                t->curattrs = A_NORMAL;
                t->curfg = t->curbg = -1;
            }
            break;
        }
    }

    /* delegate handling depending on command character (verb) */
    switch (verb) {
      case 'h':
        if (param_count == 1 && csiparam[0] == 4) /* insert mode */
            t->insert = true;
        break;
      case 'l':
        if (param_count == 1 && csiparam[0] == 4) /* replace mode */
            t->insert = false;
        break;
      case 'm': /* it's a 'set attribute' sequence */
        interpret_csi_SGR(t, csiparam, param_count); break;
      case 'J': /* it's an 'erase display' sequence */
        interpret_csi_ED(t, csiparam, param_count); break;
      case 'H': case 'f': /* it's a 'move cursor' sequence */
        interpret_csi_CUP(t, csiparam, param_count); break;
      case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
      case 'e': case 'a': case 'd': case '`':
        /* it is a 'relative move' */
        interpret_csi_C(t, verb, csiparam, param_count); break;
      case 'K': /* erase line */
        interpret_csi_EL(t, csiparam, param_count); break;
      case '@': /* insert characters */
        interpret_csi_ICH(t, csiparam, param_count); break;
      case 'P': /* delete characters */
        interpret_csi_DCH(t, csiparam, param_count); break;
      case 'L': /* insert lines */
        interpret_csi_IL(t, csiparam, param_count); break;
      case 'M': /* delete lines */
        interpret_csi_DL(t, csiparam, param_count); break;
      case 'X': /* erase chars */
        interpret_csi_ECH(t, csiparam, param_count); break;
      case 'r': /* set scrolling region */
        interpret_csi_DECSTBM(t, csiparam, param_count); break;
      case 's': /* save cursor location */
        t->curs_srow = t->curs_row - t->lines;
        t->curs_scol = t->curs_col;
        break;
      case 'u': /* restore cursor location */
        t->curs_row = t->lines + t->curs_srow;
        t->curs_col = t->curs_scol;
        clamp_cursor_to_bounds(t);
        break;
      default:
        break;
    }
}

static void try_interpret_escape_seq(madtty_t *t)
{
    char lastchar  = t->ebuf[t->elen-1];
    if(!*t->ebuf)
       return;
    if(t->handler){
       switch((*(t->handler))(t, t->ebuf)){
          case MADTTY_HANDLER_OK:
	     goto cancel;
          case MADTTY_HANDLER_NOTYET:
             if (t->elen + 1 >= (int)sizeof(t->ebuf))
                 goto cancel;
	     return;
       }
    }
    switch (*t->ebuf) {
      case 'M':
        interpret_csi_SR(t);
        cancel_escape_sequence(t);
        return;

      case '(':
      case ')':
        if (t->elen == 2)
            goto cancel;
        break;

      case ']': /* xterm thing */
        if (lastchar == '\a')
            goto cancel;
        break;

      default:
        goto cancel;

      case '[':
        if (is_valid_csi_ender(lastchar)) {
            es_interpret_csi(t);
            cancel_escape_sequence(t);
            return;
        }
        break;
    }

    if (t->elen + 1 >= (int)sizeof(t->ebuf)) {
cancel:
#ifndef NDEBUG
        fprintf(stderr, "cancelled: \\033");
        for (int i = 0; i < (int)t->elen; i++) {
            if (isprint(t->ebuf[i])) {
                fputc(t->ebuf[i], stderr);
            } else {
                fprintf(stderr, "\\%03o", t->ebuf[i]);
            }
        }
        fputc('\n', stderr);
#endif
        cancel_escape_sequence(t);
    }
}

static void madtty_process_nonprinting(madtty_t *t, wchar_t wc)
{
    switch (wc) {
      case C0_ESC:
        new_escape_sequence(t);
        break;

      case C0_BEL:
        /* do nothing for now... maybe a visual bell would be nice? */
        break;

      case C0_BS:
        if (t->curs_col > 0)
            t->curs_col--;
        break;

      case C0_HT: /* tab */
        t->curs_col = (t->curs_col + 8) & ~7;
        if (t->curs_col >= t->cols)
            t->curs_col = t->cols - 1;
        break;

      case C0_CR:
        t->curs_col = 0;
        break;

      case C0_VT:
      case C0_FF:
      case C0_LF:
        cursor_line_down(t);
        break;

      case C0_SO:		/* shift out - acs */
        t->graphmode = true;
        break;
      case C0_SI:		/* shift in - acs */
        t->graphmode = false;
        break;
    }
}

static void is_utf8_locale(void)
{
    const char *cset = nl_langinfo(CODESET) ?: "ANSI_X3.4-1968";
    is_utf8 = !strcmp(cset, "UTF-8");
}

// vt100 special graphics and line drawing
// 5f-7e standard vt100
// 40-5e rxvt extension for extra curses acs chars
static uint16_t const vt100_utf8[62] = { // 41 .. 7e
            0x2191, 0x2193, 0x2192, 0x2190, 0x2588, 0x259a, 0x2603, // 41-47 hi mr. snowman!
         0,      0,      0,      0,      0,      0,      0,      0, // 48-4f
         0,      0,      0,      0,      0,      0,      0,      0, // 50-57
         0,      0,      0,      0,      0,      0,      0, 0x0020, // 58-5f
    0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1, // 60-67
    0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba, // 68-6f
    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c, // 70-77
    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,         // 78-7e
};

static uint32_t vt100[62];

void madtty_init_vt100_graphics(void)
{
    vt100['l' - 0x41] = ACS_ULCORNER;
    vt100['m' - 0x41] = ACS_LLCORNER;
    vt100['k' - 0x41] = ACS_URCORNER;
    vt100['j' - 0x41] = ACS_LRCORNER;
    vt100['u' - 0x41] = ACS_RTEE;
    vt100['t' - 0x41] = ACS_LTEE;
    vt100['v' - 0x41] = ACS_TTEE;
    vt100['w' - 0x41] = ACS_BTEE;
    vt100['q' - 0x41] = ACS_HLINE;
    vt100['x' - 0x41] = ACS_VLINE;
    vt100['n' - 0x41] = ACS_PLUS;
    vt100['o' - 0x41] = ACS_S1;
    vt100['s' - 0x41] = ACS_S9;
    vt100['`' - 0x41] = ACS_DIAMOND;
    vt100['a' - 0x41] = ACS_CKBOARD;
    vt100['f' - 0x41] = ACS_DEGREE;
    vt100['g' - 0x41] = ACS_PLMINUS;
    vt100['~' - 0x41] = ACS_BULLET;
#if 0 /* out of bounds */
    vt100[',' - 0x41] = ACS_LARROW;
    vt100['+' - 0x41] = ACS_RARROW;
    vt100['.' - 0x41] = ACS_DARROW;
    vt100['-' - 0x41] = ACS_UARROW;
    vt100['0' - 0x41] = ACS_BLOCK;
#endif
    vt100['h' - 0x41] = ACS_BOARD;
    vt100['i' - 0x41] = ACS_LANTERN;
    /* these defaults were invented for ncurses */
    vt100['p' - 0x41] = ACS_S3;
    vt100['r' - 0x41] = ACS_S7;
    vt100['y' - 0x41] = ACS_LEQUAL;
    vt100['z' - 0x41] = ACS_GEQUAL;
    vt100['{' - 0x41] = ACS_PI;
    vt100['|' - 0x41] = ACS_NEQUAL;
    vt100['}' - 0x41] = ACS_STERLING;
    is_utf8_locale();
}

static void madtty_putc(madtty_t *t, wchar_t wc)
{
    int width = 0;

    if (!t->seen_input) {
        t->seen_input = 1;
        kill(-t->childpid, SIGWINCH);
    }

    if (t->escaped) {
        assert (t->elen + 1 < (int)sizeof(t->ebuf));
        t->ebuf[t->elen]   = wc;
        t->ebuf[++t->elen] = '\0';
        try_interpret_escape_seq(t);
    } else if (IS_CONTROL(wc)) {
        madtty_process_nonprinting(t, wc);
    } else {
        t_row_t *tmp;

        if (t->graphmode) {
            if (wc >= 0x41 && wc <= 0x7e) {
                wchar_t gc = is_utf8 ? vt100_utf8[wc - 0x41] : vt100[wc - 0x41];
                if (gc)
                    wc = gc;
            }
            width = 1;
        } else {
            width = wcwidth(wc) ?: 1;
        }

        if (width == 2 && t->curs_col == t->cols - 1) {
            tmp = t->curs_row;
            tmp->dirty = true;
            tmp->text[t->curs_col] = 0;
            tmp->attr[t->curs_col] = build_attrs(t->curattrs);
            tmp->bg[t->curs_col] = t->curbg;
            tmp->fg[t->curs_col] = t->curfg;
            t->curs_col++;
        }

        if (t->curs_col >= t->cols) {
            t->curs_col = 0;
            cursor_line_down(t);
        }

        tmp = t->curs_row;
        tmp->dirty = true;

        if (t->insert) {
            wmemmove(tmp->text + t->curs_col + width, tmp->text + t->curs_col,
                     (t->cols - t->curs_col - width));
            memmove(tmp->attr + t->curs_col + width, tmp->attr + t->curs_col,
                    (t->cols - t->curs_col - width) * sizeof(tmp->attr[0]));
            memmove(tmp->fg + t->curs_col + width, tmp->fg + t->curs_col,
                    (t->cols - t->curs_col - width) * sizeof(tmp->fg[0]));
            memmove(tmp->bg + t->curs_col + width, tmp->bg + t->curs_col,
                    (t->cols - t->curs_col - width) * sizeof(tmp->bg[0]));
        }

        tmp->text[t->curs_col] = wc;
        tmp->attr[t->curs_col] = build_attrs(t->curattrs);
        tmp->bg[t->curs_col] = t->curbg;
        tmp->fg[t->curs_col] = t->curfg;
        t->curs_col++;
        if (width == 2) {
            tmp->text[t->curs_col] = 0;
            tmp->attr[t->curs_col] = build_attrs(t->curattrs);
            tmp->bg[t->curs_col] = t->curbg;
            tmp->fg[t->curs_col] = t->curfg;
            t->curs_col++;
        }
    }
}

int madtty_process(madtty_t *t)
{
    int res, pos = 0;

    if (t->pty < 0) {
        errno = EINVAL;
        return -1;
    }

    res = read(t->pty, t->rbuf + t->rlen, sizeof(t->rbuf) - t->rlen);
    if (res < 0)
        return -1;

    t->rlen += res;
    while (pos < t->rlen) {
        wchar_t wc;
        ssize_t len;

        len = (ssize_t)mbrtowc(&wc, t->rbuf + pos, t->rlen - pos, &t->ps);
        if (len == -2) {
            t->rlen -= pos;
            memmove(t->rbuf, t->rbuf + pos, t->rlen);
            return 0;
        }

        if (len == -1) {
            len = 1;
            wc  = t->rbuf[pos];
        }

        pos += len ? len : 1;
        madtty_putc(t, wc);
    }

    t->rlen -= pos;
    memmove(t->rbuf, t->rbuf + pos, t->rlen);
    return 0;
}

madtty_t *madtty_create(int rows, int cols, int scroll_buf_sz)
{
    madtty_t *t;
    int i;

    if (rows <= 0 || cols <= 0)
        return NULL;

    t = (madtty_t*)calloc(sizeof(madtty_t), 1);
    if (!t)
        return NULL;

    /* record dimensions */
    t->rows = rows;
    t->cols = cols;

    /* default mode is replace */
    t->insert = false;

    /* create the cell matrix */
    t->lines = (t_row_t*)calloc(sizeof(t_row_t), t->rows);
    for (i = 0; i < t->rows; i++) {
        t->lines[i].text = (wchar_t *)calloc(sizeof(wchar_t), t->cols);
        t->lines[i].attr = (uint16_t *)calloc(sizeof(uint16_t), t->cols);
        t->lines[i].fg   = calloc(sizeof(short), t->cols);
        t->lines[i].bg   = calloc(sizeof(short), t->cols);
    }

    t->pty = -1;  /* no pty for now */

    /* initialization of other public fields */
    t->curs_row = t->lines;
    t->curs_col = 0;
    t->curattrs = A_NORMAL;  /* white text over black background */
    t->curfg = t->curbg = -1;

    /* initial scrolling area is the whole window */
    t->scroll_top = t->lines;
    t->scroll_bot = t->lines + t->rows;

    /* scrollback buffer */
    if (scroll_buf_sz < 0)
        scroll_buf_sz = 0;
    t->scroll_buf_sz = scroll_buf_sz;
    t->scroll_buf = calloc(sizeof(t_row_t), t->scroll_buf_sz);
    for (i = 0; i < t->scroll_buf_sz; i++) {
        t->scroll_buf[i].text = calloc(sizeof(wchar_t),  t->cols);
        t->scroll_buf[i].attr = calloc(sizeof(uint16_t), t->cols);
        t->scroll_buf[i].fg   = calloc(sizeof(short), t->cols);
        t->scroll_buf[i].bg   = calloc(sizeof(short), t->cols);
    }
    t->scroll_buf_ptr = t->scroll_buf_len = 0;
    t->scroll_amount = 0;
    return t;
}

void madtty_resize(madtty_t *t, int rows, int cols)
{
    struct winsize ws = { .ws_row = rows, .ws_col = cols };
    t_row_t *lines = t->lines;

    if (rows <= 0 || cols <= 0)
        return;

    madtty_noscroll(t);

    if (t->rows != rows) {
        if (t->curs_row > lines+rows) {
            /* scroll up instead of simply chopping off bottom */
            fill_scroll_buf(t, t->rows - rows);
        }
        while (t->rows > rows) {
            free(lines[t->rows - 1].text);
            free(lines[t->rows - 1].attr);
            t->rows--;
        }

        lines = realloc(lines, sizeof(t_row_t) * rows);
    }

    if (t->cols != cols) {
        for (int row = 0; row < t->rows; row++) {
            lines[row].text = realloc(lines[row].text, sizeof(wchar_t) * cols);
            lines[row].attr = realloc(lines[row].attr, sizeof(uint16_t) * cols);
            lines[row].fg   = realloc(lines[row].fg, sizeof(short) * cols);
            lines[row].bg   = realloc(lines[row].bg, sizeof(short) * cols);
            if (t->cols < cols)
                t_row_set(lines + row, t->cols, cols - t->cols, 0);
            else
                lines[row].dirty = true;
        }
	t_row_t *sbuf = t->scroll_buf;
        for (int row = 0; row < t->scroll_buf_sz; row++) {
            sbuf[row].text = realloc(sbuf[row].text, sizeof(wchar_t) * cols);
            sbuf[row].attr = realloc(sbuf[row].attr, sizeof(uint16_t) * cols);
            sbuf[row].fg   = realloc(sbuf[row].fg, sizeof(short) * cols);
            sbuf[row].bg   = realloc(sbuf[row].bg, sizeof(short) * cols);
            if (t->cols < cols)
                t_row_set(sbuf + row, t->cols, cols - t->cols, 0);
        }
        t->cols = cols;
    }

    while (t->rows < rows) {
        lines[t->rows].text = (wchar_t *)calloc(sizeof(wchar_t), cols);
        lines[t->rows].attr = (uint16_t *)calloc(sizeof(uint16_t), cols);
        lines[t->rows].fg   = calloc(sizeof(short), cols);
        lines[t->rows].bg   = calloc(sizeof(short), cols);
        t_row_set(lines + t->rows, 0, t->cols, 0);
        t->rows++;
    }

    t->curs_row   += lines - t->lines;
    t->scroll_top = lines;
    t->scroll_bot = lines + rows;
    t->lines = lines;
    clamp_cursor_to_bounds(t);
    ioctl(t->pty, TIOCSWINSZ, &ws);
    kill(-t->childpid, SIGWINCH);
}

void madtty_destroy(madtty_t *t)
{
    int i;
    if (!t)
        return;

    for (i = 0; i < t->rows; i++) {
        free(t->lines[i].text);
        free(t->lines[i].attr);
        free(t->lines[i].fg);
        free(t->lines[i].bg);
    }
    free(t->lines);
    for (i = 0; i < t->scroll_buf_sz; i++) {
        free(t->scroll_buf[i].text);
        free(t->scroll_buf[i].attr);
	free(t->scroll_buf[i].fg);
	free(t->scroll_buf[i].bg);
    }
    free(t->scroll_buf);
    free(t);
}

void madtty_dirty(madtty_t *t)
{
    for (int i = 0; i < t->rows; i++)
        t->lines[i].dirty = true;
}

void madtty_draw(madtty_t *t, WINDOW *win, int srow, int scol)
{
    curs_set(0);
    for (int i = 0; i < t->rows; i++) {
        t_row_t *row = t->lines + i;


        if (!row->dirty)
            continue;

        wmove(win, srow + i, scol);
        for (int j = 0; j < t->cols; j++) {
            if (!j 
                || row->attr[j] != row->attr[j - 1]
                || row->fg[j] != row->fg[j-1] 
                || row->bg[j] != row->bg[j-1]) {
                wattrset(win, (attr_t)row->attr[j] << NCURSES_ATTR_SHIFT);
                madtty_color_set(win, row->fg[j], row->bg[j]);
            }
            if (is_utf8 && row->text[j] >= 128) {
                char buf[MB_CUR_MAX + 1];
                int len;

                len = wcrtomb(buf, row->text[j], NULL);
                waddnstr(win, buf, len);
                if (wcwidth(row->text[j]) > 1)
                    j++;
            } else {
                waddch(win, row->text[j] > ' ' ? row->text[j] : ' ');
            }
        }
        row->dirty = false;
    }

    wmove(win, srow + t->curs_row - t->lines, scol + t->curs_col);
    curs_set(madtty_cursor(t));
}

void madtty_scroll(madtty_t *t, int rows)
{
    if (rows < 0) {
	/* scroll back */
	if (rows < -t->scroll_buf_len)
	    rows = -t->scroll_buf_len;
    } else {
	/* scroll forward */
	if (rows > t->scroll_amount)
	    rows = t->scroll_amount;
    }
    fill_scroll_buf(t, rows);
    t->scroll_amount -= rows;
}

void madtty_noscroll(madtty_t *t)
{
    if (t->scroll_amount)
	madtty_scroll(t, t->scroll_amount);
}

/******************************************************/

pid_t madtty_forkpty(madtty_t *t, const char *p, const char *argv[], const char *env[], int *pty)
{
    struct winsize ws;
    pid_t pid;
    const char **envp = env;

    ws.ws_row    = t->rows;
    ws.ws_col    = t->cols;
    ws.ws_xpixel = ws.ws_ypixel = 0;

    pid = forkpty(&t->pty, NULL, NULL, &ws);
    if (pid < 0)
        return -1;

    if (pid == 0) {
        setsid();
        while (envp && envp[0]) {
            setenv(envp[0], envp[1], 1);
            envp += 2;
        }
        setenv("TERM", use_palette ? "rxvt-256color" : "rxvt", 1);
        execv(p, (char *const*)argv);
        fprintf(stderr, "\nexecv() failed.\nCommand: '%s'\n", argv[0]);
        exit(1);
    }

    if (pty)
        *pty = t->pty;
    return t->childpid = pid;
}

int madtty_getpty(madtty_t *t)
{
    return t->pty;
}

static void term_write(madtty_t *t, const char *buf, int len)
{
    while (len > 0) {
        int res = write(t->pty, buf, len);
        if (res < 0 && errno != EAGAIN && errno != EINTR)
            return;

        buf += res;
        len -= res;
    }
}

void madtty_keypress(madtty_t *t, int keycode)
{
    char c = (char)keycode;

    madtty_noscroll(t);

    if (keycode >= 0 && keycode < KEY_MAX && keytable[keycode]) {
        switch(keycode) {
            case KEY_UP:
            case KEY_DOWN:
            case KEY_RIGHT:
            case KEY_LEFT: {
                char keyseq[3] = { '\e', (t->curskeymode ? 'O' : '['), keytable[keycode][0] };
                term_write(t, keyseq, 3);
                break;
            }
            default:
                term_write(t, keytable[keycode], strlen(keytable[keycode]));
        }
    } else
        term_write(t, &c, 1);
}

void madtty_keypress_sequence(madtty_t *t, const char *seq)
{
    int key, len = strlen(seq);
    /* check for function keys from putty, this makes the
     * keypad work but it's probably not the right way to
     * do it. the sequence we look for is \eO + a character
     * representing the number.
     */
    if(len == 3 && seq[0] == '\e' && seq[1] == 'O') {
        key = seq[2] - 64;
        if(key >= '0' && key <= '9')
            madtty_keypress(t, key);
    } else
        term_write(t, seq, len);
}

void madtty_color_set(WINDOW *win, short fg, short bg)
{
    if (use_palette) {
        if (fg == -1 && bg == -1) {
            wcolor_set(win, 0, 0);
        } else {
            unsigned c = color_hash(fg, bg);
            if (color2palette[c] == 0) {
                short f, g;
                color2palette[c] = palette_cur;
                pair_content(palette_cur, &f, &g);
                color2palette[color_hash(f,g)] = 0;
                init_pair(palette_cur, fg, bg);
                palette_cur++;
                if (palette_cur >= palette_end) {
                    palette_cur = palette_start;
		    /* possibly use mvwinch/mvchgat to update palette */
		}
            }
            wcolor_set(win, color2palette[c], 0);
        }
    } else {
        if (has_default) {
            wcolor_set(win, (fg+1)*16 + bg+1, 0);
        } else {
            if (fg==-1) fg = COLOR_WHITE;
            if (bg==-1) bg = COLOR_BLACK;
            wcolor_set(win, (7-fg)*8 + bg, 0);
        }
    }
}

void madtty_init_colors(void)
{
    int use_default = use_default_colors() == OK;
    use_palette = 0;

    if (COLORS >= 256 && COLOR_PAIRS >= 256) {
        use_palette = 1;
        has_default = 1;

        color2palette = calloc((COLORS+1)*(COLORS+1), sizeof(short));
        int bg = 0, fg = 0;
        for (int i = palette_start; i < palette_end; i++) {
            init_pair(i, bg, fg);
            color2palette[color_hash(bg,fg)] = i;
            if (++fg == COLORS) {
                fg = 0;
                bg++;
            }
        }
        palette_cur = palette_start;
    } else if (COLOR_PAIRS > 64) {
        has_default = 1;

        for (int bg = -1; bg < 8; bg++) {
            for (int fg = -1; fg < 8; fg++) {
                init_pair((fg + 1) * 16 + bg + 1, fg, bg);
            }
        }
    } else {
        for (int bg = 0; bg < 8; bg++) {
            for (int fg = 0; fg < 8; fg++) {
                if (use_default) {
                    init_pair((7 - fg) * 8 + bg,
                              fg == COLOR_WHITE ? -1 : fg,
                              bg == COLOR_BLACK ? -1 : bg);
                } else {
                    init_pair((7 - fg) * 8 + bg, fg, bg);
                }
            }
        }
    }
}

void madtty_set_handler(madtty_t *t, madtty_handler_t handler)
{
    t->handler = handler;
}

void madtty_set_data(madtty_t *t, void *data)
{
    t->data = data;
}

void *madtty_get_data(madtty_t *t)
{
    return t->data;
}

unsigned madtty_cursor(madtty_t *t)
{
    return t->scroll_amount ? 0 : !t->curshid;
}

/*
 * Copyright © 2004 Bruno T. C. de Oliveira
 * Copyright © 2006 Pierre Habouzit
 * Copyright © 2008-2011 Marc Andre Tanner
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

#define _GNU_SOURCE
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
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
# include <util.h>
#endif
#if defined(__CYGWIN__) || defined(_AIX)
# include <alloca.h>
#endif

#include "vt.h"

#ifndef NCURSES_ATTR_SHIFT
# define NCURSES_ATTR_SHIFT 8
#endif

#ifndef NCURSES_ACS
# ifdef PDCURSES
#  define NCURSES_ACS(c) (acs_map[(unsigned char)(c)])
# else /* BSD curses */
#  define NCURSES_ACS(c) (_acs_map[(unsigned char)(c)])
# endif
#endif

#define IS_CONTROL(ch) !((ch) & 0xffffff60UL)
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define COLOR_PALETTE_START 1
#define COLOR_PALETTE_END (unsigned)(MIN(512, COLOR_PAIRS))

static bool is_utf8, has_default_colors, use_color_palette;
static short *color2palette;

enum {
	C0_NUL = 0x00,
	C0_SOH, C0_STX, C0_ETX, C0_EOT, C0_ENQ, C0_ACK, C0_BEL,
	C0_BS, C0_HT, C0_LF, C0_VT, C0_FF, C0_CR, C0_SO, C0_SI,
	C0_DLE, C0_DC1, C0_DC2, D0_DC3, C0_DC4, C0_NAK, C0_SYN, C0_ETB,
	C0_CAN, C0_EM, C0_SUB, C0_ESC, C0_IS4, C0_IS3, C0_IS2, C0_IS1,
};

enum {
	C1_40 = 0x40,
	C1_41, C1_BPH, C1_NBH, C1_44, C1_NEL, C1_SSA, C1_ESA,
	C1_HTS, C1_HTJ, C1_VTS, C1_PLD, C1_PLU, C1_RI, C1_SS2, C1_SS3,
	C1_DCS, C1_PU1, C1_PU2, C1_STS, C1_CCH, C1_MW, C1_SPA, C1_EPA,
	C1_SOS, C1_59, C1_SCI, C1_CSI, CS_ST, C1_OSC, C1_PM, C1_APC,
};

enum {
	CSI_ICH = 0x40,
	CSI_CUU, CSI_CUD, CSI_CUF, CSI_CUB, CSI_CNL, CSI_CPL, CSI_CHA,
	CSI_CUP, CSI_CHT, CSI_ED, CSI_EL, CSI_IL, CSI_DL, CSI_EF, CSI_EA,
	CSI_DCH, CSI_SEE, CSI_CPR, CSI_SU, CSI_SD, CSI_NP, CSI_PP, CSI_CTC,
	CSI_ECH, CSI_CVT, CSI_CBT, CSI_SRS, CSI_PTX, CSI_SDS, CSI_SIMD, CSI_5F,
	CSI_HPA, CSI_HPR, CSI_REP, CSI_DA, CSI_VPA, CSI_VPR, CSI_HVP, CSI_TBC,
	CSI_SM, CSI_MC, CSI_HPB, CSI_VPB, CSI_RM, CSI_SGR, CSI_DSR, CSI_DAQ,
	CSI_70, CSI_71, CSI_72, CSI_73, CSI_74, CSI_75, CSI_76, CSI_77,
	CSI_78, CSI_79, CSI_7A, CSI_7B, CSI_7C, CSI_7D, CSI_7E, CSI_7F
};

struct Vt {
	int pty;
	pid_t childpid;

	/* flags */
	unsigned seen_input:1;
	unsigned insert:1;
	unsigned escaped:1;
	unsigned curshid:1;
	unsigned curskeymode:1;
	unsigned bell:1;
	unsigned relposmode:1;
	unsigned mousetrack:1;
	unsigned graphmode:1;
	bool charsets[2];

	/* geometry */
	int rows, cols, maxcols;
	unsigned curattrs, savattrs, defattrs;
	short curfg, curbg, savfg, savbg, deffg, defbg;

	/* scrollback buffer */
	struct Row *scroll_buf;
	int scroll_buf_sz;
	int scroll_buf_ptr;
	int scroll_buf_len;
	int scroll_amount;

	struct Row *lines;
	struct Row *scroll_top;
	struct Row *scroll_bot;

	/* cursor */
	struct Row *curs_row;
	int curs_col, curs_srow, curs_scol;

	/* buffers and parsing state */
	mbstate_t ps;
	char rbuf[BUFSIZ];
	char ebuf[BUFSIZ];
	int rlen, elen;

	/* custom escape sequence handler */
	vt_handler_t handler;
	void *data;
};

typedef struct Row {
	wchar_t *text;
	uint16_t *attr;
	short *fg;
	short *bg;
	unsigned dirty:1;
} Row;

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

static void process_nonprinting(Vt *t, wchar_t wc);
static void send_curs(Vt *t);

__attribute__ ((const))
static uint16_t build_attrs(unsigned curattrs)
{
	return ((curattrs & ~A_COLOR) | COLOR_PAIR(curattrs & 0xff))
	    >> NCURSES_ATTR_SHIFT;
}

static void row_set(Row *row, int start, int len, Vt *t)
{
	row->dirty = true;
	wmemset(row->text + start, 0, len);
	attr_t attr = t ? build_attrs(t->curattrs) : 0;
	short fg = t ? t->curfg : -1;
	short bg = t ? t->curbg : -1;
	for (int i = start; i < len + start; i++) {
		row->attr[i] = attr;
		row->fg[i] = fg;
		row->bg[i] = bg;
	}
}

static void row_roll(Row *start, Row *end, int count)
{
	int n = end - start;

	count %= n;
	if (count < 0)
		count += n;

	if (count) {
		Row *buf = alloca(count * sizeof(Row));

		memcpy(buf, start, count * sizeof(Row));
		memmove(start, start + count, (n - count) * sizeof(Row));
		memcpy(end - count, buf, count * sizeof(Row));
		for (Row *row = start; row < end; row++)
			row->dirty = true;
	}
}

static void clamp_cursor_to_bounds(Vt *t)
{
	if (t->curs_row < t->lines)
		t->curs_row = t->lines;
	if (t->curs_row >= t->lines + t->rows)
		t->curs_row = t->lines + t->rows - 1;
	if (t->curs_col < 0)
		t->curs_col = 0;
	if (t->curs_col >= t->cols)
		t->curs_col = t->cols - 1;
}

static void save_curs(Vt *t)
{
	t->curs_srow = t->curs_row - t->lines;
	t->curs_scol = t->curs_col;
}

static void restore_curs(Vt *t)
{
	t->curs_row = t->lines + t->curs_srow;
	t->curs_col = t->curs_scol;
	clamp_cursor_to_bounds(t);
}

static void save_attrs(Vt *t)
{
	t->savattrs = t->curattrs;
	t->savfg = t->curfg;
	t->savbg = t->curbg;
}

static void restore_attrs(Vt *t)
{
	t->curattrs = t->savattrs;
	t->curfg = t->savfg;
	t->curbg = t->savbg;
}

static void fill_scroll_buf(Vt *t, int s)
{
	/* work in screenfuls */
	int ssz = t->scroll_bot - t->scroll_top;
	if (s > ssz) {
		fill_scroll_buf(t, ssz);
		fill_scroll_buf(t, s - ssz);
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
			struct Row tmp = t->scroll_top[i];
			t->scroll_top[i] = t->scroll_buf[t->scroll_buf_ptr];
			t->scroll_buf[t->scroll_buf_ptr] = tmp;

			t->scroll_buf_ptr++;
			if (t->scroll_buf_ptr == t->scroll_buf_sz)
				t->scroll_buf_ptr = 0;
		}
	}
	row_roll(t->scroll_top, t->scroll_bot, s);
	if (s < 0 && t->scroll_buf_sz) {
		for (int i = (-s) - 1; i >= 0; i--) {
			t->scroll_buf_ptr--;
			if (t->scroll_buf_ptr == -1)
				t->scroll_buf_ptr = t->scroll_buf_sz - 1;

			struct Row tmp = t->scroll_top[i];
			t->scroll_top[i] = t->scroll_buf[t->scroll_buf_ptr];
			t->scroll_buf[t->scroll_buf_ptr] = tmp;
			t->scroll_top[i].dirty = true;
		}
	}
}

static void cursor_line_down(Vt *t)
{
	row_set(t->curs_row, t->cols, t->maxcols - t->cols, 0);
	t->curs_row++;
	if (t->curs_row < t->scroll_bot)
		return;

	vt_noscroll(t);

	t->curs_row = t->scroll_bot - 1;
	fill_scroll_buf(t, 1);
	row_set(t->curs_row, 0, t->cols, t);
}

static void new_escape_sequence(Vt *t)
{
	t->escaped = true;
	t->elen = 0;
	t->ebuf[0] = '\0';
}

static void cancel_escape_sequence(Vt *t)
{
	t->escaped = false;
	t->elen = 0;
	t->ebuf[0] = '\0';
}

static bool is_valid_csi_ender(int c)
{
	return (c >= 'a' && c <= 'z')
	    || (c >= 'A' && c <= 'Z')
	    || (c == '@' || c == '`');
}

/* interprets a 'set attribute' (SGR) CSI escape sequence */
static void interpret_csi_SGR(Vt *t, int param[], int pcount)
{
	if (pcount == 0) {
		/* special case: reset attributes */
		t->curattrs = A_NORMAL;
		t->curfg = t->curbg = -1;
		return;
	}

	for (int i = 0; i < pcount; i++) {
		switch (param[i]) {
		case 0:
			t->curattrs = A_NORMAL;
			t->curfg = t->curbg = -1;
			break;
		case 1:
			t->curattrs |= A_BOLD;
			break;
		case 4:
			t->curattrs |= A_UNDERLINE;
			break;
		case 5:
			t->curattrs |= A_BLINK;
			break;
		case 7:
			t->curattrs |= A_REVERSE;
			break;
		case 8:
			t->curattrs |= A_INVIS;
			break;
		case 22:
			t->curattrs &= ~A_BOLD;
			break;
		case 24:
			t->curattrs &= ~A_UNDERLINE;
			break;
		case 25:
			t->curattrs &= ~A_BLINK;
			break;
		case 27:
			t->curattrs &= ~A_REVERSE;
			break;
		case 28:
			t->curattrs &= ~A_INVIS;
			break;
		case 30 ... 37:	/* fg */
			t->curfg = param[i] - 30;
			break;
		case 38:
			if ((i + 2) < pcount && param[i + 1] == 5) {
				t->curfg = param[i + 2];
				i += 2;
			}
			break;
		case 39:
			t->curfg = -1;
			break;
		case 40 ... 47:	/* bg */
			t->curbg = param[i] - 40;
			break;
		case 48:
			if ((i + 2) < pcount && param[i + 1] == 5) {
				t->curbg = param[i + 2];
				i += 2;
			}
			break;
		case 49:
			t->curbg = -1;
			break;
		case 90 ... 97:	/* hi fg */
			t->curfg = param[i] - 82;
			break;
		case 100 ... 107: /* hi bg */
			t->curbg = param[i] - 92;
			break;
		default:
			break;
		}
	}
}

/* interprets an 'erase display' (ED) escape sequence */
static void interpret_csi_ED(Vt *t, int param[], int pcount)
{
	Row *row, *start, *end;

	save_attrs(t);
	t->curattrs = A_NORMAL;
	t->curfg = t->curbg = -1;

	if (pcount && param[0] == 2) {
		start = t->lines;
		end = t->lines + t->rows;
	} else if (pcount && param[0] == 1) {
		start = t->lines;
		end = t->curs_row;
		row_set(t->curs_row, 0, t->curs_col + 1, t);
	} else {
		row_set(t->curs_row, t->curs_col, t->cols - t->curs_col, t);
		start = t->curs_row + 1;
		end = t->lines + t->rows;
	}

	for (row = start; row < end; row++)
		row_set(row, 0, t->cols, t);

	restore_attrs(t);
}

/* interprets a 'move cursor' (CUP) escape sequence */
static void interpret_csi_CUP(Vt *t, int param[], int pcount)
{
	if (pcount == 0) {
		t->curs_row = (t->relposmode ? t->scroll_top : t->lines);
		t->curs_col = 0;
	} else if (pcount == 1) {
		t->curs_row = (t->relposmode ? t->scroll_top : t->lines) + param[0] - 1;
		t->curs_col = 0;
	} else {
		t->curs_row = (t->relposmode ? t->scroll_top : t->lines) + param[0] - 1;
		t->curs_col = param[1] - 1;
	}

	clamp_cursor_to_bounds(t);
}

/* Interpret the 'relative mode' sequences: CUU, CUD, CUF, CUB, CNL,
 * CPL, CHA, HPR, VPA, VPR, HPA */
static void interpret_csi_C(Vt *t, char verb, int param[], int pcount)
{
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	switch (verb) {
	case 'A':
		t->curs_row -= n;
		break;
	case 'B':
	case 'e':
		t->curs_row += n;
		break;
	case 'C':
	case 'a':
		t->curs_col += n;
		break;
	case 'D':
		t->curs_col -= n;
		break;
	case 'E':
		t->curs_row += n;
		t->curs_col = 0;
		break;
	case 'F':
		t->curs_row -= n;
		t->curs_col = 0;
		break;
	case 'G':
	case '`':
		t->curs_col = param[0] - 1;
		break;
	case 'd':
		t->curs_row = t->lines + param[0] - 1;
		break;
	}

	clamp_cursor_to_bounds(t);
}

/* Interpret the 'erase line' escape sequence */
static void interpret_csi_EL(Vt *t, int param[], int pcount)
{
	switch (pcount ? param[0] : 0) {
	case 1:
		row_set(t->curs_row, 0, t->curs_col + 1, t);
		break;
	case 2:
		row_set(t->curs_row, 0, t->cols, t);
		break;
	default:
		row_set(t->curs_row, t->curs_col, t->cols - t->curs_col, t);
		break;
	}
}

/* Interpret the 'insert blanks' sequence (ICH) */
static void interpret_csi_ICH(Vt *t, int param[], int pcount)
{
	Row *row = t->curs_row;
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (t->curs_col + n > t->cols)
		n = t->cols - t->curs_col;

	for (int i = t->cols - 1; i >= t->curs_col + n; i--) {
		row->text[i] = row->text[i - n];
		row->attr[i] = row->attr[i - n];
		row->bg[i] = row->bg[i - n];
		row->fg[i] = row->fg[i - n];
	}

	row_set(row, t->curs_col, n, t);
}

/* Interpret the 'delete chars' sequence (DCH) */
static void interpret_csi_DCH(Vt *t, int param[], int pcount)
{
	Row *row = t->curs_row;
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (t->curs_col + n > t->cols)
		n = t->cols - t->curs_col;

	for (int i = t->curs_col; i < t->cols - n; i++) {
		row->text[i] = row->text[i + n];
		row->attr[i] = row->attr[i + n];
		row->bg[i] = row->bg[i + n];
		row->fg[i] = row->fg[i + n];
	}

	row_set(row, t->cols - n, n, t);
}

/* Interpret an 'insert line' sequence (IL) */
static void interpret_csi_IL(Vt *t, int param[], int pcount)
{
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (t->curs_row + n >= t->scroll_bot) {
		for (Row *row = t->curs_row; row < t->scroll_bot; row++)
			row_set(row, 0, t->cols, t);
	} else {
		row_roll(t->curs_row, t->scroll_bot, -n);
		for (Row *row = t->curs_row; row < t->curs_row + n; row++)
			row_set(row, 0, t->cols, t);
	}
}

/* Interpret a 'delete line' sequence (DL) */
static void interpret_csi_DL(Vt *t, int param[], int pcount)
{
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (t->curs_row + n >= t->scroll_bot) {
		for (Row *row = t->curs_row; row < t->scroll_bot; row++)
			row_set(row, 0, t->cols, t);
	} else {
		row_roll(t->curs_row, t->scroll_bot, n);
		for (Row *row = t->scroll_bot - n; row < t->scroll_bot; row++)
			row_set(row, 0, t->cols, t);
	}
}

/* Interpret an 'erase characters' (ECH) sequence */
static void interpret_csi_ECH(Vt *t, int param[], int pcount)
{
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (t->curs_col + n < t->cols)
		n = t->cols - t->curs_col;

	row_set(t->curs_row, t->curs_col, n, t);
}

/* Interpret a 'set scrolling region' (DECSTBM) sequence */
static void interpret_csi_DECSTBM(Vt *t, int param[], int pcount)
{
	int new_top, new_bot;

	switch (pcount) {
	case 0:
		t->scroll_top = t->lines;
		t->scroll_bot = t->lines + t->rows;
		break;
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
	default:
		return;	/* malformed */
	}
}

static void es_interpret_csi(Vt *t)
{
	static int csiparam[BUFSIZ];
	int param_count = 0;
	const char *p = t->ebuf + 1;
	char verb = t->ebuf[t->elen - 1];

	/* parse numeric parameters */
	for (p += (t->ebuf[1] == '?'); *p; p++) {
		if (IS_CONTROL(*p)) {
			process_nonprinting(t, *p);
		} else if (*p == ';') {
			if (param_count >= (int)sizeof(csiparam))
				return;	/* too long! */
			csiparam[param_count++] = 0;
		} else if (isdigit((unsigned char)*p)) {
			if (param_count == 0)
				csiparam[param_count++] = 0;
			csiparam[param_count - 1] *= 10;
			csiparam[param_count - 1] += *p - '0';
		}
	}

	if (t->ebuf[1] == '?') {
		if (verb == 'h') { /* DEC Private Mode Set (DECSET) */
			switch (csiparam[0]) {
			case 1: /* set ANSI cursor (application) key mode (DECCKM) */
				t->curskeymode = true;
				break;
			case 6: /* set origin to relative (DECOM) */
				t->relposmode = true;
				break;
			case 25: /* make cursor visible (DECCM) */
				t->curshid = false;
				break;
			case 47: /* use alternate screen buffer */
				t->curattrs = A_NORMAL;
				t->curfg = t->curbg = -1;
				break;
			case 1000: /* enable normal mouse tracking */
				t->mousetrack = true;
				break;
			}
		} else if (verb == 'l') { /* DEC Private Mode Reset (DECRST) */
			switch (csiparam[0]) {
			case 1: /* reset ANSI cursor (normal) key mode (DECCKM) */
				t->curskeymode = false;
				break;
			case 6: /* set origin to absolute (DECOM) */
				t->relposmode = false;
				break;
			case 25: /* make cursor visible (DECCM) */
				t->curshid = true;
				break;
			case 47: /* use normal screen buffer */
				t->curattrs = A_NORMAL;
				t->curfg = t->curbg = -1;
				break;
			case 1000: /* disable normal mouse tracking */
				t->mousetrack = false;
				break;
			}
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
		interpret_csi_SGR(t, csiparam, param_count);
		break;
	case 'J': /* it's an 'erase display' sequence */
		interpret_csi_ED(t, csiparam, param_count);
		break;
	case 'H':
	case 'f': /* it's a 'move cursor' sequence */
		interpret_csi_CUP(t, csiparam, param_count);
		break;
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F':
	case 'G':
	case 'e':
	case 'a':
	case 'd':
	case '`':
		/* it is a 'relative move' */
		interpret_csi_C(t, verb, csiparam, param_count);
		break;
	case 'K': /* erase line */
		interpret_csi_EL(t, csiparam, param_count);
		break;
	case '@': /* insert characters */
		interpret_csi_ICH(t, csiparam, param_count);
		break;
	case 'P': /* delete characters */
		interpret_csi_DCH(t, csiparam, param_count);
		break;
	case 'L': /* insert lines */
		interpret_csi_IL(t, csiparam, param_count);
		break;
	case 'M': /* delete lines */
		interpret_csi_DL(t, csiparam, param_count);
		break;
	case 'X': /* erase chars */
		interpret_csi_ECH(t, csiparam, param_count);
		break;
	case 'r': /* set scrolling region */
		interpret_csi_DECSTBM(t, csiparam, param_count);
		break;
	case 's': /* save cursor location */
		save_curs(t);
		break;
	case 'u': /* restore cursor location */
		restore_curs(t);
		break;
	case 'n': /* query cursor location */
		if (param_count == 1 && csiparam[0] == 6)
			send_curs(t);
		break;
	default:
		break;
	}
}

/* Interpret an 'index' (IND) sequence */
static void interpret_esc_IND(Vt *t)
{
	if (t->curs_row < t->lines + t->rows - 1)
		t->curs_row++;
}

/* Interpret a 'reverse index' (RI) sequence */
static void interpret_esc_RI(Vt *t)
{
	if (t->curs_row > t->lines)
		t->curs_row--;
	else {
		row_roll(t->scroll_top, t->scroll_bot, -1);
		row_set(t->scroll_top, 0, t->cols, t);
	}
}

/* Interpret a 'next line' (NEL) sequence */
static void interpret_esc_NEL(Vt *t)
{
	if (t->curs_row < t->lines + t->rows - 1) {
		t->curs_row++;
		t->curs_col = 0;
	}
}

/* Interpret a 'select character set' (SCS) sequence */
static void interpret_esc_SCS(Vt *t)
{
	/* ESC ( sets G0, ESC ) sets G1 */
	t->charsets[! !(t->ebuf[0] == ')')] = (t->ebuf[1] == '0');
	t->graphmode = t->charsets[0];
}

static void try_interpret_escape_seq(Vt *t)
{
	char lastchar = t->ebuf[t->elen - 1];

	if (!*t->ebuf)
		return;

	if (t->handler) {
		switch ((*(t->handler)) (t, t->ebuf)) {
		case VT_HANDLER_OK:
			goto cancel;
		case VT_HANDLER_NOTYET:
			if (t->elen + 1 >= (int)sizeof(t->ebuf))
				goto cancel;
			return;
		}
	}

	switch (*t->ebuf) {
	case '(':
	case ')':
		if (t->elen == 2) {
			interpret_esc_SCS(t);
			cancel_escape_sequence(t);
			return;
		}
		break;

	case ']': /* xterm thing */
		if (lastchar == '\a')
			goto cancel;
		break;

	case '[':
		if (is_valid_csi_ender(lastchar)) {
			es_interpret_csi(t);
			cancel_escape_sequence(t);
			return;
		}
		break;

	case '7': /* DECSC: save cursor and attributes */
		save_attrs(t);
		save_curs(t);
		cancel_escape_sequence(t);
		return;

	case '8': /* DECRC: restore cursor and attributes */
		restore_attrs(t);
		restore_curs(t);
		cancel_escape_sequence(t);
		return;

	case 'D': /* IND: index */
		interpret_esc_IND(t);
		cancel_escape_sequence(t);
		return;

	case 'M': /* RI: reverse index */
		interpret_esc_RI(t);
		cancel_escape_sequence(t);
		return;

	case 'E': /* NEL: next line */
		interpret_esc_NEL(t);
		cancel_escape_sequence(t);
		return;

	default:
		goto cancel;
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

static void process_nonprinting(Vt *t, wchar_t wc)
{
	switch (wc) {
	case C0_ESC:
		new_escape_sequence(t);
		break;

	case C0_BEL:
		if (t->bell)
			beep();
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

	case C0_SO: /* shift out, invoke the G1 character set */
		t->graphmode = t->charsets[1];
		break;
	case C0_SI: /* shift in, invoke the G0 character set */
		t->graphmode = t->charsets[0];
		break;
	}
}

static void is_utf8_locale(void)
{
	const char *cset = nl_langinfo(CODESET) ? : "ANSI_X3.4-1968";
	is_utf8 = !strcmp(cset, "UTF-8");
}

static wchar_t get_vt100_graphic(char c)
{
	static char vt100_acs[] = "`afgjklmnopqrstuvwxyz{|}~";

	/*
	 * 5f-7e standard vt100
	 * 40-5e rxvt extension for extra curses acs chars
	 */
	static uint16_t const vt100_utf8[62] = {
		        0x2191, 0x2193, 0x2192, 0x2190, 0x2588, 0x259a, 0x2603, // 41-47
		     0,      0,      0,      0,      0,      0,      0,      0, // 48-4f
		     0,      0,      0,      0,      0,      0,      0,      0, // 50-57
		     0,      0,      0,      0,      0,      0,      0, 0x0020, // 58-5f
		0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1, // 60-67
		0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba, // 68-6f
		0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c, // 70-77
		0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,         // 78-7e
	};

	if (is_utf8)
		return vt100_utf8[c - 0x41];
	else if (strchr(vt100_acs, c))
		return NCURSES_ACS(c);
	return '\0';
}

static void put_wc(Vt *t, wchar_t wc)
{
	int width = 0;

	if (!t->seen_input) {
		t->seen_input = 1;
		kill(-t->childpid, SIGWINCH);
	}

	if (t->escaped) {
		if (t->elen + 1 < (int)sizeof(t->ebuf)) {
			t->ebuf[t->elen] = wc;
			t->ebuf[++t->elen] = '\0';
			try_interpret_escape_seq(t);
		} else {
			cancel_escape_sequence(t);
		}
	} else if (IS_CONTROL(wc)) {
		process_nonprinting(t, wc);
	} else {
		Row *tmp;

		if (t->graphmode) {
			if (wc >= 0x41 && wc <= 0x7e) {
				wchar_t gc = get_vt100_graphic(wc);
				if (gc)
					wc = gc;
			}
			width = 1;
		} else {
			width = wcwidth(wc) ? : 1;
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

int vt_process(Vt *t)
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
			wc = t->rbuf[pos];
		}

		pos += len ? len : 1;
		put_wc(t, wc);
	}

	t->rlen -= pos;
	memmove(t->rbuf, t->rbuf + pos, t->rlen);
	return 0;
}

void vt_set_default_colors(Vt *t, unsigned attrs, short fg, short bg)
{
	t->defattrs = attrs;
	t->deffg = fg;
	t->defbg = bg;
}

Vt *vt_create(int rows, int cols, int scroll_buf_sz)
{
	Vt *t;
	int i;

	if (rows <= 0 || cols <= 0)
		return NULL;

	t = calloc(sizeof(Vt), 1);
	if (!t)
		return NULL;

	/* record dimensions */
	t->rows = rows;
	t->cols = cols;
	t->maxcols = cols;

	/* default mode is replace */
	t->insert = false;

	/* create the cell matrix */
	t->lines = calloc(sizeof(Row), t->rows);
	for (i = 0; i < t->rows; i++) {
		t->lines[i].text = calloc(sizeof(wchar_t), t->cols);
		t->lines[i].attr = calloc(sizeof(uint16_t), t->cols);
		t->lines[i].fg = calloc(sizeof(short), t->cols);
		t->lines[i].bg = calloc(sizeof(short), t->cols);
	}

	t->pty = -1;		/* no pty for now */

	/* initialization of other public fields */
	t->curs_row = t->lines;
	t->curs_col = 0;
	t->curattrs = A_NORMAL;	/* white text over black background */
	t->curfg = t->curbg = t->deffg = t->defbg = -1;

	/* initial scrolling area is the whole window */
	t->scroll_top = t->lines;
	t->scroll_bot = t->lines + t->rows;

	/* scrollback buffer */
	if (scroll_buf_sz < 0)
		scroll_buf_sz = 0;
	t->scroll_buf_sz = scroll_buf_sz;
	t->scroll_buf = calloc(sizeof(Row), t->scroll_buf_sz);
	for (i = 0; i < t->scroll_buf_sz; i++) {
		t->scroll_buf[i].text = calloc(sizeof(wchar_t), t->cols);
		t->scroll_buf[i].attr = calloc(sizeof(uint16_t), t->cols);
		t->scroll_buf[i].fg = calloc(sizeof(short), t->cols);
		t->scroll_buf[i].bg = calloc(sizeof(short), t->cols);
	}
	t->scroll_buf_ptr = t->scroll_buf_len = 0;
	t->scroll_amount = 0;

	/* clear the screen */
	row_set(t->curs_row, t->curs_col, t->cols - t->curs_col, t);
	for (Row *row = t->curs_row + 1; row < t->lines + t->rows; row++)
		row_set(row, 0, t->cols, t);

	return t;
}

void vt_resize(Vt *t, int rows, int cols)
{
	struct winsize ws = {.ws_row = rows,.ws_col = cols };
	Row *lines = t->lines;

	if (rows <= 0 || cols <= 0)
		return;

	vt_noscroll(t);

	if (t->rows != rows) {
		if (t->curs_row > lines + rows) {
			/* scroll up instead of simply chopping off bottom */
			fill_scroll_buf(t, (t->curs_row - t->lines) - rows + 1);
		}
		while (t->rows > rows) {
			free(lines[t->rows - 1].text);
			free(lines[t->rows - 1].attr);
			free(lines[t->rows - 1].fg);
			free(lines[t->rows - 1].bg);
			t->rows--;
		}

		lines = realloc(lines, sizeof(Row) * rows);
	}

	if (t->maxcols < cols) {
		for (int row = 0; row < t->rows; row++) {
			lines[row].text = realloc(lines[row].text, sizeof(wchar_t) * cols);
			lines[row].attr = realloc(lines[row].attr, sizeof(uint16_t) * cols);
			lines[row].fg = realloc(lines[row].fg, sizeof(short) * cols);
			lines[row].bg = realloc(lines[row].bg, sizeof(short) * cols);
			if (t->cols < cols)
				row_set(lines + row, t->cols, cols - t->cols, 0);
			lines[row].dirty = true;
		}
		Row *sbuf = t->scroll_buf;
		for (int row = 0; row < t->scroll_buf_sz; row++) {
			sbuf[row].text = realloc(sbuf[row].text, sizeof(wchar_t) * cols);
			sbuf[row].attr = realloc(sbuf[row].attr, sizeof(uint16_t) * cols);
			sbuf[row].fg = realloc(sbuf[row].fg, sizeof(short) * cols);
			sbuf[row].bg = realloc(sbuf[row].bg, sizeof(short) * cols);
			if (t->cols < cols)
				row_set(sbuf + row, t->cols, cols - t->cols, 0);
		}
		t->maxcols = cols;
		t->cols = cols;
	} else if (t->cols != cols) {
		for (int row = 0; row < t->rows; row++)
			lines[row].dirty = true;
		t->cols = cols;
	}

	int deltarows = 0;
	if (t->rows < rows) {
		while (t->rows < rows) {
			lines[t->rows].text = calloc(sizeof(wchar_t), t->maxcols);
			lines[t->rows].attr = calloc(sizeof(uint16_t), t->maxcols);
			lines[t->rows].fg = calloc(sizeof(short), t->maxcols);
			lines[t->rows].bg = calloc(sizeof(short), t->maxcols);
			row_set(lines + t->rows, 0, t->maxcols, t);
			t->rows++;
		}

		/* prepare for backfill */
		if (t->curs_row >= t->scroll_bot - 1) {
			deltarows = t->lines + rows - t->curs_row - 1;
			if (deltarows > t->scroll_buf_len)
				deltarows = t->scroll_buf_len;
		}
	}

	t->curs_row += lines - t->lines;
	t->scroll_top = lines;
	t->scroll_bot = lines + rows;
	t->lines = lines;
	clamp_cursor_to_bounds(t);

	/* perform backfill */
	if (deltarows > 0) {
		fill_scroll_buf(t, -deltarows);
		t->curs_row += deltarows;
	}

	ioctl(t->pty, TIOCSWINSZ, &ws);
	kill(-t->childpid, SIGWINCH);
}

void vt_destroy(Vt *t)
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

void vt_dirty(Vt *t)
{
	for (int i = 0; i < t->rows; i++)
		t->lines[i].dirty = true;
}

void vt_draw(Vt *t, WINDOW * win, int srow, int scol)
{
	curs_set(0);
	for (int i = 0; i < t->rows; i++) {
		Row *row = t->lines + i;

		if (!row->dirty)
			continue;

		wmove(win, srow + i, scol);
		for (int j = 0; j < t->cols; j++) {
			if (!j || row->attr[j] != row->attr[j - 1]
			    || row->fg[j] != row->fg[j - 1]
			    || row->bg[j] != row->bg[j - 1]) {
				if (row->attr[j] == A_NORMAL)
					row->attr[j] = t->defattrs;
				if (row->fg[j] == -1)
					row->fg[j] = t->deffg;
				if (row->bg[j] == -1)
					row->bg[j] = t->defbg;
				wattrset(win, (attr_t) row->attr[j] << NCURSES_ATTR_SHIFT);
				wcolor_set(win, vt_color_get(row->fg[j], row->bg[j]), NULL);
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
	curs_set(vt_cursor(t));
}

void vt_scroll(Vt *t, int rows)
{
	if (rows < 0) { /* scroll back */
		if (rows < -t->scroll_buf_len)
			rows = -t->scroll_buf_len;
	} else { /* scroll forward */
		if (rows > t->scroll_amount)
			rows = t->scroll_amount;
	}
	fill_scroll_buf(t, rows);
	t->scroll_amount -= rows;
}

void vt_noscroll(Vt *t)
{
	if (t->scroll_amount)
		vt_scroll(t, t->scroll_amount);
}

void vt_bell(Vt *t, bool bell)
{
	t->bell = bell;
}

void Vtogglebell(Vt *t)
{
	t->bell = !t->bell;
}

pid_t vt_forkpty(Vt *t, const char *p, const char *argv[], const char *env[], int *pty)
{
	struct winsize ws;
	pid_t pid;
	const char **envp = env;
	int fd, maxfd;

	ws.ws_row = t->rows;
	ws.ws_col = t->cols;
	ws.ws_xpixel = ws.ws_ypixel = 0;

	pid = forkpty(&t->pty, NULL, NULL, &ws);
	if (pid < 0)
		return -1;

	if (pid == 0) {
		setsid();

		maxfd = sysconf(_SC_OPEN_MAX);
		for (fd = 3; fd < maxfd; fd++)
			if (close(fd) == -1 && errno == EBADF)
				break;

		while (envp && envp[0]) {
			setenv(envp[0], envp[1], 1);
			envp += 2;
		}
		setenv("TERM", use_color_palette ? "rxvt-256color" : "rxvt", 1);
		execv(p, (char *const *)argv);
		fprintf(stderr, "\nexecv() failed.\nCommand: '%s'\n", argv[0]);
		exit(1);
	}

	if (pty)
		*pty = t->pty;
	return t->childpid = pid;
}

int vt_getpty(Vt *t)
{
	return t->pty;
}

int vt_write(Vt *t, const char *buf, int len)
{
	int ret = len;

	while (len > 0) {
		int res = write(t->pty, buf, len);
		if (res < 0 && errno != EAGAIN && errno != EINTR)
			return -1;

		buf += res;
		len -= res;
	}

	return ret;
}

static void send_curs(Vt *t)
{
	char keyseq[16];
	sprintf(keyseq, "\e[%d;%dR", (int)(t->curs_row - t->lines), t->curs_col);
	vt_write(t, keyseq, strlen(keyseq));
}

void vt_keypress(Vt *t, int keycode)
{
	char c = (char)keycode;

	vt_noscroll(t);

	if (keycode >= 0 && keycode < KEY_MAX && keytable[keycode]) {
		switch (keycode) {
		case KEY_UP:
		case KEY_DOWN:
		case KEY_RIGHT:
		case KEY_LEFT: {
				char keyseq[3] = { '\e', (t->curskeymode ? 'O' : '['), keytable[keycode][0] };
				vt_write(t, keyseq, sizeof keyseq);
				break;
			}
		default:
			vt_write(t, keytable[keycode], strlen(keytable[keycode]));
		}
	} else {
		vt_write(t, &c, 1);
	}
}

void vt_mouse(Vt *t, int x, int y, mmask_t mask)
{
#ifdef NCURSES_MOUSE_VERSION
	char seq[6] = { '\e', '[', 'M' }, state = 0, button = 0;

	if (!t->mousetrack)
		return;

	if (mask & (BUTTON1_PRESSED | BUTTON1_CLICKED))
		button = 0;
	else if (mask & (BUTTON2_PRESSED | BUTTON2_CLICKED))
		button = 1;
	else if (mask & (BUTTON3_PRESSED | BUTTON3_CLICKED))
		button = 2;
	else if (mask & (BUTTON1_RELEASED | BUTTON2_RELEASED | BUTTON3_RELEASED))
		button = 3;

	if (mask & BUTTON_SHIFT)
		state |= 4;
	if (mask & BUTTON_ALT)
		state |= 8;
	if (mask & BUTTON_CTRL)
		state |= 16;

	seq[3] = 32 + button + state;
	seq[4] = 32 + x;
	seq[5] = 32 + y;

	vt_write(t, seq, sizeof seq);

	if (mask & (BUTTON1_CLICKED | BUTTON2_CLICKED | BUTTON3_CLICKED)) {
		/* send a button release event */
		button = 3;
		seq[3] = 32 + button + state;
		vt_write(t, seq, sizeof seq);
	}
#endif /* NCURSES_MOUSE_VERSION */
}

static unsigned color_hash(short f, short b)
{
	return ((f + 1) * COLORS) + b + 1;
}

short vt_color_get(short fg, short bg)
{
	static unsigned palette_cur = COLOR_PALETTE_START;

	if (use_color_palette) {
		if (fg == -1 && bg == -1) {
			return 0;
		} else {
			unsigned c = color_hash(fg, bg);
			if (color2palette[c] == 0) {
				short oldfg, oldbg;
				pair_content(palette_cur, &oldfg, &oldbg);
				color2palette[color_hash(oldfg, oldbg)] = 0;
				init_pair(palette_cur, fg, bg);
				color2palette[c] = palette_cur++;
				if (palette_cur >= COLOR_PALETTE_END) {
					palette_cur = COLOR_PALETTE_START;
					/* possibly use mvwinch/mvchgat to update palette */
				}
			}
			return color2palette[c];
		}
	} else {
		if (has_default_colors) {
			if (fg == -1)
				fg = COLOR_WHITE;
			if (bg == -1)
				bg = COLOR_BLACK;
		}
		return (7 - fg) * 8 + bg;
	}
}

static void init_colors(void)
{
	has_default_colors = (use_default_colors() == OK);

	if (COLORS >= 256 && COLOR_PAIRS >= 256) {
		use_color_palette = true;
		color2palette = calloc((COLORS + 1) * (COLORS + 1), sizeof(short));
		int bg = 0, fg = 0;
		for (unsigned int i = COLOR_PALETTE_START; i < COLOR_PALETTE_END; i++) {
			init_pair(i, fg, bg);
			color2palette[color_hash(fg, bg)] = i;
			if (++fg == COLORS) {
				fg = 0;
				bg++;
			}
		}
	} else {
		for (int bg = 0; bg < 8; bg++) {
			for (int fg = 0; fg < 8; fg++) {
				if (has_default_colors) {
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

void vt_init(void)
{
	init_colors();
	is_utf8_locale();
}

void vt_set_handler(Vt *t, vt_handler_t handler)
{
	t->handler = handler;
}

void vt_set_data(Vt *t, void *data)
{
	t->data = data;
}

void *vt_get_data(Vt *t)
{
	return t->data;
}

unsigned vt_cursor(Vt *t)
{
	return t->scroll_amount ? 0 : !t->curshid;
}

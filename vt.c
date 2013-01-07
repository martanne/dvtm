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
#if defined(__linux__) || defined(__CYGWIN__)
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

#ifdef _AIX
# include "forkpty-aix.c"
#endif

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

#ifdef NCURSES_VERSION
# ifndef NCURSES_EXT_COLORS
#  define NCURSES_EXT_COLORS 0
# endif
# if !NCURSES_EXT_COLORS
#  define MAX_COLOR_PAIRS 256
# endif
#endif
#ifndef MAX_COLOR_PAIRS
# define MAX_COLOR_PAIRS COLOR_PAIRS
#endif

#define IS_CONTROL(ch) !((ch) & 0xffffff60UL)
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define sstrlen(str) (sizeof(str) - 1)

#define COPYMODE_ATTR A_REVERSE
static bool is_utf8, has_default_colors;
static short color_pairs_reserved, color_pairs_max, color_pair_current;
static short *color2palette, default_fg, default_bg;

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

typedef struct {
	wchar_t text;
	uint16_t attr;
	short fg;
	short bg;
} Cell;

typedef struct {
	Cell *cells;
	unsigned dirty:1;
} Row;

typedef struct {
	Row *lines;
	Row *curs_row;
	Row *scroll_buf;
	Row *scroll_top;
	Row *scroll_bot;
	int scroll_buf_size;
	int scroll_buf_ptr;
	int scroll_amount_above;
	int scroll_amount_below;
	int rows, cols, maxcols;
	unsigned curattrs, savattrs;
	int curs_col, curs_srow, curs_scol;
	short curfg, curbg, savfg, savbg;
} Buffer;

struct Vt {
	Buffer buffer_normal;
	Buffer buffer_alternate;
	Buffer *buffer;
	unsigned defattrs;
	short deffg, defbg;
	int pty;
	pid_t childpid;

	/* flags */
	unsigned seen_input:1;
	unsigned insert:1;
	unsigned escaped:1;
	unsigned curshid:1;
	unsigned curskeymode:1;
	unsigned copymode:1;
	unsigned copymode_selecting:1;
	unsigned bell:1;
	unsigned relposmode:1;
	unsigned mousetrack:1;
	unsigned graphmode:1;
	bool charsets[2];
	char copymode_searching;
	/* copymode */
	int copymode_curs_srow, copymode_curs_scol;
	Row *copymode_sel_start_row;
	int copymode_sel_start_col;
	wchar_t *searchbuf;
	mbstate_t searchbuf_ps;
	int searchbuf_curs, searchbuf_size;
	int copymode_cmd_multiplier;
	/* buffers and parsing state */
	char rbuf[BUFSIZ];
	char ebuf[BUFSIZ];
	unsigned int rlen, elen;

	/* xterm style window title */
	char title[256];

	vt_event_handler_t event_handler;

	/* custom escape sequence handler */
	vt_escseq_handler_t escseq_handler;
	void *data;
};

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
	[KEY_F(13)]     = "\e[23~",
	[KEY_F(14)]     = "\e[24~",
	[KEY_F(15)]     = "\e[25~",
	[KEY_F(16)]     = "\e[26~",
	[KEY_F(17)]     = "\e[28~",
	[KEY_F(18)]     = "\e[29~",
	[KEY_F(19)]     = "\e[31~",
	[KEY_F(20)]     = "\e[32~",
	[KEY_F(21)]     = "\e[33~",
	[KEY_F(22)]     = "\e[34~",
	[KEY_RESIZE]    = "",
};

static void process_nonprinting(Vt *t, wchar_t wc);
static void send_curs(Vt *t);

__attribute__ ((const))
static uint16_t build_attrs(unsigned curattrs)
{
	return ((curattrs & ~A_COLOR) | COLOR_PAIR(curattrs & 0xff))
	    >> NCURSES_ATTR_SHIFT;
}

static void row_set(Row *row, int start, int len, Buffer *t)
{
	Cell cell = {
		.text = L'\0',
		.attr = t ? build_attrs(t->curattrs) : 0,
		.fg = t ? t->curfg : -1,
		.bg = t ? t->curbg : -1,
	};

	for (int i = start; i < len + start; i++)
		row->cells[i] = cell;
	row->dirty = true;
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
	Buffer *b = t->buffer;
	Row *lines = t->relposmode ? b->scroll_top : b->lines;
	int rows = t->relposmode ? b->scroll_bot - b->scroll_top : b->rows;

	if (b->curs_row < lines)
		b->curs_row = lines;
	if (b->curs_row >= lines + rows)
		b->curs_row = lines + rows - 1;
	if (b->curs_col < 0)
		b->curs_col = 0;
	if (b->curs_col >= b->cols)
		b->curs_col = b->cols - 1;
}

static void save_curs(Vt *t)
{
	Buffer *b = t->buffer;
	b->curs_srow = b->curs_row - b->lines;
	b->curs_scol = b->curs_col;
}

static void restore_curs(Vt *t)
{
	Buffer *b = t->buffer;
	b->curs_row = b->lines + b->curs_srow;
	b->curs_col = b->curs_scol;
	clamp_cursor_to_bounds(t);
}

static void save_attrs(Vt *t)
{
	Buffer *b = t->buffer;
	b->savattrs = b->curattrs;
	b->savfg = b->curfg;
	b->savbg = b->curbg;
}

static void restore_attrs(Vt *t)
{
	Buffer *b = t->buffer;
	b->curattrs = b->savattrs;
	b->curfg = b->savfg;
	b->curbg = b->savbg;
}

static void fill_scroll_buf(Buffer *t, int s)
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

	t->scroll_amount_above += s;
	if (t->scroll_amount_above >= t->scroll_buf_size)
		t->scroll_amount_above = t->scroll_buf_size;

	if (s > 0 && t->scroll_buf_size) {
		for (int i = 0; i < s; i++) {
			Row tmp = t->scroll_top[i];
			t->scroll_top[i] = t->scroll_buf[t->scroll_buf_ptr];
			t->scroll_buf[t->scroll_buf_ptr] = tmp;

			t->scroll_buf_ptr++;
			if (t->scroll_buf_ptr == t->scroll_buf_size)
				t->scroll_buf_ptr = 0;
		}
	}
	row_roll(t->scroll_top, t->scroll_bot, s);
	if (s < 0 && t->scroll_buf_size) {
		for (int i = (-s) - 1; i >= 0; i--) {
			t->scroll_buf_ptr--;
			if (t->scroll_buf_ptr == -1)
				t->scroll_buf_ptr = t->scroll_buf_size - 1;

			Row tmp = t->scroll_top[i];
			t->scroll_top[i] = t->scroll_buf[t->scroll_buf_ptr];
			t->scroll_buf[t->scroll_buf_ptr] = tmp;
			t->scroll_top[i].dirty = true;
		}
	}
}

static void cursor_line_down(Vt *t)
{
	Buffer *b = t->buffer;
	row_set(b->curs_row, b->cols, b->maxcols - b->cols, NULL);
	b->curs_row++;
	if (b->curs_row < b->scroll_bot)
		return;

	vt_noscroll(t);

	b->curs_row = b->scroll_bot - 1;
	fill_scroll_buf(b, 1);
	row_set(b->curs_row, 0, b->cols, b);
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
static void interpret_csi_sgr(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	if (pcount == 0) {
		/* special case: reset attributes */
		b->curattrs = A_NORMAL;
		b->curfg = b->curbg = -1;
		return;
	}

	for (int i = 0; i < pcount; i++) {
		switch (param[i]) {
		case 0:
			b->curattrs = A_NORMAL;
			b->curfg = b->curbg = -1;
			break;
		case 1:
			b->curattrs |= A_BOLD;
			break;
		case 4:
			b->curattrs |= A_UNDERLINE;
			break;
		case 5:
			b->curattrs |= A_BLINK;
			break;
		case 7:
			b->curattrs |= A_REVERSE;
			break;
		case 8:
			b->curattrs |= A_INVIS;
			break;
		case 22:
			b->curattrs &= ~A_BOLD;
			break;
		case 24:
			b->curattrs &= ~A_UNDERLINE;
			break;
		case 25:
			b->curattrs &= ~A_BLINK;
			break;
		case 27:
			b->curattrs &= ~A_REVERSE;
			break;
		case 28:
			b->curattrs &= ~A_INVIS;
			break;
		case 30 ... 37:	/* fg */
			b->curfg = param[i] - 30;
			break;
		case 38:
			if ((i + 2) < pcount && param[i + 1] == 5) {
				b->curfg = param[i + 2];
				i += 2;
			}
			break;
		case 39:
			b->curfg = -1;
			break;
		case 40 ... 47:	/* bg */
			b->curbg = param[i] - 40;
			break;
		case 48:
			if ((i + 2) < pcount && param[i + 1] == 5) {
				b->curbg = param[i + 2];
				i += 2;
			}
			break;
		case 49:
			b->curbg = -1;
			break;
		case 90 ... 97:	/* hi fg */
			b->curfg = param[i] - 82;
			break;
		case 100 ... 107: /* hi bg */
			b->curbg = param[i] - 92;
			break;
		default:
			break;
		}
	}
}

/* interprets an 'erase display' (ED) escape sequence */
static void interpret_csi_ed(Vt *t, int param[], int pcount)
{
	Row *row, *start, *end;
	Buffer *b = t->buffer;

	save_attrs(t);
	b->curattrs = A_NORMAL;
	b->curfg = b->curbg = -1;

	if (pcount && param[0] == 2) {
		start = b->lines;
		end = b->lines + b->rows;
	} else if (pcount && param[0] == 1) {
		start = b->lines;
		end = b->curs_row;
		row_set(b->curs_row, 0, b->curs_col + 1, b);
	} else {
		row_set(b->curs_row, b->curs_col, b->cols - b->curs_col, b);
		start = b->curs_row + 1;
		end = b->lines + b->rows;
	}

	for (row = start; row < end; row++)
		row_set(row, 0, b->cols, b);

	restore_attrs(t);
}

/* interprets a 'move cursor' (CUP) escape sequence */
static void interpret_csi_cup(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	Row *lines = t->relposmode ? b->scroll_top : b->lines;

	if (pcount == 0) {
		b->curs_row = lines;
		b->curs_col = 0;
	} else if (pcount == 1) {
		b->curs_row = lines + param[0] - 1;
		b->curs_col = 0;
	} else {
		b->curs_row = lines + param[0] - 1;
		b->curs_col = param[1] - 1;
	}

	clamp_cursor_to_bounds(t);
}

/* Interpret the 'relative mode' sequences: CUU, CUD, CUF, CUB, CNL,
 * CPL, CHA, HPR, VPA, VPR, HPA */
static void interpret_csi_c(Vt *t, char verb, int param[], int pcount)
{
	Buffer *b = t->buffer;
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	switch (verb) {
	case 'A':
		b->curs_row -= n;
		break;
	case 'B':
	case 'e':
		b->curs_row += n;
		break;
	case 'C':
	case 'a':
		b->curs_col += n;
		break;
	case 'D':
		b->curs_col -= n;
		break;
	case 'E':
		b->curs_row += n;
		b->curs_col = 0;
		break;
	case 'F':
		b->curs_row -= n;
		b->curs_col = 0;
		break;
	case 'G':
	case '`':
		b->curs_col = param[0] - 1;
		break;
	case 'd':
		b->curs_row = b->lines + param[0] - 1;
		break;
	}

	clamp_cursor_to_bounds(t);
}

/* Interpret the 'erase line' escape sequence */
static void interpret_csi_el(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	switch (pcount ? param[0] : 0) {
	case 1:
		row_set(b->curs_row, 0, b->curs_col + 1, b);
		break;
	case 2:
		row_set(b->curs_row, 0, b->cols, b);
		break;
	default:
		row_set(b->curs_row, b->curs_col, b->cols - b->curs_col, b);
		break;
	}
}

/* Interpret the 'insert blanks' sequence (ICH) */
static void interpret_csi_ich(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	Row *row = b->curs_row;
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (b->curs_col + n > b->cols)
		n = b->cols - b->curs_col;

	for (int i = b->cols - 1; i >= b->curs_col + n; i--)
		row->cells[i] = row->cells[i - n];

	row_set(row, b->curs_col, n, b);
}

/* Interpret the 'delete chars' sequence (DCH) */
static void interpret_csi_dch(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	Row *row = b->curs_row;
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (b->curs_col + n > b->cols)
		n = b->cols - b->curs_col;

	for (int i = b->curs_col; i < b->cols - n; i++)
		row->cells[i] = row->cells[i + n];

	row_set(row, b->cols - n, n, b);
}

/* Interpret an 'insert line' sequence (IL) */
static void interpret_csi_il(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (b->curs_row + n >= b->scroll_bot) {
		for (Row *row = b->curs_row; row < b->scroll_bot; row++)
			row_set(row, 0, b->cols, b);
	} else {
		row_roll(b->curs_row, b->scroll_bot, -n);
		for (Row *row = b->curs_row; row < b->curs_row + n; row++)
			row_set(row, 0, b->cols, b);
	}
}

/* Interpret a 'delete line' sequence (DL) */
static void interpret_csi_dl(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (b->curs_row + n >= b->scroll_bot) {
		for (Row *row = b->curs_row; row < b->scroll_bot; row++)
			row_set(row, 0, b->cols, b);
	} else {
		row_roll(b->curs_row, b->scroll_bot, n);
		for (Row *row = b->scroll_bot - n; row < b->scroll_bot; row++)
			row_set(row, 0, b->cols, b);
	}
}

/* Interpret an 'erase characters' (ECH) sequence */
static void interpret_csi_ech(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	int n = (pcount && param[0] > 0) ? param[0] : 1;

	if (b->curs_col + n > b->cols)
		n = b->cols - b->curs_col;

	row_set(b->curs_row, b->curs_col, n, b);
}

/* Interpret a 'set scrolling region' (DECSTBM) sequence */
static void interpret_csi_decstbm(Vt *t, int param[], int pcount)
{
	Buffer *b = t->buffer;
	int new_top, new_bot;

	switch (pcount) {
	case 0:
		b->scroll_top = b->lines;
		b->scroll_bot = b->lines + b->rows;
		break;
	case 2:
		new_top = param[0] - 1;
		new_bot = param[1];

		/* clamp to bounds */
		if (new_top < 0)
			new_top = 0;
		if (new_top >= b->rows)
			new_top = b->rows - 1;
		if (new_bot < 0)
			new_bot = 0;
		if (new_bot >= b->rows)
			new_bot = b->rows;

		/* check for range validity */
		if (new_top < new_bot) {
			b->scroll_top = b->lines + new_top;
			b->scroll_bot = b->lines + new_bot;
		}
		break;
	default:
		return;	/* malformed */
	}
}

static void interpret_csi(Vt *t)
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
				vt_copymode_leave(t);
				t->buffer = &t->buffer_alternate;
				vt_dirty(t);
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
				vt_copymode_leave(t);
				t->buffer = &t->buffer_normal;
				vt_dirty(t);
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
		interpret_csi_sgr(t, csiparam, param_count);
		break;
	case 'J': /* it's an 'erase display' sequence */
		interpret_csi_ed(t, csiparam, param_count);
		break;
	case 'H':
	case 'f': /* it's a 'move cursor' sequence */
		interpret_csi_cup(t, csiparam, param_count);
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
		interpret_csi_c(t, verb, csiparam, param_count);
		break;
	case 'K': /* erase line */
		interpret_csi_el(t, csiparam, param_count);
		break;
	case '@': /* insert characters */
		interpret_csi_ich(t, csiparam, param_count);
		break;
	case 'P': /* delete characters */
		interpret_csi_dch(t, csiparam, param_count);
		break;
	case 'L': /* insert lines */
		interpret_csi_il(t, csiparam, param_count);
		break;
	case 'M': /* delete lines */
		interpret_csi_dl(t, csiparam, param_count);
		break;
	case 'X': /* erase chars */
		interpret_csi_ech(t, csiparam, param_count);
		break;
	case 'r': /* set scrolling region */
		interpret_csi_decstbm(t, csiparam, param_count);
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
static void interpret_csi_ind(Vt *t)
{
	Buffer *b = t->buffer;
	if (b->curs_row < b->lines + b->rows - 1)
		b->curs_row++;
}

/* Interpret a 'reverse index' (RI) sequence */
static void interpret_csi_ri(Vt *t)
{
	Buffer *b = t->buffer;
	if (b->curs_row > b->lines)
		b->curs_row--;
	else {
		row_roll(b->scroll_top, b->scroll_bot, -1);
		row_set(b->scroll_top, 0, b->cols, b);
	}
}

/* Interpret a 'next line' (NEL) sequence */
static void interpret_csi_nel(Vt *t)
{
	Buffer *b = t->buffer;
	if (b->curs_row < b->lines + b->rows - 1) {
		b->curs_row++;
		b->curs_col = 0;
	}
}

/* Interpret a 'select character set' (SCS) sequence */
static void interpret_csi_scs(Vt *t)
{
	/* ESC ( sets G0, ESC ) sets G1 */
	t->charsets[!!(t->ebuf[0] == ')')] = (t->ebuf[1] == '0');
	t->graphmode = t->charsets[0];
}

/* Interpret xterm specific escape sequences */
static void interpret_esc_xterm(Vt *t)
{
	/* ESC]n;dataBEL -- the ESC is not part of t->ebuf */
	char *title = NULL;

	switch (t->ebuf[1]) {
	case '0':
	case '2':
		t->ebuf[t->elen - 1] = '\0';
		if (t->elen > sstrlen("]n;\a"))
			title = t->ebuf + sstrlen("]n;");

		if (t->event_handler)
			t->event_handler(t, VT_EVENT_TITLE, title);
	}
}

static void try_interpret_escape_seq(Vt *t)
{
	char lastchar = t->ebuf[t->elen - 1];

	if (!*t->ebuf)
		return;

	if (t->escseq_handler) {
		switch ((*(t->escseq_handler)) (t, t->ebuf)) {
		case VT_ESCSEQ_HANDLER_OK:
			cancel_escape_sequence(t);
			return;
		case VT_ESCSEQ_HANDLER_NOTYET:
			if (t->elen + 1 >= sizeof(t->ebuf))
				goto cancel;
			return;
		}
	}

	switch (*t->ebuf) {
	case '#': /* ignore DECDHL, DECSWL, DECDWL, DECHCP, DECALN, DECFPP */
		if (t->elen == 2)
			goto cancel;
		break;
	case '(':
	case ')':
		if (t->elen == 2) {
			interpret_csi_scs(t);
			goto handled;
		}
		break;
	case ']': /* xterm thing */
		if (lastchar == '\a' ||
		   (lastchar == '\\' && t->elen >= 2 && t->ebuf[t->elen - 2] == '\e')) {
			interpret_esc_xterm(t);
			goto handled;
		}
		break;
	case '[':
		if (is_valid_csi_ender(lastchar)) {
			interpret_csi(t);
			goto handled;
		}
		break;
	case '7': /* DECSC: save cursor and attributes */
		save_attrs(t);
		save_curs(t);
		goto handled;
	case '8': /* DECRC: restore cursor and attributes */
		restore_attrs(t);
		restore_curs(t);
		goto handled;
	case 'D': /* IND: index */
		interpret_csi_ind(t);
		goto handled;
	case 'M': /* RI: reverse index */
		interpret_csi_ri(t);
		goto handled;
	case 'E': /* NEL: next line */
		interpret_csi_nel(t);
		goto handled;
	default:
		goto cancel;
	}

	if (t->elen + 1 >= sizeof(t->ebuf)) {
cancel:
#ifndef NDEBUG
		fprintf(stderr, "cancelled: \\033");
		for (unsigned int i = 0; i < t->elen; i++) {
			if (isprint(t->ebuf[i])) {
				fputc(t->ebuf[i], stderr);
			} else {
				fprintf(stderr, "\\%03o", t->ebuf[i]);
			}
		}
		fputc('\n', stderr);
#endif
handled:
		cancel_escape_sequence(t);
	}
}

static void process_nonprinting(Vt *t, wchar_t wc)
{
	Buffer *b = t->buffer;
	switch (wc) {
	case C0_ESC:
		new_escape_sequence(t);
		break;
	case C0_BEL:
		if (t->bell)
			beep();
		break;
	case C0_BS:
		if (b->curs_col > 0)
			b->curs_col--;
		break;
	case C0_HT: /* tab */
		b->curs_col = (b->curs_col + 8) & ~7;
		if (b->curs_col >= b->cols)
			b->curs_col = b->cols - 1;
		break;
	case C0_CR:
		b->curs_col = 0;
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
	const char *cset = nl_langinfo(CODESET);
	if (!cset)
		cset = "ANSI_X3.4-1968";
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
		if (t->elen + 1 < sizeof(t->ebuf)) {
			t->ebuf[t->elen] = wc;
			t->ebuf[++t->elen] = '\0';
			try_interpret_escape_seq(t);
		} else {
			cancel_escape_sequence(t);
		}
	} else if (IS_CONTROL(wc)) {
		process_nonprinting(t, wc);
	} else {
		if (t->graphmode) {
			if (wc >= 0x41 && wc <= 0x7e) {
				wchar_t gc = get_vt100_graphic(wc);
				if (gc)
					wc = gc;
			}
			width = 1;
		} else if ((width = wcwidth(wc)) < 1) {
			width = 1;
		}
		Buffer *b = t->buffer;
		Cell blank_cell = { L'\0', build_attrs(b->curattrs), b->curfg, b->curbg };
		if (width == 2 && b->curs_col == b->cols - 1) {
			b->curs_row->cells[b->curs_col++] = blank_cell;
			b->curs_row->dirty = true;
		}

		if (b->curs_col >= b->cols) {
			b->curs_col = 0;
			cursor_line_down(t);
		}

		if (t->insert) {
			Cell *src = b->curs_row->cells + b->curs_col;
			Cell *dest = src + width;
			size_t len = b->cols - b->curs_col - width;
			memmove(dest, src, len);
		}

		b->curs_row->cells[b->curs_col] = blank_cell;
		b->curs_row->cells[b->curs_col++].text = wc;
		b->curs_row->dirty = true;
		if (width == 2)
			b->curs_row->cells[b->curs_col++] = blank_cell;
	}
}

int vt_process(Vt *t)
{
	int res;
	unsigned int pos = 0;
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));

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

		len = (ssize_t)mbrtowc(&wc, t->rbuf + pos, t->rlen - pos, &ps);
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

static void buffer_free(Buffer *t)
{
	for (int i = 0; i < t->rows; i++)
		free(t->lines[i].cells);
	free(t->lines);
	for (int i = 0; i < t->scroll_buf_size; i++)
		free(t->scroll_buf[i].cells);
	free(t->scroll_buf);
}

static bool buffer_init(Buffer *t, int rows, int cols, int scroll_buf_size)
{
	Row *lines, *scroll_buf;
	t->lines = lines = calloc(rows, sizeof(Row));
	if (!lines)
		return false;
	t->curattrs = A_NORMAL;	/* white text over black background */
	t->curfg = t->curbg = -1;
	for (Row *row = lines, *end = lines + rows; row < end; row++) {
		row->cells = malloc(cols * sizeof(Cell));
		if (!row->cells) {
			t->rows = row - lines;
			goto fail;
		}
		row_set(row, 0, cols, NULL);
	}
	t->rows = rows;
	if (scroll_buf_size < 0)
		scroll_buf_size = 0;
	t->scroll_buf = scroll_buf = calloc(scroll_buf_size, sizeof(Row));
	if (!scroll_buf)
		goto fail;
	for (Row *row = scroll_buf, *end = scroll_buf + scroll_buf_size; row < end; row++) {
		row->cells = calloc(cols, sizeof(Cell));
		if (!row->cells) {
			t->scroll_buf_size = row - scroll_buf;
			goto fail;
		}
	}
	t->curs_row = lines;
	t->curs_col = 0;
	/* initial scrolling area is the whole window */
	t->scroll_top = lines;
	t->scroll_bot = lines + rows;
	t->scroll_buf_size = scroll_buf_size;
	t->maxcols = t->cols = cols;
	return true;

fail:
	buffer_free(t);
	return false;
}

Vt *vt_create(int rows, int cols, int scroll_buf_size)
{
	Vt *t;

	if (rows <= 0 || cols <= 0)
		return NULL;

	t = calloc(1, sizeof(Vt));
	if (!t)
		return NULL;

	t->pty = -1;
	t->deffg = t->defbg = -1;
	if (!buffer_init(&t->buffer_normal, rows, cols, scroll_buf_size) ||
	    !buffer_init(&t->buffer_alternate, rows, cols, 0)) {
		free(t);
		return NULL;
	}
	t->buffer = &t->buffer_normal;
	t->copymode_cmd_multiplier = 0;
	return t;
}

static void buffer_resize(Buffer *t, int rows, int cols)
{
	Row *lines = t->lines;

	if (t->rows != rows) {
		if (t->curs_row > lines + rows) {
			/* scroll up instead of simply chopping off bottom */
			fill_scroll_buf(t, (t->curs_row - t->lines) - rows + 1);
		}
		while (t->rows > rows) {
			free(lines[t->rows - 1].cells);
			t->rows--;
		}

		lines = realloc(lines, sizeof(Row) * rows);
	}

	if (t->maxcols < cols) {
		for (int row = 0; row < t->rows; row++) {
			lines[row].cells = realloc(lines[row].cells, sizeof(Cell) * cols);
			if (t->cols < cols)
				row_set(lines + row, t->cols, cols - t->cols, NULL);
			lines[row].dirty = true;
		}
		Row *sbuf = t->scroll_buf;
		for (int row = 0; row < t->scroll_buf_size; row++) {
			sbuf[row].cells = realloc(sbuf[row].cells, sizeof(Cell) * cols);
			if (t->cols < cols)
				row_set(sbuf + row, t->cols, cols - t->cols, NULL);
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
			lines[t->rows].cells = calloc(t->maxcols, sizeof(Cell));
			row_set(lines + t->rows, 0, t->maxcols, t);
			t->rows++;
		}

		/* prepare for backfill */
		if (t->curs_row >= t->scroll_bot - 1) {
			deltarows = t->lines + rows - t->curs_row - 1;
			if (deltarows > t->scroll_amount_above)
				deltarows = t->scroll_amount_above;
		}
	}

	t->curs_row += lines - t->lines;
	t->scroll_top = lines;
	t->scroll_bot = lines + rows;
	t->lines = lines;

	/* perform backfill */
	if (deltarows > 0) {
		fill_scroll_buf(t, -deltarows);
		t->curs_row += deltarows;
	}
}

void vt_resize(Vt *t, int rows, int cols)
{
	struct winsize ws = { .ws_row = rows, .ws_col = cols };

	if (rows <= 0 || cols <= 0)
		return;

	vt_noscroll(t);
	if (t->copymode)
		vt_copymode_leave(t);
	buffer_resize(&t->buffer_normal, rows, cols);
	buffer_resize(&t->buffer_alternate, rows, cols);
	clamp_cursor_to_bounds(t);
	ioctl(t->pty, TIOCSWINSZ, &ws);
	kill(-t->childpid, SIGWINCH);
}

void vt_destroy(Vt *t)
{
	if (!t)
		return;
	buffer_free(&t->buffer_normal);
	buffer_free(&t->buffer_alternate);
	free(t->searchbuf);
	free(t);
}

void vt_dirty(Vt *t)
{
	Buffer *b = t->buffer;
	for (Row *row = b->lines, *end = row + b->rows; row < end; row++)
		row->dirty = true;
}

static void copymode_get_selection_boundry(Vt *t, Row **start_row, int *start_col, Row **end_row, int *end_col, bool clip) {
	Buffer *b = t->buffer;
	if (t->copymode_sel_start_row >= b->lines && t->copymode_sel_start_row < b->lines + b->rows) {
		/* within the current page */
		if (b->curs_row >= t->copymode_sel_start_row) {
			*start_row = t->copymode_sel_start_row;
			*end_row = b->curs_row;
			*start_col = t->copymode_sel_start_col;
			*end_col = b->curs_col;
		} else {
			*start_row = b->curs_row;
			*end_row = t->copymode_sel_start_row;
			*start_col = b->curs_col;
			*end_col = t->copymode_sel_start_col;
		}
		if (b->curs_col < *start_col && *start_row == *end_row) {
			*start_col = b->curs_col;
			*end_col = t->copymode_sel_start_col;
		}
	} else {
		/* part of the scrollback buffer is also selected */
		if (t->copymode_sel_start_row < b->lines) {
			/* above the current page */
			if (clip) {
				*start_row = b->lines;
				*start_col = 0;
			} else {
				int copied_lines = b->lines - t->copymode_sel_start_row;
				*start_row = &b->scroll_buf
					[(b->scroll_buf_ptr - copied_lines + b->scroll_buf_size) % b->scroll_buf_size];
				*start_col = t->copymode_sel_start_col;
			}
			*end_row = b->curs_row;
			*end_col = b->curs_col;
		} else {
			/* below the current page */
			*start_row = b->curs_row;
			*start_col = b->curs_col;
			if (clip) {
				*end_row = b->lines + b->rows;
				*end_col = b->cols - 1;
			} else {
				int copied_lines = t->copymode_sel_start_row -(b->lines + b->rows);
				*end_row = &b->scroll_buf
					[(b->scroll_buf_ptr + copied_lines) % b->scroll_buf_size];
				*end_col = t->copymode_sel_start_col;
			}
		}
	}
}

void vt_draw(Vt *t, WINDOW * win, int srow, int scol)
{
	Buffer *b = t->buffer;
	bool sel = false;
	Row *sel_row_start, *sel_row_end;
	int sel_col_start, sel_col_end;

	copymode_get_selection_boundry(t, &sel_row_start, &sel_col_start, &sel_row_end, &sel_col_end, true);
	curs_set(0);

	for (int i = 0; i < b->rows; i++) {
		Row *row = b->lines + i;

		if (!row->dirty)
			continue;

		wmove(win, srow + i, scol);
		Cell *cell = NULL;
		for (int j = 0; j < b->cols; j++) {
			Cell *prev_cell = cell;
			cell = row->cells + j;
			if (!prev_cell || cell->attr != prev_cell->attr
			    || cell->fg != prev_cell->fg
			    || cell->bg != prev_cell->bg) {
				if (cell->attr == A_NORMAL)
					cell->attr = t->defattrs;
				if (cell->fg == -1)
					cell->fg = t->deffg;
				if (cell->bg == -1)
					cell->bg = t->defbg;
				wattrset(win, (attr_t) cell->attr << NCURSES_ATTR_SHIFT);
				wcolor_set(win, vt_color_get(t, cell->fg, cell->bg), NULL);
			}

			if (t->copymode_selecting && ((row > sel_row_start && row < sel_row_end) ||
			    (row == sel_row_start && j >= sel_col_start && (row != sel_row_end || j <= sel_col_end)) ||
			    (row == sel_row_end && j <= sel_col_end && (row != sel_row_start || j >= sel_col_start)))) {
				wattrset(win, (attr_t) ((cell->attr << NCURSES_ATTR_SHIFT)|COPYMODE_ATTR));
				sel = true;
			} else if (sel) {
				wattrset(win, (attr_t) cell->attr << NCURSES_ATTR_SHIFT);
				wcolor_set(win, vt_color_get(t, cell->fg, cell->bg), NULL);
				sel = false;
			}

			if (is_utf8 && cell->text >= 128) {
				char buf[MB_CUR_MAX + 1];
				int len = wcrtomb(buf, cell->text, NULL);
				waddnstr(win, buf, len);
				if (wcwidth(cell->text) > 1)
					j++;
			} else {
				waddch(win, cell->text > ' ' ? cell->text : ' ');
			}
		}
		row->dirty = false;
	}

	wmove(win, srow + b->curs_row - b->lines, scol + b->curs_col);

	if (t->copymode_searching) {
		wattrset(win, t->defattrs << NCURSES_ATTR_SHIFT);
		mvwaddch(win, srow + b->rows - 1, 0, t->copymode_searching == 1 ? '/' : '?');
		int len = waddnwstr(win, t->searchbuf, b->cols - 1);
		whline(win, ' ', b->cols - len - 1);
	}

	curs_set(vt_cursor(t));
}

void vt_scroll(Vt *t, int rows)
{
	Buffer *b = t->buffer;
	if (!b->scroll_buf_size)
		return;
	if (rows < 0) { /* scroll back */
		if (rows < -b->scroll_amount_above)
			rows = -b->scroll_amount_above;
	} else { /* scroll forward */
		if (rows > b->scroll_amount_below)
			rows = b->scroll_amount_below;
	}
	fill_scroll_buf(b, rows);
	b->scroll_amount_below -= rows;
	if (t->copymode_selecting)
		t->copymode_sel_start_row -= rows;
}

void vt_noscroll(Vt *t)
{
	int scroll_amount_below = t->buffer->scroll_amount_below;
	if (scroll_amount_below)
		vt_scroll(t, scroll_amount_below);
}

void vt_bell(Vt *t, bool bell)
{
	t->bell = bell;
}

void vt_togglebell(Vt *t)
{
	t->bell = !t->bell;
}

pid_t vt_forkpty(Vt *t, const char *p, const char *argv[], const char *env[], int *pty)
{
	struct winsize ws;
	pid_t pid;
	const char **envp = env;
	int fd, maxfd;

	ws.ws_row = t->buffer->rows;
	ws.ws_col = t->buffer->cols;
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
		setenv("TERM", COLORS >= 256 ? "rxvt-256color" : "rxvt", 1);
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
	Buffer *b = t->buffer;
	char keyseq[16];
	sprintf(keyseq, "\e[%d;%dR", (int)(b->curs_row - b->lines), b->curs_col);
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

static Row *buffer_next_row(Buffer *t, Row *row, int direction)
{
	bool has_scroll_buf = t->scroll_buf_size > 0;
	Row *before_start_row, *before_end_row, *after_start_row, *after_end_row;
	Row *first_row = t->lines;
	Row *last_row = t->lines + t->rows - 1;

	if (has_scroll_buf) {
		before_end_row = &t->scroll_buf
			[(t->scroll_buf_ptr - 1 + t->scroll_buf_size) % t->scroll_buf_size];
		before_start_row = &t->scroll_buf
			[(t->scroll_buf_ptr - t->scroll_amount_above + t->scroll_buf_size) % t->scroll_buf_size];
		after_start_row = &t->scroll_buf[t->scroll_buf_ptr];
		after_end_row = &t->scroll_buf
			[(t->scroll_buf_ptr + t->scroll_amount_below - 1) % t->scroll_buf_size];
	}

	if (direction > 0) {
		if (row >= first_row && row < last_row)
			return ++row;
		if (row == last_row) {
			if (has_scroll_buf) {
				if (t->scroll_amount_below)
					return after_start_row;
				else if (t->scroll_amount_above)
					return before_start_row;
			}
			return first_row;
		}
		if (row == before_end_row)
			return first_row;
		if (row == after_end_row)
			return t->scroll_amount_above ? before_start_row : first_row;
		if (row == &t->scroll_buf[t->scroll_buf_size - 1])
			return t->scroll_buf;
		return ++row;
	} else {
		if (row > first_row && row <= last_row)
			return --row;
		if (row == first_row) {
			if (has_scroll_buf) {
				if (t->scroll_amount_above)
					return before_end_row;
				else if (t->scroll_amount_below)
					return after_end_row;
			}
			return last_row;
		}
		if (row == before_start_row)
			return t->scroll_amount_below ? after_end_row : last_row;
		if (row == after_start_row)
			return last_row;
		if (row == t->scroll_buf)
			return &t->scroll_buf[t->scroll_buf_size - 1];
		return --row;
	}
}

static void row_show(Vt *t, Row *r)
{
	Buffer *b = t->buffer;
	int below = b->scroll_amount_below;
	int above = b->scroll_amount_above;
	int ptr = b->scroll_buf_ptr;
	int size = b->scroll_buf_size;
	int row = r - b->scroll_buf;
	int scroll = 0;

	if (b->lines <= r && r < b->lines + b->rows) {
		b->curs_row = r;
		return;
	}

	if (!size)
		return;

	if (row < ptr) {
		if (row - ptr + size < below)
			scroll = row - ptr + size + 1;
		else if (ptr - row <= above)
			scroll = row - ptr;
	} else {
		if (row - ptr < below)
			scroll = row - ptr + 1;
		else if (ptr - row + size <= above)
			scroll = row - ptr - size;
	}

	if (scroll) {
		vt_scroll(t, scroll);
		b->curs_row = b->lines + (scroll > 0  ? b->rows - 1 : 0);
	}
}

static void copymode_search(Vt *t, int direction)
{
	if (!t->searchbuf || t->searchbuf[0] == '\0')
		return;

	Buffer *b = t->buffer;
	/* avoid match at current cursor position */
	Row *start_row = b->curs_row;
	int start_col = b->curs_col + direction;
	if (start_col >= b->cols) {
		start_col = 0;
		start_row = buffer_next_row(b, start_row, 1);
	} else if (start_col < 0) {
		start_col = b->cols - 1;
		start_row = buffer_next_row(b, start_row, -1);
	}

	Row *row = start_row, *matched_row = NULL;
	int matched_col = 0;
	int s_start = direction > 0 ? 0 : t->searchbuf_curs - 1;
	int s_end = direction > 0 ? t->searchbuf_curs - 1 : 0;
	int s = s_start;

	for (;;) {
		int col = direction > 0 ? 0 : b->cols - 1;
		if (row == start_row)
			col = start_col;
		for (; col >= 0 && col < b->cols; col += direction) {
			if (t->searchbuf[s] == row->cells[col].text) {
				if (s == s_start) {
					matched_row = row;
					matched_col = col;
				}
				if (s == s_end) {
					b->curs_col = matched_col;
					if (matched_row)
						row_show(t, matched_row);
					return;
				}
				s += direction;
				int width = wcwidth(t->searchbuf[s]);
				if (width < 0)
					width = 0;
				else if (width >= 1)
					width--;
				col += direction > 0 ? width : -width;
			} else
				s = s_start;
		}

		if ((row = buffer_next_row(b, row, direction)) == start_row)
			break;
	}
}

void vt_copymode_keypress(Vt *t, int keycode)
{
	Buffer *b = t->buffer;
	Row *start_row, *end_row;
	int direction, col, start_col, end_col, delta, scroll_page = b->rows / 2;
	char *copybuf, keychar = (char)keycode;
	wchar_t wc;
	ssize_t len;
	bool found;

	if (!t->copymode)
		return;

	if (t->copymode_searching) {
		switch (keycode) {
		case KEY_BACKSPACE:
			if (--t->searchbuf_curs < 0)
				t->searchbuf_curs = 0;
			t->searchbuf[t->searchbuf_curs] = '\0';
			break;
		case '\n':
			copymode_search(t, t->copymode_searching);
		case '\e':
			t->copymode_searching = 0;
			b->lines[b->rows - 1].dirty = true;
			break;
		default:
			len = (ssize_t)mbrtowc(&wc, &keychar, 1, &t->searchbuf_ps);

			if (len == -2)
				return;
			if (len == -1)
				wc = keycode;
			if (t->searchbuf_curs >= t->searchbuf_size - 2) {
				t->searchbuf_size *= 2;
				wchar_t *buf = realloc(t->searchbuf, t->searchbuf_size * sizeof(wchar_t));
				if (!buf)
					return;
				t->searchbuf = buf;
			}
			t->searchbuf[t->searchbuf_curs++] = wc;
			t->searchbuf[t->searchbuf_curs] = '\0';
			break;
		}
	} else {
		switch (keycode) {
		case '0' ... '9':
			t->copymode_cmd_multiplier = (t->copymode_cmd_multiplier * 10) + (keychar - '0');
			return;
		case KEY_PPAGE:
			delta = b->curs_row - b->lines;
			if (delta > scroll_page)
				b->curs_row -= scroll_page;
			else {
				b->curs_row = b->lines;
				vt_scroll(t, delta - scroll_page);
			}
			break;
		case KEY_NPAGE:
			delta = b->rows - (b->curs_row - b->lines);
			if (delta > scroll_page)
				b->curs_row += scroll_page;
			else {
				b->curs_row = b->lines + b->rows - 1;
				vt_scroll(t, scroll_page - delta);
			}
			break;
		case 'g':
			if (b->scroll_amount_above)
				vt_scroll(t, -b->scroll_amount_above);
			/* fall through */
		case 'H':
			b->curs_row = b->lines;
			break;
		case 'M':
			b->curs_row = b->lines + (b->rows / 2);
			break;
		case 'G':
			vt_noscroll(t);
			/* fall through */
		case 'L':
			b->curs_row = b->lines + b->rows - 1;
			break;
		case KEY_HOME:
		case '^':
			b->curs_col = 0;
			break;
		case KEY_END:
		case '$':
			start_col = b->cols - 1;
			for (int i = 0; i < b->cols; i++)
				if (b->curs_row->cells[i].text)
					start_col = i;
			b->curs_col = start_col;
			break;
		case '/':
		case '?':
			memset(&t->searchbuf_ps, 0, sizeof(mbstate_t));
			if (!t->searchbuf) {
				t->searchbuf_size = b->cols+1;
				t->searchbuf = malloc(t->searchbuf_size * sizeof(wchar_t));
			}
			if (!t->searchbuf)
				return;
			t->searchbuf[0] = L'\0';
			t->searchbuf_curs = 0;
			t->copymode_searching = keycode == '/' ? 1 : -1;
			break;
		case 'n':
		case 'N':
			copymode_search(t, keycode == 'n' ? 1 : -1);
			break;
		case 'v':
			t->copymode_selecting = true;
			t->copymode_sel_start_row = b->curs_row;
			t->copymode_sel_start_col = b->curs_col;
			break;
		case 'y':
			if (!t->copymode_selecting) {
				b->curs_col = 0;
				t->copymode_sel_start_row = b->curs_row +
					(t->copymode_cmd_multiplier ? t->copymode_cmd_multiplier - 1 : 0);
				if (t->copymode_sel_start_row >= b->lines + b->rows)
					t->copymode_sel_start_row = b->lines + b->rows - 1;
				t->copymode_sel_start_col = b->cols - 1;
			}

			copymode_get_selection_boundry(t, &start_row, &start_col, &end_row, &end_col, false);
			int line_count = t->copymode_sel_start_row > b->curs_row ?
				t->copymode_sel_start_row - b->curs_row :
				b->curs_row - t->copymode_sel_start_row;
			copybuf = calloc(1, (line_count + 1) * b->cols * MB_CUR_MAX + 1);

			if (copybuf) {
				char *s = copybuf;
				mbstate_t ps;
				memset(&ps, 0, sizeof(ps));
				Row *row = start_row;
				for (;;) {
					char *last_non_space = s;
					int j = (row == start_row) ? start_col : 0;
					int col = (row == end_row) ? end_col : b->cols - 1;
					for (size_t len = 0; j <= col; j++) {
						if (row->cells[j].text) {
							len = wcrtomb(s, row->cells[j].text, &ps);
							if (len > 0)
								s += len;
							last_non_space = s;
						} else if (len) {
							len = 0;
						} else {
							*s++ = ' ';
						}
					}

					s = last_non_space;

					if (row == end_row)
						break;
					else
						*s++ = '\n';

					row = buffer_next_row(b, row, 1);
				}
				*s = '\0';
				if (t->event_handler)
					t->event_handler(t, VT_EVENT_COPY_TEXT, copybuf);
			}
			/* fall through */
		case '\e':
		case 'q':
			vt_copymode_leave(t);
			return;
		default:
			for (int c = 0; c < (t->copymode_cmd_multiplier ? t->copymode_cmd_multiplier : 1); c++) {
				int width;
				switch (keycode) {
				case 'w':
				case 'W':
				case 'b':
				case 'B':
					direction = (keycode == 'w' || keycode == 'W') ? 1 : -1;
					start_col = (direction > 0) ? 0 : b->cols - 1;
					end_col = (direction > 0) ? b->cols - 1 : 0;
					col = b->curs_col;
					found = false;
					do {
						for (;;) {
							if (b->curs_row->cells[col].text == ' ') {
								found = true;
								break;
							}

							if (col == end_col)
								break;
							col += direction;
						}

						if (found) {
							while (b->curs_row->cells[col].text == ' ') {
								if (col == end_col) {
									b->curs_row += direction;
									break;
								}
								col += direction;
							}
						} else {
							col = start_col;
							b->curs_row += direction;
						}

						if (b->curs_row < b->lines) {
							b->curs_row = b->lines;
							if (b->scroll_amount_above)
								vt_scroll(t, -1);
							else
								break;
						}

						if (b->curs_row >= b->lines + b->rows) {
							b->curs_row = b->lines + b->rows - 1;
							if (b->scroll_amount_below)
								vt_scroll(t, 1);
							else
								break;
						}
					} while (!found);

					if (found)
						b->curs_col = col;
					break;
				case KEY_UP:
				case 'k':
					if (b->curs_row == b->lines)
						vt_scroll(t, -1);
					else
						b->curs_row--;
					break;
				case KEY_DOWN:
				case 'j':
					if (b->curs_row == b->lines + b->rows - 1)
						vt_scroll(t, 1);
					else
						b->curs_row++;
					break;
				case KEY_RIGHT:
				case 'l':
					width = wcwidth(b->curs_row->cells[b->curs_col].text);
					b->curs_col += width < 1 ? 1 : width;
					if (b->curs_col >= b->cols) {
						b->curs_col = b->cols - 1;
						t->copymode_cmd_multiplier = 0;
					}
					break;
				case KEY_LEFT:
				case 'h':
					width = 1;
					if (b->curs_col >= 2 && !b->curs_row->cells[b->curs_col-1].text)
						width = wcwidth(b->curs_row->cells[b->curs_col-2].text);
					b->curs_col -= width < 1 ? 1 : width;
					if (b->curs_col < 0) {
						b->curs_col = 0;
						t->copymode_cmd_multiplier = 0;
					}
					break;
				}
			}
			break;
		}
	}
	if (t->copymode_selecting)
		vt_dirty(t);
	t->copymode_cmd_multiplier = 0;
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

static unsigned int color_hash(short fg, short bg)
{
	if (fg == -1)
		fg = COLORS;
	if (bg == -1)
		bg = COLORS + 1;
	return fg * (COLORS + 2) + bg;
}

short vt_color_get(Vt *t, short fg, short bg)
{
	if (fg >= COLORS)
		fg = (t ? t->deffg : default_fg);
	if (bg >= COLORS)
		bg = (t ? t->defbg : default_bg);

	if (!has_default_colors) {
		if (fg == -1)
			fg = (t && t->deffg != -1 ? t->deffg : default_fg);
		if (bg == -1)
			bg = (t && t->defbg != -1 ? t->defbg : default_bg);
	}

	if (!color2palette || (fg == -1 && bg == -1))
		return 0;
	unsigned int index = color_hash(fg, bg);
	if (color2palette[index] == 0) {
		short oldfg, oldbg;
		for (;;) {
			if (++color_pair_current >= color_pairs_max)
				color_pair_current = color_pairs_reserved + 1;
			pair_content(color_pair_current, &oldfg, &oldbg);
			unsigned int old_index = color_hash(oldfg, oldbg);
			if (color2palette[old_index] >= 0) {
				if (init_pair(color_pair_current, fg, bg) == OK) {
					color2palette[old_index] = 0;
					color2palette[index] = color_pair_current;
				}
				break;
			}
		}
	}

	short color_pair = color2palette[index];
	return color_pair >= 0 ? color_pair : -color_pair;
}

short vt_color_reserve(short fg, short bg)
{
	if (!color2palette || fg >= COLORS || bg >= COLORS)
		return 0;
	if (!has_default_colors && fg == -1)
		fg = default_fg;
	if (!has_default_colors && bg == -1)
		bg = default_bg;
	if (fg == -1 && bg == -1)
		return 0;
	unsigned int index = color_hash(fg, bg);
	if (color2palette[index] >= 0) {
		if (init_pair(++color_pairs_reserved, fg, bg) == OK)
			color2palette[index] = -color_pairs_reserved;
	}
	short color_pair = color2palette[index];
	return color_pair >= 0 ? color_pair : -color_pair;
}

static void init_colors(void)
{
	pair_content(0, &default_fg, &default_bg);
	if (default_fg == -1)
		default_fg = COLOR_WHITE;
	if (default_bg == -1)
		default_bg = COLOR_BLACK;
	has_default_colors = (use_default_colors() == OK);
	color_pairs_max = MIN(COLOR_PAIRS, MAX_COLOR_PAIRS);
	color2palette = calloc((COLORS + 2) * (COLORS + 2), sizeof(short));
	vt_color_reserve(COLOR_WHITE, COLOR_BLACK);
}

void vt_init(void)
{
	init_colors();
	is_utf8_locale();
}

void vt_shutdown(void)
{
	free(color2palette);
}

void vt_set_escseq_handler(Vt *t, vt_escseq_handler_t handler)
{
	t->escseq_handler = handler;
}

void vt_set_event_handler(Vt *t, vt_event_handler_t handler)
{
	t->event_handler = handler;
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
	if (t->copymode)
		return 1;
	return t->buffer->scroll_amount_below ? 0 : !t->curshid;
}

unsigned vt_copymode(Vt *t)
{
	return t->copymode;
}

void vt_copymode_enter(Vt *t)
{
	if (t->copymode)
		return;
	Buffer *b = t->buffer;
	t->copymode_curs_srow = b->curs_row - b->lines;
	t->copymode_curs_scol = b->curs_col;
	t->copymode = true;
}

void vt_copymode_leave(Vt *t)
{
	if (!t->copymode)
		return;
	Buffer *b = t->buffer;
	t->copymode = false;
	t->copymode_selecting = false;
	t->copymode_searching = false;
	t->copymode_sel_start_row = b->lines;
	t->copymode_sel_start_col = 0;
	t->copymode_cmd_multiplier = 0;
	b->curs_row = b->lines + t->copymode_curs_srow;
	b->curs_col = t->copymode_curs_scol;
	vt_noscroll(t);
	vt_dirty(t);
}

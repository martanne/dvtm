static int statusfd = -1;
static char stext[512];
static int barpos = BARPOS;
static unsigned int bh = 1, by;

static void
updatebarpos(void) {
	by = 0;
	wax = 0;
	way = 0;
	wah = height;
	if (statusfd == -1)
		return;
	if (barpos == BarTop) {
		wah -= bh;
		way += bh;
	} else if (barpos == BarBot) {
		wah -= bh;
		by = wah;
	}
}

static void
drawbar() {
	wchar_t wbuf[sizeof stext];
	int w, maxwidth = width - 2;
	if (barpos == BarOff || !*stext)
		return;
	curs_set(0);
	attrset(BAR_ATTR);
	madtty_color_set(stdscr, BAR_FG, BAR_BG);
	mvaddch(by, 0, '[');
	if (mbstowcs(wbuf, stext, sizeof stext) == -1)
		return;
	if ((w = wcswidth(wbuf, maxwidth)) == -1)
		return;
	if (BAR_ALIGN == ALIGN_RIGHT) {
		for (int i = 0; i + w < maxwidth; i++)
			addch(' ');
	}
	addstr(stext);
	if (BAR_ALIGN == ALIGN_LEFT) {
		for (; w < maxwidth; w++)
			addch(' ');
	}
	mvaddch(by, width - 1, ']');
	attrset(NORMAL_ATTR);
	if (sel)
		curs_set(madtty_cursor(sel->term));
	refresh();
}

static void
togglebar(const char *args[]) {
	if (barpos == BarOff)
		barpos = (BARPOS == BarOff) ? BarTop : BARPOS;
	else
		barpos = BarOff;
	updatebarpos();
	arrange();
	drawbar();
}

static void
handle_statusbar() {
	char *p;
	int r;
	switch (r = read(statusfd, stext, sizeof stext - 1)) {
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
			for (; p >= stext && *p == '\n'; *p-- = '\0');
			for (; p >= stext && *p != '\n'; --p);
			if (p > stext)
				strncpy(stext, p + 1, sizeof stext);
			drawbar();
	}
}

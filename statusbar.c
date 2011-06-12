static void
updatebarpos(void) {
	bar.y = 0;
	wax = 0;
	way = 0;
	wah = height;
	if (bar.fd == -1)
		return;
	if (bar.pos == BarTop) {
		wah -= bar.h;
		way += bar.h;
	} else if (bar.pos == BarBot) {
		wah -= bar.h;
		bar.y = wah;
	}
}

static void
drawbar() {
	wchar_t wbuf[sizeof bar.text];
	int w, maxwidth = width - 2;
	if (bar.pos == BarOff || !bar.text[0])
		return;
	curs_set(0);
	attrset(BAR_ATTR);
	wcolor_set(stdscr, madtty_color_get(BAR_FG, BAR_BG), NULL);
	mvaddch(bar.y, 0, '[');
	if (mbstowcs(wbuf, bar.text, sizeof bar.text) == (size_t)-1)
		return;
	if ((w = wcswidth(wbuf, maxwidth)) == -1)
		return;
	if (BAR_ALIGN == ALIGN_RIGHT) {
		for (int i = 0; i + w < maxwidth; i++)
			addch(' ');
	}
	addstr(bar.text);
	if (BAR_ALIGN == ALIGN_LEFT) {
		for (; w < maxwidth; w++)
			addch(' ');
	}
	mvaddch(bar.y, width - 1, ']');
	attrset(NORMAL_ATTR);
	if (sel)
		curs_set(madtty_cursor(sel->term));
	refresh();
}

static void
togglebar(const char *args[]) {
	if (bar.pos == BarOff)
		bar.pos = (BARPOS == BarOff) ? BarTop : BARPOS;
	else
		bar.pos = BarOff;
	updatebarpos();
	arrange();
	drawbar();
}

static void
handle_statusbar() {
	char *p;
	int r;
	switch (r = read(bar.fd, bar.text, sizeof bar.text - 1)) {
		case -1:
			strncpy(bar.text, strerror(errno), sizeof bar.text - 1);
			bar.text[sizeof bar.text - 1] = '\0';
			bar.fd = -1;
			break;
		case 0:
			bar.fd = -1;
			break;
		default:
			bar.text[r] = '\0'; p = bar.text + strlen(bar.text) - 1;
			for (; p >= bar.text && *p == '\n'; *p-- = '\0');
			for (; p >= bar.text && *p != '\n'; --p);
			if (p > bar.text)
				strncpy(bar.text, p + 1, sizeof bar.text);
			drawbar();
	}
}

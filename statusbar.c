int statusfd = -1;
char stext[512];
int barpos = BARPOS;
unsigned int bh = 1, by;

void
updatebarpos(void) {
	by = 0;
	wax = 0;
	way = 0;
	waw = width;
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

void
drawbar() {
	int s, l, maxlen = width - 2;
	char t = stext[maxlen];
	if (barpos == BarOff || !*stext)
		return;
	curs_set(0);
	attrset(BAR_ATTR);
	madtty_color_set(stdscr, BAR_FG, BAR_BG);
	mvaddch(by, 0, '[');
	stext[maxlen] = '\0';
	l = strlen(stext);
	if (BAR_ALIGN_RIGHT)
		for (s = 0; s + l < maxlen; s++)
			addch(' ');
	else
		for (; l < maxlen; l++)
			stext[l] = ' ';
	addstr(stext);
	stext[maxlen] = t;
	addch(']');
	attrset(NORMAL_ATTR);
	if (sel)
		curs_set(madtty_cursor(sel->term));
	refresh();
}

void
togglebar(const char *args[]) {
	if (barpos == BarOff)
		barpos = (BARPOS == BarOff) ? BarTop : BARPOS;
	else
		barpos = BarOff;
	updatebarpos();
	arrange();
	drawbar();
}

void
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

static void tile(void)
{
	unsigned int i, n, nx, ny, nw, nh, mw, th;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
		if (!c->minimized)
			n++;

	mw = n <= 1 ? waw : screen.mfact * waw;
	th = n <= 1 ? 0 : wah / (n - 1);
	nx = wax;
	ny = way;

	for (i = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->minimized)
			continue;
		if (i == 0) {	/* master */
			nw = mw;
			nh = wah;
		} else {	/* tile window */
			if (i == 1) {
				ny = way;
				nx += mw;
				nw = waw - mw;
				mvvline(ny, nx, ACS_VLINE, wah);
				mvaddch(ny, nx, ACS_TTEE);
				nx++, nw--;
			}
			nh = (i < n - 1) ? th : (way + wah) - ny;
			if (i > 1)
				mvaddch(ny, nx - 1, ACS_LTEE);
		}
		resize(c, nx, ny, nw, nh);
		if (i > 0)
			ny += nh;
		i++;
	}
}

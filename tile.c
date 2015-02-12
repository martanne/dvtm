static void tile(void)
{
	unsigned int i, n, nx, ny, nw, nh, m, mw, mh, th;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
		if (!c->minimized)
			n++;

	m  = MAX(1, MIN(n, screen.nmaster));
	mw = n == m ? waw : screen.mfact * waw;
	mh = wah / m;
	th = n == m ? 0 : wah / (n - m);
	nx = wax;
	ny = way;

	for (i = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->minimized)
			continue;
		if (i < m) {	/* master */
			nw = mw;
			nh = (i < m - 1) ? mh : (way + wah) - ny;
		} else {	/* tile window */
			if (i == m) {
				ny = way;
				nx += mw;
				mvvline(ny, nx, ACS_VLINE, wah);
				mvaddch(ny, nx, ACS_TTEE);
				nx++;
				nw = waw - mw -1;
			}
			nh = (i < n - 1) ? th : (way + wah) - ny;
			if (i > m)
				mvaddch(ny, nx - 1, ACS_LTEE);
		}
		resize(c, nx, ny, nw, nh);
		ny += nh;
		i++;
	}

	/* Fill in nmaster intersections */
	if (n > m) {
		ny = way + mh;
		for (i = 1; i < m; i++) {
			mvaddch(ny, nx - 1, ((ny - 1) % th ? ACS_RTEE : ACS_PLUS));
			ny += mh;
		}
	}
}

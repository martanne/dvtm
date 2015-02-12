static void bstack(void)
{
	unsigned int i, n, nx, ny, nw, nh, m, mw, mh, tw;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
		if (!c->minimized)
			n++;

	m  = MAX(1, MIN(n, screen.nmaster));
	mh = n == m ? wah : screen.mfact * wah;
	mw = waw / m;
	tw = n == m ? 0 : waw / (n - m);
	nx = wax;
	ny = way;

	for (i = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->minimized)
			continue;
		if (i < m) {	/* master */
			if (i > 0) {
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				nx++;
			}
			nh = mh;
			nw = (i < m - 1) ? mw : (wax + waw) - nx;
		} else {	/* tile window */
			if (i == m) {
				nx = wax;
				ny += mh;
				nh = (way + wah) - ny;
			}
			if (i > m) {
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				nx++;
			}
			nw = (i < n - 1) ? tw : (wax + waw) - nx;
		}
		resize(c, nx, ny, nw, nh);
		nx += nw;
		i++;
	}

	/* Fill in nmaster intersections */
	if (n > m) {
		nx = wax;
		for (i = 0; i < m; i++) {
			if (i > 0) {
				mvaddch(ny, nx, ACS_PLUS);
				nx++;
			}
			nw = (i < m - 1) ? mw : (wax + waw) - nx;
			nx += nw;
		}
	}
}

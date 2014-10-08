static void bstack(void)
{
	unsigned int i, n, nx, ny, nw, nh, mh, tw;
	Client *c;

	for (n = 0, c = clients; c && !c->minimized; c = c->next, n++);

	mh = n <= 1 ? wah : screen.mfact * wah;
	tw = n <= 1 ? 0 : waw / (n - 1);
	nx = wax;
	ny = way;

	for (i = 0, c = clients; c && !c->minimized; c = c->next, i++) {
		if (i == 0) {	/* master */
			nh = mh;
			nw = waw;
		} else {	/* tile window */
			if (i == 1) {
				nx = wax;
				ny += mh;
				nh = (way + wah) - ny;
			}
			nw = (i < n - 1) ? tw : (wax + waw) - nx;
			if (i > 1) {
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				nx++, nw--;
			}
		}

		resize(c, nx, ny, nw, nh);

		if (i > 0)
			nx += nw;
	}
}

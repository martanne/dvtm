void tstack(void)
{
	unsigned int i, n, nx, ny, nw, nh, mh, tw;
	Client *c;

	for (n = 0, c = clients; c && !c->minimized; c = c->next, n++);

	mh = n <= 1 ? wah : screen.mfact * wah;
	tw = n <= 1 ? 0 : waw / (n - 1);
	nx = wax;
	nw = waw;

	for (i = 0, c = clients; c && !c->minimized; c = c->next, i++) {
		if (i == 0) {	/* master */
			ny = way + wah - mh;
			nh = mh;
		} else {	/* tile window */
			if (i == 1) {
				nx = wax;
				ny = way;
				nh = wah - mh - ny + way;
			}
			nw = (i < n - 1) ? tw : (wax + waw) - nx;
			if (i > 1) {
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				++nx, --nw;
			}
		}

		resize(c, nx, ny, nw, nh);

		if (i > 0)
			nx += nw;
	}
}

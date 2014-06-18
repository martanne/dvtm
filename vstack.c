/* A vertical stack layout, all windows have the full screen width. */
static void vstack(void)
{
	unsigned int i, m, n, ny, nh, mh, th;
	Client *c;

	for (n = 0, m = 0, c = clients; c; c = c->next, n++)
		if (c->minimized)
			m++;

	if (n == 1)
		mh = wah;
	else if (n - 1 == m)
		mh = wah - m;
	else
		mh = screen.mfact * (wah - m);
	/* true if there are at least 2 non minimized clients */
	if (n - 1 > m)
		th = (wah - mh) / (n - 1 - m);

	/* TODO: place all minimized windows on the last line */

	ny = way;
	for (i = 0, c = clients; c; c = c->next, i++) {
		if (i == 0) {  /* master */
			nh = mh;
		} else if (i == n - m - 1) {  /* last not minimized client */
			nh = (way + wah) - ny - m;
		} else if (c->minimized) {
			nh = 1;
		} else {  /* normal non minimized tile window */
			nh = th;
		}
		resize(c, wax, ny, waw, nh);
		ny += nh;
	}
}

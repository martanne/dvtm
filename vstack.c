/* A vertical stack layout, all windows have the full screen width. */
static void vstack(void)
{
	unsigned int i, n, ny, nh, m, mh, th;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
		if (!c->minimized)
			n++;

	m  = MAX(1, MIN(n, screen.nmaster));
	mh = (n == m ? wah : screen.mfact * wah);
	th = n == m ? 0 : (wah - mh) / (n - m);
	ny = way;

	for (i = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->minimized)
			continue;
		if (i < m) /* master */
			nh = (i < m - 1) ? mh / m : (way + mh) - ny;
		else /* tile window */
			nh = (i < n - 1) ? th : (way + wah) - ny;
		resize(c, wax, ny, waw, nh);
		ny += nh;
		i++;
	}
}

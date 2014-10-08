/* A vertical stack layout, all windows have the full screen width. */
static void vstack(void)
{
	unsigned int i, n, ny, nh, mh, th;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
		if (!c->minimized)
			n++;

	mh = n <= 1 ? wah : screen.mfact * wah;
	th = n <= 1 ? 0 : (wah - mh) / (n - 1);
	ny = way;

	for (i = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->minimized)
			continue;
		if (i == 0) /* master */
			nh = mh;
		else /* tile window */
			nh = (i < n - 1) ? th : (way + wah) - ny;
		resize(c, wax, ny, waw, nh);
		ny += nh;
		i++;
	}
}

static void fibonacci(int s)
{
	unsigned int nx, ny, nw, nnw, nh, nnh, i, n, mod;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
		if (!c->minimized)
			n++;

	/* initial position and dimensions */
	nx = wax;
	ny = way;
	nw = (n == 1) ? waw : screen.mfact * waw;
	/* don't waste space dviding by 2 doesn't work for odd numbers
	 * plus we need space for the border too. therefore set up these
	 * variables for the next new width/height
	 */
	nnw = waw - nw - 1;
	nnh = nh = wah;

	/* set the mod factor, 2 for dwindle, 4 for spiral */
	mod = s ? 4 : 2;

	for (i = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->minimized)
			continue;
		/* dwindle: even case, spiral: case 0 */
		if (i % mod == 0) {
			if (i) {
				if (s) {
					nh = nnh;
					ny -= nh;
				} else {
					ny += nh;
					nh = nnh;
				}
				/* don't adjust the width for the last client */
				if (i < n - 1) {
					nw /= 2;
					nnw -= nw + 1;
				}
				mvaddch(ny, nx - 1, ACS_LTEE);
			}
		} else if (i % mod == 1) {	/* dwindle: odd case, spiral: case 1 */
			nx += nw;
			mvvline(ny, nx, ACS_VLINE, nh);
			mvaddch(ny, nx, ACS_TTEE);
			++nx;
			nw = nnw;
			/* don't adjust the height for the last client */
			if (i < n - 1) {
				nh /= 2;
				nnh -= nh;
			}
		} else if (i % mod == 2 && s) {	/* spiral: case 2 */
			ny += nh;
			nh = nnh;
			/* don't adjust the width for the last client */
			if (i < n - 1) {
				nw /= 2;
				nnw -= nw + 1;
				nx += nnw;
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				++nx;
			} else {
				mvaddch(ny, nx - 1, ACS_LTEE);
			}
		} else if (s) {	/* spiral: case 3 */
			nw = nnw;
			nx -= nw + 1;	/* border */
			/* don't adjust the height for the last client */
			if (i < n - 1) {
				nh /= 2;
				nnh -= nh;
				ny += nnh;
			}
			mvaddch(ny, nx - 1, ACS_LTEE);
		}

		resize(c, nx, ny, nw, nh);
		i++;
	}
}

static void spiral(void)
{
	fibonacci(1);
}

static void dwindle(void)
{
	fibonacci(0);
}

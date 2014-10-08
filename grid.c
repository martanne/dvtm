static void grid(void)
{
	unsigned int i, n, nx, ny, nw, nh, aw, ah, cols, rows;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
		if (!c->minimized)
			n++;
	/* grid dimensions */
	for (cols = 0; cols <= n / 2; cols++)
		if (cols * cols >= n)
			break;
	rows = (cols && (cols - 1) * cols >= n) ? cols - 1 : cols;
	/* window geoms (cell height/width) */
	nh = wah / (rows ? rows : 1);
	nw = waw / (cols ? cols : 1);
	for (i = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->minimized)
			continue;
		/* if there are less clients in the last row than normal adjust the
		 * split rate to fill the empty space */
		if (rows > 1 && i == (rows * cols) - cols && (n - i) <= (n % cols))
			nw = waw / (n - i);
		nx = (i % cols) * nw + wax;
		ny = (i / cols) * nh + way;
		/* adjust height/width of last row/column's windows */
		ah = (i >= cols * (rows - 1)) ? wah - nh * rows : 0;
		/* special case if there are less clients in the last row */
		if (rows > 1 && i == n - 1 && (n - i) < (n % cols))
			/* (n % cols) == number of clients in the last row */
			aw = waw - nw * (n % cols);
		else
			aw = ((i + 1) % cols == 0) ? waw - nw * cols : 0;
		if (i % cols) {
			mvvline(ny, nx, ACS_VLINE, nh + ah);
			/* if we are on the first row, or on the last one and there are fewer clients
			 * than normal whose border does not match the line above, print a top tree char
			 * otherwise a plus sign. */
			if (i <= cols
			    || (i >= rows * cols - cols && n % cols
				&& (cols - (n % cols)) % 2))
				mvaddch(ny, nx, ACS_TTEE);
			else
				mvaddch(ny, nx, ACS_PLUS);
			nx++, aw--;
		}
		resize(c, nx, ny, nw + aw, nh + ah);
		i++;
	}
}

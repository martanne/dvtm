void
grid(void) {
	unsigned int i, n, cx, cy, cw, ch, aw, ah, cols, rows;
	Client *c;

	for(n = 0, c = clients; c; c = c->next)
		n++;

	/* grid dimensions */
	for(cols = 0; cols <= n/2; cols++)
		if(cols*cols >= n)
			break;
	rows = (cols && (cols - 1) * cols >= n) ? cols - 1 : cols;
	/* window geoms (cell height/width) */
	ch = wah / (rows ? rows : 1);
	cw = waw / (cols ? cols : 1);
	for(i = 0, c = clients; c; c = c->next,i++) {
		/* if there are less clients in the last row than normal adjust the 
		 * split rate to fill the empty space */
		if(rows > 1 && i == (rows * cols) - cols && (n - i) <= (n % cols))
			cw = waw / (n - i);
		cx = (i % cols) * cw;
		cy = (i / cols) * ch;
		/* adjust height/width of last row/column's windows */
		ah = (i >= cols * (rows -1)) ? wah - ch * rows : 0;
		/* special case if there are less clients in the last row */
		if(rows > 1 && i + 1 == n && (n - i) < (n % cols))
			/* (n % cols) == number of clients in the last row */
			aw = waw - cw * (n % cols);
		else
			aw = ((i + 1) % cols == 0) ? waw - cw * cols : 0;
		resize(c, cx, cy, cw + aw, ch + ah);
	}
}

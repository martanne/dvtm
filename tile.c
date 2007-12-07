void
tile(void) {
	unsigned int i, n, nx, ny, nw, nh, mw, th;
	Client *c;

	for(n = 0, c = clients; c; c = c->next)
		n++;

	/* window geoms */
	mw = (n == 1) ? waw : mwfact * waw;
	th = (n > 1) ? wah / (n - 1) : wah;

	nx = wax;
	ny = way;
	for(i = 0, c = clients; c; c = c->next, i++) {
		if(i == 0) { /* master */
			nw = mw;
			nh = wah;
		} else {  /* tile window */
			if(i == 1) {
				ny = way;
				nx += mw;
			}
			nw = waw - mw;
			if(i + 1 == n) /* remainder */
				nh = (way + wah) - ny;
			else
				nh = th;
		}
		resize(c,nx,ny,nw,nh);
		if(n > 1 && th != wah)
			ny += nh;
	}
}

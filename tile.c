void
tile(void) {
	unsigned int i, m, n, nx, ny, nw, nh, mw, th;
	Client *c; 

	for(n = 0, m = 0, c = clients; c; c = c->next, n++)
		if(c->minimized)
			m++;
	/* window geoms */
	mw = (n == 1 || n - 1 == m) ? waw : mwfact * waw;
	/* check if there are at least 2 non minimized clients */
	if(n - 1 > m) 
		th = (wah - m) / (n - m - 1);

	nx = wax;
	ny = way;
	for(i = 0, c = clients; c; c = c->next, i++) {
		if(i == 0) { /* master */
			nw = mw;
			nh = (n - 1 > m) ? wah : wah - m; 
		} else {  /* tile window */
			if(i == 1) {
				if(n - 1 > m){
					ny = way;
					nx += mw;
					nw = waw - mw;
				} else 
					ny = way + wah - m;
			}
			/* remainder */
			if(m == 0 && i + 1 == n) /* no minimized clients */ 
				nh = (way + wah) - ny;
			else if(i == n - m - 1) /* last not minimized client */
				nh = (way + wah - (n - i - 1)) - ny;
			else
				nh = th;

			if(c->minimized)
				nh = 1;
		}
		resize(c,nx,ny,nw,nh);
		if(n > 1 && th != wah)
			ny += nh;
	}
}

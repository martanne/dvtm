static void
bstack(void) {
	unsigned int i, m, n, nx, ny, nw, nh, mh, tw;
	Client *c;

	for(n = 0, m = 0, c = clients; c; c = c->next, n++)
		if(c->minimized)
			m++;

	if(n == 1)
		mh = wah;
	else if(n - 1 == m)
		mh = wah - m;
	else
		mh = mwfact * (wah - m);
	/* true if there are at least 2 non minimized clients */
	if(n - 1 > m)
		tw = waw / (n - m - 1);

	nx = wax;
	ny = way;
	for(i = 0, c = clients; c; c = c->next, i++){
		if(i == 0){ /* master */
			nh = mh;
			nw = waw;
		} else { /* tile window */
			if(i == 1){
				nx = wax;
				ny += mh;
				nh = (way + wah - m) - ny;
			}
			if(i == n - m - 1){ /* last not minimized client */
				nw = (wax + waw) - nx;
			} else if(i == n - m){ /* first minimized client */
				ny += nh;
				nx = wax;
				nw = waw;
				nh = 1;
			} else if(c->minimized) { /* minimized window */
				nw = waw;
				nh = 1;
				ny++;
			} else /* normal non minimized tile window */
				nw = tw;
			if(i > 1 && !c->minimized){
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				nx++, nw--;
			}
		}

		resize(c,nx,ny,nw,nh);

		if(n > 1 && i < n - m - 1)
			nx += nw;
	}
}

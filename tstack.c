void
tstack(void) {
	unsigned int i, m, n, nx, ny, nw, nh, mh, tw;
	Client *c;

	for(n = 0, m = 0, c = clients; c; c = c->next, n++)
		if(c->minimized)
			m++;

	/* relative height */
	mh = (wah - m) * (n == 1 || n - 1 == m ? 1 : mwfact);

	/* true if there are at least 2 non minimized clients */
	if(n - 1 > m)
		tw = waw / (n - m - 1);

	nx = wax, nw = waw;
	for(i = 0, c = clients; c; c = c->next, i++){
		if(i == 0){ /* master */
			ny = way + wah - mh;
			nh = mh;
		}
		else { /* tile window */
			if(i == 1){
				nx = wax;
				ny = way + m;
				nh = wah - mh - ny + way;
			}
			if(i == n - m - 1){ /* last not minimized client */
				nw = (wax + waw) - nx;
			}
			else if(i == n - m) { /* first minimized client */
				nx = wax;
				--ny;
				nh = 1;
				nw = waw;
			}
			else if(c->minimized) { /* minimized window */
				--ny;
				nh = 1;
				nw = waw;
			}
			else /* normal non minimized tile window */
				nw = tw;

			if(i > 1 && !c->minimized){
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				++nx, --nw;
			}
		}

		resize(c,nx,ny,nw,nh);

		if(n > 1 && i < n - m - 1)
			nx += nw;
	}
}

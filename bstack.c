void
bstack(void) {
	unsigned int i, n, nx, ny, nw, nh, mh, tw;
	Client *c;

	for(n = 0, c = clients; c; c = c->next)
		n++;

	mh = (n == 1) ? wah : mwfact * wah;
	tw = (n > 1) ? waw / (n - 1) : 0;

	nx = wax;
	ny = way;
	nh = 0;
	for(i = 0, c = clients; c; c = c->next, i++){
		if(i == 0){
			nh = mh;
			nw = waw;
		} else {
			if(i == 1){
				nx = wax;
				ny += mh;
				nh = (way + wah) - ny;
			}
			if(i + 1 == n)
				nw = (wax + waw) - nx;
			else
				nw = tw;
		}
		resize(c,nx,ny,nw,nh);
		if(n > 1 && tw != waw)
			nx += nw;
	}
}

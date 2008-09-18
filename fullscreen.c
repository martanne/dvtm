static void
fullscreen(void) {
	Client *c;
	for(c = clients; c; c = c->next)
		resize(c, wax, way, waw, wah);
}

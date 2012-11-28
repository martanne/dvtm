static void fullscreen(void)
{
	for (Client *c = clients; c; c = c->next)
		resize(c, wax, way, waw, wah);
}

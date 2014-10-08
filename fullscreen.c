static void fullscreen(void)
{
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next))
		resize(c, wax, way, waw, wah);
}

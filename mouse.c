static Client *msel = NULL;
static bool mouse_events_enabled = true;

static void
mouse_focus(const char *args[]) {
	focus(msel);
	if (msel->minimized)
		toggleminimize(NULL);
}

static void
mouse_fullscreen(const char *args[]) {
	mouse_focus(NULL);
	if (isarrange(fullscreen))
		setlayout(NULL);
	else
		setlayout(args);
}

static void
mouse_minimize(const char *args[]) {
	focus(msel);
	toggleminimize(NULL);
}

static void
mouse_zoom(const char *args[]) {
	focus(msel);
	zoom(NULL);
}

static Client*
get_client_by_coord(int x, int y) {
	Client *c;
	if (y < way || y >= wah)
		return NULL;
	if (isarrange(fullscreen))
		return sel;
	for (c = clients; c; c = c->next) {
		if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) {
			debug("mouse event, x: %d y: %d client: %d\n", x, y, c->order);
			return c;
		}
	}
	return NULL;
}

static void
handle_mouse() {
	MEVENT event;
	unsigned int i;
	if (getmouse(&event) != OK)
		return;
	msel = get_client_by_coord(event.x, event.y);

	if (!msel)
		return;

	debug("mouse x:%d y:%d cx:%d cy:%d mask:%d\n", event.x, event.y, event.x - msel->x, event.y - msel->y, event.bstate);

	madtty_mouse(msel->term, event.x - msel->x, event.y - msel->y, event.bstate);

	if (mouse_events_enabled) {
		for (i = 0; i < countof(buttons); i++) {
			if (event.bstate & buttons[i].mask)
				buttons[i].action.cmd(buttons[i].action.args);
		}
	}

	msel = NULL;
}

static void
mouse_setup() {
	int i;
	mmask_t mask = BUTTON1_CLICKED | BUTTON2_CLICKED;
	for (i = 0; i < countof(buttons); i++)
		mask |= buttons[i].mask;
	mousemask(mask, NULL);
}

static void
mouse_toggle() {
	mouse_events_enabled = !mouse_events_enabled;
}

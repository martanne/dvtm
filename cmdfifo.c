int cmdfd = -1;
const char *cmdpath = NULL;

Cmd *
get_cmd_by_name(const char *name) {
	for (int i = 0; i < countof(commands); i++) {
		if (!strcmp(name, commands[i].name))
			return &commands[i];
	}
	return NULL;
}

void
handle_cmdfifo() {
	int r;
	char *p, *s, cmdbuf[512];
	Cmd *cmd;
	switch (r = read(cmdfd, cmdbuf, sizeof cmdbuf - 1)) {
		case -1:
		case 0:
			cmdfd = -1;
			break;
		default:
			cmdbuf[r] = '\0';
			/* find the command name */
			for (p = cmdbuf; *p == ' '; p++);
			for (s = p; *p != ' ' && *p != '\n'; p++);
			*p++ = '\0';
			if ((cmd = get_cmd_by_name(s)) != NULL) {
				bool quote = false;
				int argc = 0;
				const char *args[MAX_ARGS], *arg = p;
				if (cmd->action.args[0]) {
					cmd->action.cmd(cmd->action.args);
					break;
				}
				for (; *p; p++) {
					switch (*p) {
					case '\\':
						/* remove the escape character '\\' move every
						 * following character to the left by one position
						 */
						switch (*(++p)) {
							case '\\':
							case '\'':
							case '\"': {
								char *t = p;
								for (;;) {
									*(t - 1) = *t;
									if (*t++ == '\0')
										break;
								}
							}
						}
						break;
					case '\'':
					case '\"':
						quote = !quote;
						break;
					case ' ':
						if (!quote) {
					case '\n':
							/* remove trailing quote */
							if (*(p - 1) == '\'' || *(p - 1) == '\"')
								*(p - 1) = '\0';
							*p++ = '\0';
							/* remove leading quote */
							if (*arg == '\'' || *arg == '\"')
								arg++;

							args[argc++] = arg;
							while (*p == ' ')
								p++;
							arg = p;
						}
						break;
					}
				}
				cmd->action.cmd(args);
			}
	}
}

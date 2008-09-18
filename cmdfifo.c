static int cmdfd = -1;
static unsigned short int client_id = 0;
static const char *cmdpath = NULL;

/* glibc has a non-standard realpath(3) implementation which allocates
 * the destination buffer, other C libraries may have a broken implementation
 * which expect an already allocated destination buffer.
 */

#ifndef __GLIBC__
# include <limits.h>
# ifndef PATH_MAX
#  define PATH_MAX 1024
# endif
#endif

static char *get_realpath(const char *path) {
#ifdef __GLIBC__
	return realpath(path, NULL);
#else
	static char buf[PATH_MAX];
	return realpath(path, buf);
#endif
}

static Cmd *
get_cmd_by_name(const char *name) {
	for (int i = 0; i < countof(commands); i++) {
		if (!strcmp(name, commands[i].name))
			return &commands[i];
	}
	return NULL;
}

static void
handle_cmdfifo() {
	int r;
	char *p, *s, cmdbuf[512], c;
	Cmd *cmd;
	switch (r = read(cmdfd, cmdbuf, sizeof cmdbuf - 1)) {
		case -1:
		case 0:
			cmdfd = -1;
			break;
		default:
			cmdbuf[r] = '\0';
			p = cmdbuf;
			while (*p) {
				/* find the command name */
				for (; *p == ' ' || *p == '\n'; p++);
				for (s = p; *p && *p != ' ' && *p != '\n'; p++);
				if ((c = *p))
					*p++ = '\0';
				if (*s && (cmd = get_cmd_by_name(s)) != NULL) {
					bool quote = false;
					int argc = 0;
					/* XXX: initializer assumes MAX_ARGS == 2 use a initialization loop? */
					const char *args[MAX_ARGS] = { NULL, NULL}, *arg;
					/* if arguments were specified in config.h ignore the one given via
					 * the named pipe and thus skip everything until we find a new line
					 */
					if (cmd->action.args[0] || c == '\n') {
						debug("execute %s", s);
						cmd->action.cmd(cmd->action.args);
						while (*p && *p != '\n')
							p++;
						continue;
					}
					/* no arguments were given in config.h so we parse the command line */
					while (*p == ' ')
						p++;
					arg = p;
					for (; (c = *p); p++) {
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
									p -= 2;
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
								/* remove trailing quote if there is one */
								if (*(p - 1) == '\'' || *(p - 1) == '\"')
									*(p - 1) = '\0';
								*p++ = '\0';
								/* remove leading quote if there is one */
								if (*arg == '\'' || *arg == '\"')
									arg++;
								if (argc < MAX_ARGS)
									args[argc++] = arg;

								while (*p == ' ')
									++p;
								arg = p;
							}
							break;
						}

						if (c == '\n' || *p == '\n') {
							debug("execute %s", s);
							for(int i = 0; i < argc; i++)
								debug(" %s", args[i]);
							debug("\n");
							cmd->action.cmd(args);
							break;
						}
					}
				}
			}
	}
}

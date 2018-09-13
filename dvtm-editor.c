/* Invoke $EDITOR as a filter.
 *
 * Copyright (c) 2016 Dmitry Bogatov <KAction@gnu.org>
 * Copyright (c) 2017 Marc André Tanner <mat@brain-dump.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void error(const char *msg, ...) {
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	if (errno)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");
}

int main(int argc, char *argv[])
{
	int exit_status = EXIT_FAILURE, tmp_write = -1;

	const char *editor = getenv("DVTM_EDITOR");
	if (!editor)
		editor = getenv("VISUAL");
	if (!editor)
		editor = getenv("EDITOR");
	if (!editor)
		editor = "vi";

	char tempname[] = "/tmp/dvtm-editor.XXXXXX";
	if ((tmp_write = mkstemp(tempname)) == -1) {
		error("failed to open temporary file `%s'", tempname);
		goto err;
	}

	/* POSIX does not mandates modes of temporary file. */
	if (fchmod(tmp_write, 0600) == -1) {
		error("failed to change mode of temporary file `%s'", tempname);
		goto err;
	}

	char buffer[2048];
	ssize_t bytes;
	while ((bytes = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
		do {
			ssize_t written = write(tmp_write, buffer, bytes);
			if (written == -1) {
				error("failed to write data to temporary file `%s'",
				      tempname);
				goto err;
			}
			bytes -= written;
		} while (bytes > 0);
	}

	if (fsync(tmp_write) == -1) {
		error("failed to fsync temporary file `%s'", tempname);
		goto err;
	}

	struct stat stat_before;
	if (fstat(tmp_write, &stat_before) == -1) {
		error("failed to stat newly created temporary file `%s'", tempname);
		goto err;
	}

	if (close(tmp_write) == -1) {
		error("failed to close temporary file `%s'", tempname);
		goto err;
	}

	pid_t pid = fork();
	if (pid == -1) {
		error("failed to fork editor process");
		goto err;
	} else if (pid == 0) {
		int tty = open("/dev/tty", O_RDWR);
		if (tty == -1) {
			error("failed to open /dev/tty");
			_exit(1);
		}

		if (dup2(tty, STDIN_FILENO) == -1) {
			error("failed to set tty as stdin");
			_exit(1);
		}

		if (dup2(tty, STDOUT_FILENO) == -1) {
			error("failed to set tty as stdout");
			_exit(1);
		}

		if (dup2(tty, STDERR_FILENO) == -1) {
			error("failed to set tty as stderr");
			_exit(1);
		}

		close(tty);

		const char *editor_argv[argc+2];
		editor_argv[0] = editor;
		for (int i = 1; i < argc; i++)
			editor_argv[i] = argv[i];
		editor_argv[argc] = tempname;
		editor_argv[argc+1] = NULL;

		execvp(editor, (char* const*)editor_argv);
		error("failed to exec editor process `%s'", editor);
		_exit(127);
	}

	int status;
	if (waitpid(pid, &status, 0) == -1) {
		error("waitpid failed");
		goto err;
	}
	if (!WIFEXITED(status)) {
		error("editor invocation failed");
		goto err;
	}
	if ((status = WEXITSTATUS(status)) != 0) {
		error("editor terminated with exit status: %d", status);
		goto err;
	}

	int tmp_read = open(tempname, O_RDONLY);
	if (tmp_read == -1) {
		error("failed to open for reading of edited temporary file `%s'",
		      tempname);
		goto err;
	}

	struct stat stat_after;
	if (fstat(tmp_read, &stat_after) == -1) {
		error("failed to stat edited temporary file `%s'", tempname);
		goto err;
	}

	if (stat_before.st_mtime == stat_after.st_mtime)
		goto ok; /* no modifications */

	while ((bytes = read(tmp_read, buffer, sizeof(buffer))) > 0) {
		do {
			ssize_t written = write(STDOUT_FILENO, buffer, bytes);
			if (written == -1) {
				error("failed to write data to stdout");
				goto err;
			}
			bytes -= written;
		} while (bytes > 0);
	}

ok:
	exit_status = EXIT_SUCCESS;
err:
	if (tmp_write != -1)
		unlink(tempname);
	return exit_status;
}

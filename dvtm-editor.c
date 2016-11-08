/* edit-stream.c --- invoke $EDITOR as filter */
/* Copyright (C) 2016 Dmitry Bogatov <KAction@gnu.org> ISC licensed */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

/* stdio.h would be overkill */
#define write2(str) write(2, str, sizeof(str))

int
main(int argc, char **argv)
{
	int return_value = 1;
	const char *tty = ttyname(2); /* POSIX.1-2001, did you knew? */
	if (!tty) {
		write2("stderr is not tty");
		return 1;
	}
	int in = open(tty, O_RDONLY);
	if (in == -1) {
		write2("failed to open tty for reading");
		return 1;
	}
	char tempname[] = "/tmp/edit-stream.XXXXXX";
	int tempfd = mkstemp(tempname);
	if (tempfd == -1) {
		write2("failed to open temporary file");
		goto err_close_tty;
	}
	/* POSIX does not mandates modes of temporary file. */
	if (fchmod(tempfd, 0600) == -1) {
		write2("failed to change mode of temporary file");
		goto err_remove_tempfile;
	}
	char buffer[2048];
	ssize_t bytes;
	while ((bytes = read(0, buffer, sizeof(buffer))) > 0) {
		/* Here actually must be loop, since write(2) does not guarates
		 * that it will be able to write everything. But I am reckless.
                 */
		if (write(tempfd, buffer, bytes) != bytes) {
			write2("failed to write data to temporary file");
			goto err_remove_tempfile;
		}
	}
	if (fsync(tempfd) == -1) {
		write2("failed to fsync temporary file");
		goto err_remove_tempfile;
	}
	if (close(tempfd) == -1) {
		write2("failed to close temporary file");
		goto err_remove_tempfile;
	}
	if (dup2(in, 0) == -1) {
		write2("failed to set tty as stdin");
		goto err_remove_tempfile;
	}
	int stdout = dup(1);
	if (stdout == -1) {
		write2("failed to create copy of stdout");
		goto err_remove_tempfile;
	}
	/* Descriptor 2 (stderr) still points to tty */
	if (dup2(2, 1) == -1) {
		write2("failed to set tty as stdout");
		goto err_close_stdout;
	}
	const char *editor = getenv("EDITOR");
	if (!editor) {
		write2("EDITOR is not set");
		goto err_close_stdout;
	}
	pid_t pid = fork();
	if (pid == 0) {
		close(stdout);
		close(tempfd);
		close(in);
		execlp(editor, editor, tempname, NULL);
		_exit(129);
	}
	int status;
	if (wait(&status) == -1) {
		write2("wait failed");
		goto err_close_stdout;
	}
	if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
		write2("editor invocation failed");
		goto err_close_stdout;
	}
	int tempfd_r = open(tempname, O_RDONLY);
	if (tempfd_r == -1) {
		write2("failed to open for reading edited temporary file");
		goto err_close_stdout;
	}
	while ((bytes = read(tempfd_r, buffer, sizeof(buffer))) > 0) {
		if (write(stdout, buffer, bytes) != bytes) {
			write2("failed to write data to stdout");
			goto err_close_tempfile_read;
		}
	}

	return_value = 0;

/* Clean up on error is best efford. Descriptors are closed, files
   are unlinked, but nothing is checked. */
err_close_tempfile_read:
	close(tempfd_r);
err_close_stdout:
	close(stdout);
err_remove_tempfile:
	close(tempfd);
	unlink(tempname);
err_close_tty:
	close(in);

	close(0);
	close(1);
	close(2);

	return return_value;
}

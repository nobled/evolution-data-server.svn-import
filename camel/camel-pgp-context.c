/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-pgp-context.h"

#include "camel-stream-fs.h"
#include "camel-stream-mem.h"

#include "camel-operation.h"

#include "camel-charset-map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <iconv.h>
#include <gal/unicode/gunicode.h>
#include <gal/util/e-iconv.h>

#define d(x)

struct _CamelPgpContextPrivate {
	CamelPgpType type;
	char *path;
};

static int                  pgp_sign (CamelCipherContext *ctx, const char *userid, CamelCipherHash hash,
				      CamelStream *istream, CamelStream *ostream, CamelException *ex);
static int                  pgp_clearsign (CamelCipherContext *context, const char *userid,
					   CamelCipherHash hash, CamelStream *istream,
					   CamelStream *ostream, CamelException *ex);
static CamelCipherValidity *pgp_verify (CamelCipherContext *context, CamelCipherHash hash,
					CamelStream *istream, CamelStream *sigstream,
					CamelException *ex);
static int                  pgp_encrypt (CamelCipherContext *context, gboolean sign, const char *userid,
					 GPtrArray *recipients, CamelStream *istream, CamelStream *ostream,
					 CamelException *ex);
static int                  pgp_decrypt (CamelCipherContext *context, CamelStream *istream,
					 CamelStream *ostream, CamelException *ex);

static CamelCipherContextClass *parent_class;

static void
camel_pgp_context_init (CamelPgpContext *context)
{
	context->priv = g_new0 (struct _CamelPgpContextPrivate, 1);
}

static void
camel_pgp_context_finalise (CamelObject *o)
{
	CamelPgpContext *context = (CamelPgpContext *)o;
	
	g_free (context->priv->path);
	
	g_free (context->priv);
}

static void
camel_pgp_context_class_init (CamelPgpContextClass *camel_pgp_context_class)
{
	CamelCipherContextClass *camel_cipher_context_class =
		CAMEL_CIPHER_CONTEXT_CLASS (camel_pgp_context_class);
	
	parent_class = CAMEL_CIPHER_CONTEXT_CLASS (camel_type_get_global_classfuncs (camel_cipher_context_get_type ()));
	
	camel_cipher_context_class->sign = pgp_sign;
	camel_cipher_context_class->clearsign = pgp_clearsign;
	camel_cipher_context_class->verify = pgp_verify;
	camel_cipher_context_class->encrypt = pgp_encrypt;
	camel_cipher_context_class->decrypt = pgp_decrypt;
}

CamelType
camel_pgp_context_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_cipher_context_get_type (),
					    "CamelPgpContext",
					    sizeof (CamelPgpContext),
					    sizeof (CamelPgpContextClass),
					    (CamelObjectClassInitFunc) camel_pgp_context_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_pgp_context_init,
					    (CamelObjectFinalizeFunc) camel_pgp_context_finalise);
	}
	
	return type;
}


/**
 * camel_pgp_context_new:
 * @session: CamelSession
 * @type: One of CAMEL_PGP_TYPE_PGP2, PGP5, GPG, or PGP6
 * @path: path to PGP binary
 * @remember: Remember the pgp passphrase
 *
 * This creates a new CamelPgpContext object which is used to sign,
 * verify, encrypt and decrypt streams.
 *
 * Return value: the new CamelPgpContext
 **/
CamelPgpContext *
camel_pgp_context_new (CamelSession *session, CamelPgpType type, const char *path)
{
	CamelPgpContext *context;
	
	g_return_val_if_fail (session != NULL, NULL);
	
	if (type == CAMEL_PGP_TYPE_NONE || !path || !*path)
		return NULL;
	
	context = CAMEL_PGP_CONTEXT (camel_object_new (CAMEL_PGP_CONTEXT_TYPE));
	
	camel_cipher_context_construct (CAMEL_CIPHER_CONTEXT (context), session);
	
	context->priv->type = type;
	context->priv->path = g_strdup (path);
	
	return context;
}



static const gchar *
pgp_get_type_as_string (CamelPgpType type)
{
	switch (type) {
	case CAMEL_PGP_TYPE_PGP2:
		return "PGP 2.6.x";
	case CAMEL_PGP_TYPE_PGP5:
		return "PGP 5.0";
	case CAMEL_PGP_TYPE_PGP6:
		return "PGP 6.5.8";
	case CAMEL_PGP_TYPE_GPG:
		return "GnuPG";
	default:
		g_assert_not_reached ();
		return NULL;
	}
}

static gchar *
pgp_get_passphrase (CamelSession *session, CamelPgpType pgp_type, char *userid)
{
	gchar *passphrase, *prompt;
	const char *type;
	
	type = pgp_get_type_as_string (pgp_type);
	
	if (userid)
		prompt = g_strdup_printf (_("Please enter your %s passphrase for %s"),
					  type, userid);
	else
		prompt = g_strdup_printf (_("Please enter your %s passphrase"),
					  type);
	
	/* Use the userid as a key if possible, else be generic and use the type */
	passphrase = camel_session_get_password (session, prompt, TRUE,
						 NULL, userid ? userid : type,
						 NULL);
	g_free (prompt);
	
	return passphrase;
}

static void
pgp_forget_passphrase (CamelSession *session, CamelPgpType pgp_type, char *userid)
{
	const char *type = NULL;
	
	if (!userid)
		type = pgp_get_type_as_string (pgp_type);
	
	camel_session_forget_password (session, NULL, userid ? userid : type, NULL);
}

static void
pass_free (char *passphrase)
{
	if (passphrase) {
		memset (passphrase, 0, strlen (passphrase));
		g_free (passphrase);
	}
}

static int
cleanup_child (pid_t child)
{
	int status;
	pid_t wait_result;
	sigset_t mask, omask;
	
	/* PGP5 closes fds before exiting, meaning this might be called
	 * too early. So wait a bit for the result.
	 */
	sigemptyset (&mask);
	sigaddset (&mask, SIGALRM);
	sigprocmask (SIG_BLOCK, &mask, &omask);
	alarm (1);
	wait_result = waitpid (child, &status, 0);
	alarm (0);
	sigprocmask (SIG_SETMASK, &omask, NULL);
	
	if (wait_result == -1 && errno == EINTR) {
		/* The child is hanging: send a friendly reminder. */
		kill (child, SIGTERM);
		sleep (1);
		wait_result = waitpid (child, &status, WNOHANG);
		if (wait_result == 0) {
			/* Still hanging; use brute force. */
			kill (child, SIGKILL);
			sleep (1);
			wait_result = waitpid (child, &status, WNOHANG);
		}
	}
	
	if (wait_result != -1 && WIFEXITED (status))
		return WEXITSTATUS (status);
	else
		return -1;
}

static void
cleanup_before_exec (int fd)
{
	int maxfd, i;
	
	maxfd = sysconf (_SC_OPEN_MAX);
	if (maxfd < 0)
		return;
	
	/* Loop over all fds. */
	for (i = 0; i < maxfd; i++) {
		if ((STDIN_FILENO != i) &&
		    (STDOUT_FILENO != i) &&
		    (STDERR_FILENO != i) &&
		    (fd != i))
			close (i);
	}
}

static int
crypto_exec_with_passwd (const char *path, char *argv[], const char *input, int inlen,
			 int passwd_fds[], const char *passphrase,
			 char **output, int *outlen, char **diagnostics)
{
	gboolean eof_seen, diag_eof_seen, passwd_eof_seen, input_eof_seen;
	size_t passwd_remaining, passwd_incr, input_remaining, input_incr;
	size_t size, alloc_size, diag_size, diag_alloc_size;
	int select_result, read_len, write_len, cancel_fd;
	int fds[6], *ip_fds, *op_fds, *diag_fds;
	const char *passwd_next, *input_next;
	char *buf = NULL, *diag_buf = NULL;
	fd_set fdset, write_fdset;
	struct timeval timeout;
	size_t tmp_len;
	pid_t child;
	int i;
	
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
	
	for (i = 0; i < 6; i++)
		fds[i] = -1;
	
	ip_fds = fds;
	op_fds = fds + 2;
	diag_fds = fds + 4;
	
	if ((pipe (ip_fds) == -1) || (pipe (op_fds) == -1) || (pipe (diag_fds) == -1)) {
		*diagnostics = g_strdup_printf ("Couldn't create pipe to %s: %s",
						path, g_strerror (errno));
		
		for (i = 0; i < 6; i++)
			close (fds[i]);
		
		close (passwd_fds[0]);
		close (passwd_fds[1]);
		
		return -1;
	}
	
	if (!(child = fork ())) {
		/* In child */
		
		if ((dup2 (ip_fds[0], STDIN_FILENO) < 0 ) ||
		    (dup2 (op_fds[1], STDOUT_FILENO) < 0 ) ||
		    (dup2 (diag_fds[1], STDERR_FILENO) < 0 )) {
			_exit (255);
		}
		
		/* Dissociate from evolution-mail's controlling
		 * terminal so that pgp/gpg won't be able to read from
		 * it: PGP 2 will fall back to asking for the password
		 * on /dev/tty if the passed-in password is incorrect.
		 * This will make that fail rather than hanging.
		 */
		setsid ();
		
		/* Close excess fds */
		cleanup_before_exec (passwd_fds[0]);
		
		execvp (path, argv);
		fprintf (stderr, "Could not execute %s: %s\n", argv[0],
			 g_strerror (errno));
		_exit (255);
	} else if (child < 0) {
		*diagnostics = g_strdup_printf ("Cannot fork %s: %s",
						argv[0], g_strerror (errno));
		return -1;
	}
	
	/* Parent */
	close (ip_fds[0]);
	close (op_fds[1]);
	close (diag_fds[1]);
	close (passwd_fds[0]);
	
	fcntl (ip_fds[1], F_SETFL, O_NONBLOCK);
	fcntl (op_fds[0], F_SETFL, O_NONBLOCK);
	fcntl (diag_fds[0], F_SETFL, O_NONBLOCK);
	
	timeout.tv_sec = 10; /* timeout in seconds */
	timeout.tv_usec = 0;
	
	size = diag_size = 0;
	alloc_size = 4096;
	diag_alloc_size = 1024;
	eof_seen = diag_eof_seen = FALSE;
	
	buf = g_malloc (alloc_size);
	diag_buf = g_malloc (diag_alloc_size);
	
	passwd_next = passphrase;
	passwd_remaining = passphrase ? strlen (passphrase) : 0;
	passwd_incr = fpathconf (passwd_fds[1], _PC_PIPE_BUF);
	/* Use a reasonable default value on error. */
	if (passwd_incr <= 0)
		passwd_incr = 1024;
	passwd_eof_seen = FALSE;
	
	input_next = input;
	input_remaining = inlen;
	input_incr = fpathconf (ip_fds[1], _PC_PIPE_BUF);
	if (input_incr <= 0)
		input_incr = 1024;
	input_eof_seen = FALSE;
	
	cancel_fd = camel_operation_cancel_fd (NULL);
	
	while (!(eof_seen && diag_eof_seen)) {
		int max = 0;
		
		FD_ZERO (&fdset);
		if (!eof_seen) {
			FD_SET (op_fds[0], &fdset);
			max = op_fds[0];
		}
		if (!diag_eof_seen) {
			FD_SET (diag_fds[0], &fdset);
			max = MAX (max, diag_fds[0]);
		}
		if (cancel_fd != -1) {
			FD_SET (cancel_fd, &fdset);
			max = MAX (max, cancel_fd);
		}
		
		FD_ZERO (&write_fdset);
		if (!passwd_eof_seen) {
			FD_SET (passwd_fds[1], &write_fdset);
			max = MAX (max, passwd_fds[1]);
		}
		if (!input_eof_seen) {
			FD_SET (ip_fds[1], &write_fdset);
			max = MAX (max, ip_fds[1]);
		}
		
		select_result = select (max + 1, &fdset, &write_fdset,
					NULL, &timeout);
		
		if (cancel_fd != -1 && FD_ISSET (cancel_fd, &fdset)) {
			/* user-cancelled */
			break;
		}
		
		if (select_result < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (select_result == 0) {
			/* timeout */
			break;
		}
		
		if (FD_ISSET (op_fds[0], &fdset)) {
			/* More output is available. */
			
			if (size + 4096 > alloc_size) {
				alloc_size += 4096;
				buf = g_realloc (buf, alloc_size);
			}
			read_len = read (op_fds[0], &buf[size],
					 alloc_size - size - 1);
			if (read_len < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			if (read_len == 0)
				eof_seen = TRUE;
			size += read_len;
		}
		
		if (FD_ISSET (diag_fds[0], &fdset) ) {
			/* More stderr is available. */
			
			if (diag_size + 1024 > diag_alloc_size) {
				diag_alloc_size += 1024;
				diag_buf = g_realloc (diag_buf,
						      diag_alloc_size);
			}
			
			read_len = read (diag_fds[0], &diag_buf[diag_size],
					 diag_alloc_size - diag_size - 1);
			if (read_len < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			if (read_len == 0)
				diag_eof_seen = TRUE;
			diag_size += read_len;
		}
		
		if (FD_ISSET (passwd_fds[1], &write_fdset)) {
			/* Ready for more password input. */
			
			tmp_len = passwd_incr;
			if (tmp_len > passwd_remaining)
				tmp_len = passwd_remaining;
			write_len = write (passwd_fds[1], passwd_next,
					   tmp_len);
			if (write_len < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			passwd_next += write_len;
			passwd_remaining -= write_len;
			if (passwd_remaining == 0) {
				close (passwd_fds[1]);
				passwd_eof_seen = TRUE;
			}
		}
		
		if (FD_ISSET (ip_fds[1], &write_fdset)) {
			/* Ready for more ciphertext input. */
			
			tmp_len = input_incr;
			if (tmp_len > input_remaining)
				tmp_len = input_remaining;
			write_len = write (ip_fds[1], input_next, tmp_len);
			if (write_len < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			input_next += write_len;
			input_remaining -= write_len;
			if (input_remaining == 0 ) {
				close (ip_fds[1]);
				input_eof_seen = TRUE;
			}
		}
	}
	
	buf[size] = 0;
	diag_buf[diag_size] = 0;
	close (op_fds[0]);
	close (diag_fds[0]);
	
	if (!passwd_eof_seen)
		close (passwd_fds[1]);
	
	*output = buf;
	if (outlen)
		*outlen = size;
	*diagnostics = diag_buf;
	
	return cleanup_child (child);
}


/*----------------------------------------------------------------------*
 *                     Public crypto functions
 *----------------------------------------------------------------------*/

static int
pgp_sign (CamelCipherContext *ctx, const char *userid, CamelCipherHash hash,
	  CamelStream *istream, CamelStream *ostream, CamelException *ex)
{
	CamelPgpContext *context = CAMEL_PGP_CONTEXT (ctx);
	GByteArray *plaintext;
	CamelStream *stream;
	char *argv[20];
	char *ciphertext = NULL;
	char *diagnostics = NULL;
	char *passphrase = NULL;
	char *hash_str = NULL;
	int passwd_fds[2];
	char passwd_fd[32];
	int retval, i;
	
	/* check for the now unsupported pgp 2.6.x type */
	if (context->priv->type == CAMEL_PGP_TYPE_PGP2) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     "PGP 2.6.x is no longer supported.");
		return -1;
	}
	
	/* get the plaintext in a form we can use */
	plaintext = g_byte_array_new ();
	stream = camel_stream_mem_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream), plaintext);
	camel_stream_write_to_stream (istream, stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	
	if (!plaintext->len) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot sign this message: no plaintext to sign"));
		goto exception;
	}
	
	passphrase = pgp_get_passphrase (ctx->session, context->priv->type, (char *) userid);
	if (!passphrase) {
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     _("Cannot sign this message: no password provided"));
		goto exception;
	}
	
	if (pipe (passwd_fds) < 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot sign this message: couldn't create pipe to GPG/PGP: %s"),
				      g_strerror (errno));
		goto exception;
	}
	
	switch (hash) {
	case CAMEL_CIPHER_HASH_DEFAULT:
		hash_str = NULL;
		break;
	case CAMEL_CIPHER_HASH_MD5:
		hash_str = "MD5";
		break;
	case CAMEL_CIPHER_HASH_SHA1:
		hash_str = "SHA1";
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	i = 0;
	switch (context->priv->type) {
	case CAMEL_PGP_TYPE_GPG:
		argv[i++] = "gpg";
		
		argv[i++] = "--sign";
		argv[i++] = "-b";
		if (hash_str) {
			argv[i++] = "--digest-algo";
			argv[i++] = hash_str;
		}
		
		if (userid) {
			argv[i++] = "-u";
			argv[i++] = (char *) userid;
		}
		
		argv[i++] = "--verbose";
		argv[i++] = "--no-secmem-warning";
		argv[i++] = "--no-greeting";
		argv[i++] = "--yes";
		argv[i++] = "--batch";
		
		argv[i++] = "--armor";
		
		argv[i++] = "--output";
		argv[i++] = "-";            /* output to stdout */
		
		argv[i++] = "--passphrase-fd";
		sprintf (passwd_fd, "%d", passwd_fds[0]);
		argv[i++] = passwd_fd;
		break;
	case CAMEL_PGP_TYPE_PGP5:
		/* FIXME: respect hash */
		argv[i++] = "pgps";
		
		if (userid) {
			argv[i++] = "-u";
			argv[i++] = (char *) userid;
		}
		
		argv[i++] = "-b";
		argv[i++] = "-f";
		argv[i++] = "-z";
		argv[i++] = "-a";
		argv[i++] = "-o";
		argv[i++] = "-";        /* output to stdout */
		
		sprintf (passwd_fd, "PGPPASSFD=%d", passwd_fds[0]);
		putenv (passwd_fd);
		break;
	case CAMEL_PGP_TYPE_PGP2:
	case CAMEL_PGP_TYPE_PGP6:
		/* FIXME: respect hash */
		argv[i++] = "pgp";
		
		if (userid) {
			argv[i++] = "-u";
			argv[i++] = (char *) userid;
		}
		
		argv[i++] = "-f";
		argv[i++] = "-a";
		argv[i++] = "-o";
		argv[i++] = "-";
		
		argv[i++] = "-sb"; /* create a detached signature */
		sprintf (passwd_fd, "PGPPASSFD=%d", passwd_fds[0]);
		putenv (passwd_fd);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	argv[i++] = NULL;
	
	retval = crypto_exec_with_passwd (context->priv->path, argv,
					  plaintext->data, plaintext->len,
					  passwd_fds, passphrase,
					  &ciphertext, NULL,
					  &diagnostics);
	
	g_byte_array_free (plaintext, TRUE);
	pass_free (passphrase);
	
	if (retval != 0 || !*ciphertext) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, diagnostics);
		g_free (diagnostics);
		g_free (ciphertext);
		pgp_forget_passphrase (ctx->session, context->priv->type, (char *) userid);
		
		return -1;
	}
	
	g_free (diagnostics);
	
	camel_stream_write (ostream, ciphertext, strlen (ciphertext));
	g_free (ciphertext);
	
	return 0;
	
 exception:
	
	g_byte_array_free (plaintext, TRUE);
	
	if (passphrase) {
		pgp_forget_passphrase (ctx->session, context->priv->type, (char *) userid);
		pass_free (passphrase);
	}
	
	return -1;
}


static int
pgp_clearsign (CamelCipherContext *ctx, const char *userid, CamelCipherHash hash,
	       CamelStream *istream, CamelStream *ostream, CamelException *ex)
{
	CamelPgpContext *context = CAMEL_PGP_CONTEXT (ctx);
	GByteArray *plaintext;
	CamelStream *stream;
	char *argv[15];
	char *ciphertext = NULL;
	char *diagnostics = NULL;
	char *passphrase = NULL;
	char *hash_str = NULL;
	int passwd_fds[2];
	char passwd_fd[32];
	int retval, i;
	
	/* check for the now unsupported pgp 2.6.x type */
	if (context->priv->type == CAMEL_PGP_TYPE_PGP2) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     "PGP 2.6.x is no longer supported.");
		return -1;
	}
	
	/* get the plaintext in a form we can use */
	plaintext = g_byte_array_new ();
	stream = camel_stream_mem_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream), plaintext);
	camel_stream_write_to_stream (istream, stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	
	if (!plaintext->len) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot sign this message: no plaintext to clearsign"));
		goto exception;
	}
	
	passphrase = pgp_get_passphrase (ctx->session, context->priv->type, (char *) userid);
	if (!passphrase) {
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     _("Cannot sign this message: no password provided"));
		goto exception;
	}
	
	if (pipe (passwd_fds) < 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot sign this message: couldn't create pipe to GPG/PGP: %s"),
				      g_strerror (errno));
		goto exception;
	}
	
	switch (hash) {
	case CAMEL_CIPHER_HASH_DEFAULT:
		hash_str = NULL;
		break;
	case CAMEL_CIPHER_HASH_MD5:
		hash_str = "MD5";
		break;
	case CAMEL_CIPHER_HASH_SHA1:
		hash_str = "SHA1";
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	i = 0;
	switch (context->priv->type) {
	case CAMEL_PGP_TYPE_GPG:
		argv[i++] = "gpg";
		
		argv[i++] = "--clearsign";
		
		if (hash_str) {
			argv[i++] = "--digest-algo";
			argv[i++] = hash_str;
		}
		
		if (userid) {
			argv[i++] = "-u";
			argv[i++] = (char *) userid;
		}
		
		argv[i++] = "--verbose";
		argv[i++] = "--no-secmem-warning";
		argv[i++] = "--no-greeting";
		argv[i++] = "--yes";
		argv[i++] = "--batch";
		
		argv[i++] = "--armor";
		
		argv[i++] = "--output";
		argv[i++] = "-";            /* output to stdout */
		
		argv[i++] = "--passphrase-fd";
		sprintf (passwd_fd, "%d", passwd_fds[0]);
		argv[i++] = passwd_fd;
		break;
	case CAMEL_PGP_TYPE_PGP5:
		/* FIXME: modify to respect hash */
		argv[i++] = "pgps";
		
		if (userid) {
			argv[i++] = "-u";
			argv[i++] = (char *) userid;
		}
		
		argv[i++] = "-f";
		argv[i++] = "-z";
		argv[i++] = "-a";
		argv[i++] = "-o";
		argv[i++] = "-";        /* output to stdout */
		
		sprintf (passwd_fd, "PGPPASSFD=%d", passwd_fds[0]);
		putenv (passwd_fd);
		break;
	case CAMEL_PGP_TYPE_PGP2:
	case CAMEL_PGP_TYPE_PGP6:
		/* FIXME: modify to respect hash */
		argv[i++] = "pgp";
		
		if (userid) {
			argv[i++] = "-u";
			argv[i++] = (char *) userid;
		}
		
		argv[i++] = "-f";
		argv[i++] = "-a";
		argv[i++] = "-o";
		argv[i++] = "-";
		
		argv[i++] = "-st";
		sprintf (passwd_fd, "PGPPASSFD=%d", passwd_fds[0]);
		putenv (passwd_fd);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	argv[i++] = NULL;
	
	retval = crypto_exec_with_passwd (context->priv->path, argv,
					  plaintext->data, plaintext->len,
					  passwd_fds, passphrase,
					  &ciphertext, NULL,
					  &diagnostics);
	
	g_byte_array_free (plaintext, TRUE);
	pass_free (passphrase);
	
	if (retval != 0 || !*ciphertext) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, diagnostics);
		g_free (diagnostics);
		g_free (ciphertext);
		pgp_forget_passphrase (ctx->session, context->priv->type, (char *) userid);
	}
	
	g_free (diagnostics);
	
	camel_stream_write (ostream, ciphertext, strlen (ciphertext));
	g_free (ciphertext);
	
	return 0;
	
 exception:
	
	g_byte_array_free (plaintext, TRUE);
	
	if (passphrase) {
		pgp_forget_passphrase (ctx->session, context->priv->type, (char *) userid);
		pass_free (passphrase);
	}
	
	return -1;
}


static char *
swrite (CamelStream *istream)
{
	CamelStream *ostream;
	char *template;
	int fd;
	
	template = g_strdup ("/tmp/evolution-pgp.XXXXXX");
	fd = mkstemp (template);
	if (fd == -1) {
		g_free (template);
		return NULL;
	}
	
	ostream = camel_stream_fs_new_with_fd (fd);
	camel_stream_write_to_stream (istream, ostream);
	camel_object_unref (CAMEL_OBJECT (ostream));
	
	return template;
}


static CamelCipherValidity *
pgp_verify (CamelCipherContext *ctx, CamelCipherHash hash, CamelStream *istream,
	    CamelStream *sigstream, CamelException *ex)
{
	CamelPgpContext *context = CAMEL_PGP_CONTEXT (ctx);
	CamelCipherValidity *valid = NULL;
	GByteArray *plaintext;
	CamelStream *stream;
	char *argv[20];
	char *cleartext = NULL;
	char *diagnostics = NULL;
	int passwd_fds[2];
	char *sigfile = NULL;
	int retval, i, clearlen;
	
	/* check for the now unsupported pgp 2.6.x type */
	if (context->priv->type == CAMEL_PGP_TYPE_PGP2) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     "PGP 2.6.x is no longer supported.");
		return NULL;
	}
	
	/* get the plaintext in a form we can use */
	plaintext = g_byte_array_new ();
	stream = camel_stream_mem_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream), plaintext);
	camel_stream_write_to_stream (istream, stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	
	if (!plaintext->len) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot verify this message: no plaintext to verify"));
		goto exception;
	}
	
	if (pipe (passwd_fds) < 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot verify this message: couldn't create pipe to GPG/PGP: %s"),
				      g_strerror (errno));
		goto exception;
	}
	
	if (sigstream != NULL) {
		/* We are going to verify a detached signature so save
		   the signature to a temp file. */
		sigfile = swrite (sigstream);
		if (!sigfile) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Cannot verify this message: couldn't create temp file: %s"),
					      g_strerror (errno));
			goto exception;
		}
	}
	
	i = 0;
	switch (context->priv->type) {
	case CAMEL_PGP_TYPE_GPG:
		argv[i++] = "gpg";
		
		argv[i++] = "--verbose";
		argv[i++] = "--no-secmem-warning";
		argv[i++] = "--no-greeting";
		argv[i++] = "--no-tty";
		if (!camel_session_is_online (ctx->session))
			argv[i++] = "--no-auto-key-retrieve";
		
		argv[i++] = "--yes";
		argv[i++] = "--batch";
		
		argv[i++] = "--verify";
		
		if (sigstream != NULL)
			argv[i++] = sigfile;
		
		argv[i++] = "-";
		break;
	case CAMEL_PGP_TYPE_PGP5:
		argv[i++] = "pgpv";
		
		argv[i++] = "-z";
		
		if (sigstream != NULL)
			argv[i++] = sigfile;
		
		argv[i++] = "-f";
		
		break;
	case CAMEL_PGP_TYPE_PGP2:
	case CAMEL_PGP_TYPE_PGP6:
		argv[i++] = "pgp";
		
		if (sigstream != NULL)
			argv[i++] = sigfile;
		
		argv[i++] = "-f";
		
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	argv[i++] = NULL;
	
	clearlen = 0;
	retval = crypto_exec_with_passwd (context->priv->path, argv,
					  plaintext->data, plaintext->len,
					  passwd_fds, NULL,
					  &cleartext, &clearlen,
					  &diagnostics);
	
	g_byte_array_free (plaintext, TRUE);
	
	/* cleanup */
	if (sigfile) {
		unlink (sigfile);
		g_free (sigfile);
	}
	
	valid = camel_cipher_validity_new ();
	
	if (retval != 0) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, diagnostics);
		
		camel_cipher_validity_set_valid (valid, FALSE);
	} else {
		camel_cipher_validity_set_valid (valid, TRUE);
	}
	
	if (diagnostics) {
		const char *locale;
		char *desc, *outbuf;
		size_t inlen, outlen;
		iconv_t cd;
		
		inlen = strlen (diagnostics);
		outlen = inlen * 4;
		
		desc = outbuf = g_new (unsigned char, outlen + 1);
		
		locale = e_iconv_locale_charset ();
		if (!locale)
			locale = "iso-8859-1";
		
		cd = e_iconv_open ("UTF-8", locale);
		if (cd != (iconv_t) -1) {
			const char *inbuf;
			size_t ret;
			
			inbuf = diagnostics;
			ret = e_iconv (cd, &inbuf, &inlen, &outbuf, &outlen);
			if (ret != (size_t) -1) {
				e_iconv (cd, NULL, 0, &outbuf, &outlen);
			}
			e_iconv_close (cd);
			
			*outbuf = '\0';
		} else {
			const char *inptr, *inend;
			
			g_warning ("CamelPgpContext::pgp_verify: cannot convert from %s to UTF-8", locale);
			
			inptr = diagnostics;
			inend = inptr + inlen;
			
			while (inptr && inptr < inend && g_unichar_validate (g_utf8_get_char (inptr))) {
				*outbuf++ = g_utf8_get_char (inptr) & 0xff;
				inptr = g_utf8_next_char (inptr);
			}
			
			*outbuf = '\0';
		}
		
		camel_cipher_validity_set_description (valid, desc);
		g_free (desc);
	}
	
	g_free (diagnostics);
	g_free (cleartext);
	
	return valid;
	
 exception:
	
	g_byte_array_free (plaintext, TRUE);
	
	return NULL;
}


static int
pgp_encrypt (CamelCipherContext *ctx, gboolean sign, const char *userid, GPtrArray *recipients,
	     CamelStream *istream, CamelStream *ostream, CamelException *ex)
{
	CamelPgpContext *context = CAMEL_PGP_CONTEXT (ctx);
	GByteArray *plaintext;
	CamelStream *stream;
	GPtrArray *argv;
	int retval, r;
	char *ciphertext = NULL;
	char *diagnostics = NULL;
	int passwd_fds[2];
	char passwd_fd[32];
	char *passphrase = NULL;
	
	/* check for the now unsupported pgp 2.6.x type */
	if (context->priv->type == CAMEL_PGP_TYPE_PGP2) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     "PGP 2.6.x is no longer supported.");
		return -1;
	}
	
	/* get the plaintext in a form we can use */
	plaintext = g_byte_array_new ();
	stream = camel_stream_mem_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream), plaintext);
	camel_stream_write_to_stream (istream, stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	
	if (!plaintext->len) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot encrypt this message: no plaintext to encrypt"));
		goto exception;
	}
	
	if (sign) {
		/* we only need a passphrase if we intend on signing */
		passphrase = pgp_get_passphrase (ctx->session, context->priv->type,
						 (char *) userid);
		if (!passphrase) {
			camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
					     _("Cannot encrypt this message: no password provided"));
			goto exception;
		}
	}
	
	if (pipe (passwd_fds) < 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot encrypt this message: couldn't create pipe to GPG/PGP: %s"),
				      g_strerror (errno));
		
		goto exception;
	}
	
	/* check to make sure we have recipients */
	if (recipients->len == 0) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot encrypt this message: no recipients specified"));
		
		goto exception;
	}
	
	argv = g_ptr_array_new ();
	
	switch (context->priv->type) {
	case CAMEL_PGP_TYPE_GPG:
		g_ptr_array_add (argv, "gpg");
		
		g_ptr_array_add (argv, "--verbose");
		g_ptr_array_add (argv, "--no-secmem-warning");
		g_ptr_array_add (argv, "--no-greeting");
		g_ptr_array_add (argv, "--yes");
		g_ptr_array_add (argv, "--batch");
		
		g_ptr_array_add (argv, "--armor");
		
		for (r = 0; r < recipients->len; r++) {
			g_ptr_array_add (argv, "-r");
			g_ptr_array_add (argv, recipients->pdata[r]);
		}
		
		g_ptr_array_add (argv, "--output");
		g_ptr_array_add (argv, "-");            /* output to stdout */
		
		g_ptr_array_add (argv, "--encrypt");
		
		if (sign) {
			g_ptr_array_add (argv, "--sign");
			
			g_ptr_array_add (argv, "-u");
			g_ptr_array_add (argv, (char *) userid);
			
			g_ptr_array_add (argv, "--passphrase-fd");
			sprintf (passwd_fd, "%d", passwd_fds[0]);
			g_ptr_array_add (argv, passwd_fd);
		}
		break;
	case CAMEL_PGP_TYPE_PGP5:
		g_ptr_array_add (argv, "pgpe");
		
		for (r = 0; r < recipients->len; r++) {
			g_ptr_array_add (argv, "-r");
			g_ptr_array_add (argv, recipients->pdata[r]);
		}
		
		g_ptr_array_add (argv, "-f");
		g_ptr_array_add (argv, "-z");
		g_ptr_array_add (argv, "-a");
		g_ptr_array_add (argv, "-o");
		g_ptr_array_add (argv, "-");        /* output to stdout */
		
		if (sign) {
			g_ptr_array_add (argv, "-s");
			
			g_ptr_array_add (argv, "-u");
			g_ptr_array_add (argv, (gchar *) userid);
			
			sprintf (passwd_fd, "PGPPASSFD=%d", passwd_fds[0]);
			putenv (passwd_fd);
		}
		break;
	case CAMEL_PGP_TYPE_PGP2:
	case CAMEL_PGP_TYPE_PGP6:
		g_ptr_array_add (argv, "pgp");
		g_ptr_array_add (argv, "-f");
		g_ptr_array_add (argv, "-e");
		g_ptr_array_add (argv, "-a");
		g_ptr_array_add (argv, "-o");
		g_ptr_array_add (argv, "-");
		
		for (r = 0; r < recipients->len; r++)
			g_ptr_array_add (argv, recipients->pdata[r]);
		
		if (sign) {
			g_ptr_array_add (argv, "-s");
			
			g_ptr_array_add (argv, "-u");
			g_ptr_array_add (argv, (gchar *) userid);
			
			sprintf (passwd_fd, "PGPPASSFD=%d", passwd_fds[0]);
			putenv (passwd_fd);
		}
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	g_ptr_array_add (argv, NULL);
	
	retval = crypto_exec_with_passwd (context->priv->path,
					  (char **) argv->pdata,
					  plaintext->data, plaintext->len,
					  passwd_fds, passphrase,
					  &ciphertext, NULL,
					  &diagnostics);
	
	g_byte_array_free (plaintext, TRUE);
	
	pass_free (passphrase);
	g_ptr_array_free (argv, TRUE);
	
	if (retval != 0 || !*ciphertext) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, diagnostics);
		g_free (diagnostics);
		g_free (ciphertext);
		if (sign)
			pgp_forget_passphrase (ctx->session, context->priv->type,
					       (char *) userid);
		
		return -1;
	}
	
	g_free (diagnostics);
	
	camel_stream_write (ostream, ciphertext, strlen (ciphertext));
	g_free (ciphertext);
	
	return 0;
	
 exception:
	
	g_byte_array_free (plaintext, TRUE);
	
	if (sign) {
		pass_free (passphrase);
		pgp_forget_passphrase (ctx->session, context->priv->type, (char *) userid);
	}
	
	return -1;
}


static int
pgp_decrypt (CamelCipherContext *ctx, CamelStream *istream,
	     CamelStream *ostream, CamelException *ex)
{
	CamelPgpContext *context = CAMEL_PGP_CONTEXT (ctx);
	GByteArray *ciphertext;
	CamelStream *stream;
	char *argv[15];
	char *plaintext = NULL;
	int plainlen;
	char *diagnostics = NULL;
	char *passphrase = NULL;
	int passwd_fds[2];
	char passwd_fd[32];
	int retval, i;
	
	/* check for the now unsupported pgp 2.6.x type */
	if (context->priv->type == CAMEL_PGP_TYPE_PGP2) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     "PGP 2.6.x is no longer supported.");
		return -1;
	}
	
	/* get the ciphertext in a form we can use */
	ciphertext = g_byte_array_new ();
	stream = camel_stream_mem_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream), ciphertext);
	camel_stream_write_to_stream (istream, stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	
	if (!ciphertext->len) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot decrypt this message: no ciphertext to decrypt"));
		
		goto exception;
	}
	
	passphrase = pgp_get_passphrase (ctx->session, context->priv->type, NULL);
	if (!passphrase) {
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     _("Cannot decrypt this message: no password provided"));
		
		goto exception;
	}
	
	if (pipe (passwd_fds) < 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot decrypt this message: couldn't create pipe to GPG/PGP: %s"),
				      g_strerror (errno));
		
		goto exception;
	}
	
	i = 0;
	switch (context->priv->type) {
	case CAMEL_PGP_TYPE_GPG:
		argv[i++] = "gpg";
		
		argv[i++] = "--verbose";
		argv[i++] = "--no-secmem-warning";
		argv[i++] = "--no-greeting";
		argv[i++] = "--yes";
		argv[i++] = "--batch";
		
		if (!camel_session_is_online (ctx->session))
			argv[i++] = "--no-auto-key-retrieve";
		
		argv[i++] = "--output";
		argv[i++] = "-";            /* output to stdout */
		
		argv[i++] = "--decrypt";
		
		argv[i++] = "--passphrase-fd";
		sprintf (passwd_fd, "%d", passwd_fds[0]);
		argv[i++] = passwd_fd;
		break;
	case CAMEL_PGP_TYPE_PGP5:
		argv[i++] = "pgpv";
		argv[i++] = "-f";
		argv[i++] = "+batchmode=1";
		
		sprintf (passwd_fd, "PGPPASSFD=%d", passwd_fds[0]);
		putenv (passwd_fd);
		break;
	case CAMEL_PGP_TYPE_PGP2:
	case CAMEL_PGP_TYPE_PGP6:
		argv[i++] = "pgp";
		argv[i++] = "-f";
		
		sprintf (passwd_fd, "PGPPASSFD=%d", passwd_fds[0]);
		putenv (passwd_fd);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	argv[i++] = NULL;
	
	retval = crypto_exec_with_passwd (context->priv->path, argv,
					  ciphertext->data, ciphertext->len,
					  passwd_fds, passphrase,
					  &plaintext, &plainlen,
					  &diagnostics);
	
	g_byte_array_free (ciphertext, TRUE);
	pass_free (passphrase);
	
	/* gpg returns '1' if it succeedes in decrypting but can't verify the signature */
	if (retval != 0 || (context->priv->type == CAMEL_PGP_TYPE_GPG && retval == 1) || !*plaintext) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, diagnostics);
		g_free (plaintext);
		g_free (diagnostics);
		
		pgp_forget_passphrase (ctx->session, context->priv->type, NULL);
		
		return -1;
	}
	
	g_free (diagnostics);
	
	camel_stream_write (ostream, plaintext, plainlen);
	g_free (plaintext);
	
	return 0;
	
 exception:
	
	g_byte_array_free (ciphertext, TRUE);
	
	if (passphrase) {
		pgp_forget_passphrase (ctx->session, context->priv->type, NULL);
		pass_free (passphrase);
	}
	
	return -1;
}

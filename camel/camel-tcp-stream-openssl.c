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

#ifdef HAVE_OPENSSL

#include "camel-tcp-stream-openssl.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "camel-session.h"
#include "camel-service.h"
#include "camel-operation.h"
#ifdef ENABLE_THREADS
#include <pthread.h>
#endif

static CamelTcpStreamClass *parent_class = NULL;

/* Returns the class for a CamelTcpStreamOpenSSL */
#define CTSR_CLASS(so) CAMEL_TCP_STREAM_OPENSSL_CLASS (CAMEL_OBJECT_GET_CLASS (so))

static ssize_t stream_read (CamelStream *stream, char *buffer, size_t n);
static ssize_t stream_write (CamelStream *stream, const char *buffer, size_t n);
static int stream_flush  (CamelStream *stream);
static int stream_close  (CamelStream *stream);

static int stream_connect (CamelTcpStream *stream, struct hostent *host, int port);
static int stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data);
static int stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data);
static gpointer stream_get_socket (CamelTcpStream *stream);

struct _CamelTcpStreamOpenSSLPrivate {
	int sockfd;
	SSL *ssl;
	
	CamelService *service;
	char *expected_host;
};

static void
camel_tcp_stream_openssl_class_init (CamelTcpStreamOpenSSLClass *camel_tcp_stream_openssl_class)
{
	CamelTcpStreamClass *camel_tcp_stream_class =
		CAMEL_TCP_STREAM_CLASS (camel_tcp_stream_openssl_class);
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_tcp_stream_openssl_class);
	
	parent_class = CAMEL_TCP_STREAM_CLASS (camel_type_get_global_classfuncs (camel_tcp_stream_get_type ()));
	
	/* virtual method overload */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->flush = stream_flush;
	camel_stream_class->close = stream_close;
	
	camel_tcp_stream_class->connect = stream_connect;
	camel_tcp_stream_class->getsockopt = stream_getsockopt;
	camel_tcp_stream_class->setsockopt = stream_setsockopt;
	camel_tcp_stream_class->get_socket = stream_get_socket;
}

static void
camel_tcp_stream_openssl_init (gpointer object, gpointer klass)
{
	CamelTcpStreamOpenSSL *stream = CAMEL_TCP_STREAM_OPENSSL (object);
	
	stream->priv = g_new0 (struct _CamelTcpStreamOpenSSLPrivate, 1);
	stream->priv->sockfd = -1;
}

static void
camel_tcp_stream_openssl_finalize (CamelObject *object)
{
	CamelTcpStreamOpenSSL *stream = CAMEL_TCP_STREAM_OPENSSL (object);
	
	if (stream->priv->ssl) {
		SSL_shutdown (stream->priv->ssl);
		
		if (stream->priv->ssl->ctx) {
			SSL_CTX_free (stream->priv->ssl->ctx);
		}
		
		SSL_free (stream->priv->ssl);
	}
	
	if (stream->priv->sockfd != -1)
		close (stream->priv->sockfd);
	
	g_free (stream->priv->expected_host);
	
	g_free (stream->priv);
}


CamelType
camel_tcp_stream_openssl_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_tcp_stream_get_type (),
					    "CamelTcpStreamOpenSSL",
					    sizeof (CamelTcpStreamOpenSSL),
					    sizeof (CamelTcpStreamOpenSSLClass),
					    (CamelObjectClassInitFunc) camel_tcp_stream_openssl_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_tcp_stream_openssl_init,
					    (CamelObjectFinalizeFunc) camel_tcp_stream_openssl_finalize);
	}
	
	return type;
}


/**
 * camel_tcp_stream_openssl_new:
 * @service: camel service
 * @expected_host: host that the stream is expecting to connect with.
 *
 * Since the SSL certificate authenticator may need to prompt the
 * user, a CamelService is needed. @expected_host is needed as a
 * protection against an MITM attack.
 *
 * Return value: a tcp stream
 **/
CamelStream *
camel_tcp_stream_openssl_new (CamelService *service, const char *expected_host)
{
	CamelTcpStreamOpenSSL *stream;
	
	stream = CAMEL_TCP_STREAM_OPENSSL (camel_object_new (camel_tcp_stream_openssl_get_type ()));
	
	stream->priv->service = service;
	stream->priv->expected_host = g_strdup (expected_host);
	
	return CAMEL_STREAM (stream);
}

static void
errlib_error_to_errno (int ret)
{
	long error;
	
	error = ERR_get_error ();
	if (error == 0) {
		if (ret == 0)
			errno = EINVAL; /* unexpected EOF */
		else
			errno = 0;
	} else if (!errno) {
		/* ok, we get the shaft now. */
		errno = EINTR;
	}
}

static void
ssl_error_to_errno (SSL *ssl, int ret)
{
	/* hm, a CamelException might be useful right about now! */
	
	switch (SSL_get_error (ssl, ret)) {
	case SSL_ERROR_NONE:
		errno = 0;
		return;
	case SSL_ERROR_ZERO_RETURN:
		/* this one does not map well at all */
		errno = EINVAL;
		return;
	case SSL_ERROR_WANT_READ: /* non-fatal; retry */
	case SSL_ERROR_WANT_WRITE:  /* non-fatal; retry */
	case SSL_ERROR_WANT_X509_LOOKUP: /* non-fatal; retry */
		errno = EAGAIN;
		return;
	case SSL_ERROR_SYSCALL:
		errlib_error_to_errno (ret);
		return;
	case SSL_ERROR_SSL:
		errlib_error_to_errno (-1);
		return;
	}
}

static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	CamelTcpStreamOpenSSL *tcp_stream_openssl = CAMEL_TCP_STREAM_OPENSSL (stream);
	SSL *ssl = tcp_stream_openssl->priv->ssl;
	ssize_t nread;
	int cancel_fd;
	
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
	
	cancel_fd = camel_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {
		do {
			nread = SSL_read (ssl, buffer, n);
			if (nread < 0)
				ssl_error_to_errno (ssl, nread);
		} while (nread < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
	} else {
		int error, flags, fdmax;
		fd_set rdset;
		
		flags = fcntl (tcp_stream_openssl->priv->sockfd, F_GETFL);
		fcntl (tcp_stream_openssl->priv->sockfd, F_SETFL, flags | O_NONBLOCK);
		
		do {
			FD_ZERO (&rdset);
			FD_SET (tcp_stream_openssl->priv->sockfd, &rdset);
			FD_SET (cancel_fd, &rdset);
			fdmax = MAX (tcp_stream_openssl->priv->sockfd, cancel_fd) + 1;
			
			select (fdmax, &rdset, 0, 0, NULL);
			if (FD_ISSET (cancel_fd, &rdset)) {
				fcntl (tcp_stream_openssl->priv->sockfd, F_SETFL, flags);
				errno = EINTR;
				return -1;
			}
			
			do {
				nread = SSL_read (ssl, buffer, n);
				if (nread < 0)
					ssl_error_to_errno (ssl, nread);
			} while (nread < 0 && errno == EINTR);
		} while (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
		
		error = errno;
		fcntl (tcp_stream_openssl->priv->sockfd, F_SETFL, flags);
		errno = error;
	}
	
	return nread;
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	CamelTcpStreamOpenSSL *tcp_stream_openssl = CAMEL_TCP_STREAM_OPENSSL (stream);
	SSL *ssl = tcp_stream_openssl->priv->ssl;
	ssize_t w, written = 0;
	int cancel_fd;
	
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
	
	cancel_fd = camel_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {
		do {
			do {
				w = SSL_write (ssl, buffer + written, n - written);
				if (w < 0)
					ssl_error_to_errno (ssl, SSL_get_error (ssl, w));
			} while (w < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
			
			if (w > 0)
				written += w;
		} while (w != -1 && written < n);
	} else {
		fd_set rdset, wrset;
		int error, flags, fdmax;
		
		flags = fcntl (tcp_stream_openssl->priv->sockfd, F_GETFL);
		fcntl (tcp_stream_openssl->priv->sockfd, F_SETFL, flags | O_NONBLOCK);
		
		fdmax = MAX (tcp_stream_openssl->priv->sockfd, cancel_fd) + 1;
		do {
			FD_ZERO (&rdset);
			FD_ZERO (&wrset);
			FD_SET (tcp_stream_openssl->priv->sockfd, &wrset);
			FD_SET (cancel_fd, &rdset);
			
			select (fdmax, &rdset, &wrset, 0, NULL);
			if (FD_ISSET (cancel_fd, &rdset)) {
				fcntl (tcp_stream_openssl->priv->sockfd, F_SETFL, flags);
				errno = EINTR;
				return -1;
			}
			
			do {
				w = SSL_write (ssl, buffer + written, n - written);
				if (w < 0)
					ssl_error_to_errno (ssl, w);
			} while (w < 0 && errno == EINTR);
			
			if (w < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					w = 0;
				} else {
					error = errno;
					fcntl (tcp_stream_openssl->priv->sockfd, F_SETFL, flags);
					errno = error;
					return -1;
				}
			} else
				written += w;
		} while (w >= 0 && written < n);
		
		fcntl (tcp_stream_openssl->priv->sockfd, F_SETFL, flags);
	}
	
	return written;
}

static int
stream_flush (CamelStream *stream)
{
	return 0;
}


static void
close_ssl_connection (SSL *ssl)
{
	if (ssl) {
		SSL_shutdown (ssl);
		
		if (ssl->ctx)
			SSL_CTX_free (ssl->ctx);
		
		SSL_free (ssl);
	}
}

static int
stream_close (CamelStream *stream)
{
	close_ssl_connection (((CamelTcpStreamOpenSSL *)stream)->priv->ssl);
	((CamelTcpStreamOpenSSL *)stream)->priv->ssl = NULL;
	
	if (close (((CamelTcpStreamOpenSSL *)stream)->priv->sockfd) == -1)
		return -1;
	
	((CamelTcpStreamOpenSSL *)stream)->priv->sockfd = -1;
	return 0;
}

/* this is a 'cancellable' connect, cancellable from camel_operation_cancel etc */
/* returns -1 & errno == EINTR if the connection was cancelled */
static int
socket_connect (struct hostent *h, int port)
{
	struct sockaddr_in sin;
	int fd;
	int ret;
	socklen_t len;
	struct timeval tv;
	int cancel_fd;
	
	/* see if we're cancelled yet */
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
	
	/* setup connect, we do it using a nonblocking socket so we can poll it */
	sin.sin_port = htons (port);
	sin.sin_family = h->h_addrtype;
	memcpy (&sin.sin_addr, h->h_addr, sizeof (sin.sin_addr));
	
	fd = socket (h->h_addrtype, SOCK_STREAM, 0);
	
	cancel_fd = camel_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {
		ret = connect (fd, (struct sockaddr *)&sin, sizeof (sin));
		if (ret == -1) {
			close (fd);
			return -1;
		}
		
		return fd;
	} else {
		fd_set rdset, wrset;
		int flags, fdmax;
		
		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);
		
		ret = connect (fd, (struct sockaddr *)&sin, sizeof (sin));
		if (ret == 0) {
			fcntl (fd, F_SETFL, flags);
			return fd;
		}
		
		if (errno != EINPROGRESS) {
			close (fd);
			return -1;
		}
		
		FD_ZERO (&rdset);
		FD_ZERO (&wrset);
		FD_SET (fd, &wrset);
		FD_SET (cancel_fd, &rdset);
		fdmax = MAX (fd, cancel_fd) + 1;
		tv.tv_usec = 0;
		tv.tv_sec = 60 * 4;
		
		if (select (fdmax, &rdset, &wrset, 0, &tv) == 0) {
			close (fd);
			errno = ETIMEDOUT;
			return -1;
		}
		
		if (cancel_fd != -1 && FD_ISSET (cancel_fd, &rdset)) {
			close (fd);
			errno = EINTR;
			return -1;
		} else {
			len = sizeof (int);
			
			if (getsockopt (fd, SOL_SOCKET, SO_ERROR, &ret, &len) == -1) {
				close (fd);
				return -1;
			}
			
			if (ret != 0) {
				close (fd);
				errno = ret;
				return -1;
			}
		}
		
		fcntl (fd, F_SETFL, flags);
	}
	
	return fd;
}

static void
save_ssl_cert (const char *certid)
{
	char *path, *filename;
	struct stat st;
	int fd;
	
	path = g_strdup_printf ("%s/.camel_certs", getenv ("HOME"));
	if (mkdir (path, 0700) == -1) {
		if (errno != EEXIST)
			return;
		
		if (stat (path, &st) == -1)
			return;
		
		if (!S_ISDIR (st.st_mode))
			return;
	}
	
	filename = g_strdup_printf ("%s/%s", path, certid);
	g_free (path);
	
	fd = open (filename, O_WRONLY | O_CREAT);
	if (fd != -1)
		close (fd);
	
	g_free (filename);
}

static gboolean
ssl_cert_is_saved (const char *certid)
{
	char *filename;
	struct stat st;
	
	filename = g_strdup_printf ("%s/.camel_certs/%s", getenv ("HOME"), certid);
	
	if (stat (filename, &st) == -1) {
		g_free (filename);
		return FALSE;
	}
	
	g_free (filename);
	
	return st.st_uid == getuid ();
}

static int
ssl_verify (int ok, X509_STORE_CTX *ctx)
{
	CamelTcpStreamOpenSSL *stream;
	X509 *cert;
	SSL *ssl;
	int err;
	
	ssl = X509_STORE_CTX_get_ex_data (ctx, SSL_get_ex_data_X509_STORE_CTX_idx ());
	
	stream = SSL_CTX_get_app_data (ssl->ctx);
	
	cert = X509_STORE_CTX_get_current_cert (ctx);
	err = X509_STORE_CTX_get_error (ctx);
	
	if (stream)
		ok = ssl_cert_is_saved (stream->priv->expected_host);
	
	if (!ok && stream) {
		CamelService *service = stream->priv->service;
		char *prompt, *cert_str;
		char buf[257];
		
#define GET_STRING(name) X509_NAME_oneline (name, buf, 256)
		
		cert_str = g_strdup_printf (_("Issuer: %s\n"
					      "Subject: %s"),
					    GET_STRING (X509_get_issuer_name (cert)),
					    GET_STRING (X509_get_subject_name (cert)));
		
		prompt = g_strdup_printf (_("Bad certificate from %s:\n\n%s\n\n"
					    "Do you wish to accept anyway?"),
					  service->url->host, cert_str);
		
		ok = camel_session_alert_user (service->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE);
		g_free (prompt);
		
		if (ok)
			save_ssl_cert (stream->priv->expected_host);
	}
	
	return ok;
}

static SSL *
open_ssl_connection (CamelService *service, int sockfd, CamelTcpStreamOpenSSL *openssl)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	int n;
	
	SSLeay_add_ssl_algorithms();
	SSL_load_error_strings();

	/* SSLv23_client_method will negotiate with SSL v2, v3, or TLS v1 */
	ssl_ctx = SSL_CTX_new (SSLv23_client_method ());
	g_return_val_if_fail (ssl_ctx != NULL, NULL);
	
	SSL_CTX_set_verify (ssl_ctx, SSL_VERIFY_PEER, &ssl_verify);
	ssl = SSL_new (ssl_ctx);
	SSL_set_fd (ssl, sockfd);

	SSL_CTX_set_app_data (ssl_ctx, openssl);

	n = SSL_connect (ssl);
	if (n != 1) {
		ssl_error_to_errno (ssl, n);

		SSL_shutdown (ssl);
		
		if (ssl->ctx)
			SSL_CTX_free (ssl->ctx);
		
		SSL_free (ssl);
		ssl = NULL;
	}
	
	return ssl;
}

static int
stream_connect (CamelTcpStream *stream, struct hostent *host, int port)
{
	CamelTcpStreamOpenSSL *openssl = CAMEL_TCP_STREAM_OPENSSL (stream);
	SSL *ssl;
	int fd;
	
	g_return_val_if_fail (host != NULL, -1);
	
	fd = socket_connect (host, port);
	if (fd == -1)
		return -1;
	
	ssl = open_ssl_connection (openssl->priv->service, fd, openssl);
	if (!ssl)
		return -1;
	
	openssl->priv->sockfd = fd;
	openssl->priv->ssl = ssl;
	
	return 0;
}


static int
get_sockopt_level (const CamelSockOptData *data)
{
	switch (data->option) {
	case CAMEL_SOCKOPT_MAXSEGMENT:
	case CAMEL_SOCKOPT_NODELAY:
		return IPPROTO_TCP;
	default:
		return SOL_SOCKET;
	}
}

static int
get_sockopt_optname (const CamelSockOptData *data)
{
	switch (data->option) {
	case CAMEL_SOCKOPT_MAXSEGMENT:
		return TCP_MAXSEG;
	case CAMEL_SOCKOPT_NODELAY:
		return TCP_NODELAY;
	case CAMEL_SOCKOPT_BROADCAST:
		return SO_BROADCAST;
	case CAMEL_SOCKOPT_KEEPALIVE:
		return SO_KEEPALIVE;
	case CAMEL_SOCKOPT_LINGER:
		return SO_LINGER;
	case CAMEL_SOCKOPT_RECVBUFFERSIZE:
		return SO_RCVBUF;
	case CAMEL_SOCKOPT_SENDBUFFERSIZE:
		return SO_SNDBUF;
	case CAMEL_SOCKOPT_REUSEADDR:
		return SO_REUSEADDR;
	case CAMEL_SOCKOPT_IPTYPEOFSERVICE:
		return SO_TYPE;
	default:
		return -1;
	}
}

static int
stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	int optname, optlen;
	
	if ((optname = get_sockopt_optname (data)) == -1)
		return -1;
	
	if (data->option == CAMEL_SOCKOPT_NONBLOCKING) {
		int flags;
		
		flags = fcntl (((CamelTcpStreamOpenSSL *)stream)->priv->sockfd, F_GETFL);
		if (flags == -1)
			return -1;
		
		data->value.non_blocking = flags & O_NONBLOCK;
		
		return 0;
	}
	
	return getsockopt (((CamelTcpStreamOpenSSL *)stream)->priv->sockfd,
			   get_sockopt_level (data),
			   optname,
			   (void *) &data->value,
			   &optlen);
}

static int
stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	int optname;
	
	if ((optname = get_sockopt_optname (data)) == -1)
		return -1;
	
	if (data->option == CAMEL_SOCKOPT_NONBLOCKING) {
		int flags, set;
		
		flags = fcntl (((CamelTcpStreamOpenSSL *)stream)->priv->sockfd, F_GETFL);
		if (flags == -1)
			return -1;
		
		set = data->value.non_blocking ? 1 : 0;
		flags = (flags & ~O_NONBLOCK) | (set & O_NONBLOCK);
		
		if (fcntl (((CamelTcpStreamOpenSSL *)stream)->priv->sockfd, F_SETFL, flags) == -1)
			return -1;
		
		return 0;
	}
	
	return setsockopt (((CamelTcpStreamOpenSSL *)stream)->priv->sockfd,
			   get_sockopt_level (data),
			   optname,
			   (void *) &data->value,
			   sizeof (data->value));
}

static gpointer
stream_get_socket (CamelTcpStream *stream)
{
	return GINT_TO_POINTER (CAMEL_TCP_STREAM_OPENSSL (stream)->priv->sockfd);
}

#endif /* HAVE_OPENSSL */

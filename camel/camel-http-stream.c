/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximain, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "camel-http-stream.h"
#include "camel-stream-buffer.h"
#include "camel-tcp-stream-raw.h"
#ifdef HAVE_NSS
#include "camel-tcp-stream-ssl.h"
#endif
#ifdef HAVE_OPENSSL
#include "camel-tcp-stream-openssl.h"
#endif
#include "camel-exception.h"
#include "camel-session.h"

#define d(x) x


static CamelStreamClass *parent_class = NULL;

static ssize_t stream_read (CamelStream *stream, char *buffer, size_t n);
static ssize_t stream_write (CamelStream *stream, const char *buffer, size_t n);
static int stream_flush  (CamelStream *stream);
static int stream_close  (CamelStream *stream);
static int stream_reset  (CamelStream *stream);

static void
camel_http_stream_class_init (CamelHttpStreamClass *camel_http_stream_class)
{
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_http_stream_class);
	
	parent_class = CAMEL_STREAM_CLASS (camel_type_get_global_classfuncs (camel_stream_get_type ()));
	
	/* virtual method overload */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->flush = stream_flush;
	camel_stream_class->close = stream_close;
	camel_stream_class->reset = stream_reset;
}

static void
camel_http_stream_init (gpointer object, gpointer klass)
{
	CamelHttpStream *stream = CAMEL_HTTP_STREAM (object);
	
	stream->content_type = NULL;
	stream->headers = NULL;
	stream->service = NULL;
	stream->url = NULL;
	stream->raw = NULL;
}

static void
headers_free (http->headers)
{
	struct _header_raw *node, *next;
	
	node = http->headers;
	while (node) {
		next = node->next;
		g_free (node->name);
		g_free (node->value);
		g_free (node);
		node = next;
	}
}

static void
camel_http_stream_finalize (CamelObject *object)
{
	CamelHttpStream *stream = CAMEL_HTTP_STREAM (object);
	
	if (stream->content_type)
		header_content_type_unref (stream->content_type);
	
	if (stream->headers)
		headers_free (stream->headers);
	
	if (stream->service)
		camel_object_unref (CAMEL_OBJECT (service));
	
	if (stream->url)
		camel_url_free (stream->url);
	
	if (stream->raw)
		camel_object_unref (CAMEL_OBJECT (stream->raw));
}


CamelType
camel_http_stream_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_stream_get_type (),
					    "CamelHttpStream",
					    sizeof (CamelHttpStream),
					    sizeof (CamelHttpStreamClass),
					    (CamelObjectClassInitFunc) camel_http_stream_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_http_stream_init,
					    (CamelObjectFinalizeFunc) camel_http_stream_finalize);
	}
	
	return type;
}


/**
 * camel_http_stream_new:
 * @method: HTTP method
 * @service: parent service (for SSL/TLS)
 * @url: URL to act upon
 *
 * Return value: a http stream
 **/
CamelStream *
camel_http_stream_new (CamelHttpMethod method, CamelService *service, CamelURL *url)
{
	CamelHttpStream *stream;
	char *str;
	
	g_return_val_if_fail (!CAMEL_IS_SERVICE (service), NULL);
	g_return_val_if_fail (url != NULL, NULL);
	
	stream = CAMEL_HTTP_STREAM (camel_object_new (camel_http_stream_get_type ()));
	
	stream->method = method;
	stream->service = service;
	camel_object_ref (CAMEL_OBJECT (service));
	
	str = camel_url_to_string (url, 0);
	stream->url = camel_url_new (str, NULL);
	g_free (str);
	
	return CAMEL_STREAM (stream);
}


static CamelStream *
http_connect (CamelService *service, CamelURL *url)
{
	CamelStream *buffered, *stream = NULL;
	struct hostent *host;
	int errsave;
	
	if (!strcasecmp (url->protocol, "https")) {
#ifdef HAVE_NSS
		stream = camel_tcp_stream_ssl_new (service, url->host);
#else
#ifdef HAVE_OPENSSL
		stream = camel_tcp_stream_openssl_new (service, url->host);
#endif /* HAVE_OPENSSL */
#endif /* HAVE_NSS */  
	} else {
		stream = camel_tcp_stream_raw_new ();
	}
	
	if (stream == NULL) {
		errno = EINVAL;
		return NULL;
	}
	
	host = camel_get_host_byname (url->host, NULL);
	if (!host) {
		errno = EHOSTUNREACH;
		return NULL;
	}
	
	if (camel_tcp_stream_connect (CAMEL_TCP_STREAM (stream), host, url->port ? url->port : 80) == -1) {
		errsave = errno;
		camel_object_unref (CAMEL_OBJECT (stream));
		camel_free_host (host);
		errno = errsave;
		return NULL;
	}
	
	camel_free_host (host);
	
	buffered = camel_stream_buffer_new (stream, CAMEL_STREAM_BUFFER_READ);
	camel_object_unref (CAMEL_OBJECT (stream));
	
	return buffered;
}


static const char *
http_next_token (const unsigned char *in)
{
	const char *inptr = in;
	
	while (*inptr && !is_lwsp (*inptr))
		inptr++;
	
	while (*inptr && is_lwsp (*inptr))
		inptr++;
	
	return inptr;
}

static int
http_get_headers (CamelHttpStream *http)
{
	CamelMimeParser *mp;
	struct _header_raw *headers, *node, *tail;
	const char *type, *token;
	int status, len, err;
	char *buf;
	
	if (camel_stream_buffer_gets (CAMEL_STREAM_BUFFER (http->raw), buffer, 4096) <= 0)
		return -1;
	
	if (!stncasecmp (buffer, "HTTP/", 5)) {
		token = http_next_token (buffer);
		status = header_decode_int (&token);
		/* FIXME: don't just check for 200 */
		if (status != 200 /* OK */)
			goto exception;
	} else
		goto exception;
	
	mp = camel_mime_parser_new ();
	camel_mime_parser_init_with_stream (mp, http->raw);
	
	switch (camel_mime_parser_step (mp, &buf, &len)) {
	case HSCAN_MESSAGE:
	case HSCAN_HEADER:
	case HSCAN_MULTIPART:
		/* we have the headers, build them into 'us' */
		headers = camel_mime_parser_headers_raw (mp);
		
		/* if content-type exists, process it first, set for fallback charset in headers */
		if (http->content_type)
			header_content_type_unref (http->content_type);
		type = header_raw_find (&headers, "Content-Type", NULL);
		if (type)
			http->content_type = header_content_type_decode (type);
		else
			http->content_type = NULL;
		
		if (http->headers)
			headers_free (http->headers);
		
		http->headers = NULL;
		tail = (struct _header_raw *) &http->headers;
		
		while (headers) {
			node = g_new (struct _header_raw, 1);
			node->next = NULL;
			node->name = g_strdup (headers->name);
			node->value = g_strdup (headers->value);
			node->offset = headers->offset;
			tail->next = node;
			tail = node;
			headers = headers->next;
		}
		
		break;
	default:
		g_warning ("Invalid state encountered???: %d", camel_mime_parser_state (mp));
	}
	
	err = camel_mime_parser_errno (mp);
	camel_object_unref (CAMEL_OBJECT (mp));
	
	if (err != 0)
		goto exception;
	
	return 0;
	
 exception:
	camel_object_unref (CAMEL_OBJECT (http->raw));
	http->raw = NULL;
	
	return -1;
}


static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	CamelHttpStream *http = CAMEL_HTTP_STREAM (stream);
	
	if (http->method != CAMEL_HTTP_METHOD_GET || http->method != CAMEL_HTTP_METHOD_HEAD) {
		errno = ENOTSUPP;
		return -1;
	}
	
	if (!http->raw) {
		const char *method;
		char *url;
		
		http->raw = http_connect (http->service, http->url);
		if (!http->raw)
			return -1;
		
		switch (http->method) {
		case CAMEL_HTTP_METHOD_GET:
			method = "GET";
			break;
		case CAMEL_HTTP_METHOD_HEAD:
			method = "HEAD";
			break;
		}
		
		url = camel_url_to_string (http->url, 0);
		if (camel_stream_printf (http->raw, "%s %s HTTP/1.1\r\nHost: %s\r\n\r\n",
					 method, http->url->path ? http->url->path : "/",
					 http->url->host) == -1 ||
		    camel_stream_flush (http->raw) == -1) {
			camel_object_unref (CAMEL_OBJECT (http->raw));
			http->raw = NULL;
			return -1;
		}
		g_free (url);
		
		if (http_get_headers (http) == -1)
			return -1;
	}
	
	return camel_stream_read (http->raw, buffer, n);
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	CamelHttpStream *http = CAMEL_HTTP_STREAM (stream);
	
	if (http->method == CAMEL_HTTP_METHOD_GET || http->method == CAMEL_HTTP_METHOD_HEAD) {
		errno = ENOTSUPP;
		return -1;
	}
	
	return -1;
#if 0	
	if (!http->raw) {
		const char *method;
		char *url;
		
		http->raw = http_connect (http->service, http->url);
		if (!http->raw)
			return -1;
		
		switch (http->method) {
		case CAMEL_HTTP_METHOD_PUT:
			method = "PUT";
			break;
		case CAMEL_HTTP_METHOD_POST:
			method = "POST";
			break;
		}
		
		url = camel_url_to_string (http->url, 0);
		if (camel_stream_printf (http->raw, "%s %s HTTP/1.1\r\nHost: %s\r\n\r\n",
					 method, http->url->path ? http->url->path : "/",
					 http->url->host) == -1 ||
		    camel_stream_flush (http->raw) == -1) {
			camel_object_unref (CAMEL_OBJECT (http->raw));
			http->raw = NULL;
			return -1;
		}
		g_free (url);
		
		if (http_get_headers (http) == -1)
			return -1;
	}
	
	return camel_stream_write (http->raw, buffer, n);
#endif
}

static int
stream_flush (CamelStream *stream)
{
	if (stream->raw)
		return camel_stream_flush (stream->raw);
	else
		return 0;
}

static int
stream_close (CamelStream *stream)
{
	CamelHttpStream *http = CAMEL_HTTP_STREAM (stream);
	
	if (http->raw) {
		if (camel_stream_close (http->raw) == -1)
			return -1;
		
		camel_object_unref (CAMEL_OBJECT (http->raw));
		http->raw = NULL;
	}
	
	return 0;
}

static int
stream_reset (CamelStream *stream)
{
	CamelHttpStream *http = CAMEL_HTTP_STREAM (stream);
	
	if (http->raw) {
		camel_stream_close (http->raw);
		camel_object_unref (CAMEL_OBJECT (http->raw));
		http->raw = NULL;
	}
	
	return 0;
};

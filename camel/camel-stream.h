/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-stream.h : class for an abstract stream */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifndef CAMEL_STREAM_H
#define CAMEL_STREAM_H 1

#include <stdarg.h>
#include <unistd.h>
#include <camel/camel-object.h>

#define CAMEL_STREAM_TYPE     (camel_stream_get_type ())
#define CAMEL_STREAM(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_STREAM_TYPE, CamelStream))
#define CAMEL_STREAM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_STREAM_TYPE, CamelStreamClass))
#define CAMEL_IS_STREAM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_STREAM_TYPE))

G_BEGIN_DECLS

struct _CamelStream
{
	CamelObject parent_object;

	gboolean eos;
};

typedef struct {
	CamelObjectClass parent_class;

	/* Virtual methods */

	ssize_t   (*read)       (CamelStream *stream, char *buffer, size_t n);
	ssize_t   (*write)      (CamelStream *stream, const char *buffer, size_t n);
	int       (*close)      (CamelStream *stream);
	int       (*flush)      (CamelStream *stream);
	gboolean  (*eos)        (CamelStream *stream);
	int       (*reset)      (CamelStream *stream);

} CamelStreamClass;

/* Standard Camel function */
CamelType camel_stream_get_type (void);

/* public methods */
ssize_t    camel_stream_read       (CamelStream *stream, char *buffer, size_t n);
ssize_t    camel_stream_write      (CamelStream *stream, const char *buffer, size_t n);
int        camel_stream_flush      (CamelStream *stream);
int        camel_stream_close      (CamelStream *stream);
gboolean   camel_stream_eos        (CamelStream *stream);
int        camel_stream_reset      (CamelStream *stream);

/* utility macros and funcs */
ssize_t camel_stream_write_string (CamelStream *stream, const char *string);
ssize_t camel_stream_printf (CamelStream *stream, const char *fmt, ... ) G_GNUC_PRINTF (2, 3);
ssize_t camel_stream_vprintf (CamelStream *stream, const char *fmt, va_list ap);

/* Write a whole stream to another stream, until eof or error on
 * either stream.
 */
ssize_t camel_stream_write_to_stream (CamelStream *stream, CamelStream *output_stream);

G_END_DECLS

#endif /* CAMEL_STREAM_H */

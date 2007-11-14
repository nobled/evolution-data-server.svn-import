/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000 Ximian, Inc.
 *
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>

#include "camel-mime-filter-linewrap.h"


static void filter (CamelMimeFilter *f, char *in, size_t len, size_t prespace,
		    char **out, size_t *outlen, size_t *outprespace);
static void complete (CamelMimeFilter *f, char *in, size_t len,
		      size_t prespace, char **out, size_t *outlen,
		      size_t *outprespace);
static void reset (CamelMimeFilter *f);


static void
camel_mime_filter_linewrap_class_init (CamelMimeFilterLinewrapClass *klass)
{
	CamelMimeFilterClass *mime_filter_class =
		(CamelMimeFilterClass *) klass;

	mime_filter_class->filter = filter;
	mime_filter_class->complete = complete;
	mime_filter_class->reset = reset;
}

CamelType
camel_mime_filter_linewrap_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type(), "CamelMimeFilterLinewrap",
					    sizeof (CamelMimeFilterLinewrap),
					    sizeof (CamelMimeFilterLinewrapClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_linewrap_class_init,
					    NULL,
					    NULL,
					    NULL);
	}

	return type;
}

static void
filter (CamelMimeFilter *f, char *in, size_t len, size_t prespace,
	char **out, size_t *outlen, size_t *outprespace)
{
	CamelMimeFilterLinewrap *linewrap = (CamelMimeFilterLinewrap *)f;
	char *inend, *p, *q;
	int nchars = linewrap->nchars;

	/* we'll be adding chars here so we need a bigger buffer */
	camel_mime_filter_set_size (f, 3 * len, FALSE);

	p = in;
	q = f->outbuf;
	inend = in + len;

	while (p < inend) {
		if (*p == '\n') {
			*q++ = *p++;
			nchars = 0;
		} else if (isspace (*p)) {
			if (nchars >= linewrap->wrap_len) {
				*q++ = '\n';
				p++;
				nchars = 0;
			} else {
				*q++ = *p++;
			}
		} else {
			*q++ = *p++;
			nchars++;
		}

		/* line is getting way too long, we must force a wrap here */
		if (nchars >= (linewrap->max_len - 1) && *p != '\n') {
			*q++ = '\n';
			*q++ = linewrap->indent;
			nchars = 0;
		}
	}

	linewrap->nchars = nchars;

	*out = f->outbuf;
	*outlen = q - f->outbuf;
	*outprespace = f->outpre;
}

static void
complete (CamelMimeFilter *f, char *in, size_t len, size_t prespace,
	  char **out, size_t *outlen, size_t *outprespace)
{
	if (len)
		filter (f, in, len, prespace, out, outlen, outprespace);
}

static void
reset (CamelMimeFilter *f)
{
	CamelMimeFilterLinewrap *linewrap = (CamelMimeFilterLinewrap *)f;

	linewrap->nchars = 0;
}

CamelMimeFilter *
camel_mime_filter_linewrap_new (guint preferred_len, guint max_len, char indent_char)
{
	CamelMimeFilterLinewrap *linewrap =
		CAMEL_MIME_FILTER_LINEWRAP (camel_object_new (CAMEL_MIME_FILTER_LINEWRAP_TYPE));

	linewrap->indent = indent_char;
	linewrap->wrap_len = preferred_len;
	linewrap->max_len = max_len;
	linewrap->nchars = 0;

	return (CamelMimeFilter *) linewrap;
}

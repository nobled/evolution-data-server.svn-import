/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2001 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "camel-html-parser.h"
#include "camel-mime-filter-html.h"

#define d(x)

static void camel_mime_filter_html_class_init (CamelMimeFilterHTMLClass *klass);
static void camel_mime_filter_html_init       (CamelObject *o);
static void camel_mime_filter_html_finalize   (CamelObject *o);

static CamelMimeFilterClass *camel_mime_filter_html_parent;

struct _CamelMimeFilterHTMLPrivate {
	CamelHTMLParser *ctxt;
};

/* ********************************************************************** */

#if 0

/* well we odnt use this stuff yet */

#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))

static struct {
	char *element;
	char *remap;
} map_start[] = {
	{ "p", "\n\n" },
	{ "br", "\n" },
	{ "h1", "\n" }, { "h2", "\n" }, { "h3", "\n" }, { "h4", "\n" }, { "h5", "\n" }, { "h6", "\n" },
};


static struct {
	char *element;
	char *remap;
} map_end[] = {
	{ "h1", "\n" }, { "h2", "\n" }, { "h3", "\n" }, { "h4", "\n" }, { "h5", "\n" }, { "h6", "\n" },
};
#endif


/* ********************************************************************** */


CamelType
camel_mime_filter_html_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type (), "CamelMimeFilterHTML",
					    sizeof (CamelMimeFilterHTML),
					    sizeof (CamelMimeFilterHTMLClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_html_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_mime_filter_html_init,
					    (CamelObjectFinalizeFunc) camel_mime_filter_html_finalize);
	}
	
	return type;
}

static void
camel_mime_filter_html_finalize(CamelObject *o)
{
	CamelMimeFilterHTML *f = (CamelMimeFilterHTML *)o;

	camel_object_unref((CamelObject *)f->priv->ctxt);
	g_free(f->priv);
}

static void
camel_mime_filter_html_init       (CamelObject *o)
{
	CamelMimeFilterHTML *f = (CamelMimeFilterHTML *)o;

	f->priv = g_malloc0(sizeof(*f->priv));
	f->priv->ctxt = camel_html_parser_new();
}

static void
run(CamelMimeFilter *mf, char *in, size_t inlen, size_t prespace, char **out, size_t *outlenptr, size_t *outprespace, int last)
{
	CamelMimeFilterHTML *f = (CamelMimeFilterHTML *) mf;
	camel_html_parser_t state;
	char *outp;
	
	d(printf("converting html:\n%.*s\n", (int)inlen, in));
	
	/* We should generally shrink the data, but this'll do */
	camel_mime_filter_set_size (mf, inlen * 2 + 256, FALSE);
	outp = mf->outbuf;
	
	camel_html_parser_set_data (f->priv->ctxt, in, inlen, last);
	do {
		const char *data;
		int len;
		
		state = camel_html_parser_step(f->priv->ctxt, &data, &len);
		
		switch(state) {
		case CAMEL_HTML_PARSER_DATA:
		case CAMEL_HTML_PARSER_ENT:
			memcpy(outp, data, len);
			outp += len;
			break;
		case CAMEL_HTML_PARSER_ELEMENT:
			/* FIXME: do some whitespace processing here */
			break;
		default:
			/* ignore everything else */
			break;
		}
	} while (state != CAMEL_HTML_PARSER_EOF && state != CAMEL_HTML_PARSER_EOD);

	*out = mf->outbuf;
	*outlenptr = outp - mf->outbuf;
	*outprespace = mf->outbuf - mf->outreal;

	d(printf("converted html end:\n%.*s\n", (int)*outlenptr, *out));
}

static void
complete(CamelMimeFilter *mf, char *in, size_t len, size_t prespace, char **out, size_t *outlenptr, size_t *outprespace)
{
	run(mf, in, len, prespace, out, outlenptr, outprespace, TRUE);
}

static void
filter(CamelMimeFilter *mf, char *in, size_t len, size_t prespace, char **out, size_t *outlenptr, size_t *outprespace)
{
	run(mf, in, len, prespace, out, outlenptr, outprespace, FALSE);
}

static void
reset(CamelMimeFilter *mf)
{
	CamelMimeFilterHTML *f = (CamelMimeFilterHTML *)mf;

	camel_object_unref((CamelObject *)f->priv->ctxt);
	f->priv->ctxt = camel_html_parser_new();
}

static void
camel_mime_filter_html_class_init (CamelMimeFilterHTMLClass *klass)
{
	CamelMimeFilterClass *filter_class = (CamelMimeFilterClass *) klass;
	
	camel_mime_filter_html_parent = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));

	filter_class->reset = reset;
	filter_class->filter = filter;
	filter_class->complete = complete;
}


/**
 * camel_mime_filter_html_new:
 *
 * Create a new #CamelMimeFilterHTML object.
 * 
 * Returns a new #CamelMimeFilterHTML object
 **/
CamelMimeFilterHTML *
camel_mime_filter_html_new (void)
{
	CamelMimeFilterHTML *new = CAMEL_MIME_FILTER_HTML ( camel_object_new (camel_mime_filter_html_get_type ()));
	return new;
}

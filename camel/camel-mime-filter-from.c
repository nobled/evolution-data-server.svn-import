/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000 Ximian Inc.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-mime-filter-from.h"

#define d(x)

static void camel_mime_filter_from_class_init (CamelMimeFilterFromClass *klass);
static void camel_mime_filter_from_init       (CamelMimeFilterFrom *obj);

static CamelMimeFilterClass *camel_mime_filter_from_parent;

CamelType
camel_mime_filter_from_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type (), "CamelMimeFilterFrom",
					    sizeof (CamelMimeFilterFrom),
					    sizeof (CamelMimeFilterFromClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_from_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_mime_filter_from_init,
					    NULL);
	}
	
	return type;
}

struct fromnode {
	struct fromnode *next;
	char *pointer;
};

static void
complete(CamelMimeFilter *mf, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace)
{
	*out = in;
	*outlen = len;
	*outprespace = prespace;
}

/* Yes, it is complicated ... */
static void
filter(CamelMimeFilter *mf, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace)
{
	CamelMimeFilterFrom *f = (CamelMimeFilterFrom *)mf;
	register char *inptr, *inend;
	int left;
	int midline = f->midline;
	int fromcount = 0;
	struct fromnode *head = NULL, *tail = (struct fromnode *)&head, *node;
	char *outptr;

	inptr = in;
	inend = inptr+len;

	d(printf("Filtering '%.*s'\n", len, in));

	/* first, see if we need to escape any from's */
	while (inptr<inend) {
		register int c = -1;

		if (midline)
			while (inptr < inend && (c = *inptr++) != '\n')
				;

		if (c == '\n' || !midline) {
			left = inend-inptr;
			if (left > 0) {
				midline = TRUE;
				if (left < 5) {
					if (inptr[0] == 'F') {
						camel_mime_filter_backup(mf, inptr, left);
						midline = FALSE;
						inend = inptr;
						break;
					}
				} else {
					if (!strncmp(inptr, "From ", 5)) {
						fromcount++;
						/* yes, we do alloc them on the stack ... at most we're going to get
						   len / 7 of them anyway */
						node = alloca(sizeof(*node));
						node->pointer = inptr;
						node->next = NULL;
						tail->next = node;
						tail = node;
						inptr += 5;
					}
				}
			} else {
				/* \n is at end of line, check next buffer */
				midline = FALSE;
			}
		}
	}

	f->midline = midline;

	if (fromcount > 0) {
		camel_mime_filter_set_size(mf, len + fromcount, FALSE);
		node = head;
		inptr = in;
		outptr = mf->outbuf;
		while (node) {
			memcpy(outptr, inptr, node->pointer - inptr);
			outptr += node->pointer - inptr;
			*outptr++ = '>';
			inptr = node->pointer;
			node = node->next;
		}
		memcpy(outptr, inptr, inend - inptr);
		outptr += inend - inptr;
		*out = mf->outbuf;
		*outlen = outptr - mf->outbuf;
		*outprespace = mf->outbuf - mf->outreal;

		d(printf("Filtered '%.*s'\n", *outlen, *out));
	} else {
		*out = in;
		*outlen = inend - in;
		*outprespace = prespace;
		
		d(printf("Filtered '%.*s'\n", *outlen, *out));
	}
}

static void
camel_mime_filter_from_class_init (CamelMimeFilterFromClass *klass)
{
	CamelMimeFilterClass *filter_class = (CamelMimeFilterClass *) klass;
	
	camel_mime_filter_from_parent = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));

	filter_class->filter = filter;
	filter_class->complete = complete;
}

static void
camel_mime_filter_from_init (CamelMimeFilterFrom *obj)
{
	;
}


/**
 * camel_mime_filter_from_new:
 *
 * Create a new #CamelMimeFilterFrom object.
 * 
 * Returns a new #CamelMimeFilterFrom object
 **/
CamelMimeFilterFrom *
camel_mime_filter_from_new (void)
{
	CamelMimeFilterFrom *new = CAMEL_MIME_FILTER_FROM ( camel_object_new (camel_mime_filter_from_get_type ()));
	return new;
}

#if 0

#include <stdio.h>

int main(int argc, char **argv)
{
	CamelMimeFilterFrom *f;
	char *buffer;
	int len, prespace;

	g_tk_init(&argc, &argv);


	f = camel_mime_filter_from_new();

	buffer = "This is a test\nFrom Someone\nTo someone. From Someone else, From\n From blah\nFromblah\nBye! \nFrom ";
	len = strlen(buffer);
	prespace = 0;

	printf("input = '%.*s'\n", len, buffer);
	camel_mime_filter_filter(f, buffer, len, prespace, &buffer, &len, &prespace);
	printf("output = '%.*s'\n", len, buffer);
	buffer = "";
	len = 0;
	prespace = 0;
	camel_mime_filter_complete(f, buffer, len, prespace, &buffer, &len, &prespace);
	printf("complete = '%.*s'\n", len, buffer);
	

	return 0;
}

#endif

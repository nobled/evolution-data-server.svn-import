/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camelMimePart.c : Abstract class for a mime_part */

/* 
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <string.h>
#include "camel-mime-part.h"
#include <stdio.h>
#include "string-utils.h"
#include "hash-table-utils.h"
#include "camel-mime-part-utils.h"
#include <ctype.h>
#include "camel-mime-parser.h"
#include "camel-stream-mem.h"
#include "camel-stream-filter.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-filter-charset.h"
#include "camel-exception.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

typedef enum {
	HEADER_UNKNOWN,
	HEADER_DESCRIPTION,
	HEADER_DISPOSITION,
	HEADER_CONTENT_ID,
	HEADER_ENCODING,
	HEADER_CONTENT_MD5,
	HEADER_CONTENT_LANGUAGES,
	HEADER_CONTENT_TYPE
} CamelHeaderType;


static GHashTable *header_name_table;
static GHashTable *header_formatted_table;

static CamelMediumClass *parent_class=NULL;

/* Returns the class for a CamelMimePart */
#define CMP_CLASS(so) CAMEL_MIME_PART_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CDW_CLASS(so) CAMEL_DATA_WRAPPER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMD_CLASS(so) CAMEL_MEDIUM_CLASS (CAMEL_OBJECT_GET_CLASS(so))

/* from CamelDataWrapper */
static int             write_to_stream                 (CamelDataWrapper *data_wrapper, CamelStream *stream);
static int	       construct_from_stream	       (CamelDataWrapper *dw, CamelStream *s);

/* from CamelMedia */ 
static void            add_header                      (CamelMedium *medium, const char *header_name, const void *header_value);
static void            set_header                      (CamelMedium *medium, const char *header_name, const void *header_value);
static void            remove_header                   (CamelMedium *medium, const char *header_name);
static const void     *get_header                      (CamelMedium *medium, const char *header_name);

static void            set_content_object              (CamelMedium *medium, CamelDataWrapper *content);

/* from camel mime parser */
static int             construct_from_parser           (CamelMimePart *, CamelMimeParser *);

/* forward references */
static void set_disposition (CamelMimePart *mime_part, const gchar *disposition);


/* loads in a hash table the set of header names we */
/* recognize and associate them with a unique enum  */
/* identifier (see CamelHeaderType above)           */
static void
init_header_name_table()
{
	header_name_table = g_hash_table_new (g_strcase_hash, g_strcase_equal);
	g_hash_table_insert (header_name_table, "Content-Description", (gpointer)HEADER_DESCRIPTION);
	g_hash_table_insert (header_name_table, "Content-Disposition", (gpointer)HEADER_DISPOSITION);
	g_hash_table_insert (header_name_table, "Content-id", (gpointer)HEADER_CONTENT_ID);
	g_hash_table_insert (header_name_table, "Content-Transfer-Encoding", (gpointer)HEADER_ENCODING);
	g_hash_table_insert (header_name_table, "Content-MD5", (gpointer)HEADER_CONTENT_MD5);
	g_hash_table_insert (header_name_table, "Content-Type", (gpointer)HEADER_CONTENT_TYPE);

	header_formatted_table = g_hash_table_new(g_strcase_hash, g_strcase_equal);
	g_hash_table_insert(header_formatted_table, "Content-Type", (void *)1);
	g_hash_table_insert(header_formatted_table, "Content-Disposition", (void *)1);
}

static void
camel_mime_part_class_init (CamelMimePartClass *camel_mime_part_class)
{
	CamelMediumClass *camel_medium_class = CAMEL_MEDIUM_CLASS (camel_mime_part_class);
	CamelDataWrapperClass *camel_data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (camel_mime_part_class);

	parent_class = CAMEL_MEDIUM_CLASS (camel_type_get_global_classfuncs (camel_medium_get_type ()));
	init_header_name_table();

	camel_mime_part_class->construct_from_parser = construct_from_parser;
	
	/* virtual method overload */	
	camel_medium_class->add_header                = add_header;
	camel_medium_class->set_header                = set_header;
	camel_medium_class->get_header                = get_header;
	camel_medium_class->remove_header             = remove_header;
	camel_medium_class->set_content_object        = set_content_object;

	camel_data_wrapper_class->write_to_stream     = write_to_stream;
	camel_data_wrapper_class->construct_from_stream= construct_from_stream;
}

static void
camel_mime_part_init (gpointer   object,  gpointer   klass)
{
	CamelMimePart *camel_mime_part = CAMEL_MIME_PART (object);
	
	camel_mime_part->content_type         = header_content_type_new ("text", "plain");
	camel_mime_part->description          = NULL;
	camel_mime_part->disposition          = NULL;
	camel_mime_part->content_id           = NULL;
	camel_mime_part->content_MD5          = NULL;
	camel_mime_part->content_languages    = NULL;
	camel_mime_part->encoding             = CAMEL_MIME_PART_ENCODING_DEFAULT;
}


static void           
camel_mime_part_finalize (CamelObject *object)
{
	CamelMimePart *mime_part = CAMEL_MIME_PART (object);

	g_free (mime_part->description);
	g_free (mime_part->content_id);
	g_free (mime_part->content_MD5);
	string_list_free (mime_part->content_languages);
	header_disposition_unref(mime_part->disposition);
	
	if (mime_part->content_type)
		header_content_type_unref (mime_part->content_type);

	header_raw_clear(&mime_part->headers);
}



CamelType
camel_mime_part_get_type (void)
{
	static CamelType camel_mime_part_type = CAMEL_INVALID_TYPE;
	
	if (camel_mime_part_type == CAMEL_INVALID_TYPE)	{
		camel_mime_part_type = camel_type_register (CAMEL_MEDIUM_TYPE, "CamelMimePart",
							    sizeof (CamelMimePart),
							    sizeof (CamelMimePartClass),
							    (CamelObjectClassInitFunc) camel_mime_part_class_init,
							    NULL,
							    (CamelObjectInitFunc) camel_mime_part_init,
							    (CamelObjectFinalizeFunc) camel_mime_part_finalize);
	}
	
	return camel_mime_part_type;
}


/* **** */

static gboolean
process_header(CamelMedium *medium, const char *header_name, const char *header_value)
{
	CamelMimePart *mime_part = CAMEL_MIME_PART (medium);
	CamelHeaderType header_type;
	char *text;

	/* Try to parse the header pair. If it corresponds to something   */
	/* known, the job is done in the parsing routine. If not,         */
	/* we simply add the header in a raw fashion                      */

	header_type = (CamelHeaderType) g_hash_table_lookup (header_name_table, header_name);
	switch (header_type) {
	case HEADER_DESCRIPTION: /* raw header->utf8 conversion */
		text = header_decode_string(header_value);
		g_free(mime_part->description);
		mime_part->description = g_strstrip (text);
		break;
	case HEADER_DISPOSITION:
		set_disposition(mime_part, header_value);
		break;
	case HEADER_CONTENT_ID:
		text = header_msgid_decode(header_value);
		g_free(mime_part->content_id);
		mime_part->content_id = text;
		break;
	case HEADER_ENCODING:
		text = header_token_decode(header_value);
		mime_part->encoding = camel_mime_part_encoding_from_string (text);
		g_free(text);
		break;
	case HEADER_CONTENT_MD5:
		g_free(mime_part->content_MD5);
		mime_part->content_MD5 = g_strdup(header_value);
		break;
	case HEADER_CONTENT_TYPE: 
		if (mime_part->content_type)
			header_content_type_unref (mime_part->content_type);
		mime_part->content_type = header_content_type_decode (header_value);
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

static void
set_header (CamelMedium *medium, const char *header_name, const void *header_value)
{
	CamelMimePart *part = CAMEL_MIME_PART (medium);
	
	process_header(medium, header_name, header_value);
	header_raw_replace(&part->headers, header_name, header_value, -1);
}

static void
add_header (CamelMedium *medium, const char *header_name, const void *header_value)
{
	CamelMimePart *part = CAMEL_MIME_PART (medium);
	
	/* Try to parse the header pair. If it corresponds to something   */
	/* known, the job is done in the parsing routine. If not,         */
	/* we simply add the header in a raw fashion                      */

	/* If it was one of the headers we handled, it must be unique, set it instead of add */
	if (process_header(medium, header_name, header_value))
		header_raw_replace(&part->headers, header_name, header_value, -1);
	else
		header_raw_append(&part->headers, header_name, header_value, -1);
}

static void
remove_header (CamelMedium *medium, const char *header_name)
{
	CamelMimePart *part = (CamelMimePart *)medium;
	
	process_header(medium, header_name, NULL);
	header_raw_remove(&part->headers, header_name);
}

static const void *
get_header (CamelMedium *medium, const char *header_name)
{
	CamelMimePart *part = (CamelMimePart *)medium;

	return header_raw_find(&part->headers, header_name, NULL);
}


/* **** Content-Description */
void
camel_mime_part_set_description (CamelMimePart *mime_part, const gchar *description)
{
	char *text = header_encode_string (description);
	
	camel_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Description", text);
	g_free (text);
}

const gchar *
camel_mime_part_get_description (CamelMimePart *mime_part)
{
	return mime_part->description;
}

/* **** Content-Disposition */

static void
set_disposition (CamelMimePart *mime_part, const gchar *disposition)
{
	header_disposition_unref(mime_part->disposition);
	if (disposition)
		mime_part->disposition = header_disposition_decode(disposition);
	else
		mime_part->disposition = NULL;
}


void
camel_mime_part_set_disposition (CamelMimePart *mime_part, const gchar *disposition)
{
	char *text;

	/* we poke in a new disposition (so we dont lose 'filename', etc) */
	if (mime_part->disposition == NULL) {
		set_disposition(mime_part, disposition);
	}
	if (mime_part->disposition != NULL) {
		g_free(mime_part->disposition->disposition);
		mime_part->disposition->disposition = g_strdup(disposition);
	}
	text = header_disposition_format(mime_part->disposition);

	camel_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Disposition", text);

	g_free(text);
}

const gchar *
camel_mime_part_get_disposition (CamelMimePart *mime_part)
{
	if (mime_part->disposition)
		return (mime_part->disposition)->disposition;
	else
		return NULL;
}


/* **** Content-Disposition: filename="xxx" */

void
camel_mime_part_set_filename (CamelMimePart *mime_part, const gchar *filename)
{
	char *str;
	if (mime_part->disposition == NULL)
		mime_part->disposition = header_disposition_decode("attachment");

	header_set_param(&mime_part->disposition->params, "filename", filename);
	str = header_disposition_format(mime_part->disposition);

	camel_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Disposition", str);
	g_free(str);
}

const gchar *
camel_mime_part_get_filename (CamelMimePart *mime_part)
{
	if (mime_part->disposition) {
		const gchar *name = header_param (mime_part->disposition->params, "filename");
		if (name)
			return name;
	}

	return header_content_type_param (mime_part->content_type, "name");
}


/* **** Content-ID: */

void
camel_mime_part_set_content_id (CamelMimePart *mime_part, const char *contentid)
{
	camel_medium_set_header (CAMEL_MEDIUM (mime_part), "Content-ID",
				 contentid);
}

const gchar *
camel_mime_part_get_content_id (CamelMimePart *mime_part)
{
	return mime_part->content_id;
}

/* **** Content-MD5: */

void
camel_mime_part_set_content_MD5 (CamelMimePart *mime_part, const char *md5)
{
	camel_medium_set_header (CAMEL_MEDIUM (mime_part), "Content-MD5", md5);
}

const gchar *
camel_mime_part_get_content_MD5 (CamelMimePart *mime_part)
{
	return mime_part->content_MD5;
}

/* **** Content-Transfer-Encoding: */

void
camel_mime_part_set_encoding (CamelMimePart *mime_part,
			      CamelMimePartEncodingType encoding)
{
	const char *text;

	text = camel_mime_part_encoding_to_string (encoding);
	camel_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Transfer-Encoding", text);
}

const CamelMimePartEncodingType
camel_mime_part_get_encoding (CamelMimePart *mime_part)
{
	return mime_part->encoding;
}

/* FIXME: do something with this stuff ... */

void
camel_mime_part_set_content_languages (CamelMimePart *mime_part, GList *content_languages)
{
	if (mime_part->content_languages) string_list_free (mime_part->content_languages);
	mime_part->content_languages = content_languages;

	/* FIXME: translate to a header and set it */
}

const GList *
camel_mime_part_get_content_languages (CamelMimePart *mime_part)
{
	return mime_part->content_languages;
}


/* **** */

/* **** Content-Type: */

void 
camel_mime_part_set_content_type (CamelMimePart *mime_part, gchar *content_type)
{
	camel_medium_set_header (CAMEL_MEDIUM (mime_part),
				 "Content-Type", content_type);
}

CamelContentType *
camel_mime_part_get_content_type (CamelMimePart *mime_part)
{
	return mime_part->content_type;
}

/*********/



static void
set_content_object (CamelMedium *medium, CamelDataWrapper *content)
{
	CamelMimePart *mime_part = CAMEL_MIME_PART (medium);
	CamelContentType *object_content_type;

	parent_class->set_content_object (medium, content);

	object_content_type = camel_data_wrapper_get_mime_type_field (content);
	if (mime_part->content_type != object_content_type) {
		char *txt;

		txt = header_content_type_format (object_content_type);
		camel_medium_set_header (CAMEL_MEDIUM (mime_part), "Content-Type", txt);
		g_free(txt);
	}
}

/**********************************************************************/

static int
write_to_stream(CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	CamelMimePart *mp = CAMEL_MIME_PART(data_wrapper);
	CamelMedium *medium = CAMEL_MEDIUM(data_wrapper);
	CamelDataWrapper *content;
	int total = 0;
	int count;

	d(printf("mime_part::write_to_stream\n"));

	/* FIXME: something needs to be done about this ... */
	/* FIXME: need to count these bytes too */
#ifndef NO_WARNINGS
#warning content-languages should be stored as a header
#endif

	if (mp->headers) {
		struct _header_raw *h = mp->headers;
		char *val;

		/* fold/write the headers.   But dont fold headers that are already formatted
		   (e.g. ones with parameter-lists, that we know about, and have created) */
		while (h) {
			val = h->value;
			if (val == NULL) {
				g_warning("h->value is NULL here for %s", h->name);
				count = 0;
			} else if (g_hash_table_lookup(header_formatted_table, val) == NULL) {
				val = header_fold(val, strlen(h->name));
				count = camel_stream_printf(stream, "%s%s%s\n", h->name, isspace(val[0]) ? ":" : ": ", val);
				g_free(val);
			} else {
				count = camel_stream_printf(stream, "%s%s%s\n", h->name, isspace(val[0]) ? ":" : ": ", val);
			}
			if (count == -1)
				return -1;
			total += count;
			h = h->next;
		}
	}

	count = camel_stream_write(stream, "\n", 1);
	if (count == -1)
		return -1;
	total += count;

	content = camel_medium_get_content_object(medium);
	if (content) {
		/* I dont really like this here, but i dont know where else it might go ... */
#define CAN_THIS_GO_ELSEWHERE
#ifdef CAN_THIS_GO_ELSEWHERE
		CamelMimeFilter *filter = NULL;
		CamelStreamFilter *filter_stream = NULL;
		CamelMimeFilter *charenc = NULL;
		const char *charset;

		switch(mp->encoding) {
		case CAMEL_MIME_PART_ENCODING_BASE64:
			filter = (CamelMimeFilter *)camel_mime_filter_basic_new_type(CAMEL_MIME_FILTER_BASIC_BASE64_ENC);
			break;
		case CAMEL_MIME_PART_ENCODING_QUOTEDPRINTABLE:
			filter = (CamelMimeFilter *)camel_mime_filter_basic_new_type(CAMEL_MIME_FILTER_BASIC_QP_ENC);
			break;
		default:
			break;
		}

		if (header_content_type_is(mp->content_type, "text", "*")) {
			charset = header_content_type_param(mp->content_type, "charset");
			if (!(charset == NULL || !strcasecmp(charset, "us-ascii") || !strcasecmp(charset, "utf-8"))) {
				charenc = (CamelMimeFilter *)camel_mime_filter_charset_new_convert("utf-8", charset);
			} 
		}

		if (filter || charenc) {
			filter_stream = camel_stream_filter_new_with_stream(stream);

			/* if we have a character encoder, add that always */
			if (charenc) {
				camel_stream_filter_add(filter_stream, charenc);
				camel_object_unref((CamelObject *)charenc);
			}

			/* we only re-do crlf on encoded blocks */
			if (filter && header_content_type_is(mp->content_type, "text", "*")) {
				CamelMimeFilter *crlf = camel_mime_filter_crlf_new(CAMEL_MIME_FILTER_CRLF_ENCODE,
										   CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);

				camel_stream_filter_add(filter_stream, crlf);
				camel_object_unref((CamelObject *)crlf);

			}

			if (filter) {
				camel_stream_filter_add(filter_stream, filter);
				camel_object_unref((CamelObject *)filter);
			}

			stream = (CamelStream *)filter_stream;
		}

#endif
		count = camel_data_wrapper_write_to_stream(content, stream);
		if (filter_stream) {
			camel_stream_flush((CamelStream *)filter_stream);
			camel_object_unref((CamelObject *)filter_stream);
		}
		if (count == -1)
			return -1;
		total += count;
	} else {
		g_warning("No content for medium, nothing to write");
	}
	return total;
}

/* mime_part */
static int
construct_from_parser(CamelMimePart *dw, CamelMimeParser *mp)
{
	struct _header_raw *headers;
	char *buf;
	int len;

	d(printf("mime_part::construct_from_parser()\n"));

	switch (camel_mime_parser_step(mp, &buf, &len)) {
	case HSCAN_MESSAGE:
		/* set the default type of a message always */
		if (dw->content_type)
			header_content_type_unref (dw->content_type);
		dw->content_type = header_content_type_decode ("message/rfc822");
	case HSCAN_HEADER:
	case HSCAN_MULTIPART:
		/* we have the headers, build them into 'us' */
		headers = camel_mime_parser_headers_raw(mp);
		while (headers) {
			camel_medium_add_header((CamelMedium *)dw, headers->name, headers->value);
			headers = headers->next;
		}
		camel_mime_part_construct_content_from_parser(dw, mp);
		break;
	default:
		g_warning("Invalid state encountered???: %d", camel_mime_parser_state(mp));
	}

	d(printf("mime_part::construct_from_parser() leaving\n"));
#ifndef NO_WARNINGS
#warning "Need to work out how to detect a (fatally) bad parse in the parser"
#endif
	return 0;
}

/**
 * camel_mime_part_construct_from_parser:
 * @mime_part: 
 * @mp: 
 * 
 * 
 * 
 * Return value: 
 **/
int
camel_mime_part_construct_from_parser(CamelMimePart *mime_part, CamelMimeParser *mp)
{
	return CMP_CLASS (mime_part)->construct_from_parser (mime_part, mp);
}

static int
construct_from_stream(CamelDataWrapper *dw, CamelStream *s)
{
	CamelMimeParser *mp;
	int ret;

	d(printf("mime_part::construct_from_stream()\n"));

	mp = camel_mime_parser_new();
	if (camel_mime_parser_init_with_stream(mp, s) == -1) {
		g_warning("Cannot create parser for stream");
		ret = -1;
	} else {
		ret = camel_mime_part_construct_from_parser((CamelMimePart *)dw, mp);
	}
	camel_object_unref((CamelObject *)mp);
	return ret;
}

/* this must be kept in sync with the header */
static const char *encodings[] = {
	"",
	"7bit",
	"8bit",
	"base64",
	"quoted-printable",
	"binary"
};

const char *
camel_mime_part_encoding_to_string (CamelMimePartEncodingType encoding)
{
	if (encoding >= sizeof(encodings)/sizeof(encodings[0]))
		encoding = 0;

	return encodings[encoding];
}

/* FIXME I am not sure this is the correct way to do this.  */
CamelMimePartEncodingType
camel_mime_part_encoding_from_string (const gchar *string)
{
	int i;

	if (string != NULL) {
		for (i=0;i<sizeof(encodings)/sizeof(encodings[0]);i++)
			if (!strcasecmp(string, encodings[i]))
				return i;
	}

	return CAMEL_MIME_PART_ENCODING_DEFAULT;
}


/******************************/
/**  Misc utility functions  **/

/**
 * camel_mime_part_new:
 *
 * Return value: a new CamelMimePart
 **/
CamelMimePart *
camel_mime_part_new (void)
{
	return (CamelMimePart *)camel_object_new (CAMEL_MIME_PART_TYPE);
}

/**
 * camel_mime_part_set_content:
 * @camel_mime_part: Mime part
 * @data: data to put into the part
 * @length: length of @data
 * @type: Content-Type of the data
 * 
 * Utility function used to set the content of a mime part object to 
 * be the provided data. If @length is 0, this routine can be used as
 * a way to remove old content (in which case @data and @type are
 * ignored and may be %NULL).
 **/
void 
camel_mime_part_set_content (CamelMimePart *camel_mime_part,
			     const char *data, int length,
			     const char *type) /* why on earth is the type last? */
{
	CamelMedium *medium = CAMEL_MEDIUM (camel_mime_part);

	if (length) {
		CamelDataWrapper *dw;
		CamelStream *stream;

		dw = camel_data_wrapper_new ();
		camel_data_wrapper_set_mime_type (dw, type);
		stream = camel_stream_mem_new_with_buffer (data, length);
		camel_data_wrapper_construct_from_stream (dw, stream);
		camel_object_unref (CAMEL_OBJECT (stream));
		camel_medium_set_content_object (medium, dw);
		camel_object_unref (CAMEL_OBJECT (dw));
	} else {
		if (medium->content)
			camel_object_unref (CAMEL_OBJECT (medium->content));
		medium->content = NULL;
	}
}

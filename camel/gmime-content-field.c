/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mime-content_field.c : mime content type field utilities  */

/* 
 *
 * Copyright (C) 1999 Bertrand Guiheneuf <Bertrand.Guiheneuf@inria.fr> .
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
#include "gmime-content-field.h"
/* #include "-util.h" */
#include "camel-log.h"

/

/**
 * gmime_content_field_new: Creates a new GMimeContentField object
 * @type: mime type
 * @subtype: mime subtype 
 * 
 * Creates a GMimeContentField object and initialize it with
 * a mime type and a mime subtype. For example, 
 * gmime_content_field_new ("application", "postcript");
 * will create a content field with complete mime type 
 * "application/postscript"
 * 
 * Return value: The newly created GMimeContentField object
 **/
void
gmime_content_field_new (const gchar *type, const gchar *subtype)
{
	GMimeContentField *ctf;

	ctf = g_new (GMimeContentField, 1);
	ctf->type = type;
	ctf->subtype = subtype;
	ctf->parameters =  g_hash_table_new (g_str_hash, g_str_equal);
	
	return ctf;
} 


/**
 * gmime_content_field_set_parameter: set a parameter for a GMimeContentField object
 * @content_field: content field
 * @attribute: parameter name 
 * @value: paramteter value
 * 
 * set a parameter (called attribute in RFC 2045) of a content field. Meaningfull
 * or valid parameters name depend on the content type object. For example, 
 * gmime_content_field_set_parameter (cf, "charset", "us-ascii");
 * will make sense for a "text/plain" content field but not for a 
 * "image/gif". This routine does not check parameter validuty.
 **/
void 
gmime_content_field_set_parameter (GMimeContentField *content_field, const gchar *attribute, const gchar *value)
{
	gboolean attribute_exists;
	gchar *old_attribute;
	gchar *old_value;

	attribute_exists = g_hash_table_lookup_extended (content_field->parameters, 
							 attribute, 
							 (gpointer *) &old_attribute,
							 (gpointer *) &old_value);
	/** CHECK THAT **/
	if (attribute_exists) {
		g_string_free (old_value, TRUE);
		g_string_free (old_attribute, TRUE);
	} 
		
	g_hash_table_insert (content_field->parameters, attribute, value);
}


/**
 * _print_parameter: print a parameter/value pair to a stream as 
 * described in RFC 2045
 * @name: name of the parameter
 * @value: value of the parameter
 * @user_data: CamelStream object to write the text to.
 * 
 * 
 **/
static void
_print_parameter (gpointer name, gpointer value, gpointer user_data)
{
	CamelStream *stream = (CamelStream *)user_data;
	
	camel_stream_write_strings (stream, 
				    "; \n    ", 
				    (gchar *)name, 
				    "=", 
				    (gchar *)value,
				    NULL);
	
}

/**
 * gmime_content_field_write_to: write a mime content type to a stream
 * @content_field: content type object
 * @stream: the stream
 * 
 * 
 **/
void
gmime_content_field_write_to_stream (GMimeContentField *content_field, CamelStream *stream)
{
	if (!content_field) return;
	if ((content_field->type) && ((content_field->type)->str)) {
		camel_stream_write_strings (stream, "Content-Type: ", content_field->type, NULL);
		if (content_field->subtype) {
			camel_stream_write_strings (stream, "/", content_field->subtype, NULL);
		}
		/* print all parameters */
		g_hash_table_foreach (content_field->parameters, _print_parameter, stream);
		camel_stream_write_string (stream, "\n");
	}
}

gchar * 
gmime_content_field_get_mime_type (GMimeContentField *content_field)
{
	gchar *mime_type;

	if (!content_field->type) return NULL;
	mime_type = g_strdup (content_field->type);
	if (content_field->subtype) {
		g_string_append_c (mime_type, '/');
		g_string_append_g_string (mime_type, content_field->subtype);
	}
	return mime_type;
}


void
gmime_content_field_construct_from_string (GMimeContentField *content_field, gchar *string)
{
	gint first, len;
	gchar *str;
	gint i=0;
	gchar *type, *subtype;
	gchar *param_name, *param_value;
	gboolean param_end;
	
	CAMEL_LOG (TRACE, "Entering gmime_content_field_construct_from_string\n");
	g_assert (string);
	g_assert (string->str);
	g_assert (content_field);
 
	if (content_field->type) {
		CAMEL_LOG (FULL_DEBUG, "Freeing old mime type string\n");
		g_string_free (content_field->type, FALSE);
	}
	if (content_field->subtype) {
		CAMEL_LOG (FULL_DEBUG, "Freeing old mime type substring\n");
		g_string_free (content_field->subtype, FALSE);
	}
	
	str = string->str;
	first = 0;
	len = string->len;
	if (!len) return;
	CAMEL_LOG (TRACE, "All checks done\n");

	/* find the type */
	while ( (i<len) && (!strchr ("/;", str[i])) ) i++;
	
	if (i == 0) return;
	
	type = g_string_new (g_strndup (str, i));
	content_field->type = type;
	CAMEL_LOG (TRACE, "Found mime type : %s\n", type->str); 
	if (i == len) {
		content_field->subtype = NULL;
		return;
	}
	
	first = i+1;
	/* find the subtype, if any */
	if (str[i++] == '/') {
		while ( (i<len) && (str[i] != ';') ) i++;
		if (i != first) {
			subtype = g_string_new (g_strndup (str+first, i-first));
			content_field->subtype = subtype;
			if (i == len) return;
		}
 	}
	first = i+1;

	/* parse parameters list */
	param_end = FALSE;
	do {
		while ( (i<len) && (str[i] != '=') ) i++;
		if ((i == len) || (i==first)) param_end = TRUE;
		else {
			/* we have found parameter name */
			param_name = g_string_new (g_strndup (str+first, i-first));
			i++;
			first = i;
			/* Let's find parameter value */
			while ( (i<len) && (str[i] != ';') ) i++;
			if (i != first) param_value = g_string_new (g_strndup (str+first, i-first));
			else param_value = g_string_new ("");
			gmime_content_field_set_parameter (content_field, param_name, param_value);
			i++;
			first = i;
		}
	} while ((!param_end) && (first < len));


}


static void
_free_parameter (gpointer name, gpointer value, gpointer user_data)
{
	g_string_free (name, FALSE);
	g_string_free (value, FALSE);
}

void 
gmime_content_field_free (GMimeContentField *content_field)
{
	g_hash_table_foreach (content_field->parameters, _free_parameter, NULL);
	g_string_free (content_field->type, FALSE);
	g_string_free (content_field->subtype, FALSE);
	g_hash_table_destroy (content_field->parameters);
	g_free (content_field);
}

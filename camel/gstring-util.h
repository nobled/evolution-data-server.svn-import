/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* gstring-util : utilities for gstring object  */

/* 
 *
 * Author : 
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
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



#ifndef GSTRING_UTIL_H
#define GSTRING_UTIL_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <glib.h>

typedef enum {
    GSTRING_TRIM_NONE            =     0,
    GSTRING_TRIM_STRIP_TRAILING  =     1,
    GSTRING_TRIM_STRIP_LEADING   =     2
} GStringTrimOption;


gboolean g_string_equals          (GString *string1, GString *string2);
GString *g_string_clone           (GString *string);
void     g_string_append_g_string (GString *dest_string,
				   GString *other_string);

gboolean g_string_equal_for_hash  (gconstpointer v, gconstpointer v2);
gboolean g_string_equal_for_glist (gconstpointer v, gconstpointer v2);
guint    g_string_hash            (gconstpointer v);
void     g_string_list_free       (GList *string_list);

GList   *g_string_split           (GString *string, char sep,
				   gchar *trim_chars, GStringTrimOption trim_options);
void     g_string_trim            (GString *string, gchar *chars,
				   GStringTrimOption options);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* GSTRING_UTIL_H */

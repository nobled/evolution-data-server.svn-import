/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003 Novell Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <glib/gfileutils.h>
#include <glib/gmem.h>
#include <glib/gmessages.h>
#include <glib/gstrfuncs.h>
#include <glib/gunicode.h>
#include <glib/gutils.h>
#include <glib/galloca.h>
#include <glib/gconvert.h>
#include "e-util.h"

/**
 * e_util_mkdir_hier:
 * @path: The directory hierarchy to create.
 * @mode: The permissions to use for the directories.
 *
 * Creates a directory hierarchy based on the string @path. If @path
 * is prefixed by a '/', the directories will be created relative to
 * the root of the file system; otherwise, the directories will be
 * created relative to the current directory.
 *
 * Returns: 0 on success; -1 on failure.
 **/
int
e_util_mkdir_hier (const char *path, mode_t mode)
{
        char *copy, *p;
                                                                                
        if (path[0] == '/') {
                p = copy = g_strdup (path);
        } else {
                gchar *current_dir = g_get_current_dir();
                p = copy = g_build_filename (current_dir, path, NULL);
		g_free (current_dir);
        }
                                                                                
        do {
                p = strchr (p + 1, '/');
                if (p)
                        *p = '\0';
                if (access (copy, F_OK) == -1) {
                        if (mkdir (copy, mode) == -1) {
                                g_free (copy);
                                return -1;
                        }
               }
                if (p)
                        *p = '/';
        } while (p);
                                                                                
        g_free (copy);
        return 0;
}

/**
 * e_util_strstrcase:
 * @haystack: The string to search in.
 * @needle: The string to search for.
 *
 * Find the first instance of @needle in @haystack, ignoring case.
 *
 * Returns: A pointer to the start of @needle in @haystack, or NULL if
 *          @needle is not found.
 **/
gchar *
e_util_strstrcase (const gchar *haystack, const gchar *needle)
{
        /* find the needle in the haystack neglecting case */
        const gchar *ptr;
        guint len;
                                                                                
        g_return_val_if_fail (haystack != NULL, NULL);
        g_return_val_if_fail (needle != NULL, NULL);
                                                                                
        len = strlen(needle);
        if (len > strlen(haystack))
                return NULL;
                                                                                
        if (len == 0)
                return (gchar *) haystack;
                                                                                
        for (ptr = haystack; *(ptr + len - 1) != '\0'; ptr++)
                if (!g_ascii_strncasecmp (ptr, needle, len))
                        return (gchar *) ptr;
                                                                                
        return NULL;
}

/** 
 * e_util_unicode_get_utf8:
 * @text: The string to take the UTF-8 character from.
 * @out: The location to store the UTF-8 character in.
 * 
 * Get a UTF-8 character from the beginning of @text.
 *
 * Returns: A pointer to the next character in @text after @out. 
 **/
gchar *
e_util_unicode_get_utf8 (const gchar *text, gunichar *out)
{
        *out = g_utf8_get_char (text);
        return (*out == (gunichar)-1) ? NULL : g_utf8_next_char (text);
}

/** 
 * e_util_utf8_strstrcase:
 * @haystack: The string to search in.
 * @needle: The string to search for.
 * 
 * Find the first instance of @needle in @haystack, ignoring
 * case. Both @needle and @haystack are UTF-8 strings.
 *
 * Returns: A pointer to the first instance of @needle in @haystack, or
 *          %NULL if either of the strings are not legal UTF-8 strings.
 **/
const gchar *
e_util_utf8_strstrcase (const gchar *haystack, const gchar *needle)
{
        gunichar *nuni;
        gunichar unival;
        gint nlen;
        const guchar *o, *p;
                                                                                
        if (haystack == NULL) return NULL;
        if (needle == NULL) return NULL;
        if (strlen (needle) == 0) return haystack;
        if (strlen (haystack) == 0) return NULL;
                                                                                
        nuni = g_alloca (sizeof (gunichar) * strlen (needle));
                                                                                
        nlen = 0;
        for (p = e_util_unicode_get_utf8 (needle, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                nuni[nlen++] = g_unichar_tolower (unival);
        }
        /* NULL means there was illegal utf-8 sequence */
        if (!p) return NULL;

	o = haystack;
        for (p = e_util_unicode_get_utf8 (o, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                gint sc;
                sc = g_unichar_tolower (unival);
                /* We have valid stripped char */
                if (sc == nuni[0]) {
                        const gchar *q = p;
                        gint npos = 1;
                        while (npos < nlen) {
                                q = e_util_unicode_get_utf8 (q, &unival);
                                if (!q || !unival) return NULL;
                                sc = g_unichar_tolower (unival);
                                if (sc != nuni[npos]) break;
                                npos++;
                        }
                        if (npos == nlen) {
                                return o;
                        }
                }
                o = p;
        }
                                                                                
        return NULL;
}

#define MAX_DECOMP 8

typedef struct {
	unsigned short ch;
	unsigned char *expansion;
} decomposition;

static decomposition decomp_table[] = {
	{ 0x00c0, "\x00\x41\x03\x00\0" },
	{ 0x00c1, "\x00\x41\x03\x01\0" },
	{ 0x00c2, "\x00\x41\x03\x02\0" },
	{ 0x00c3, "\x00\x41\x03\x03\0" },
	{ 0x00c4, "\x00\x41\x03\x08\0" },
	{ 0x00c5, "\x00\x41\x03\x0a\0" },
	{ 0x00c7, "\x00\x43\x03\x27\0" },
	{ 0x00c8, "\x00\x45\x03\x00\0" },
	{ 0x00c9, "\x00\x45\x03\x01\0" },
	{ 0x00ca, "\x00\x45\x03\x02\0" },
	{ 0x00cb, "\x00\x45\x03\x08\0" },
	{ 0x00cc, "\x00\x49\x03\x00\0" },
	{ 0x00cd, "\x00\x49\x03\x01\0" },
	{ 0x00ce, "\x00\x49\x03\x02\0" },
	{ 0x00cf, "\x00\x49\x03\x08\0" },
	{ 0x00d1, "\x00\x4e\x03\x03\0" },
	{ 0x00d2, "\x00\x4f\x03\x00\0" },
	{ 0x00d3, "\x00\x4f\x03\x01\0" },
	{ 0x00d4, "\x00\x4f\x03\x02\0" },
	{ 0x00d5, "\x00\x4f\x03\x03\0" },
	{ 0x00d6, "\x00\x4f\x03\x08\0" },
	{ 0x00d9, "\x00\x55\x03\x00\0" },
	{ 0x00da, "\x00\x55\x03\x01\0" },
	{ 0x00db, "\x00\x55\x03\x02\0" },
	{ 0x00dc, "\x00\x55\x03\x08\0" },
	{ 0x00dd, "\x00\x59\x03\x01\0" },
	{ 0x00e0, "\x00\x61\x03\x00\0" },
	{ 0x00e1, "\x00\x61\x03\x01\0" },
	{ 0x00e2, "\x00\x61\x03\x02\0" },
	{ 0x00e3, "\x00\x61\x03\x03\0" },
	{ 0x00e4, "\x00\x61\x03\x08\0" },
	{ 0x00e5, "\x00\x61\x03\x0a\0" },
	{ 0x00e7, "\x00\x63\x03\x27\0" },
	{ 0x00e8, "\x00\x65\x03\x00\0" },
	{ 0x00e9, "\x00\x65\x03\x01\0" },
	{ 0x00ea, "\x00\x65\x03\x02\0" },
	{ 0x00eb, "\x00\x65\x03\x08\0" },
	{ 0x00ec, "\x00\x69\x03\x00\0" },
	{ 0x00ed, "\x00\x69\x03\x01\0" },
	{ 0x00ee, "\x00\x69\x03\x02\0" },
	{ 0x00ef, "\x00\x69\x03\x08\0" },
	{ 0x00f1, "\x00\x6e\x03\x03\0" },
	{ 0x00f2, "\x00\x6f\x03\x00\0" },
	{ 0x00f3, "\x00\x6f\x03\x01\0" },
	{ 0x00f4, "\x00\x6f\x03\x02\0" },
	{ 0x00f5, "\x00\x6f\x03\x03\0" },
	{ 0x00f6, "\x00\x6f\x03\x08\0" },
	{ 0x00f9, "\x00\x75\x03\x00\0" },
	{ 0x00fa, "\x00\x75\x03\x01\0" },
	{ 0x00fb, "\x00\x75\x03\x02\0" },
	{ 0x00fc, "\x00\x75\x03\x08\0" },
	{ 0x00fd, "\x00\x79\x03\x01\0" },
	{ 0x00ff, "\x00\x79\x03\x08\0" },
	{ 0x0100, "\x00\x41\x03\x04\0" },
	{ 0x0101, "\x00\x61\x03\x04\0" },
	{ 0x0102, "\x00\x41\x03\x06\0" },
	{ 0x0103, "\x00\x61\x03\x06\0" },
	{ 0x0104, "\x00\x41\x03\x28\0" },
	{ 0x0105, "\x00\x61\x03\x28\0" },
	{ 0x0106, "\x00\x43\x03\x01\0" },
	{ 0x0107, "\x00\x63\x03\x01\0" },
	{ 0x0108, "\x00\x43\x03\x02\0" },
	{ 0x0109, "\x00\x63\x03\x02\0" },
	{ 0x010a, "\x00\x43\x03\x07\0" },
	{ 0x010b, "\x00\x63\x03\x07\0" },
	{ 0x010c, "\x00\x43\x03\x0c\0" },
	{ 0x010d, "\x00\x63\x03\x0c\0" },
	{ 0x010e, "\x00\x44\x03\x0c\0" },
	{ 0x010f, "\x00\x64\x03\x0c\0" },
	{ 0x0112, "\x00\x45\x03\x04\0" },
	{ 0x0113, "\x00\x65\x03\x04\0" },
	{ 0x0114, "\x00\x45\x03\x06\0" },
	{ 0x0115, "\x00\x65\x03\x06\0" },
	{ 0x0116, "\x00\x45\x03\x07\0" },
	{ 0x0117, "\x00\x65\x03\x07\0" },
	{ 0x0118, "\x00\x45\x03\x28\0" },
	{ 0x0119, "\x00\x65\x03\x28\0" },
	{ 0x011a, "\x00\x45\x03\x0c\0" },
	{ 0x011b, "\x00\x65\x03\x0c\0" },
	{ 0x011c, "\x00\x47\x03\x02\0" },
	{ 0x011d, "\x00\x67\x03\x02\0" },
	{ 0x011e, "\x00\x47\x03\x06\0" },
	{ 0x011f, "\x00\x67\x03\x06\0" },
	{ 0x0120, "\x00\x47\x03\x07\0" },
	{ 0x0121, "\x00\x67\x03\x07\0" },
	{ 0x0122, "\x00\x47\x03\x27\0" },
	{ 0x0123, "\x00\x67\x03\x27\0" },
	{ 0x0124, "\x00\x48\x03\x02\0" },
	{ 0x0125, "\x00\x68\x03\x02\0" },
	{ 0x0128, "\x00\x49\x03\x03\0" },
	{ 0x0129, "\x00\x69\x03\x03\0" },
	{ 0x012a, "\x00\x49\x03\x04\0" },
	{ 0x012b, "\x00\x69\x03\x04\0" },
	{ 0x012c, "\x00\x49\x03\x06\0" },
	{ 0x012d, "\x00\x69\x03\x06\0" },
	{ 0x012e, "\x00\x49\x03\x28\0" },
	{ 0x012f, "\x00\x69\x03\x28\0" },
	{ 0x0130, "\x00\x49\x03\x07\0" },
	{ 0x0134, "\x00\x4a\x03\x02\0" },
	{ 0x0135, "\x00\x6a\x03\x02\0" },
	{ 0x0136, "\x00\x4b\x03\x27\0" },
	{ 0x0137, "\x00\x6b\x03\x27\0" },
	{ 0x0139, "\x00\x4c\x03\x01\0" },
	{ 0x013a, "\x00\x6c\x03\x01\0" },
	{ 0x013b, "\x00\x4c\x03\x27\0" },
	{ 0x013c, "\x00\x6c\x03\x27\0" },
	{ 0x013d, "\x00\x4c\x03\x0c\0" },
	{ 0x013e, "\x00\x6c\x03\x0c\0" },
	{ 0x0143, "\x00\x4e\x03\x01\0" },
	{ 0x0144, "\x00\x6e\x03\x01\0" },
	{ 0x0145, "\x00\x4e\x03\x27\0" },
	{ 0x0146, "\x00\x6e\x03\x27\0" },
	{ 0x0147, "\x00\x4e\x03\x0c\0" },
	{ 0x0148, "\x00\x6e\x03\x0c\0" },
	{ 0x014c, "\x00\x4f\x03\x04\0" },
	{ 0x014d, "\x00\x6f\x03\x04\0" },
	{ 0x014e, "\x00\x4f\x03\x06\0" },
	{ 0x014f, "\x00\x6f\x03\x06\0" },
	{ 0x0150, "\x00\x4f\x03\x0b\0" },
	{ 0x0151, "\x00\x6f\x03\x0b\0" },
	{ 0x0154, "\x00\x52\x03\x01\0" },
	{ 0x0155, "\x00\x72\x03\x01\0" },
	{ 0x0156, "\x00\x52\x03\x27\0" },
	{ 0x0157, "\x00\x72\x03\x27\0" },
	{ 0x0158, "\x00\x52\x03\x0c\0" },
	{ 0x0159, "\x00\x72\x03\x0c\0" },
	{ 0x015a, "\x00\x53\x03\x01\0" },
	{ 0x015b, "\x00\x73\x03\x01\0" },
	{ 0x015c, "\x00\x53\x03\x02\0" },
	{ 0x015d, "\x00\x73\x03\x02\0" },
	{ 0x015e, "\x00\x53\x03\x27\0" },
	{ 0x015f, "\x00\x73\x03\x27\0" },
	{ 0x0160, "\x00\x53\x03\x0c\0" },
	{ 0x0161, "\x00\x73\x03\x0c\0" },
	{ 0x0162, "\x00\x54\x03\x27\0" },
	{ 0x0163, "\x00\x74\x03\x27\0" },
	{ 0x0164, "\x00\x54\x03\x0c\0" },
	{ 0x0165, "\x00\x74\x03\x0c\0" },
	{ 0x0168, "\x00\x55\x03\x03\0" },
	{ 0x0169, "\x00\x75\x03\x03\0" },
	{ 0x016a, "\x00\x55\x03\x04\0" },
	{ 0x016b, "\x00\x75\x03\x04\0" },
	{ 0x016c, "\x00\x55\x03\x06\0" },
	{ 0x016d, "\x00\x75\x03\x06\0" },
	{ 0x016e, "\x00\x55\x03\x0a\0" },
	{ 0x016f, "\x00\x75\x03\x0a\0" },
	{ 0x0170, "\x00\x55\x03\x0b\0" },
	{ 0x0171, "\x00\x75\x03\x0b\0" },
	{ 0x0172, "\x00\x55\x03\x28\0" },
	{ 0x0173, "\x00\x75\x03\x28\0" },
	{ 0x0174, "\x00\x57\x03\x02\0" },
	{ 0x0175, "\x00\x77\x03\x02\0" },
	{ 0x0176, "\x00\x59\x03\x02\0" },
	{ 0x0177, "\x00\x79\x03\x02\0" },
	{ 0x0178, "\x00\x59\x03\x08\0" },
	{ 0x0179, "\x00\x5a\x03\x01\0" },
	{ 0x017a, "\x00\x7a\x03\x01\0" },
	{ 0x017b, "\x00\x5a\x03\x07\0" },
	{ 0x017c, "\x00\x7a\x03\x07\0" },
	{ 0x017d, "\x00\x5a\x03\x0c\0" },
	{ 0x017e, "\x00\x7a\x03\x0c\0" },
	{ 0x01a0, "\x00\x4f\x03\x1b\0" },
	{ 0x01a1, "\x00\x6f\x03\x1b\0" },
	{ 0x01af, "\x00\x55\x03\x1b\0" },
	{ 0x01b0, "\x00\x75\x03\x1b\0" },
	{ 0x01cd, "\x00\x41\x03\x0c\0" },
	{ 0x01ce, "\x00\x61\x03\x0c\0" },
	{ 0x01cf, "\x00\x49\x03\x0c\0" },
	{ 0x01d0, "\x00\x69\x03\x0c\0" },
	{ 0x01d1, "\x00\x4f\x03\x0c\0" },
	{ 0x01d2, "\x00\x6f\x03\x0c\0" },
	{ 0x01d3, "\x00\x55\x03\x0c\0" },
	{ 0x01d4, "\x00\x75\x03\x0c\0" },
	{ 0x01d5, "\x00\x55\x03\x08\x03\x04\0" },
	{ 0x01d6, "\x00\x75\x03\x08\x03\x04\0" },
	{ 0x01d7, "\x00\x55\x03\x08\x03\x01\0" },
	{ 0x01d8, "\x00\x75\x03\x08\x03\x01\0" },
	{ 0x01d9, "\x00\x55\x03\x08\x03\x0c\0" },
	{ 0x01da, "\x00\x75\x03\x08\x03\x0c\0" },
	{ 0x01db, "\x00\x55\x03\x08\x03\x00\0" },
	{ 0x01dc, "\x00\x75\x03\x08\x03\x00\0" },
	{ 0x01de, "\x00\x41\x03\x08\x03\x04\0" },
	{ 0x01df, "\x00\x61\x03\x08\x03\x04\0" },
	{ 0x01e0, "\x00\x41\x03\x07\x03\x04\0" },
	{ 0x01e1, "\x00\x61\x03\x07\x03\x04\0" },
	{ 0x01e2, "\x00\xc6\x03\x04\0" },
	{ 0x01e3, "\x00\xe6\x03\x04\0" },
	{ 0x01e6, "\x00\x47\x03\x0c\0" },
	{ 0x01e7, "\x00\x67\x03\x0c\0" },
	{ 0x01e8, "\x00\x4b\x03\x0c\0" },
	{ 0x01e9, "\x00\x6b\x03\x0c\0" },
	{ 0x01ea, "\x00\x4f\x03\x28\0" },
	{ 0x01eb, "\x00\x6f\x03\x28\0" },
	{ 0x01ec, "\x00\x4f\x03\x28\x03\x04\0" },
	{ 0x01ed, "\x00\x6f\x03\x28\x03\x04\0" },
	{ 0x01ee, "\x01\xb7\x03\x0c\0" },
	{ 0x01ef, "\x02\x92\x03\x0c\0" },
	{ 0x01f0, "\x00\x6a\x03\x0c\0" },
	{ 0x01f4, "\x00\x47\x03\x01\0" },
	{ 0x01f5, "\x00\x67\x03\x01\0" },
	{ 0x01fa, "\x00\x41\x03\x0a\x03\x01\0" },
	{ 0x01fb, "\x00\x61\x03\x0a\x03\x01\0" },
	{ 0x01fc, "\x00\xc6\x03\x01\0" },
	{ 0x01fd, "\x00\xe6\x03\x01\0" },
	{ 0x01fe, "\x00\xd8\x03\x01\0" },
	{ 0x01ff, "\x00\xf8\x03\x01\0" },
	{ 0x0200, "\x00\x41\x03\x0f\0" },
	{ 0x0201, "\x00\x61\x03\x0f\0" },
	{ 0x0202, "\x00\x41\x03\x11\0" },
	{ 0x0203, "\x00\x61\x03\x11\0" },
	{ 0x0204, "\x00\x45\x03\x0f\0" },
	{ 0x0205, "\x00\x65\x03\x0f\0" },
	{ 0x0206, "\x00\x45\x03\x11\0" },
	{ 0x0207, "\x00\x65\x03\x11\0" },
	{ 0x0208, "\x00\x49\x03\x0f\0" },
	{ 0x0209, "\x00\x69\x03\x0f\0" },
	{ 0x020a, "\x00\x49\x03\x11\0" },
	{ 0x020b, "\x00\x69\x03\x11\0" },
	{ 0x020c, "\x00\x4f\x03\x0f\0" },
	{ 0x020d, "\x00\x6f\x03\x0f\0" },
	{ 0x020e, "\x00\x4f\x03\x11\0" },
	{ 0x020f, "\x00\x6f\x03\x11\0" },
	{ 0x0210, "\x00\x52\x03\x0f\0" },
	{ 0x0211, "\x00\x72\x03\x0f\0" },
	{ 0x0212, "\x00\x52\x03\x11\0" },
	{ 0x0213, "\x00\x72\x03\x11\0" },
	{ 0x0214, "\x00\x55\x03\x0f\0" },
	{ 0x0215, "\x00\x75\x03\x0f\0" },
	{ 0x0216, "\x00\x55\x03\x11\0" },
	{ 0x0217, "\x00\x75\x03\x11\0" },
	{ 0x0340, "\x03\x00\0" },
	{ 0x0341, "\x03\x01\0" },
	{ 0x0343, "\x03\x13\0" },
	{ 0x0344, "\x03\x08\x03\x01\0" },
	{ 0x0374, "\x02\xb9\0" },
	{ 0x037e, "\x00\x3b\0" },
	{ 0x0385, "\x00\xa8\x03\x01\0" },
	{ 0x0386, "\x03\x91\x03\x01\0" },
	{ 0x0387, "\x00\xb7\0" },
	{ 0x0388, "\x03\x95\x03\x01\0" },
	{ 0x0389, "\x03\x97\x03\x01\0" },
	{ 0x038a, "\x03\x99\x03\x01\0" },
	{ 0x038c, "\x03\x9f\x03\x01\0" },
	{ 0x038e, "\x03\xa5\x03\x01\0" },
	{ 0x038f, "\x03\xa9\x03\x01\0" },
	{ 0x0390, "\x03\xb9\x03\x08\x03\x01\0" },
	{ 0x03aa, "\x03\x99\x03\x08\0" },
	{ 0x03ab, "\x03\xa5\x03\x08\0" },
	{ 0x03ac, "\x03\xb1\x03\x01\0" },
	{ 0x03ad, "\x03\xb5\x03\x01\0" },
	{ 0x03ae, "\x03\xb7\x03\x01\0" },
	{ 0x03af, "\x03\xb9\x03\x01\0" },
	{ 0x03b0, "\x03\xc5\x03\x08\x03\x01\0" },
	{ 0x03ca, "\x03\xb9\x03\x08\0" },
	{ 0x03cb, "\x03\xc5\x03\x08\0" },
	{ 0x03cc, "\x03\xbf\x03\x01\0" },
	{ 0x03cd, "\x03\xc5\x03\x01\0" },
	{ 0x03ce, "\x03\xc9\x03\x01\0" },
	{ 0x03d3, "\x03\xd2\x03\x01\0" },
	{ 0x03d4, "\x03\xd2\x03\x08\0" },
	{ 0x0401, "\x04\x15\x03\x08\0" },
	{ 0x0403, "\x04\x13\x03\x01\0" },
	{ 0x0407, "\x04\x06\x03\x08\0" },
	{ 0x040c, "\x04\x1a\x03\x01\0" },
	{ 0x040e, "\x04\x23\x03\x06\0" },
	{ 0x0419, "\x04\x18\x03\x06\0" },
	{ 0x0439, "\x04\x38\x03\x06\0" },
	{ 0x0451, "\x04\x35\x03\x08\0" },
	{ 0x0453, "\x04\x33\x03\x01\0" },
	{ 0x0457, "\x04\x56\x03\x08\0" },
	{ 0x045c, "\x04\x3a\x03\x01\0" },
	{ 0x045e, "\x04\x43\x03\x06\0" },
	{ 0x0476, "\x04\x74\x03\x0f\0" },
	{ 0x0477, "\x04\x75\x03\x0f\0" },
	{ 0x04c1, "\x04\x16\x03\x06\0" },
	{ 0x04c2, "\x04\x36\x03\x06\0" },
	{ 0x04d0, "\x04\x10\x03\x06\0" },
	{ 0x04d1, "\x04\x30\x03\x06\0" },
	{ 0x04d2, "\x04\x10\x03\x08\0" },
	{ 0x04d3, "\x04\x30\x03\x08\0" },
	{ 0x04d6, "\x04\x15\x03\x06\0" },
	{ 0x04d7, "\x04\x35\x03\x06\0" },
	{ 0x04da, "\x04\xd8\x03\x08\0" },
	{ 0x04db, "\x04\xd9\x03\x08\0" },
	{ 0x04dc, "\x04\x16\x03\x08\0" },
	{ 0x04dd, "\x04\x36\x03\x08\0" },
	{ 0x04de, "\x04\x17\x03\x08\0" },
	{ 0x04df, "\x04\x37\x03\x08\0" },
	{ 0x04e2, "\x04\x18\x03\x04\0" },
	{ 0x04e3, "\x04\x38\x03\x04\0" },
	{ 0x04e4, "\x04\x18\x03\x08\0" },
	{ 0x04e5, "\x04\x38\x03\x08\0" },
	{ 0x04e6, "\x04\x1e\x03\x08\0" },
	{ 0x04e7, "\x04\x3e\x03\x08\0" },
	{ 0x04ea, "\x04\xe8\x03\x08\0" },
	{ 0x04eb, "\x04\xe9\x03\x08\0" },
	{ 0x04ee, "\x04\x23\x03\x04\0" },
	{ 0x04ef, "\x04\x43\x03\x04\0" },
	{ 0x04f0, "\x04\x23\x03\x08\0" },
	{ 0x04f1, "\x04\x43\x03\x08\0" },
	{ 0x04f2, "\x04\x23\x03\x0b\0" },
	{ 0x04f3, "\x04\x43\x03\x0b\0" },
	{ 0x04f4, "\x04\x27\x03\x08\0" },
	{ 0x04f5, "\x04\x47\x03\x08\0" },
	{ 0x04f8, "\x04\x2b\x03\x08\0" },
	{ 0x04f9, "\x04\x4b\x03\x08\0" },
	{ 0x0929, "\x09\x28\x09\x3c\0" },
	{ 0x0931, "\x09\x30\x09\x3c\0" },
	{ 0x0934, "\x09\x33\x09\x3c\0" },
	{ 0x0958, "\x09\x15\x09\x3c\0" },
	{ 0x0959, "\x09\x16\x09\x3c\0" },
	{ 0x095a, "\x09\x17\x09\x3c\0" },
	{ 0x095b, "\x09\x1c\x09\x3c\0" },
	{ 0x095c, "\x09\x21\x09\x3c\0" },
	{ 0x095d, "\x09\x22\x09\x3c\0" },
	{ 0x095e, "\x09\x2b\x09\x3c\0" },
	{ 0x095f, "\x09\x2f\x09\x3c\0" },
	{ 0x09b0, "\x09\xac\x09\xbc\0" },
	{ 0x09cb, "\x09\xc7\x09\xbe\0" },
	{ 0x09cc, "\x09\xc7\x09\xd7\0" },
	{ 0x09dc, "\x09\xa1\x09\xbc\0" },
	{ 0x09dd, "\x09\xa2\x09\xbc\0" },
	{ 0x09df, "\x09\xaf\x09\xbc\0" },
	{ 0x0a59, "\x0a\x16\x0a\x3c\0" },
	{ 0x0a5a, "\x0a\x17\x0a\x3c\0" },
	{ 0x0a5b, "\x0a\x1c\x0a\x3c\0" },
	{ 0x0a5c, "\x0a\x21\x0a\x3c\0" },
	{ 0x0a5e, "\x0a\x2b\x0a\x3c\0" },
	{ 0x0b48, "\x0b\x47\x0b\x56\0" },
	{ 0x0b4b, "\x0b\x47\x0b\x3e\0" },
	{ 0x0b4c, "\x0b\x47\x0b\x57\0" },
	{ 0x0b5c, "\x0b\x21\x0b\x3c\0" },
	{ 0x0b5d, "\x0b\x22\x0b\x3c\0" },
	{ 0x0b5f, "\x0b\x2f\x0b\x3c\0" },
	{ 0x0b94, "\x0b\x92\x0b\xd7\0" },
	{ 0x0bca, "\x0b\xc6\x0b\xbe\0" },
	{ 0x0bcb, "\x0b\xc7\x0b\xbe\0" },
	{ 0x0bcc, "\x0b\xc6\x0b\xd7\0" },
	{ 0x0c48, "\x0c\x46\x0c\x56\0" },
	{ 0x0cc0, "\x0c\xbf\x0c\xd5\0" },
	{ 0x0cc7, "\x0c\xc6\x0c\xd5\0" },
	{ 0x0cc8, "\x0c\xc6\x0c\xd6\0" },
	{ 0x0cca, "\x0c\xc6\x0c\xc2\0" },
	{ 0x0ccb, "\x0c\xc6\x0c\xc2\x0c\xd5\0" },
	{ 0x0d4a, "\x0d\x46\x0d\x3e\0" },
	{ 0x0d4b, "\x0d\x47\x0d\x3e\0" },
	{ 0x0d4c, "\x0d\x46\x0d\x57\0" },
	{ 0x0e33, "\x0e\x4d\x0e\x32\0" },
	{ 0x0eb3, "\x0e\xcd\x0e\xb2\0" },
	{ 0x0f43, "\x0f\x42\x0f\xb7\0" },
	{ 0x0f4d, "\x0f\x4c\x0f\xb7\0" },
	{ 0x0f52, "\x0f\x51\x0f\xb7\0" },
	{ 0x0f57, "\x0f\x56\x0f\xb7\0" },
	{ 0x0f5c, "\x0f\x5b\x0f\xb7\0" },
	{ 0x0f69, "\x0f\x40\x0f\xb5\0" },
	{ 0x0f73, "\x0f\x71\x0f\x72\0" },
	{ 0x0f75, "\x0f\x71\x0f\x74\0" },
	{ 0x0f76, "\x0f\xb2\x0f\x80\0" },
	{ 0x0f78, "\x0f\xb3\x0f\x80\0" },
	{ 0x0f81, "\x0f\x71\x0f\x80\0" },
	{ 0x0f93, "\x0f\x92\x0f\xb7\0" },
	{ 0x0f9d, "\x0f\x9c\x0f\xb7\0" },
	{ 0x0fa2, "\x0f\xa1\x0f\xb7\0" },
	{ 0x0fa7, "\x0f\xa6\x0f\xb7\0" },
	{ 0x0fac, "\x0f\xab\x0f\xb7\0" },
	{ 0x0fb9, "\x0f\x90\x0f\xb5\0" },
	{ 0x1e00, "\x00\x41\x03\x25\0" },
	{ 0x1e01, "\x00\x61\x03\x25\0" },
	{ 0x1e02, "\x00\x42\x03\x07\0" },
	{ 0x1e03, "\x00\x62\x03\x07\0" },
	{ 0x1e04, "\x00\x42\x03\x23\0" },
	{ 0x1e05, "\x00\x62\x03\x23\0" },
	{ 0x1e06, "\x00\x42\x03\x31\0" },
	{ 0x1e07, "\x00\x62\x03\x31\0" },
	{ 0x1e08, "\x00\x43\x03\x27\x03\x01\0" },
	{ 0x1e09, "\x00\x63\x03\x27\x03\x01\0" },
	{ 0x1e0a, "\x00\x44\x03\x07\0" },
	{ 0x1e0b, "\x00\x64\x03\x07\0" },
	{ 0x1e0c, "\x00\x44\x03\x23\0" },
	{ 0x1e0d, "\x00\x64\x03\x23\0" },
	{ 0x1e0e, "\x00\x44\x03\x31\0" },
	{ 0x1e0f, "\x00\x64\x03\x31\0" },
	{ 0x1e10, "\x00\x44\x03\x27\0" },
	{ 0x1e11, "\x00\x64\x03\x27\0" },
	{ 0x1e12, "\x00\x44\x03\x2d\0" },
	{ 0x1e13, "\x00\x64\x03\x2d\0" },
	{ 0x1e14, "\x00\x45\x03\x04\x03\x00\0" },
	{ 0x1e15, "\x00\x65\x03\x04\x03\x00\0" },
	{ 0x1e16, "\x00\x45\x03\x04\x03\x01\0" },
	{ 0x1e17, "\x00\x65\x03\x04\x03\x01\0" },
	{ 0x1e18, "\x00\x45\x03\x2d\0" },
	{ 0x1e19, "\x00\x65\x03\x2d\0" },
	{ 0x1e1a, "\x00\x45\x03\x30\0" },
	{ 0x1e1b, "\x00\x65\x03\x30\0" },
	{ 0x1e1c, "\x00\x45\x03\x27\x03\x06\0" },
	{ 0x1e1d, "\x00\x65\x03\x27\x03\x06\0" },
	{ 0x1e1e, "\x00\x46\x03\x07\0" },
	{ 0x1e1f, "\x00\x66\x03\x07\0" },
	{ 0x1e20, "\x00\x47\x03\x04\0" },
	{ 0x1e21, "\x00\x67\x03\x04\0" },
	{ 0x1e22, "\x00\x48\x03\x07\0" },
	{ 0x1e23, "\x00\x68\x03\x07\0" },
	{ 0x1e24, "\x00\x48\x03\x23\0" },
	{ 0x1e25, "\x00\x68\x03\x23\0" },
	{ 0x1e26, "\x00\x48\x03\x08\0" },
	{ 0x1e27, "\x00\x68\x03\x08\0" },
	{ 0x1e28, "\x00\x48\x03\x27\0" },
	{ 0x1e29, "\x00\x68\x03\x27\0" },
	{ 0x1e2a, "\x00\x48\x03\x2e\0" },
	{ 0x1e2b, "\x00\x68\x03\x2e\0" },
	{ 0x1e2c, "\x00\x49\x03\x30\0" },
	{ 0x1e2d, "\x00\x69\x03\x30\0" },
	{ 0x1e2e, "\x00\x49\x03\x08\x03\x01\0" },
	{ 0x1e2f, "\x00\x69\x03\x08\x03\x01\0" },
	{ 0x1e30, "\x00\x4b\x03\x01\0" },
	{ 0x1e31, "\x00\x6b\x03\x01\0" },
	{ 0x1e32, "\x00\x4b\x03\x23\0" },
	{ 0x1e33, "\x00\x6b\x03\x23\0" },
	{ 0x1e34, "\x00\x4b\x03\x31\0" },
	{ 0x1e35, "\x00\x6b\x03\x31\0" },
	{ 0x1e36, "\x00\x4c\x03\x23\0" },
	{ 0x1e37, "\x00\x6c\x03\x23\0" },
	{ 0x1e38, "\x00\x4c\x03\x23\x03\x04\0" },
	{ 0x1e39, "\x00\x6c\x03\x23\x03\x04\0" },
	{ 0x1e3a, "\x00\x4c\x03\x31\0" },
	{ 0x1e3b, "\x00\x6c\x03\x31\0" },
	{ 0x1e3c, "\x00\x4c\x03\x2d\0" },
	{ 0x1e3d, "\x00\x6c\x03\x2d\0" },
	{ 0x1e3e, "\x00\x4d\x03\x01\0" },
	{ 0x1e3f, "\x00\x6d\x03\x01\0" },
	{ 0x1e40, "\x00\x4d\x03\x07\0" },
	{ 0x1e41, "\x00\x6d\x03\x07\0" },
	{ 0x1e42, "\x00\x4d\x03\x23\0" },
	{ 0x1e43, "\x00\x6d\x03\x23\0" },
	{ 0x1e44, "\x00\x4e\x03\x07\0" },
	{ 0x1e45, "\x00\x6e\x03\x07\0" },
	{ 0x1e46, "\x00\x4e\x03\x23\0" },
	{ 0x1e47, "\x00\x6e\x03\x23\0" },
	{ 0x1e48, "\x00\x4e\x03\x31\0" },
	{ 0x1e49, "\x00\x6e\x03\x31\0" },
	{ 0x1e4a, "\x00\x4e\x03\x2d\0" },
	{ 0x1e4b, "\x00\x6e\x03\x2d\0" },
	{ 0x1e4c, "\x00\x4f\x03\x03\x03\x01\0" },
	{ 0x1e4d, "\x00\x6f\x03\x03\x03\x01\0" },
	{ 0x1e4e, "\x00\x4f\x03\x03\x03\x08\0" },
	{ 0x1e4f, "\x00\x6f\x03\x03\x03\x08\0" },
	{ 0x1e50, "\x00\x4f\x03\x04\x03\x00\0" },
	{ 0x1e51, "\x00\x6f\x03\x04\x03\x00\0" },
	{ 0x1e52, "\x00\x4f\x03\x04\x03\x01\0" },
	{ 0x1e53, "\x00\x6f\x03\x04\x03\x01\0" },
	{ 0x1e54, "\x00\x50\x03\x01\0" },
	{ 0x1e55, "\x00\x70\x03\x01\0" },
	{ 0x1e56, "\x00\x50\x03\x07\0" },
	{ 0x1e57, "\x00\x70\x03\x07\0" },
	{ 0x1e58, "\x00\x52\x03\x07\0" },
	{ 0x1e59, "\x00\x72\x03\x07\0" },
	{ 0x1e5a, "\x00\x52\x03\x23\0" },
	{ 0x1e5b, "\x00\x72\x03\x23\0" },
	{ 0x1e5c, "\x00\x52\x03\x23\x03\x04\0" },
	{ 0x1e5d, "\x00\x72\x03\x23\x03\x04\0" },
	{ 0x1e5e, "\x00\x52\x03\x31\0" },
	{ 0x1e5f, "\x00\x72\x03\x31\0" },
	{ 0x1e60, "\x00\x53\x03\x07\0" },
	{ 0x1e61, "\x00\x73\x03\x07\0" },
	{ 0x1e62, "\x00\x53\x03\x23\0" },
	{ 0x1e63, "\x00\x73\x03\x23\0" },
	{ 0x1e64, "\x00\x53\x03\x01\x03\x07\0" },
	{ 0x1e65, "\x00\x73\x03\x01\x03\x07\0" },
	{ 0x1e66, "\x00\x53\x03\x0c\x03\x07\0" },
	{ 0x1e67, "\x00\x73\x03\x0c\x03\x07\0" },
	{ 0x1e68, "\x00\x53\x03\x23\x03\x07\0" },
	{ 0x1e69, "\x00\x73\x03\x23\x03\x07\0" },
	{ 0x1e6a, "\x00\x54\x03\x07\0" },
	{ 0x1e6b, "\x00\x74\x03\x07\0" },
	{ 0x1e6c, "\x00\x54\x03\x23\0" },
	{ 0x1e6d, "\x00\x74\x03\x23\0" },
	{ 0x1e6e, "\x00\x54\x03\x31\0" },
	{ 0x1e6f, "\x00\x74\x03\x31\0" },
	{ 0x1e70, "\x00\x54\x03\x2d\0" },
	{ 0x1e71, "\x00\x74\x03\x2d\0" },
	{ 0x1e72, "\x00\x55\x03\x24\0" },
	{ 0x1e73, "\x00\x75\x03\x24\0" },
	{ 0x1e74, "\x00\x55\x03\x30\0" },
	{ 0x1e75, "\x00\x75\x03\x30\0" },
	{ 0x1e76, "\x00\x55\x03\x2d\0" },
	{ 0x1e77, "\x00\x75\x03\x2d\0" },
	{ 0x1e78, "\x00\x55\x03\x03\x03\x01\0" },
	{ 0x1e79, "\x00\x75\x03\x03\x03\x01\0" },
	{ 0x1e7a, "\x00\x55\x03\x04\x03\x08\0" },
	{ 0x1e7b, "\x00\x75\x03\x04\x03\x08\0" },
	{ 0x1e7c, "\x00\x56\x03\x03\0" },
	{ 0x1e7d, "\x00\x76\x03\x03\0" },
	{ 0x1e7e, "\x00\x56\x03\x23\0" },
	{ 0x1e7f, "\x00\x76\x03\x23\0" },
	{ 0x1e80, "\x00\x57\x03\x00\0" },
	{ 0x1e81, "\x00\x77\x03\x00\0" },
	{ 0x1e82, "\x00\x57\x03\x01\0" },
	{ 0x1e83, "\x00\x77\x03\x01\0" },
	{ 0x1e84, "\x00\x57\x03\x08\0" },
	{ 0x1e85, "\x00\x77\x03\x08\0" },
	{ 0x1e86, "\x00\x57\x03\x07\0" },
	{ 0x1e87, "\x00\x77\x03\x07\0" },
	{ 0x1e88, "\x00\x57\x03\x23\0" },
	{ 0x1e89, "\x00\x77\x03\x23\0" },
	{ 0x1e8a, "\x00\x58\x03\x07\0" },
	{ 0x1e8b, "\x00\x78\x03\x07\0" },
	{ 0x1e8c, "\x00\x58\x03\x08\0" },
	{ 0x1e8d, "\x00\x78\x03\x08\0" },
	{ 0x1e8e, "\x00\x59\x03\x07\0" },
	{ 0x1e8f, "\x00\x79\x03\x07\0" },
	{ 0x1e90, "\x00\x5a\x03\x02\0" },
	{ 0x1e91, "\x00\x7a\x03\x02\0" },
	{ 0x1e92, "\x00\x5a\x03\x23\0" },
	{ 0x1e93, "\x00\x7a\x03\x23\0" },
	{ 0x1e94, "\x00\x5a\x03\x31\0" },
	{ 0x1e95, "\x00\x7a\x03\x31\0" },
	{ 0x1e96, "\x00\x68\x03\x31\0" },
	{ 0x1e97, "\x00\x74\x03\x08\0" },
	{ 0x1e98, "\x00\x77\x03\x0a\0" },
	{ 0x1e99, "\x00\x79\x03\x0a\0" },
	{ 0x1e9b, "\x01\x7f\x03\x07\0" },
	{ 0x1ea0, "\x00\x41\x03\x23\0" },
	{ 0x1ea1, "\x00\x61\x03\x23\0" },
	{ 0x1ea2, "\x00\x41\x03\x09\0" },
	{ 0x1ea3, "\x00\x61\x03\x09\0" },
	{ 0x1ea4, "\x00\x41\x03\x02\x03\x01\0" },
	{ 0x1ea5, "\x00\x61\x03\x02\x03\x01\0" },
	{ 0x1ea6, "\x00\x41\x03\x02\x03\x00\0" },
	{ 0x1ea7, "\x00\x61\x03\x02\x03\x00\0" },
	{ 0x1ea8, "\x00\x41\x03\x02\x03\x09\0" },
	{ 0x1ea9, "\x00\x61\x03\x02\x03\x09\0" },
	{ 0x1eaa, "\x00\x41\x03\x02\x03\x03\0" },
	{ 0x1eab, "\x00\x61\x03\x02\x03\x03\0" },
	{ 0x1eac, "\x00\x41\x03\x23\x03\x02\0" },
	{ 0x1ead, "\x00\x61\x03\x23\x03\x02\0" },
	{ 0x1eae, "\x00\x41\x03\x06\x03\x01\0" },
	{ 0x1eaf, "\x00\x61\x03\x06\x03\x01\0" },
	{ 0x1eb0, "\x00\x41\x03\x06\x03\x00\0" },
	{ 0x1eb1, "\x00\x61\x03\x06\x03\x00\0" },
	{ 0x1eb2, "\x00\x41\x03\x06\x03\x09\0" },
	{ 0x1eb3, "\x00\x61\x03\x06\x03\x09\0" },
	{ 0x1eb4, "\x00\x41\x03\x06\x03\x03\0" },
	{ 0x1eb5, "\x00\x61\x03\x06\x03\x03\0" },
	{ 0x1eb6, "\x00\x41\x03\x23\x03\x06\0" },
	{ 0x1eb7, "\x00\x61\x03\x23\x03\x06\0" },
	{ 0x1eb8, "\x00\x45\x03\x23\0" },
	{ 0x1eb9, "\x00\x65\x03\x23\0" },
	{ 0x1eba, "\x00\x45\x03\x09\0" },
	{ 0x1ebb, "\x00\x65\x03\x09\0" },
	{ 0x1ebc, "\x00\x45\x03\x03\0" },
	{ 0x1ebd, "\x00\x65\x03\x03\0" },
	{ 0x1ebe, "\x00\x45\x03\x02\x03\x01\0" },
	{ 0x1ebf, "\x00\x65\x03\x02\x03\x01\0" },
	{ 0x1ec0, "\x00\x45\x03\x02\x03\x00\0" },
	{ 0x1ec1, "\x00\x65\x03\x02\x03\x00\0" },
	{ 0x1ec2, "\x00\x45\x03\x02\x03\x09\0" },
	{ 0x1ec3, "\x00\x65\x03\x02\x03\x09\0" },
	{ 0x1ec4, "\x00\x45\x03\x02\x03\x03\0" },
	{ 0x1ec5, "\x00\x65\x03\x02\x03\x03\0" },
	{ 0x1ec6, "\x00\x45\x03\x23\x03\x02\0" },
	{ 0x1ec7, "\x00\x65\x03\x23\x03\x02\0" },
	{ 0x1ec8, "\x00\x49\x03\x09\0" },
	{ 0x1ec9, "\x00\x69\x03\x09\0" },
	{ 0x1eca, "\x00\x49\x03\x23\0" },
	{ 0x1ecb, "\x00\x69\x03\x23\0" },
	{ 0x1ecc, "\x00\x4f\x03\x23\0" },
	{ 0x1ecd, "\x00\x6f\x03\x23\0" },
	{ 0x1ece, "\x00\x4f\x03\x09\0" },
	{ 0x1ecf, "\x00\x6f\x03\x09\0" },
	{ 0x1ed0, "\x00\x4f\x03\x02\x03\x01\0" },
	{ 0x1ed1, "\x00\x6f\x03\x02\x03\x01\0" },
	{ 0x1ed2, "\x00\x4f\x03\x02\x03\x00\0" },
	{ 0x1ed3, "\x00\x6f\x03\x02\x03\x00\0" },
	{ 0x1ed4, "\x00\x4f\x03\x02\x03\x09\0" },
	{ 0x1ed5, "\x00\x6f\x03\x02\x03\x09\0" },
	{ 0x1ed6, "\x00\x4f\x03\x02\x03\x03\0" },
	{ 0x1ed7, "\x00\x6f\x03\x02\x03\x03\0" },
	{ 0x1ed8, "\x00\x4f\x03\x23\x03\x02\0" },
	{ 0x1ed9, "\x00\x6f\x03\x23\x03\x02\0" },
	{ 0x1eda, "\x00\x4f\x03\x1b\x03\x01\0" },
	{ 0x1edb, "\x00\x6f\x03\x1b\x03\x01\0" },
	{ 0x1edc, "\x00\x4f\x03\x1b\x03\x00\0" },
	{ 0x1edd, "\x00\x6f\x03\x1b\x03\x00\0" },
	{ 0x1ede, "\x00\x4f\x03\x1b\x03\x09\0" },
	{ 0x1edf, "\x00\x6f\x03\x1b\x03\x09\0" },
	{ 0x1ee0, "\x00\x4f\x03\x1b\x03\x03\0" },
	{ 0x1ee1, "\x00\x6f\x03\x1b\x03\x03\0" },
	{ 0x1ee2, "\x00\x4f\x03\x1b\x03\x23\0" },
	{ 0x1ee3, "\x00\x6f\x03\x1b\x03\x23\0" },
	{ 0x1ee4, "\x00\x55\x03\x23\0" },
	{ 0x1ee5, "\x00\x75\x03\x23\0" },
	{ 0x1ee6, "\x00\x55\x03\x09\0" },
	{ 0x1ee7, "\x00\x75\x03\x09\0" },
	{ 0x1ee8, "\x00\x55\x03\x1b\x03\x01\0" },
	{ 0x1ee9, "\x00\x75\x03\x1b\x03\x01\0" },
	{ 0x1eea, "\x00\x55\x03\x1b\x03\x00\0" },
	{ 0x1eeb, "\x00\x75\x03\x1b\x03\x00\0" },
	{ 0x1eec, "\x00\x55\x03\x1b\x03\x09\0" },
	{ 0x1eed, "\x00\x75\x03\x1b\x03\x09\0" },
	{ 0x1eee, "\x00\x55\x03\x1b\x03\x03\0" },
	{ 0x1eef, "\x00\x75\x03\x1b\x03\x03\0" },
	{ 0x1ef0, "\x00\x55\x03\x1b\x03\x23\0" },
	{ 0x1ef1, "\x00\x75\x03\x1b\x03\x23\0" },
	{ 0x1ef2, "\x00\x59\x03\x00\0" },
	{ 0x1ef3, "\x00\x79\x03\x00\0" },
	{ 0x1ef4, "\x00\x59\x03\x23\0" },
	{ 0x1ef5, "\x00\x79\x03\x23\0" },
	{ 0x1ef6, "\x00\x59\x03\x09\0" },
	{ 0x1ef7, "\x00\x79\x03\x09\0" },
	{ 0x1ef8, "\x00\x59\x03\x03\0" },
	{ 0x1ef9, "\x00\x79\x03\x03\0" },
	{ 0x1f00, "\x03\xb1\x03\x13\0" },
	{ 0x1f01, "\x03\xb1\x03\x14\0" },
	{ 0x1f02, "\x03\xb1\x03\x13\x03\x00\0" },
	{ 0x1f03, "\x03\xb1\x03\x14\x03\x00\0" },
	{ 0x1f04, "\x03\xb1\x03\x13\x03\x01\0" },
	{ 0x1f05, "\x03\xb1\x03\x14\x03\x01\0" },
	{ 0x1f06, "\x03\xb1\x03\x13\x03\x42\0" },
	{ 0x1f07, "\x03\xb1\x03\x14\x03\x42\0" },
	{ 0x1f08, "\x03\x91\x03\x13\0" },
	{ 0x1f09, "\x03\x91\x03\x14\0" },
	{ 0x1f0a, "\x03\x91\x03\x13\x03\x00\0" },
	{ 0x1f0b, "\x03\x91\x03\x14\x03\x00\0" },
	{ 0x1f0c, "\x03\x91\x03\x13\x03\x01\0" },
	{ 0x1f0d, "\x03\x91\x03\x14\x03\x01\0" },
	{ 0x1f0e, "\x03\x91\x03\x13\x03\x42\0" },
	{ 0x1f0f, "\x03\x91\x03\x14\x03\x42\0" },
	{ 0x1f10, "\x03\xb5\x03\x13\0" },
	{ 0x1f11, "\x03\xb5\x03\x14\0" },
	{ 0x1f12, "\x03\xb5\x03\x13\x03\x00\0" },
	{ 0x1f13, "\x03\xb5\x03\x14\x03\x00\0" },
	{ 0x1f14, "\x03\xb5\x03\x13\x03\x01\0" },
	{ 0x1f15, "\x03\xb5\x03\x14\x03\x01\0" },
	{ 0x1f18, "\x03\x95\x03\x13\0" },
	{ 0x1f19, "\x03\x95\x03\x14\0" },
	{ 0x1f1a, "\x03\x95\x03\x13\x03\x00\0" },
	{ 0x1f1b, "\x03\x95\x03\x14\x03\x00\0" },
	{ 0x1f1c, "\x03\x95\x03\x13\x03\x01\0" },
	{ 0x1f1d, "\x03\x95\x03\x14\x03\x01\0" },
	{ 0x1f20, "\x03\xb7\x03\x13\0" },
	{ 0x1f21, "\x03\xb7\x03\x14\0" },
	{ 0x1f22, "\x03\xb7\x03\x13\x03\x00\0" },
	{ 0x1f23, "\x03\xb7\x03\x14\x03\x00\0" },
	{ 0x1f24, "\x03\xb7\x03\x13\x03\x01\0" },
	{ 0x1f25, "\x03\xb7\x03\x14\x03\x01\0" },
	{ 0x1f26, "\x03\xb7\x03\x13\x03\x42\0" },
	{ 0x1f27, "\x03\xb7\x03\x14\x03\x42\0" },
	{ 0x1f28, "\x03\x97\x03\x13\0" },
	{ 0x1f29, "\x03\x97\x03\x14\0" },
	{ 0x1f2a, "\x03\x97\x03\x13\x03\x00\0" },
	{ 0x1f2b, "\x03\x97\x03\x14\x03\x00\0" },
	{ 0x1f2c, "\x03\x97\x03\x13\x03\x01\0" },
	{ 0x1f2d, "\x03\x97\x03\x14\x03\x01\0" },
	{ 0x1f2e, "\x03\x97\x03\x13\x03\x42\0" },
	{ 0x1f2f, "\x03\x97\x03\x14\x03\x42\0" },
	{ 0x1f30, "\x03\xb9\x03\x13\0" },
	{ 0x1f31, "\x03\xb9\x03\x14\0" },
	{ 0x1f32, "\x03\xb9\x03\x13\x03\x00\0" },
	{ 0x1f33, "\x03\xb9\x03\x14\x03\x00\0" },
	{ 0x1f34, "\x03\xb9\x03\x13\x03\x01\0" },
	{ 0x1f35, "\x03\xb9\x03\x14\x03\x01\0" },
	{ 0x1f36, "\x03\xb9\x03\x13\x03\x42\0" },
	{ 0x1f37, "\x03\xb9\x03\x14\x03\x42\0" },
	{ 0x1f38, "\x03\x99\x03\x13\0" },
	{ 0x1f39, "\x03\x99\x03\x14\0" },
	{ 0x1f3a, "\x03\x99\x03\x13\x03\x00\0" },
	{ 0x1f3b, "\x03\x99\x03\x14\x03\x00\0" },
	{ 0x1f3c, "\x03\x99\x03\x13\x03\x01\0" },
	{ 0x1f3d, "\x03\x99\x03\x14\x03\x01\0" },
	{ 0x1f3e, "\x03\x99\x03\x13\x03\x42\0" },
	{ 0x1f3f, "\x03\x99\x03\x14\x03\x42\0" },
	{ 0x1f40, "\x03\xbf\x03\x13\0" },
	{ 0x1f41, "\x03\xbf\x03\x14\0" },
	{ 0x1f42, "\x03\xbf\x03\x13\x03\x00\0" },
	{ 0x1f43, "\x03\xbf\x03\x14\x03\x00\0" },
	{ 0x1f44, "\x03\xbf\x03\x13\x03\x01\0" },
	{ 0x1f45, "\x03\xbf\x03\x14\x03\x01\0" },
	{ 0x1f48, "\x03\x9f\x03\x13\0" },
	{ 0x1f49, "\x03\x9f\x03\x14\0" },
	{ 0x1f4a, "\x03\x9f\x03\x13\x03\x00\0" },
	{ 0x1f4b, "\x03\x9f\x03\x14\x03\x00\0" },
	{ 0x1f4c, "\x03\x9f\x03\x13\x03\x01\0" },
	{ 0x1f4d, "\x03\x9f\x03\x14\x03\x01\0" },
	{ 0x1f50, "\x03\xc5\x03\x13\0" },
	{ 0x1f51, "\x03\xc5\x03\x14\0" },
	{ 0x1f52, "\x03\xc5\x03\x13\x03\x00\0" },
	{ 0x1f53, "\x03\xc5\x03\x14\x03\x00\0" },
	{ 0x1f54, "\x03\xc5\x03\x13\x03\x01\0" },
	{ 0x1f55, "\x03\xc5\x03\x14\x03\x01\0" },
	{ 0x1f56, "\x03\xc5\x03\x13\x03\x42\0" },
	{ 0x1f57, "\x03\xc5\x03\x14\x03\x42\0" },
	{ 0x1f59, "\x03\xa5\x03\x14\0" },
	{ 0x1f5b, "\x03\xa5\x03\x14\x03\x00\0" },
	{ 0x1f5d, "\x03\xa5\x03\x14\x03\x01\0" },
	{ 0x1f5f, "\x03\xa5\x03\x14\x03\x42\0" },
	{ 0x1f60, "\x03\xc9\x03\x13\0" },
	{ 0x1f61, "\x03\xc9\x03\x14\0" },
	{ 0x1f62, "\x03\xc9\x03\x13\x03\x00\0" },
	{ 0x1f63, "\x03\xc9\x03\x14\x03\x00\0" },
	{ 0x1f64, "\x03\xc9\x03\x13\x03\x01\0" },
	{ 0x1f65, "\x03\xc9\x03\x14\x03\x01\0" },
	{ 0x1f66, "\x03\xc9\x03\x13\x03\x42\0" },
	{ 0x1f67, "\x03\xc9\x03\x14\x03\x42\0" },
	{ 0x1f68, "\x03\xa9\x03\x13\0" },
	{ 0x1f69, "\x03\xa9\x03\x14\0" },
	{ 0x1f6a, "\x03\xa9\x03\x13\x03\x00\0" },
	{ 0x1f6b, "\x03\xa9\x03\x14\x03\x00\0" },
	{ 0x1f6c, "\x03\xa9\x03\x13\x03\x01\0" },
	{ 0x1f6d, "\x03\xa9\x03\x14\x03\x01\0" },
	{ 0x1f6e, "\x03\xa9\x03\x13\x03\x42\0" },
	{ 0x1f6f, "\x03\xa9\x03\x14\x03\x42\0" },
	{ 0x1f70, "\x03\xb1\x03\x00\0" },
	{ 0x1f71, "\x03\xb1\x03\x01\0" },
	{ 0x1f72, "\x03\xb5\x03\x00\0" },
	{ 0x1f73, "\x03\xb5\x03\x01\0" },
	{ 0x1f74, "\x03\xb7\x03\x00\0" },
	{ 0x1f75, "\x03\xb7\x03\x01\0" },
	{ 0x1f76, "\x03\xb9\x03\x00\0" },
	{ 0x1f77, "\x03\xb9\x03\x01\0" },
	{ 0x1f78, "\x03\xbf\x03\x00\0" },
	{ 0x1f79, "\x03\xbf\x03\x01\0" },
	{ 0x1f7a, "\x03\xc5\x03\x00\0" },
	{ 0x1f7b, "\x03\xc5\x03\x01\0" },
	{ 0x1f7c, "\x03\xc9\x03\x00\0" },
	{ 0x1f7d, "\x03\xc9\x03\x01\0" },
	{ 0x1f80, "\x03\xb1\x03\x13\x03\x45\0" },
	{ 0x1f81, "\x03\xb1\x03\x14\x03\x45\0" },
	{ 0x1f82, "\x03\xb1\x03\x13\x03\x00\x03\x45\0" },
	{ 0x1f83, "\x03\xb1\x03\x14\x03\x00\x03\x45\0" },
	{ 0x1f84, "\x03\xb1\x03\x13\x03\x01\x03\x45\0" },
	{ 0x1f85, "\x03\xb1\x03\x14\x03\x01\x03\x45\0" },
	{ 0x1f86, "\x03\xb1\x03\x13\x03\x42\x03\x45\0" },
	{ 0x1f87, "\x03\xb1\x03\x14\x03\x42\x03\x45\0" },
	{ 0x1f88, "\x03\x91\x03\x13\x03\x45\0" },
	{ 0x1f89, "\x03\x91\x03\x14\x03\x45\0" },
	{ 0x1f8a, "\x03\x91\x03\x13\x03\x00\x03\x45\0" },
	{ 0x1f8b, "\x03\x91\x03\x14\x03\x00\x03\x45\0" },
	{ 0x1f8c, "\x03\x91\x03\x13\x03\x01\x03\x45\0" },
	{ 0x1f8d, "\x03\x91\x03\x14\x03\x01\x03\x45\0" },
	{ 0x1f8e, "\x03\x91\x03\x13\x03\x42\x03\x45\0" },
	{ 0x1f8f, "\x03\x91\x03\x14\x03\x42\x03\x45\0" },
	{ 0x1f90, "\x03\xb7\x03\x13\x03\x45\0" },
	{ 0x1f91, "\x03\xb7\x03\x14\x03\x45\0" },
	{ 0x1f92, "\x03\xb7\x03\x13\x03\x00\x03\x45\0" },
	{ 0x1f93, "\x03\xb7\x03\x14\x03\x00\x03\x45\0" },
	{ 0x1f94, "\x03\xb7\x03\x13\x03\x01\x03\x45\0" },
	{ 0x1f95, "\x03\xb7\x03\x14\x03\x01\x03\x45\0" },
	{ 0x1f96, "\x03\xb7\x03\x13\x03\x42\x03\x45\0" },
	{ 0x1f97, "\x03\xb7\x03\x14\x03\x42\x03\x45\0" },
	{ 0x1f98, "\x03\x97\x03\x13\x03\x45\0" },
	{ 0x1f99, "\x03\x97\x03\x14\x03\x45\0" },
	{ 0x1f9a, "\x03\x97\x03\x13\x03\x00\x03\x45\0" },
	{ 0x1f9b, "\x03\x97\x03\x14\x03\x00\x03\x45\0" },
	{ 0x1f9c, "\x03\x97\x03\x13\x03\x01\x03\x45\0" },
	{ 0x1f9d, "\x03\x97\x03\x14\x03\x01\x03\x45\0" },
	{ 0x1f9e, "\x03\x97\x03\x13\x03\x42\x03\x45\0" },
	{ 0x1f9f, "\x03\x97\x03\x14\x03\x42\x03\x45\0" },
	{ 0x1fa0, "\x03\xc9\x03\x13\x03\x45\0" },
	{ 0x1fa1, "\x03\xc9\x03\x14\x03\x45\0" },
	{ 0x1fa2, "\x03\xc9\x03\x13\x03\x00\x03\x45\0" },
	{ 0x1fa3, "\x03\xc9\x03\x14\x03\x00\x03\x45\0" },
	{ 0x1fa4, "\x03\xc9\x03\x13\x03\x01\x03\x45\0" },
	{ 0x1fa5, "\x03\xc9\x03\x14\x03\x01\x03\x45\0" },
	{ 0x1fa6, "\x03\xc9\x03\x13\x03\x42\x03\x45\0" },
	{ 0x1fa7, "\x03\xc9\x03\x14\x03\x42\x03\x45\0" },
	{ 0x1fa8, "\x03\xa9\x03\x13\x03\x45\0" },
	{ 0x1fa9, "\x03\xa9\x03\x14\x03\x45\0" },
	{ 0x1faa, "\x03\xa9\x03\x13\x03\x00\x03\x45\0" },
	{ 0x1fab, "\x03\xa9\x03\x14\x03\x00\x03\x45\0" },
	{ 0x1fac, "\x03\xa9\x03\x13\x03\x01\x03\x45\0" },
	{ 0x1fad, "\x03\xa9\x03\x14\x03\x01\x03\x45\0" },
	{ 0x1fae, "\x03\xa9\x03\x13\x03\x42\x03\x45\0" },
	{ 0x1faf, "\x03\xa9\x03\x14\x03\x42\x03\x45\0" },
	{ 0x1fb0, "\x03\xb1\x03\x06\0" },
	{ 0x1fb1, "\x03\xb1\x03\x04\0" },
	{ 0x1fb2, "\x03\xb1\x03\x00\x03\x45\0" },
	{ 0x1fb3, "\x03\xb1\x03\x45\0" },
	{ 0x1fb4, "\x03\xb1\x03\x01\x03\x45\0" },
	{ 0x1fb6, "\x03\xb1\x03\x42\0" },
	{ 0x1fb7, "\x03\xb1\x03\x42\x03\x45\0" },
	{ 0x1fb8, "\x03\x91\x03\x06\0" },
	{ 0x1fb9, "\x03\x91\x03\x04\0" },
	{ 0x1fba, "\x03\x91\x03\x00\0" },
	{ 0x1fbb, "\x03\x91\x03\x01\0" },
	{ 0x1fbc, "\x03\x91\x03\x45\0" },
	{ 0x1fbe, "\x03\xb9\0" },
	{ 0x1fc1, "\x00\xa8\x03\x42\0" },
	{ 0x1fc2, "\x03\xb7\x03\x00\x03\x45\0" },
	{ 0x1fc3, "\x03\xb7\x03\x45\0" },
	{ 0x1fc4, "\x03\xb7\x03\x01\x03\x45\0" },
	{ 0x1fc6, "\x03\xb7\x03\x42\0" },
	{ 0x1fc7, "\x03\xb7\x03\x42\x03\x45\0" },
	{ 0x1fc8, "\x03\x95\x03\x00\0" },
	{ 0x1fc9, "\x03\x95\x03\x01\0" },
	{ 0x1fca, "\x03\x97\x03\x00\0" },
	{ 0x1fcb, "\x03\x97\x03\x01\0" },
	{ 0x1fcc, "\x03\x97\x03\x45\0" },
	{ 0x1fcd, "\x1f\xbf\x03\x00\0" },
	{ 0x1fce, "\x1f\xbf\x03\x01\0" },
	{ 0x1fcf, "\x1f\xbf\x03\x42\0" },
	{ 0x1fd0, "\x03\xb9\x03\x06\0" },
	{ 0x1fd1, "\x03\xb9\x03\x04\0" },
	{ 0x1fd2, "\x03\xb9\x03\x08\x03\x00\0" },
	{ 0x1fd3, "\x03\xb9\x03\x08\x03\x01\0" },
	{ 0x1fd6, "\x03\xb9\x03\x42\0" },
	{ 0x1fd7, "\x03\xb9\x03\x08\x03\x42\0" },
	{ 0x1fd8, "\x03\x99\x03\x06\0" },
	{ 0x1fd9, "\x03\x99\x03\x04\0" },
	{ 0x1fda, "\x03\x99\x03\x00\0" },
	{ 0x1fdb, "\x03\x99\x03\x01\0" },
	{ 0x1fdd, "\x1f\xfe\x03\x00\0" },
	{ 0x1fde, "\x1f\xfe\x03\x01\0" },
	{ 0x1fdf, "\x1f\xfe\x03\x42\0" },
	{ 0x1fe0, "\x03\xc5\x03\x06\0" },
	{ 0x1fe1, "\x03\xc5\x03\x04\0" },
	{ 0x1fe2, "\x03\xc5\x03\x08\x03\x00\0" },
	{ 0x1fe3, "\x03\xc5\x03\x08\x03\x01\0" },
	{ 0x1fe4, "\x03\xc1\x03\x13\0" },
	{ 0x1fe5, "\x03\xc1\x03\x14\0" },
	{ 0x1fe6, "\x03\xc5\x03\x42\0" },
	{ 0x1fe7, "\x03\xc5\x03\x08\x03\x42\0" },
	{ 0x1fe8, "\x03\xa5\x03\x06\0" },
	{ 0x1fe9, "\x03\xa5\x03\x04\0" },
	{ 0x1fea, "\x03\xa5\x03\x00\0" },
	{ 0x1feb, "\x03\xa5\x03\x01\0" },
	{ 0x1fec, "\x03\xa1\x03\x14\0" },
	{ 0x1fed, "\x00\xa8\x03\x00\0" },
	{ 0x1fee, "\x00\xa8\x03\x01\0" },
	{ 0x1fef, "\x00\x60\0" },
	{ 0x1ff2, "\x03\xc9\x03\x00\x03\x45\0" },
	{ 0x1ff3, "\x03\xc9\x03\x45\0" },
	{ 0x1ff4, "\x03\xc9\x03\x01\x03\x45\0" },
	{ 0x1ff6, "\x03\xc9\x03\x42\0" },
	{ 0x1ff7, "\x03\xc9\x03\x42\x03\x45\0" },
	{ 0x1ff8, "\x03\x9f\x03\x00\0" },
	{ 0x1ff9, "\x03\x9f\x03\x01\0" },
	{ 0x1ffa, "\x03\xa9\x03\x00\0" },
	{ 0x1ffb, "\x03\xa9\x03\x01\0" },
	{ 0x1ffc, "\x03\xa9\x03\x45\0" },
	{ 0x1ffd, "\x00\xb4\0" },
	{ 0x2000, "\x20\x02\0" },
	{ 0x2001, "\x20\x03\0" },
	{ 0x2126, "\x03\xa9\0" },
	{ 0x212a, "\x00\x4b\0" },
	{ 0x212b, "\x00\x41\x03\x0a\0" },
	{ 0x2204, "\x22\x03\x03\x38\0" },
	{ 0x2209, "\x22\x08\x03\x38\0" },
	{ 0x220c, "\x22\x0b\x03\x38\0" },
	{ 0x2224, "\x22\x23\x03\x38\0" },
	{ 0x2226, "\x22\x25\x03\x38\0" },
	{ 0x2241, "\x00\x7e\x03\x38\0" },
	{ 0x2244, "\x22\x43\x03\x38\0" },
	{ 0x2247, "\x22\x45\x03\x38\0" },
	{ 0x2249, "\x22\x48\x03\x38\0" },
	{ 0x2260, "\x00\x3d\x03\x38\0" },
	{ 0x2262, "\x22\x61\x03\x38\0" },
	{ 0x226d, "\x22\x4d\x03\x38\0" },
	{ 0x226e, "\x00\x3c\x03\x38\0" },
	{ 0x226f, "\x00\x3e\x03\x38\0" },
	{ 0x2270, "\x22\x64\x03\x38\0" },
	{ 0x2271, "\x22\x65\x03\x38\0" },
	{ 0x2274, "\x22\x72\x03\x38\0" },
	{ 0x2275, "\x22\x73\x03\x38\0" },
	{ 0x2278, "\x22\x76\x03\x38\0" },
	{ 0x2279, "\x22\x77\x03\x38\0" },
	{ 0x2280, "\x22\x7a\x03\x38\0" },
	{ 0x2281, "\x22\x7b\x03\x38\0" },
	{ 0x2284, "\x22\x82\x03\x38\0" },
	{ 0x2285, "\x22\x83\x03\x38\0" },
	{ 0x2288, "\x22\x86\x03\x38\0" },
	{ 0x2289, "\x22\x87\x03\x38\0" },
	{ 0x22ac, "\x22\xa2\x03\x38\0" },
	{ 0x22ad, "\x22\xa8\x03\x38\0" },
	{ 0x22ae, "\x22\xa9\x03\x38\0" },
	{ 0x22af, "\x22\xab\x03\x38\0" },
	{ 0x22e0, "\x22\x7c\x03\x38\0" },
	{ 0x22e1, "\x22\x7d\x03\x38\0" },
	{ 0x22e2, "\x22\x91\x03\x38\0" },
	{ 0x22e3, "\x22\x92\x03\x38\0" },
	{ 0x22ea, "\x22\xb2\x03\x38\0" },
	{ 0x22eb, "\x22\xb3\x03\x38\0" },
	{ 0x22ec, "\x22\xb4\x03\x38\0" },
	{ 0x22ed, "\x22\xb5\x03\x38\0" },
	{ 0x2329, "\x30\x08\0" },
	{ 0x232a, "\x30\x09\0" },
	{ 0x304c, "\x30\x4b\x30\x99\0" },
	{ 0x304e, "\x30\x4d\x30\x99\0" },
	{ 0x3050, "\x30\x4f\x30\x99\0" },
	{ 0x3052, "\x30\x51\x30\x99\0" },
	{ 0x3054, "\x30\x53\x30\x99\0" },
	{ 0x3056, "\x30\x55\x30\x99\0" },
	{ 0x3058, "\x30\x57\x30\x99\0" },
	{ 0x305a, "\x30\x59\x30\x99\0" },
	{ 0x305c, "\x30\x5b\x30\x99\0" },
	{ 0x305e, "\x30\x5d\x30\x99\0" },
	{ 0x3060, "\x30\x5f\x30\x99\0" },
	{ 0x3062, "\x30\x61\x30\x99\0" },
	{ 0x3065, "\x30\x64\x30\x99\0" },
	{ 0x3067, "\x30\x66\x30\x99\0" },
	{ 0x3069, "\x30\x68\x30\x99\0" },
	{ 0x3070, "\x30\x6f\x30\x99\0" },
	{ 0x3071, "\x30\x6f\x30\x9a\0" },
	{ 0x3073, "\x30\x72\x30\x99\0" },
	{ 0x3074, "\x30\x72\x30\x9a\0" },
	{ 0x3076, "\x30\x75\x30\x99\0" },
	{ 0x3077, "\x30\x75\x30\x9a\0" },
	{ 0x3079, "\x30\x78\x30\x99\0" },
	{ 0x307a, "\x30\x78\x30\x9a\0" },
	{ 0x307c, "\x30\x7b\x30\x99\0" },
	{ 0x307d, "\x30\x7b\x30\x9a\0" },
	{ 0x3094, "\x30\x46\x30\x99\0" },
	{ 0x309e, "\x30\x9d\x30\x99\0" },
	{ 0x30ac, "\x30\xab\x30\x99\0" },
	{ 0x30ae, "\x30\xad\x30\x99\0" },
	{ 0x30b0, "\x30\xaf\x30\x99\0" },
	{ 0x30b2, "\x30\xb1\x30\x99\0" },
	{ 0x30b4, "\x30\xb3\x30\x99\0" },
	{ 0x30b6, "\x30\xb5\x30\x99\0" },
	{ 0x30b8, "\x30\xb7\x30\x99\0" },
	{ 0x30ba, "\x30\xb9\x30\x99\0" },
	{ 0x30bc, "\x30\xbb\x30\x99\0" },
	{ 0x30be, "\x30\xbd\x30\x99\0" },
	{ 0x30c0, "\x30\xbf\x30\x99\0" },
	{ 0x30c2, "\x30\xc1\x30\x99\0" },
	{ 0x30c5, "\x30\xc4\x30\x99\0" },
	{ 0x30c7, "\x30\xc6\x30\x99\0" },
	{ 0x30c9, "\x30\xc8\x30\x99\0" },
	{ 0x30d0, "\x30\xcf\x30\x99\0" },
	{ 0x30d1, "\x30\xcf\x30\x9a\0" },
	{ 0x30d3, "\x30\xd2\x30\x99\0" },
	{ 0x30d4, "\x30\xd2\x30\x9a\0" },
	{ 0x30d6, "\x30\xd5\x30\x99\0" },
	{ 0x30d7, "\x30\xd5\x30\x9a\0" },
	{ 0x30d9, "\x30\xd8\x30\x99\0" },
	{ 0x30da, "\x30\xd8\x30\x9a\0" },
	{ 0x30dc, "\x30\xdb\x30\x99\0" },
	{ 0x30dd, "\x30\xdb\x30\x9a\0" },
	{ 0x30f4, "\x30\xa6\x30\x99\0" },
	{ 0x30f7, "\x30\xef\x30\x99\0" },
	{ 0x30f8, "\x30\xf0\x30\x99\0" },
	{ 0x30f9, "\x30\xf1\x30\x99\0" },
	{ 0x30fa, "\x30\xf2\x30\x99\0" },
	{ 0x30fe, "\x30\xfd\x30\x99\0" },
	{ 0xf900, "\x8c\x48\0" },
	{ 0xf901, "\x66\xf4\0" },
	{ 0xf902, "\x8e\xca\0" },
	{ 0xf903, "\x8c\xc8\0" },
	{ 0xf904, "\x6e\xd1\0" },
	{ 0xf905, "\x4e\x32\0" },
	{ 0xf906, "\x53\xe5\0" },
	{ 0xf907, "\x9f\x9c\0" },
	{ 0xf908, "\x9f\x9c\0" },
	{ 0xf909, "\x59\x51\0" },
	{ 0xf90a, "\x91\xd1\0" },
	{ 0xf90b, "\x55\x87\0" },
	{ 0xf90c, "\x59\x48\0" },
	{ 0xf90d, "\x61\xf6\0" },
	{ 0xf90e, "\x76\x69\0" },
	{ 0xf90f, "\x7f\x85\0" },
	{ 0xf910, "\x86\x3f\0" },
	{ 0xf911, "\x87\xba\0" },
	{ 0xf912, "\x88\xf8\0" },
	{ 0xf913, "\x90\x8f\0" },
	{ 0xf914, "\x6a\x02\0" },
	{ 0xf915, "\x6d\x1b\0" },
	{ 0xf916, "\x70\xd9\0" },
	{ 0xf917, "\x73\xde\0" },
	{ 0xf918, "\x84\x3d\0" },
	{ 0xf919, "\x91\x6a\0" },
	{ 0xf91a, "\x99\xf1\0" },
	{ 0xf91b, "\x4e\x82\0" },
	{ 0xf91c, "\x53\x75\0" },
	{ 0xf91d, "\x6b\x04\0" },
	{ 0xf91e, "\x72\x1b\0" },
	{ 0xf91f, "\x86\x2d\0" },
	{ 0xf920, "\x9e\x1e\0" },
	{ 0xf921, "\x5d\x50\0" },
	{ 0xf922, "\x6f\xeb\0" },
	{ 0xf923, "\x85\xcd\0" },
	{ 0xf924, "\x89\x64\0" },
	{ 0xf925, "\x62\xc9\0" },
	{ 0xf926, "\x81\xd8\0" },
	{ 0xf927, "\x88\x1f\0" },
	{ 0xf928, "\x5e\xca\0" },
	{ 0xf929, "\x67\x17\0" },
	{ 0xf92a, "\x6d\x6a\0" },
	{ 0xf92b, "\x72\xfc\0" },
	{ 0xf92c, "\x90\xce\0" },
	{ 0xf92d, "\x4f\x86\0" },
	{ 0xf92e, "\x51\xb7\0" },
	{ 0xf92f, "\x52\xde\0" },
	{ 0xf930, "\x64\xc4\0" },
	{ 0xf931, "\x6a\xd3\0" },
	{ 0xf932, "\x72\x10\0" },
	{ 0xf933, "\x76\xe7\0" },
	{ 0xf934, "\x80\x01\0" },
	{ 0xf935, "\x86\x06\0" },
	{ 0xf936, "\x86\x5c\0" },
	{ 0xf937, "\x8d\xef\0" },
	{ 0xf938, "\x97\x32\0" },
	{ 0xf939, "\x9b\x6f\0" },
	{ 0xf93a, "\x9d\xfa\0" },
	{ 0xf93b, "\x78\x8c\0" },
	{ 0xf93c, "\x79\x7f\0" },
	{ 0xf93d, "\x7d\xa0\0" },
	{ 0xf93e, "\x83\xc9\0" },
	{ 0xf93f, "\x93\x04\0" },
	{ 0xf940, "\x9e\x7f\0" },
	{ 0xf941, "\x8a\xd6\0" },
	{ 0xf942, "\x58\xdf\0" },
	{ 0xf943, "\x5f\x04\0" },
	{ 0xf944, "\x7c\x60\0" },
	{ 0xf945, "\x80\x7e\0" },
	{ 0xf946, "\x72\x62\0" },
	{ 0xf947, "\x78\xca\0" },
	{ 0xf948, "\x8c\xc2\0" },
	{ 0xf949, "\x96\xf7\0" },
	{ 0xf94a, "\x58\xd8\0" },
	{ 0xf94b, "\x5c\x62\0" },
	{ 0xf94c, "\x6a\x13\0" },
	{ 0xf94d, "\x6d\xda\0" },
	{ 0xf94e, "\x6f\x0f\0" },
	{ 0xf94f, "\x7d\x2f\0" },
	{ 0xf950, "\x7e\x37\0" },
	{ 0xf951, "\x96\xfb\0" },
	{ 0xf952, "\x52\xd2\0" },
	{ 0xf953, "\x80\x8b\0" },
	{ 0xf954, "\x51\xdc\0" },
	{ 0xf955, "\x51\xcc\0" },
	{ 0xf956, "\x7a\x1c\0" },
	{ 0xf957, "\x7d\xbe\0" },
	{ 0xf958, "\x83\xf1\0" },
	{ 0xf959, "\x96\x75\0" },
	{ 0xf95a, "\x8b\x80\0" },
	{ 0xf95b, "\x62\xcf\0" },
	{ 0xf95c, "\x6a\x02\0" },
	{ 0xf95d, "\x8a\xfe\0" },
	{ 0xf95e, "\x4e\x39\0" },
	{ 0xf95f, "\x5b\xe7\0" },
	{ 0xf960, "\x60\x12\0" },
	{ 0xf961, "\x73\x87\0" },
	{ 0xf962, "\x75\x70\0" },
	{ 0xf963, "\x53\x17\0" },
	{ 0xf964, "\x78\xfb\0" },
	{ 0xf965, "\x4f\xbf\0" },
	{ 0xf966, "\x5f\xa9\0" },
	{ 0xf967, "\x4e\x0d\0" },
	{ 0xf968, "\x6c\xcc\0" },
	{ 0xf969, "\x65\x78\0" },
	{ 0xf96a, "\x7d\x22\0" },
	{ 0xf96b, "\x53\xc3\0" },
	{ 0xf96c, "\x58\x5e\0" },
	{ 0xf96d, "\x77\x01\0" },
	{ 0xf96e, "\x84\x49\0" },
	{ 0xf96f, "\x8a\xaa\0" },
	{ 0xf970, "\x6b\xba\0" },
	{ 0xf971, "\x8f\xb0\0" },
	{ 0xf972, "\x6c\x88\0" },
	{ 0xf973, "\x62\xfe\0" },
	{ 0xf974, "\x82\xe5\0" },
	{ 0xf975, "\x63\xa0\0" },
	{ 0xf976, "\x75\x65\0" },
	{ 0xf977, "\x4e\xae\0" },
	{ 0xf978, "\x51\x69\0" },
	{ 0xf979, "\x51\xc9\0" },
	{ 0xf97a, "\x68\x81\0" },
	{ 0xf97b, "\x7c\xe7\0" },
	{ 0xf97c, "\x82\x6f\0" },
	{ 0xf97d, "\x8a\xd2\0" },
	{ 0xf97e, "\x91\xcf\0" },
	{ 0xf97f, "\x52\xf5\0" },
	{ 0xf980, "\x54\x42\0" },
	{ 0xf981, "\x59\x73\0" },
	{ 0xf982, "\x5e\xec\0" },
	{ 0xf983, "\x65\xc5\0" },
	{ 0xf984, "\x6f\xfe\0" },
	{ 0xf985, "\x79\x2a\0" },
	{ 0xf986, "\x95\xad\0" },
	{ 0xf987, "\x9a\x6a\0" },
	{ 0xf988, "\x9e\x97\0" },
	{ 0xf989, "\x9e\xce\0" },
	{ 0xf98a, "\x52\x9b\0" },
	{ 0xf98b, "\x66\xc6\0" },
	{ 0xf98c, "\x6b\x77\0" },
	{ 0xf98d, "\x8f\x62\0" },
	{ 0xf98e, "\x5e\x74\0" },
	{ 0xf98f, "\x61\x90\0" },
	{ 0xf990, "\x62\x00\0" },
	{ 0xf991, "\x64\x9a\0" },
	{ 0xf992, "\x6f\x23\0" },
	{ 0xf993, "\x71\x49\0" },
	{ 0xf994, "\x74\x89\0" },
	{ 0xf995, "\x79\xca\0" },
	{ 0xf996, "\x7d\xf4\0" },
	{ 0xf997, "\x80\x6f\0" },
	{ 0xf998, "\x8f\x26\0" },
	{ 0xf999, "\x84\xee\0" },
	{ 0xf99a, "\x90\x23\0" },
	{ 0xf99b, "\x93\x4a\0" },
	{ 0xf99c, "\x52\x17\0" },
	{ 0xf99d, "\x52\xa3\0" },
	{ 0xf99e, "\x54\xbd\0" },
	{ 0xf99f, "\x70\xc8\0" },
	{ 0xf9a0, "\x88\xc2\0" },
	{ 0xf9a1, "\x8a\xaa\0" },
	{ 0xf9a2, "\x5e\xc9\0" },
	{ 0xf9a3, "\x5f\xf5\0" },
	{ 0xf9a4, "\x63\x7b\0" },
	{ 0xf9a5, "\x6b\xae\0" },
	{ 0xf9a6, "\x7c\x3e\0" },
	{ 0xf9a7, "\x73\x75\0" },
	{ 0xf9a8, "\x4e\xe4\0" },
	{ 0xf9a9, "\x56\xf9\0" },
	{ 0xf9aa, "\x5b\xe7\0" },
	{ 0xf9ab, "\x5d\xba\0" },
	{ 0xf9ac, "\x60\x1c\0" },
	{ 0xf9ad, "\x73\xb2\0" },
	{ 0xf9ae, "\x74\x69\0" },
	{ 0xf9af, "\x7f\x9a\0" },
	{ 0xf9b0, "\x80\x46\0" },
	{ 0xf9b1, "\x92\x34\0" },
	{ 0xf9b2, "\x96\xf6\0" },
	{ 0xf9b3, "\x97\x48\0" },
	{ 0xf9b4, "\x98\x18\0" },
	{ 0xf9b5, "\x4f\x8b\0" },
	{ 0xf9b6, "\x79\xae\0" },
	{ 0xf9b7, "\x91\xb4\0" },
	{ 0xf9b8, "\x96\xb8\0" },
	{ 0xf9b9, "\x60\xe1\0" },
	{ 0xf9ba, "\x4e\x86\0" },
	{ 0xf9bb, "\x50\xda\0" },
	{ 0xf9bc, "\x5b\xee\0" },
	{ 0xf9bd, "\x5c\x3f\0" },
	{ 0xf9be, "\x65\x99\0" },
	{ 0xf9bf, "\x6a\x02\0" },
	{ 0xf9c0, "\x71\xce\0" },
	{ 0xf9c1, "\x76\x42\0" },
	{ 0xf9c2, "\x84\xfc\0" },
	{ 0xf9c3, "\x90\x7c\0" },
	{ 0xf9c4, "\x9f\x8d\0" },
	{ 0xf9c5, "\x66\x88\0" },
	{ 0xf9c6, "\x96\x2e\0" },
	{ 0xf9c7, "\x52\x89\0" },
	{ 0xf9c8, "\x67\x7b\0" },
	{ 0xf9c9, "\x67\xf3\0" },
	{ 0xf9ca, "\x6d\x41\0" },
	{ 0xf9cb, "\x6e\x9c\0" },
	{ 0xf9cc, "\x74\x09\0" },
	{ 0xf9cd, "\x75\x59\0" },
	{ 0xf9ce, "\x78\x6b\0" },
	{ 0xf9cf, "\x7d\x10\0" },
	{ 0xf9d0, "\x98\x5e\0" },
	{ 0xf9d1, "\x51\x6d\0" },
	{ 0xf9d2, "\x62\x2e\0" },
	{ 0xf9d3, "\x96\x78\0" },
	{ 0xf9d4, "\x50\x2b\0" },
	{ 0xf9d5, "\x5d\x19\0" },
	{ 0xf9d6, "\x6d\xea\0" },
	{ 0xf9d7, "\x8f\x2a\0" },
	{ 0xf9d8, "\x5f\x8b\0" },
	{ 0xf9d9, "\x61\x44\0" },
	{ 0xf9da, "\x68\x17\0" },
	{ 0xf9db, "\x73\x87\0" },
	{ 0xf9dc, "\x96\x86\0" },
	{ 0xf9dd, "\x52\x29\0" },
	{ 0xf9de, "\x54\x0f\0" },
	{ 0xf9df, "\x5c\x65\0" },
	{ 0xf9e0, "\x66\x13\0" },
	{ 0xf9e1, "\x67\x4e\0" },
	{ 0xf9e2, "\x68\xa8\0" },
	{ 0xf9e3, "\x6c\xe5\0" },
	{ 0xf9e4, "\x74\x06\0" },
	{ 0xf9e5, "\x75\xe2\0" },
	{ 0xf9e6, "\x7f\x79\0" },
	{ 0xf9e7, "\x88\xcf\0" },
	{ 0xf9e8, "\x88\xe1\0" },
	{ 0xf9e9, "\x91\xcc\0" },
	{ 0xf9ea, "\x96\xe2\0" },
	{ 0xf9eb, "\x53\x3f\0" },
	{ 0xf9ec, "\x6e\xba\0" },
	{ 0xf9ed, "\x54\x1d\0" },
	{ 0xf9ee, "\x71\xd0\0" },
	{ 0xf9ef, "\x74\x98\0" },
	{ 0xf9f0, "\x85\xfa\0" },
	{ 0xf9f1, "\x96\xa3\0" },
	{ 0xf9f2, "\x9c\x57\0" },
	{ 0xf9f3, "\x9e\x9f\0" },
	{ 0xf9f4, "\x67\x97\0" },
	{ 0xf9f5, "\x6d\xcb\0" },
	{ 0xf9f6, "\x81\xe8\0" },
	{ 0xf9f7, "\x7a\xcb\0" },
	{ 0xf9f8, "\x7b\x20\0" },
	{ 0xf9f9, "\x7c\x92\0" },
	{ 0xf9fa, "\x72\xc0\0" },
	{ 0xf9fb, "\x70\x99\0" },
	{ 0xf9fc, "\x8b\x58\0" },
	{ 0xf9fd, "\x4e\xc0\0" },
	{ 0xf9fe, "\x83\x36\0" },
	{ 0xf9ff, "\x52\x3a\0" },
	{ 0xfa00, "\x52\x07\0" },
	{ 0xfa01, "\x5e\xa6\0" },
	{ 0xfa02, "\x62\xd3\0" },
	{ 0xfa03, "\x7c\xd6\0" },
	{ 0xfa04, "\x5b\x85\0" },
	{ 0xfa05, "\x6d\x1e\0" },
	{ 0xfa06, "\x66\xb4\0" },
	{ 0xfa07, "\x8f\x3b\0" },
	{ 0xfa08, "\x88\x4c\0" },
	{ 0xfa09, "\x96\x4d\0" },
	{ 0xfa0a, "\x89\x8b\0" },
	{ 0xfa0b, "\x5e\xd3\0" },
	{ 0xfa0c, "\x51\x40\0" },
	{ 0xfa0d, "\x55\xc0\0" },
	{ 0xfa10, "\x58\x5a\0" },
	{ 0xfa12, "\x66\x74\0" },
	{ 0xfa15, "\x51\xde\0" },
	{ 0xfa16, "\x73\x2a\0" },
	{ 0xfa17, "\x76\xca\0" },
	{ 0xfa18, "\x79\x3c\0" },
	{ 0xfa19, "\x79\x5e\0" },
	{ 0xfa1a, "\x79\x65\0" },
	{ 0xfa1b, "\x79\x8f\0" },
	{ 0xfa1c, "\x97\x56\0" },
	{ 0xfa1d, "\x7c\xbe\0" },
	{ 0xfa1e, "\x7f\xbd\0" },
	{ 0xfa20, "\x86\x12\0" },
	{ 0xfa22, "\x8a\xf8\0" },
	{ 0xfa25, "\x90\x38\0" },
	{ 0xfa26, "\x90\xfd\0" },
	{ 0xfa2a, "\x98\xef\0" },
	{ 0xfa2b, "\x98\xfc\0" },
	{ 0xfa2c, "\x99\x28\0" },
	{ 0xfa2d, "\x9d\xb4\0" },
	{ 0xfb1f, "\x05\xf2\x05\xb7\0" },
	{ 0xfb2a, "\x05\xe9\x05\xc1\0" },
	{ 0xfb2b, "\x05\xe9\x05\xc2\0" },
	{ 0xfb2c, "\x05\xe9\x05\xbc\x05\xc1\0" },
	{ 0xfb2d, "\x05\xe9\x05\xbc\x05\xc2\0" },
	{ 0xfb2e, "\x05\xd0\x05\xb7\0" },
	{ 0xfb2f, "\x05\xd0\x05\xb8\0" },
	{ 0xfb30, "\x05\xd0\x05\xbc\0" },
	{ 0xfb31, "\x05\xd1\x05\xbc\0" },
	{ 0xfb32, "\x05\xd2\x05\xbc\0" },
	{ 0xfb33, "\x05\xd3\x05\xbc\0" },
	{ 0xfb34, "\x05\xd4\x05\xbc\0" },
	{ 0xfb35, "\x05\xd5\x05\xbc\0" },
	{ 0xfb36, "\x05\xd6\x05\xbc\0" },
	{ 0xfb38, "\x05\xd8\x05\xbc\0" },
	{ 0xfb39, "\x05\xd9\x05\xbc\0" },
	{ 0xfb3a, "\x05\xda\x05\xbc\0" },
	{ 0xfb3b, "\x05\xdb\x05\xbc\0" },
	{ 0xfb3c, "\x05\xdc\x05\xbc\0" },
	{ 0xfb3e, "\x05\xde\x05\xbc\0" },
	{ 0xfb40, "\x05\xe0\x05\xbc\0" },
	{ 0xfb41, "\x05\xe1\x05\xbc\0" },
	{ 0xfb43, "\x05\xe3\x05\xbc\0" },
	{ 0xfb44, "\x05\xe4\x05\xbc\0" },
	{ 0xfb46, "\x05\xe6\x05\xbc\0" },
	{ 0xfb47, "\x05\xe7\x05\xbc\0" },
	{ 0xfb48, "\x05\xe8\x05\xbc\0" },
	{ 0xfb49, "\x05\xe9\x05\xbc\0" },
	{ 0xfb4a, "\x05\xea\x05\xbc\0" },
	{ 0xfb4b, "\x05\xd5\x05\xb9\0" },
	{ 0xfb4c, "\x05\xd1\x05\xbf\0" },
	{ 0xfb4d, "\x05\xdb\x05\xbf\0" },
	{ 0xfb4e, "\x05\xe4\x05\xbf\0" }
};

static gint
canonical_decomposition (gunichar ch, gunichar * buf)
{
	gint len = 0;
	
	if (ch <= 0xffff) {
		int start = 0;
		int end = G_N_ELEMENTS (decomp_table);
		while (start != end) {
			int half = (start + end) / 2;
			if (ch == decomp_table[half].ch) {
				/* Found it.  */
				int i;
				/* We store as a double-nul terminated string.  */
				for (len = 0; (decomp_table[half].expansion[len] || decomp_table[half].expansion[len + 1]); len += 2) ;
				/* We've counted twice as many bytes as there are
				   characters.  */
				len /= 2;
                                                                                
				for (i = 0; i < len; i ++) {
					buf[i] = (decomp_table[half].expansion[2 * i] << 8) | decomp_table[half].expansion[2 * i + 1];
				}
				break;
			} else if (ch > decomp_table[half].ch) {
				if (start == half) break;
				start = half;
			} else {
				if (end == half) break;
				end = half;
			}
		}
	}
	if (len == 0) {
		/* Not in our table.  */
		*buf = ch;
		len = 1;
	}
                                                                                
	/* Supposedly following the Unicode 2.1.9 table means that the
	   decompositions come out in canonical order.  I haven't tested
	   this, but we rely on it here.  */
	return len;
}

static gunichar
stripped_char (gunichar ch)
{
        gunichar decomp[MAX_DECOMP];
        GUnicodeType utype;
        gint dlen;
                                                                                
        utype = g_unichar_type (ch);
                                                                                
        switch (utype) {
        case G_UNICODE_CONTROL:
        case G_UNICODE_FORMAT:
        case G_UNICODE_UNASSIGNED:
        case G_UNICODE_COMBINING_MARK:
                /* Ignore those */
                return 0;
               break;
        default:
                /* Convert to lowercase, fall through */
                ch = g_unichar_tolower (ch);
        case G_UNICODE_LOWERCASE_LETTER:
                dlen = canonical_decomposition (ch, decomp);
                if (dlen > 0) return *decomp;
                break;
        }
                                                                                
        return 0;
}

/** 
 * e_util_utf8_strstrcasedecomp:
 * @haystack: The string to search in.
 * @needle: The string to search for.
 * 
 * Find the first instance of @needle in @haystack, where both @needle
 * and @haystack are UTF-8 strings. Both strings are stripped and
 * decomposed for comparison, and case is ignored.
 *
 * Returns: A pointer to the first instance of @needle in @haystack, or
 *          %NULL if either of the strings are not legal UTF-8 strings.
 **/
const gchar *
e_util_utf8_strstrcasedecomp (const gchar *haystack, const gchar *needle)
{
        gunichar *nuni;
        gunichar unival;
        gint nlen;
        const guchar *o, *p;
                                                                                
        if (haystack == NULL) return NULL;
        if (needle == NULL) return NULL;
        if (strlen (needle) == 0) return haystack;
        if (strlen (haystack) == 0) return NULL;
                                                                                
        nuni = g_alloca (sizeof (gunichar) * strlen (needle));
                                                                                
        nlen = 0;
        for (p = e_util_unicode_get_utf8 (needle, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                gint sc;
                sc = stripped_char (unival);
                if (sc) {
                       nuni[nlen++] = sc;
                }
        }
        /* NULL means there was illegal utf-8 sequence */
        if (!p) return NULL;
        /* If everything is correct, we have decomposed, lowercase, stripped needle */
        if (nlen < 1) return haystack;
                                                                                
        o = haystack;
        for (p = e_util_unicode_get_utf8 (o, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                gint sc;
                sc = stripped_char (unival);
                if (sc) {
                        /* We have valid stripped char */
                        if (sc == nuni[0]) {
                                const gchar *q = p;
                                gint npos = 1;
                                while (npos < nlen) {
                                        q = e_util_unicode_get_utf8 (q, &unival);
                                        if (!q || !unival) return NULL;
                                        sc = stripped_char (unival);
                                        if ((!sc) || (sc != nuni[npos])) break;
                                        npos++;
                                }
                                if (npos == nlen) {
                                        return o;
                                }
                        }
                }
                o = p;
        }
                                                                                
        return NULL;
}

/** 
 * e_strftime:
 * @s: The string array to store the result in.
 * @max: The size of array @s.
 * @fmt: The formatting to use on @tm.
 * @tm: The time value to format.
 *
 * This function is a wrapper around the strftime(3) function, which
 * converts the &percnt;l and &percnt;k (12h and 24h) format variables if necessary.
 *
 * Returns: The number of characters placed in @s.
 **/
size_t e_strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
#ifdef HAVE_LKSTRFTIME
	return strftime(s, max, fmt, tm);
#else
	char *c, *ffmt, *ff;
	size_t ret;

	ffmt = g_strdup(fmt);
	ff = ffmt;
	while ((c = strstr(ff, "%l")) != NULL) {
		c[1] = 'I';
		ff = c;
	}

	ff = fmt;
	while ((c = strstr(ff, "%k")) != NULL) {
		c[1] = 'H';
		ff = c;
	}

	ret = strftime(s, max, ffmt, tm);
	g_free(ffmt);
	return ret;
#endif
}

/** 
 * e_utf8_strftime:
 * @s: The string array to store the result in.
 * @max: The size of array @s.
 * @fmt: The formatting to use on @tm.
 * @tm: The time value to format.
 *
 * The UTF-8 equivalent of e_strftime().
 *
 * Returns: The number of characters placed in @s.
 **/
size_t 
e_utf8_strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
	size_t sz, ret;
	char *locale_fmt, *buf;

	locale_fmt = g_locale_from_utf8(fmt, -1, NULL, &sz, NULL);
	if (!locale_fmt)
		return 0;

	ret = e_strftime(s, max, locale_fmt, tm);
	if (!ret) {
		g_free (locale_fmt);
		return 0;
	}

	buf = g_locale_to_utf8(s, ret, NULL, &sz, NULL);
	if (!buf) {
		g_free (locale_fmt);
		return 0;
	}

	if (sz >= max) {
		char *tmp = buf + max - 1;
		tmp = g_utf8_find_prev_char(buf, tmp);
		if (tmp)
			sz = tmp - buf;
		else
			sz = 0;
	}
	memcpy(s, buf, sz);
	s[sz] = '\0';
	g_free(locale_fmt);
	g_free(buf);
	return sz;
}

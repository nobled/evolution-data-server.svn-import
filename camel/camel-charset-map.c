/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; -*- */

/* 
 * Authors:
 *   Michael Zucchi <notzed@ximian.com>
 *   Dan Winship <danw@ximian.com>
 *
 * Copyright 2000, 2001 Ximian, Inc. (www.ximian.com)
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>

/*
  if you want to build the charset map, compile this with something like:
    gcc -DBUILD_MAP camel-charset-map.c `glib-config --cflags`
  (plus any -I/-L/-l flags you need for iconv), then run it as 
    ./a.out > camel-charset-map-private.h

  Note that the big-endian variant isn't tested...

  The tables genereated work like this:

   An indirect array for each page of unicode character
   Each array element has an indirect pointer to one of the bytes of
   the generated bitmask.
*/

#ifdef BUILD_MAP
#include <iconv.h>
#include <glib.h>

static struct {
	char *name;
	unsigned int bit;	/* assigned bit */
} tables[] = {
	/* These are the 8bit character sets (other than iso-8859-1,
	 * which is special-cased) which are supported by both other
	 * mailers and the GNOME environment. Note that the order
	 * they're listed in is the order they'll be tried in, so put
	 * the more-popular ones first.
	 */
	{ "iso-8859-2", 0 },	/* Central/Eastern European */
	{ "iso-8859-4", 0 },	/* Baltic */
	{ "koi8-r", 0 },	/* Russian */
	{ "windows-1251", 0 },	/* Russian */
	{ "koi8-u", 0 },	/* Ukranian */
	{ "iso-8859-5", 0 },	/* Least-popular Russian encoding */
	{ "iso-8859-7", 0 },	/* Greek */
	{ "iso-8859-9", 0 },	/* Turkish */
	{ "iso-8859-13", 0 },	/* Baltic again */
	{ "iso-8859-15", 0 },	/* New-and-improved iso-8859-1, but most
				 * programs that support this support UTF8
				 */
	{ 0, 0 }
};

unsigned int encoding_map[256 * 256];

#if G_BYTE_ORDER == G_BIG_ENDIAN
#define UCS "UCS-4BE"
#else
#define UCS "UCS-4LE"
#endif

void main(void)
{
	int i, j;
	int max, min;
	int bit = 0x01;
	int k;
	int bytes;
	iconv_t cd;
	char in[128];
	guint32 out[128];
	char *inptr, *outptr;
	size_t inlen, outlen;

	/* dont count the terminator */
	bytes = ((sizeof(tables)/sizeof(tables[0]))+7-1)/8;

	for (i = 0; i < 128; i++)
		in[i] = i + 128;

	for (j = 0; tables[j].name; j++) {
		cd = iconv_open (UCS, tables[j].name);
		inptr = in;
		outptr = (char *)(out);
		inlen = sizeof (in);
		outlen = sizeof (out);
		while (iconv (cd, &inptr, &inlen, &outptr, &outlen) == -1) {
			if (errno == EILSEQ) {
				inptr++;
				inlen--;
			} else {
				printf ("%s\n", strerror (errno));
				exit (1);
			}
		}
		iconv_close (cd);

		for (i = 0; i < 128 - outlen / 4; i++) {
			encoding_map[i] |= bit;
			encoding_map[out[i]] |= bit;
		}

		tables[j].bit = bit;
		bit <<= 1;
	}

	printf("/* This file is automatically generated: DO NOT EDIT */\n\n");

	for (i=0;i<256;i++) {
		/* first, do we need this block? */
		for (k=0;k<bytes;k++) {
			for (j=0;j<256;j++) {
				if ((encoding_map[i*256 + j] & (0xff << (k*8))) != 0)
					break;
			}
			if (j < 256) {
				/* yes, dump it */
				printf("static unsigned char m%02x%x[256] = {\n\t", i, k);
				for (j=0;j<256;j++) {
					printf("0x%02x, ", (encoding_map[i*256+j] >> (k*8)) & 0xff );
					if (((j+1)&7) == 0 && j<255)
						printf("\n\t");
				}
				printf("\n};\n\n");
			}
		}
	}

	printf("struct {\n");
	for (k=0;k<bytes;k++) {
		printf("\tunsigned char *bits%d;\n", k);
	}
	printf("} camel_charmap[256] = {\n\t");
	for (i=0;i<256;i++) {
		/* first, do we need this block? */
		printf("{ ");
		for (k=0;k<bytes;k++) {
			for (j=0;j<256;j++) {
				if ((encoding_map[i*256 + j] & (0xff << (k*8))) != 0)
					break;
			}
			if (j < 256) {
				printf("m%02x%x, ", i, k);
			} else {
				printf("0, ");
			}
		}
		printf("}, ");
		if (((i+1)&7) == 0 && i<255)
			printf("\n\t");
	}
	printf("\n};\n\n");

	printf("struct {\n\tconst char *name;\n\tunsigned int bit;\n} camel_charinfo[] = {\n");
	for (j=0;tables[j].name;j++) {
		printf("\t{ \"%s\", 0x%04x },\n", tables[j].name, tables[j].bit);
	}
	printf("};\n\n");

	printf("#define charset_mask(x) \\\n");
	for (k=0;k<bytes;k++) {
		if (k!=0)
			printf("\t| ");
		else
			printf("\t");
		printf("(camel_charmap[(x)>>8].bits%d?camel_charmap[(x)>>8].bits%d[(x)&0xff]<<%d:0)", k, k, k*8);
		if (k<bytes-1)
			printf("\t\\\n");
	}
	printf("\n\n");

}

#else

#include "camel-charset-map.h"
#include "camel-charset-map-private.h"
#include "hash-table-utils.h"
#include <gal/unicode/gunicode.h>
#include <locale.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#ifdef ENABLE_THREADS
#include <pthread.h>
#endif
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

void
camel_charset_init (CamelCharset *c)
{
	c->mask = ~0;
	c->level = 0;
}

void
camel_charset_step (CamelCharset *c, const char *in, int len)
{
	register unsigned int mask;
	register int level;
	const char *inptr = in, *inend = in+len;

	mask = c->mask;
	level = c->level;

	/* check what charset a given string will fit in */
	while (inptr < inend) {
		gunichar c;
		const char *newinptr;
		newinptr = g_utf8_next_char(inptr);
		c = g_utf8_get_char(inptr);
		if (newinptr == NULL || !g_unichar_validate (c)) {
			inptr++;
			continue;
		}

		inptr = newinptr;
		if (c<=0xffff) {
			mask &= charset_mask(c);
		
			if (c>=128 && c<256)
				level = MAX(level, 1);
			else if (c>=256)
				level = MAX(level, 2);
		} else {
			mask = 0;
			level = MAX(level, 2);
		}
	}

	c->mask = mask;
	c->level = level;
}

/* gets the best charset from the mask of chars in it */
static const char *
camel_charset_best_mask(unsigned int mask)
{
	int i;

	for (i=0;i<sizeof(camel_charinfo)/sizeof(camel_charinfo[0]);i++) {
		if (camel_charinfo[i].bit & mask)
			return camel_charinfo[i].name;
	}
	return "UTF-8";
}

const char *
camel_charset_best_name (CamelCharset *charset)
{
	if (charset->level == 1)
		return "ISO-8859-1";
	else if (charset->level == 2)
		return camel_charset_best_mask (charset->mask);
	else
		return NULL;

}

/* finds the minimum charset for this string NULL means US-ASCII */
const char *
camel_charset_best (const char *in, int len)
{
	CamelCharset charset;

	camel_charset_init (&charset);
	camel_charset_step (&charset, in, len);
	return camel_charset_best_name (&charset);
}


/**
 * camel_charset_iso_to_windows:
 * @isocharset: an ISO charset
 *
 * Returns the equivalent Windows charset.
 **/
const char *
camel_charset_iso_to_windows (const char *isocharset)
{
	/* According to http://czyborra.com/charsets/codepages.html,
	 * the charset mapping is as follows:
	 *
	 * iso-8859-1  maps to windows-cp1252
	 * iso-8859-2  maps to windows-cp1250
	 * iso-8859-3  maps to windows-cp????
	 * iso-8859-4  maps to windows-cp????
	 * iso-8859-5  maps to windows-cp1251
	 * iso-8859-6  maps to windows-cp1256
	 * iso-8859-7  maps to windows-cp1253
	 * iso-8859-8  maps to windows-cp1255
	 * iso-8859-9  maps to windows-cp1254
	 * iso-8859-10 maps to windows-cp????
	 * iso-8859-11 maps to windows-cp????
	 * iso-8859-12 maps to windows-cp????
	 * iso-8859-13 maps to windows-cp1257
	 *
	 * Assumptions:
	 *  - I'm going to assume that since iso-8859-4 and
	 *    iso-8859-13 are Baltic that it also maps to
	 *    windows-cp1257.
	 */
	
	if (!strcasecmp (isocharset, "iso-8859-1"))
		return "windows-cp1252";
	else if (!strcasecmp (isocharset, "iso-8859-2"))
		return "windows-cp1250";
	else if (!strcasecmp (isocharset, "iso-8859-4"))
		return "windows-cp1257";
	else if (!strcasecmp (isocharset, "iso-8859-5"))
		return "windows-cp1251";
	else if (!strcasecmp (isocharset, "iso-8859-6"))
		return "windows-cp1256";
	else if (!strcasecmp (isocharset, "iso-8859-7"))
		return "windows-cp1253";
	else if (!strcasecmp (isocharset, "iso-8859-8"))
		return "windows-cp1255";
	else if (!strcasecmp (isocharset, "iso-8859-9"))
		return "windows-cp1254";
	else if (!strcasecmp (isocharset, "iso-8859-13"))
		return "windows-cp1257";
	
	return isocharset;
}

#endif /* !BUILD_MAP */


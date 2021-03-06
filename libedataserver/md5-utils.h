/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD5Context structure, pass it to rpmMD5Init, call rpmMD5Update as
 * needed on buffers full of bytes, and then call rpmMD5Final, which
 * will fill a supplied 16-byte array with the digest.
 */

/* parts of this file are :
 * Written March 1993 by Branko Lankester
 * Modified June 1993 by Colin Plumb for altered md5.c.
 * Modified October 1995 by Erik Troan for RPM
 */


#ifndef MD5_UTILS_H
#define MD5_UTILS_H

/* This API is deprecated.  Use GLib's GChecksum instead. */

#ifndef EDS_DISABLE_DEPRECATED

#include <glib.h>

G_BEGIN_DECLS

/**
 * MD5Context:
 *
 * A buffer structure used for md5 calculation.
 **/
typedef struct _MD5Context {
	/*< private >*/
	guint32 buf[4];
	guint32 bits[2];
	guchar in[64];
} MD5Context;


void md5_get_digest (const gchar *buffer, gint buffer_size, guchar digest[16]);

/* use this one when speed is needed */
/* for use in provider code only */
void md5_get_digest_from_file (const gchar *filename, guchar digest[16]);

/* raw routines */
void md5_init (MD5Context *ctx);
void md5_update (MD5Context *ctx, const guchar *buf, guint32 len);
void md5_final (MD5Context *ctx, guchar digest[16]);

G_END_DECLS

#endif  /* EDS_DISABLE_DEPRECATED */

#endif	/* MD5_UTILS_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef CAMEL_CIPHER_CONTEXT_H
#define CAMEL_CIPHER_CONTEXT_H

#include <camel/camel-session.h>
#include <camel/camel-stream.h>
#include <camel/camel-exception.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define CAMEL_CIPHER_CONTEXT_TYPE     (camel_cipher_context_get_type ())
#define CAMEL_CIPHER_CONTEXT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_CIPHER_CONTEXT_TYPE, CamelCipherContext))
#define CAMEL_CIPHER_CONTEXT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_CIPHER_CONTEXT_TYPE, CamelCipherContextClass))
#define CAMEL_IS_CIPHER_CONTEXT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_CIPHER_CONTEXT_TYPE))

typedef struct _CamelCipherValidity CamelCipherValidity;

typedef enum {
	CAMEL_CIPHER_HASH_DEFAULT,
	CAMEL_CIPHER_HASH_MD2,
	CAMEL_CIPHER_HASH_MD5,
	CAMEL_CIPHER_HASH_SHA1,
	CAMEL_CIPHER_HASH_RIPEMD160
} CamelCipherHash;

typedef struct _CamelCipherContext {
	CamelObject parent_object;
	
	struct _CamelCipherContextPrivate *priv;
	
	CamelSession *session;
	
	/* these MUST be set by implementors */
	const char *sign_protocol;
	const char *encrypt_protocol;
} CamelCipherContext;

typedef struct _CamelCipherContextClass {
	CamelObjectClass parent_class;
	
	int                   (*sign)      (CamelCipherContext *ctx, const char *userid, CamelCipherHash hash,
					    CamelStream *istream, CamelStream *ostream, CamelException *ex);
	
	CamelCipherValidity * (*verify)    (CamelCipherContext *context, CamelCipherHash hash,
					    CamelStream *istream, CamelStream *sigstream,
					    CamelException *ex);
	
	int                   (*encrypt)   (CamelCipherContext *context, gboolean sign, const char *userid,
					    GPtrArray *recipients, CamelStream *istream, CamelStream *ostream,
					    CamelException *ex);
	
	int                   (*decrypt)   (CamelCipherContext *context, CamelStream *istream, CamelStream *ostream,
					    CamelException *ex);
	
	CamelCipherHash	      (*id_to_hash)(CamelCipherContext *context, const char *id);
	const char *	      (*hash_to_id)(CamelCipherContext *context, CamelCipherHash hash);
	
} CamelCipherContextClass;

CamelType            camel_cipher_context_get_type (void);

CamelCipherContext  *camel_cipher_context_new (CamelSession *session);

void                 camel_cipher_context_construct (CamelCipherContext *context, CamelSession *session);

/* cipher routines */
int                  camel_cipher_sign (CamelCipherContext *context, const char *userid, CamelCipherHash hash,
					CamelStream *istream, CamelStream *ostream, CamelException *ex);

CamelCipherValidity *camel_cipher_verify (CamelCipherContext *context, CamelCipherHash hash,
					  CamelStream *istream, CamelStream *sigstream,
					  CamelException *ex);

int                  camel_cipher_encrypt (CamelCipherContext *context, gboolean sign, const char *userid,
					   GPtrArray *recipients, CamelStream *istream, CamelStream *ostream,
					   CamelException *ex);

int                  camel_cipher_decrypt (CamelCipherContext *context, CamelStream *istream, CamelStream *ostream,
					   CamelException *ex);

/* cipher context util routines */
CamelCipherHash	     camel_cipher_id_to_hash (CamelCipherContext *context, const char *id);
const char *	     camel_cipher_hash_to_id (CamelCipherContext *context, CamelCipherHash hash);

/* CamelCipherValidity utility functions */
CamelCipherValidity *camel_cipher_validity_new (void);

void                 camel_cipher_validity_init (CamelCipherValidity *validity);

gboolean             camel_cipher_validity_get_valid (CamelCipherValidity *validity);

void                 camel_cipher_validity_set_valid (CamelCipherValidity *validity, gboolean valid);

char                *camel_cipher_validity_get_description (CamelCipherValidity *validity);

void                 camel_cipher_validity_set_description (CamelCipherValidity *validity, const char *description);

void                 camel_cipher_validity_clear (CamelCipherValidity *validity);

void                 camel_cipher_validity_free (CamelCipherValidity *validity);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_CIPHER_CONTEXT_H */

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-pkcs7-context.h"

#include "camel-stream-fs.h"
#include "camel-stream-mem.h"

#include <nss.h>
#include <cert.h>
#include <secpkcs7.h>

#include <gtk/gtk.h> /* for _() macro */

#define d(x)

struct _CamelPkcs7ContextPrivate {
	CERTCertDBHandle *certdb;
};


static int                  pkcs7_sign (CamelCipherContext *ctx, const char *userid, CamelCipherHash hash,
					CamelStream *istream, CamelStream *ostream, CamelException *ex);
static int                  pkcs7_clearsign (CamelCipherContext *context, const char *userid,
					     CamelCipherHash hash, CamelStream *istream,
					     CamelStream *ostream, CamelException *ex);
static CamelCipherValidity *pkcs7_verify (CamelCipherContext *context, CamelStream *istream,
					  CamelStream *sigstream, CamelException *ex);
static int                  pkcs7_encrypt (CamelCipherContext *context, gboolean sign, const char *userid,
					   GPtrArray *recipients, CamelStream *istream, CamelStream *ostream,
					   CamelException *ex);
static int                  pkcs7_decrypt (CamelCipherContext *context, CamelStream *istream,
					   CamelStream *ostream, CamelException *ex);


static CamelCipherContextClass *parent_class;

static void
camel_pkcs7_context_init (CamelPkcs7Context *context)
{
	context->priv = g_new0 (struct _CamelPkcs7ContextPrivate, 1);
}

static void
camel_pkcs7_context_finalise (CamelObject *o)
{
	CamelPkcs7Context *context = (CamelPkcs7Context *)o;
	
	CERT_ClosePermCertDB (context->priv->certdb);
	g_free (context->priv->certdb);
	
	g_free (context->priv);
}

static void
camel_pkcs7_context_class_init (CamelPkcs7ContextClass *camel_pkcs7_context_class)
{
	CamelCipherContextClass *camel_cipher_context_class =
		CAMEL_CIPHER_CONTEXT_CLASS (camel_pkcs7_context_class);
	
	parent_class = CAMEL_CIPHER_CONTEXT_CLASS (camel_type_get_global_classfuncs (camel_cipher_context_get_type ()));
	
	camel_cipher_context_class->sign = pkcs7_sign;
	camel_cipher_context_class->clearsign = pkcs7_clearsign;
	camel_cipher_context_class->verify = pkcs7_verify;
	camel_cipher_context_class->encrypt = pkcs7_encrypt;
	camel_cipher_context_class->decrypt = pkcs7_decrypt;
}

CamelType
camel_pkcs7_context_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_cipher_context_get_type (),
					    "CamelPkcs7Context",
					    sizeof (CamelPkcs7Context),
					    sizeof (CamelPkcs7ContextClass),
					    (CamelObjectClassInitFunc) camel_pkcs7_context_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_pkcs7_context_init,
					    (CamelObjectFinalizeFunc) camel_pkcs7_context_finalise);
	}
	
	return type;
}


/**
 * camel_pkcs7_context_new:
 * @session: CamelSession
 * @certdb: certificate db
 *
 * This creates a new CamelPkcs7Context object which is used to sign,
 * verify, encrypt and decrypt streams.
 *
 * Return value: the new CamelPkcs7Context
 **/
CamelPkcs7Context *
camel_pkcs7_context_new (CamelSession *session, const char *certdb)
{
	CamelPkcs7Context *context;
	CERTCertDBHandle *handle;
	
	g_return_val_if_fail (session != NULL, NULL);
	
	context = CAMEL_PKCS7_CONTEXT (camel_object_new (CAMEL_PKCS7_CONTEXT_TYPE));
	
	camel_cipher_construct (CAMEL_CIPHER_CONTEXT (context), session);
	
	handle = g_new0 (CERTCertDBHandle, 1);
	if (certdb) {
		if (!CERT_OpenCertDBFilename (handle, certdb, FALSE)) {
			g_free (handle);
			return NULL;
		}
	} else {
		if (!CERT_OpenVolatileCertDB (handle)) {
			g_free (handle);
			return NULL;
		}
	}
	
	context->priv->certdb = handle;
	
	return context;
}

/*----------------------------------------------------------------------*
 *                     Public crypto functions
 *----------------------------------------------------------------------*/

struct _GetPasswdData {
	CamelSession *session;
	CamelException *ex;
	const char *userid;
};

static SECItem *
get_zero_len_passwd (SECKEYKeyDBHandle *handle)
{
	SECItem *pwitem;
	SECStatus rv;
	
	/* hash the empty string as a password */
	pwitem = SECKEY_DeriveKeyDBPassword (handle, "");
	if (pwitem == NULL)
		return NULL;
	
	/* check to see if this is the right password */
	rv = SECKEY_CheckKeyDBPassword (handle, pwitem);
	if (rv == SECFailure)
		return NULL;
	
	return pwitem;
}

static SECItem *
get_password (void *arg, SECKEYKeyDBHandle *handle)
{
	CamelSession *session = ((struct _GetPasswdData *) arg)->session;
	CamelException *ex = ((struct _GetPasswdData *) arg)->ex;
	const char *userid = ((struct _GetPasswdData *) arg)->userid;
	char *prompt, *passwd = NULL;
	SECItem *pwitem;
	SECStatus rv;
	
	/* Check to see if zero length password or not */
	pwitem = get_zero_len_passwd (handle);
	if (pwitem)
		return pwitem;
	
	prompt = g_strdup_printf (_("Please enter your password for %s"), userid);
	passwd = camel_session_get_password (session, prompt, FALSE, TRUE,
					     NULL, userid, NULL);
	g_free (prompt);
	
	/* hash the password */
	pwitem = SECKEY_DeriveKeyDBPassword (handle, passwd ? passwd : "");
	
	/* clear out the password strings */
	if (passwd) {
		memset (passwd, 0, strlen (passwd));
		g_free (passwd);
	}
	
	if (pwitem == NULL) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Error hashing password."));
		
		return NULL;
	}
	
	/* confirm the password */
	rv = SECKEY_CheckKeyDBPassword (handle, pwitem);
	if (rv) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Invalid password."));
		
		SECITEM_ZfreeItem (pwitem, PR_TRUE);
		
		return NULL;
	}
	
	return pwitem;
}

static HASH_HashType
camel_cipher_hash_to_nss (CamelCipherHash hash)
{
	switch (hash) {
	case CAMEL_CIPHER_HASH_DEFAULT:
		return HASH_AlgSHA1;
	case CAMEL_CIPHER_HASH_MD2:
		return HASH_AlgMD2;
	case CAMEL_CIPHER_HASH_MD5:
		return HASH_AlgMD5;
	case CAMEL_CIPHER_HASH_SHA1:
		return HASH_AlgSHA1;
	}
	
	return HASH_AlgNULL;
}

static SECOidTag
nss_hash_to_sec_oid (HASH_HashType hash)
{
	switch (hash) {
	case HASH_AlgMD2:
		return SEC_OID_MD2;
	case HASH_AlgMD5:
		return SEC_OID_MD5;
	case Hash_AlgSHA1:
		return SEC_OID_SHA1;
	default:
		g_assert_not_reached ();
		return 0;
	}
}

static int
pkcs7_digest (SECItem *data, char *digestdata, unsigned int *len, unsigned int maxlen, HASH_HashType hash)
{
	SECHashObject *hashObj;
	void *hashcx;
	
	hashObj = &SECHashObjects[hash];
	
	hashcx = (* hashObj->create)();
	if (hashcx == NULL)
		return -1;
	
	(* hashObj->begin)(hashcx);
	(* hashObj->update)(hashcx, data->data, data->len);
	(* hashObj->end)(hashcx, (unsigned char *)digestdata, len, maxlen);
	(* hashObj->destroy)(hashcx, PR_TRUE);
	
	return 0;
}

static void
sec_output_cb (void *arg, const char *buf, unsigned long len)
{
	CamelStream *stream;
	
	stream = CAMEL_STREAM (arg);
	camel_stream_write (stream, buf, len);
}

static int
pkcs7_sign (CamelCipherContext *ctx, const char *userid, CamelCipherHash hash,
	    CamelStream *istream, CamelStream *ostream, CamelException *ex)
{
	CamelPkcs7Context *context = CAMEL_PKCS7_CONTEXT (ctx);
	struct _GetPasswdData *data;
	SEC_PKCS7ContentInfo *cinfo;
	SECItem data2sign, digest;
	HASH_HashType hash_type;
	CERTCertificate *cert;
	guchar digestdata[32];
	CamelStream *stream;
	GByteArray *buf;
	guint len;
	
	g_return_val_if_fail (userid != NULL, -1);
	g_return_val_if_fail (istream != NULL, -1);
	g_return_val_if_fail (ostream != NULL, -1);
	
	stream = camel_stream_mem_new ();
	camel_stream_write_to_stream (istream, stream);
	buf = CAMEL_STREAM_MEM (stream)->buffer;
	data2sign.data = buf->data;
	data2sign.len = buf->len;
	
	hash_type = camel_cipher_hash_to_nss (hash);
	pkcs7_digest (&data2sign, digestdata, &len, 32, hash_type);
	digest.data = (unsigned char *)digestdata;
	digest.len = len;
	
	camel_object_unref (CAMEL_OBJECT (stream));
	
	cert = CERT_FindCertByNickname (context->priv->certdb, userid);
	if (!cert) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not sign: certificate not found for \"%s\"."),
				      userid);
		return -1;
	}
	
	cinfo = SEC_PKCS7CreateSignedData (cert, certUsageEmailSigner, NULL,
					   nss_hash_to_sec_oid (hash_type),
					   &digest, NULL, NULL);
	
	SEC_PKCS7IncludeCertChain (cinfo, NULL);
	
	data = g_new (struct _GetPasswdData, 1);
	data->session = ctx->session;
	data->userid = userid;
	data->ex = ex;
	
	SEC_PKCS7Encode (cinfo, sec_output_cb, ostream, NULL, get_password, data);
	
	g_free (data);
	
	SEC_PKCS7DestroyContentInfo (cinfo);
	
	return 0;
}


static int
pkcs7_clearsign (CamelCipherContext *ctx, const char *userid, CamelCipherHash hash,
		 CamelStream *istream, CamelStream *ostream, CamelException *ex)
{
	CamelPkcs7Context *context = CAMEL_PKCS7_CONTEXT (ctx);
	struct _GetPasswdData *data;
	SEC_PKCS7ContentInfo *cinfo;
	HASH_HashType hash_type;
	CERTCertificate *cert;
	CamelStream *stream;
	SECItem data2sign;
	GByteArray *buf;
	
	g_return_val_if_fail (userid != NULL, -1);
	g_return_val_if_fail (istream != NULL, -1);
	g_return_val_if_fail (ostream != NULL, -1);
	
	hash_type = camel_cipher_hash_to_nss (hash);
	
	cert = CERT_FindCertByNickname (context->priv->certdb, userid);
	if (!cert) {
		camel_object_unref (CAMEL_OBJECT (stream));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not clearsign: certificate not found for \"%s\"."),
				      userid);
		return -1;
	}
	
	cinfo = SEC_PKCS7CreateSignedData (cert, certUsageEmailSigner, NULL,
					   nss_hash_to_sec_oid (hash_type),
					   NULL, NULL, NULL);
	
	stream = camel_stream_mem_new ();
	camel_stream_write_to_stream (istream, stream);
	buf = CAMEL_STREAM_MEM (stream)->buffer;
	data2sign.data = buf->data;
	data2sign.len = buf->len;
	SEC_PKCS7SetContent (cinfo, (char *)data2sign.data, data2sign.len);
	camel_object_unref (CAMEL_OBJECT (stream));
	
	SEC_PKCS7IncludeCertChain (cinfo, NULL);
	
	data = g_new (struct _GetPasswdData, 1);
	data->session = ctx->session;
	data->userid = userid;
	data->ex = ex;
	
	SEC_PKCS7Encode (cinfo, sec_output_cb, ostream, NULL, get_password, data);
	
	g_free (data);
	
	SEC_PKCS7DestroyContentInfo (cinfo);
	
	return 0;
}

#if 0
/* this is just meant as a reference so I can see what the valid enums are */
typedef enum {
	certUsageSSLClient,
	certUsageSSLServer,
	certUsageSSLServerWithStepUp,
	certUsageSSLCA,
	certUsageEmailSigner,
	certUsageEmailRecipient,
	certUsageObjectSigner,
	certUsageUserCertImport,
	certUsageVerifyCA,
	certUsageProtectedObjectSigner,
	certUsageStatusResponder,
	certUsageAnyCA
} SECCertUsage;
#endif

#if 0
static HASH_HashType
AlgorithmToHashType (SECAlgorithmID *digestAlgorithms)
{
	SECOidTag tag;
	
	tag = SECOID_GetAlgorithmTag (digestAlgorithms);
	
	switch (tag) {
	case SEC_OID_MD2:
		return HASH_AlgMD2;
	case SEC_OID_MD5:
		return HASH_AlgMD5;
	case SEC_OID_SHA1:
		return HASH_AlgSHA1;
	default:
		g_assert_not_reached ();
		return HASH_AlgNULL;
	}
}
#endif

/* FIXME: god knows if this code works, NSS "docs" are so not helpful at all */
static CamelCipherValidity *
pkcs7_verify (CamelCipherContext *ctx, CamelCipherHash hash, CamelStream *istream,
	      CamelStream *sigstream, CamelException *ex)
{
	CamelPkcs7Context *context = CAMEL_PKCS7_CONTEXT (ctx);
	CamelCipherValidity *valid = NULL;
	SEC_PKCS7ContentInfo *cinfo;
	SECCertUsage usage;
	GByteArray *plaintext;
	CamelStream *stream;
	
	/* create our ContentInfo object */
	stream = camel_stream_mem_new ();
	camel_stream_write_to_stream (istream, stream);
	plaintext = CAMEL_STREAM_MEM (stream)->buffer;
	cinfo = SEC_PKCS7CreateData ();
	SEC_PKCS7SetContent (cinfo, plaintext->data, plaintext->len);
	camel_object_unref (CAMEL_OBJECT (stream));
	
	usage = certUsageEmailSigner;  /* just a guess. or maybe certUsageVerifyCA?? */
	
	valid = camel_cipher_validity_new ();
	
	if (sigstream) {
		HASH_HashType digest_type;
		GByteArray *signature;
		SECItem digest;
		
		/* create our digest object */
		stream = camel_stream_mem_new ();
		camel_stream_write_to_stream (sigstream, stream);
		signature = CAMEL_STREAM_MEM (stream)->buffer;
		digest.data = signature->data;
		digest.len = signature->len;
		
		switch (hash) {
		default:
		case CAMEL_CIPHER_HASH_DEFAULT:
			digest_type = HASH_AlgNULL;
			break;
		case CAMEL_CIPHER_HASH_MD2:
			digest_type = HASH_AlgMD2;
			break;
		case CAMEL_CIPHER_HASH_MD5:
			digest_type = HASH_AlgMD5;
			break;
		case CAMEL_CIPHER_HASH_SHA1:
			digest_type = HASH_AlgSHA1;
			break;
		}
		
		valid->valid = SEC_PKCS7VerifyDetachedSignature (cinfo, usage, &digest, digest_type, PR_FALSE);
		camel_object_unref (CAMEL_OBJECT (stream));
	} else {
		valid->valid = SEC_PKCS7VerifySignature (cinfo, usage, PR_FALSE);
	}
	
	SEC_PKCS7DestroyContentInfo (cinfo);
	
	/* FIXME: set a meaningful description...in UTF8 */
	camel_cipher_validity_set_description (valid, "");
	
	return valid;
}

/* FIXME: we need to respect the 'sign' argument... */
static int
pkcs7_encrypt (CamelCipherContext *ctx, gboolean sign, const char *userid, GPtrArray *recipients,
	       CamelStream *istream, CamelStream *ostream, CamelException *ex)
{
	CamelPkcs7Context *context = CAMEL_PKCS7_CONTEXT (ctx);
	const char *invalid_userkey = NULL;
	SEC_PKCS7ContentInfo *cinfo = NULL;
	CERTCertificate *cert, *usercert;
	SEC_PKCS7EncoderContext *ecx;
	struct _GetPasswdData *data;
	CamelStream *stream = NULL;
	SECItem secdata;
	GByteArray *buf;
	int i = 0;
	
	g_return_val_if_fail (userid != NULL, -1);
	g_return_val_if_fail (recipients != NULL, -1);
	g_return_val_if_fail (recipients->len != 0, -1);
	g_return_val_if_fail (istream != NULL, -1);
	g_return_val_if_fail (ostream != NULL, -1);

#if 0
	/* this isn't needed until we respect the 'sign' argument... */
	usercert = CERT_FindCertByNickname (context->priv->certdb, userid);
	if (!usercert) {
		invalid_userkey = userid;
		goto exception;
	}
#endif
	
	cert = CERT_FindCertByNickname (context->priv->certdb, recipients->pdata[i]);
	if (!cert) {
		invalid_userkey = recipients->pdata[i];
		goto exception;
	}
	
	data = g_new (struct _GetPasswdData, 1);
	data->session = session;
	data->userid = userid;
	data->ex = ex;
	
	/* FIXME: extend CamelCipherContext to allow selecting an encryption algorithm?? */
	cinfo = SEC_PKCS7CreateEncryptedData (SEC_OID_DES_EDE3_CBC, 0, 
					      get_password, data);
	
	g_free (data);
	
	if (!cinfo) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not encrypt: failed to create enveloped data."));
		goto exception;
	}
	
	for (i++; i < recipients->len; i++) {
		SECStatus retval;
		
		cert = CERT_FindCertByNickname (context->priv->certdb, recipients->pdata[i]);
		if (!cert) {
			invalid_userkey = recipients->pdata[i];
			goto exception;
		}
		
		retval = SEC_PKCS7AddRecipient (cinfo, cert, certUsageEmailRecipient, NULL);
		if (retval != SECSuccess) {
			invalid_userkey = recipients->pdata[i];
			goto exception;
		}
	}
	
	ecx = SEC_PKCS7EncoderStart (cinfo, sec_output_cb, ostream, NULL);
	if (ecx == NULL) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not encrypt: failed to create encryption context."));
		goto exception;
	}
	
	stream = camel_stream_mem_new ();
	camel_stream_write_to_stream (istream, stream);
	buf = CAMEL_STREAM_MEM (stream)->buffer;
	if (SEC_PKCS7EncoderUpdate (ecx, buf->data, buf->len) != SECSuccess)
		goto exception;
	
	camel_object_unref (CAMEL_OBJECT (stream));
	stream = NULL;
	
	if (SEC_PKCS7EncoderFinish (ecx, NULL, NULL) != SECSuccess)
		goto exception;
	
	SEC_PKCS7DestroyContentInfo (cinfo);
	
	return 0;
	
 exception:
	
	if (stream)
		camel_object_unref (CAMEL_OBJECT (stream));
	
	if (cinfo)
		SEC_PKCS7DestroyContentInfo (cinfo);
	
	if (invalid_userkey) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not encrypt data: invalid user key: \"%s\"."),
				      invalid_userkey);
	}
	
	if (!camel_exception_is_set (ex)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not encrypt: encoding failed."));
	}
	
	return -1;
}

static PRBool
decryption_allowed (SECAlgorithmID *algid, PK11SymKey *key)
{
	return PR_TRUE;
}

static int
pkcs7_decrypt (CamelCipherContext *ctx, CamelStream *istream,
	       CamelStream *ostream, CamelException *ex)
{
	CamelPkcs7Context *context = CAMEL_PKCS7_CONTEXT (ctx);
	struct _GetPasswdData *data;
	SEC_PKCS7DecoderContext *dcx;
	SEC_PKCS7ContentInfo *cinfo;
	CamelStream *stream = NULL;
	SECItem secdata;
	GByteArray *buf;
	
	g_return_val_if_fail (istream != NULL, -1);
	g_return_val_if_fail (ostream != NULL, -1);
	
	stream = camel_stream_mem_new ();
	camel_stream_write_to_stream (istream, stream);
	buf = CAMEL_STREAM_MEM (stream)->buffer;
	secdata.data = buf->data;
	secdata.len = buf->len;
	
	data = g_new (struct _GetPasswdData, 1);
	data->session = ctx->session;
	data->userid = NULL;
	data->ex = ex;
	
	dcx = SEC_PKCS7DecoderStart (sec_output_cb, ostream, get_password, data,
				     NULL, NULL, decryption_allowed);
	if (dcx == NULL)
		goto exception;
	
	SEC_PKCS7DecoderUpdate (dcx, secdata.data, secdata.len);
	cinfo = SEC_PKCS7DecoderFinish (dcx);
	
	camel_object_unref (CAMEL_OBJECT (stream));
	g_free (data);
	
	if (cinfo == NULL) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Failed to decrypt: Unknown"));
		return -1;
	}
	
	SEC_PKCS7DestroyContentInfo (cinfo);
	
	return 0;
	
 exception:
	
	if (stream)
		camel_object_unref (CAMEL_OBJECT (stream));
	
	return -1;
}

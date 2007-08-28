/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
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
 *
 */

#ifndef CAMEL_SASL_CRAM_MD5_H
#define CAMEL_SASL_CRAM_MD5_H

#include <camel/camel-sasl.h>

#define CAMEL_SASL_CRAM_MD5_TYPE     (camel_sasl_cram_md5_get_type ())
#define CAMEL_SASL_CRAM_MD5(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SASL_CRAM_MD5_TYPE, CamelSaslCramMd5))
#define CAMEL_SASL_CRAM_MD5_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SASL_CRAM_MD5_TYPE, CamelSaslCramMd5Class))
#define CAMEL_IS_SASL_CRAM_MD5(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SASL_CRAM_MD5_TYPE))

G_BEGIN_DECLS

typedef struct _CamelSaslCramMd5 {
	CamelSasl parent_object;

} CamelSaslCramMd5;


typedef struct _CamelSaslCramMd5Class {
	CamelSaslClass parent_class;
	
} CamelSaslCramMd5Class;


/* Standard Camel function */
CamelType camel_sasl_cram_md5_get_type (void);

extern CamelServiceAuthType camel_sasl_cram_md5_authtype;

G_END_DECLS

#endif /* CAMEL_SASL_CRAM_MD5_H */

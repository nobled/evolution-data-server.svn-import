/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-wrapper.h: data wrapper for offline IMAP data */

/*
 * Copyright 2000 Helix Code, Inc. (http://www.helixcode.com)
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


#ifndef CAMEL_IMAP_WRAPPER_H
#define CAMEL_IMAP_WRAPPER_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-data-wrapper.h>

#define CAMEL_IMAP_WRAPPER_TYPE     (camel_imap_wrapper_get_type ())
#define CAMEL_IMAP_WRAPPER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAP_WRAPPER_TYPE, CamelImapWrapper))
#define CAMEL_IMAP_WRAPPER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAP_WRAPPER_TYPE, CamelImapWrapperClass))
#define CAMEL_IS_IMAP_WRAPPER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAP_WRAPPER_TYPE))

typedef struct
{
	CamelDataWrapper parent_object;

	struct _CamelImapWrapperPrivate *priv;

	CamelFolder *folder;
	char *uid, *part_spec;
	CamelMimePart *part;
} CamelImapWrapper;

typedef struct {
	CamelDataWrapperClass parent_class;

} CamelImapWrapperClass;

/* Standard Camel function */
CamelType camel_imap_wrapper_get_type (void);

/* Constructor */
CamelDataWrapper *camel_imap_wrapper_new (CamelFolder *folder,
					  CamelContentType *type,
					  const char *uid,
					  const char *part_spec,
					  CamelMimePart *part);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_DATA_WRAPPER_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef CAMEL_MAILDIR_STORE_H
#define CAMEL_MAILDIR_STORE_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif				/* __cplusplus } */

#include "camel-local-store.h"

#define CAMEL_MAILDIR_STORE_TYPE     (camel_maildir_store_get_type ())
#define CAMEL_MAILDIR_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MAILDIR_STORE_TYPE, CamelMaildirStore))
#define CAMEL_MAILDIR_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MAILDIR_STORE_TYPE, CamelMaildirStoreClass))
#define CAMEL_IS_MAILDIR_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MAILDIR_STORE_TYPE))

typedef struct {
	CamelLocalStore parent_object;
	
} CamelMaildirStore;

typedef struct {
	CamelLocalStoreClass parent_class;
	
} CamelMaildirStoreClass;

/* public methods */

/* Standard Camel function */
CamelType camel_maildir_store_get_type(void);

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* CAMEL_MAILDIR_STORE_H */

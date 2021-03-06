/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-groupwise.h - Groupwise contact backend.
 *
 * Copyright (C) 2005 Novell, Inc.
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
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 */

#ifndef __E_BOOK_BACKEND_GROUPWISE_H__
#define __E_BOOK_BACKEND_GROUPWISE_H__

#include <libedata-book/e-book-backend-sync.h>
#include "db.h"

#define E_TYPE_BOOK_BACKEND_GROUPWISE        (e_book_backend_groupwise_get_type ())
#define E_BOOK_BACKEND_GROUPWISE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_GROUPWISE, EBookBackendGroupwise))
#define E_BOOK_BACKEND_GROUPWISE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_GROUPWISE, EBookBackendGroupwiseClass))
#define E_IS_BOOK_BACKEND_GROUPWISE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_GROUPWISE))
#define E_IS_BOOK_BACKEND_GROUPWISE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_GROUPWISE))
#define E_BOOK_BACKEND_GROUPWISE_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_GROUPWISE, EBookBackenGroupwiseClass))
typedef struct _EBookBackendGroupwisePrivate EBookBackendGroupwisePrivate;

typedef struct {
	EBookBackend         parent_object;
	EBookBackendGroupwisePrivate *priv;
} EBookBackendGroupwise;

typedef struct {
	EBookBackendClass parent_class;
} EBookBackendGroupwiseClass;

EBookBackend *e_book_backend_groupwise_new      (void);
GType       e_book_backend_groupwise_get_type (void);

#endif /* ! __E_BOOK_BACKEND_GROUPWISE_H__ */




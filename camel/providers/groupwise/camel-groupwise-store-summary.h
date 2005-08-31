/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002 Ximian Inc.
 *
 * Authors: Parthasarathi Susarla <sparthasrathi@novell.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef _CAMEL_GW_STORE_SUMMARY_H
#define _CAMEL_GW_STORE_SUMMARY_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <camel/camel-object.h>
#include <camel/camel-store-summary.h>

#define CAMEL_GW_STORE_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_groupwise_store_summary_get_type (), CamelGroupwiseStoreSummary)
#define CAMEL_GW_STORE_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_groupwise_store_summary_get_type (), CamelGroupwiseStoreSummaryClass)
#define CAMEL_IS_GW_STORE_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_groupwise_store_summary_get_type ())

typedef struct _CamelGroupwiseStoreSummary      CamelGroupwiseStoreSummary;
typedef struct _CamelGroupwiseStoreSummaryClass CamelGroupwiseStoreSummaryClass;

typedef struct _CamelGroupwiseStoreInfo CamelGroupwiseStoreInfo;

enum {
	CAMEL_GW_STORE_INFO_FULL_NAME = CAMEL_STORE_INFO_LAST,
	CAMEL_GW_STORE_INFO_LAST,
};

struct _CamelGroupwiseStoreInfo {
	CamelStoreInfo info;
	char *full_name;
};

typedef struct _CamelGroupwiseStoreNamespace CamelGroupwiseStoreNamespace;

struct _CamelGroupwiseStoreNamespace {
	char *path;             /* display path */
	char *full_name;        /* real name */
	char sep;               /* directory separator */
};

struct _CamelGroupwiseStoreSummary {
	CamelStoreSummary summary;

	struct _CamelGroupwiseStoreSummaryPrivate *priv;

	/* header info */
	guint32 version;        /* version of base part of file */
	guint32 capabilities;
	CamelGroupwiseStoreNamespace *namespace; /* eventually to be a list */
};

struct _CamelGroupwiseStoreSummaryClass {
	CamelStoreSummaryClass summary_class;
};

CamelType                        camel_groupwise_store_summary_get_type      (void);
CamelGroupwiseStoreSummary      *camel_groupwise_store_summary_new        (void);
CamelGroupwiseStoreInfo *camel_groupwise_store_summary_full_name(CamelGroupwiseStoreSummary *s, const char *full_name) ;
CamelGroupwiseStoreInfo *camel_groupwise_store_summary_add_from_full(CamelGroupwiseStoreSummary *s, const char *full, char dir_sep) ;

char *camel_groupwise_store_summary_full_to_path(CamelGroupwiseStoreSummary *s, const char *full_name, char dir_sep) ;
char *camel_groupwise_store_summary_path_to_full(CamelGroupwiseStoreSummary *s, const char *path, char dir_sep) ;
char *camel_groupwise_store_summary_full_from_path(CamelGroupwiseStoreSummary *s, const char *path) ;

CamelGroupwiseStoreNamespace *camel_groupwise_store_summary_namespace_new(CamelGroupwiseStoreSummary *s, const char *full_name, char dir_sep) ;
CamelGroupwiseStoreNamespace *camel_groupwise_store_summary_namespace_find_path(CamelGroupwiseStoreSummary *s, const char *path) ;
void camel_groupwise_store_summary_namespace_set(CamelGroupwiseStoreSummary *s, CamelGroupwiseStoreNamespace *ns) ;

#define camel_groupwise_store_info_full_name(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_STORE_INFO_LAST))

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _CAMEL_IMAP_STORE_SUMMARY_H */


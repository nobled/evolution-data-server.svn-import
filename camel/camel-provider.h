/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-provider.h :  provider definition  */

/*
 *
 * Authors:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *  Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
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


#ifndef CAMEL_PROVIDER_H
#define CAMEL_PROVIDER_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-types.h>
#include <camel/camel-object.h>

#define CAMEL_PROVIDER(obj) ((CamelProvider *)(obj))

typedef enum {
	CAMEL_PROVIDER_STORE,
	CAMEL_PROVIDER_TRANSPORT,
	CAMEL_NUM_PROVIDER_TYPES
} CamelProviderType;

extern char *camel_provider_type_name[CAMEL_NUM_PROVIDER_TYPES];

/* Provider flags:
 *
 * _IS_REMOTE   the provider works with remote data
 * _IS_LOCAL    it can be used as a backend for local folder
 *                tree folders. (*Not* just the opposite of _IS_REMOTE)
 * _IS_SOURCE   mail arrives there, so it should be offered as an
 *                option in the mail config dialog.
 * _IS_STORAGE  mail is stored there. it will appear in the folder tree.
 * _IS_EXTERNAL it appears in the folder tree but is not created by
 *                the mail component.
 */
#define CAMEL_PROVIDER_IS_REMOTE	(1 << 0)
#define CAMEL_PROVIDER_IS_LOCAL		(1 << 1)
#define CAMEL_PROVIDER_IS_EXTERNAL	(1 << 2)
#define CAMEL_PROVIDER_IS_SOURCE	(1 << 3)
#define CAMEL_PROVIDER_IS_STORAGE	(1 << 4)
#define CAMEL_PROVIDER_SUPPORTS_SSL	(1 << 5)


/* Flags for url_flags. "ALLOW" means the config dialog will let
 * the user configure it. "NEED" implies "ALLOW" but means the user
 * must configure it. Service code can assume that any url part
 * for which it has set the NEED flag will be set when the service
 * is created.
 */
#define CAMEL_URL_PART_USER	 (1 << 0)
#define CAMEL_URL_PART_AUTH	 (1 << 1)
#define CAMEL_URL_PART_PASSWORD	 (1 << 2)
#define CAMEL_URL_PART_HOST	 (1 << 3)
#define CAMEL_URL_PART_PORT	 (1 << 4)
#define CAMEL_URL_PART_PATH	 (1 << 5)

#define CAMEL_URL_PART_NEED	       6

/* Use these macros to test a provider's url_flags */
#define CAMEL_PROVIDER_ALLOWS(prov, flags) (prov->url_flags & (flags | (flags << CAMEL_URL_PART_NEED)))
#define CAMEL_PROVIDER_NEEDS(prov, flags) (prov->url_flags & (flags << CAMEL_URL_PART_NEED))

/* Providers use these macros to actually define their url_flags */
#define CAMEL_URL_ALLOW_USER	 (CAMEL_URL_PART_USER)
#define CAMEL_URL_ALLOW_AUTH	 (CAMEL_URL_PART_AUTH)
#define CAMEL_URL_ALLOW_PASSWORD (CAMEL_URL_PART_PASSWORD)
#define CAMEL_URL_ALLOW_HOST	 (CAMEL_URL_PART_HOST)
#define CAMEL_URL_ALLOW_PORT	 (CAMEL_URL_PART_PORT)
#define CAMEL_URL_ALLOW_PATH	 (CAMEL_URL_PART_PATH)

#define CAMEL_URL_NEED_USER	 (CAMEL_URL_PART_USER << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_AUTH	 (CAMEL_URL_PART_AUTH << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_PASSWORD	 (CAMEL_URL_PART_PASSWORD << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_HOST	 (CAMEL_URL_PART_HOST << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_PORT	 (CAMEL_URL_PART_PORT << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_PATH	 (CAMEL_URL_PART_PATH << CAMEL_URL_PART_NEED)

#define CAMEL_URL_PATH_IS_ABSOLUTE (1 << 12)


/* Generic extra config stuff */
typedef enum {
	CAMEL_PROVIDER_CONF_END,
	CAMEL_PROVIDER_CONF_SECTION_START,
	CAMEL_PROVIDER_CONF_SECTION_END,
	CAMEL_PROVIDER_CONF_CHECKBOX,
	CAMEL_PROVIDER_CONF_CHECKSPIN,
	CAMEL_PROVIDER_CONF_ENTRY,
} CamelProviderConfType;

typedef struct {
	CamelProviderConfType type;
	char *name, *depname;
	char *text, *value;
} CamelProviderConfEntry;



typedef struct {
	/* Provider name used in CamelURLs. */
	char *protocol;

	/* Provider name as used by people. (May be the same as protocol) */
	char *name;

	/* Description of the provider. A novice user should be able
	 * to read this description, and the information provided by
	 * an ISP, IS department, etc, and determine whether or not
	 * this provider is relevant to him, and if so, which
	 * information goes with it.
	 */
	char *description;

	/* The category of message that this provider works with.
	 * (evolution-mail will only list a provider in the store/transport
	 * config dialogs if its domain is "mail".)
	 */
	char *domain;

	/* Flags describing the provider, flags describing its URLs */
	int flags, url_flags;

	/* Extra configuration information */
	CamelProviderConfEntry *extra_conf;

	CamelType object_types [CAMEL_NUM_PROVIDER_TYPES];

	/* GList of CamelServiceAuthTypes the provider supports */
	GList *authtypes;

	GHashTable *service_cache;

	GHashFunc url_hash;
	GCompareFunc url_equal;
} CamelProvider;

GHashTable *camel_provider_init (void);
void camel_provider_load (CamelSession *session, const char *path, CamelException *ex);

/* This is defined by each module, not by camel-provider.c. */
void camel_provider_module_init (CamelSession *session);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_PROVIDER_H */

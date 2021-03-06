/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.c : class for an imap store */

/*
 *  Authors:
 *    Dan Winship <danw@ximian.com>
 *    Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2000, 2003 Ximian, Inc.
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

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel/camel-debug.h"
#include "camel/camel-disco-diary.h"
#include "camel/camel-exception.h"
#include "camel/camel-file-utils.h"
#include "camel/camel-folder.h"
#include "camel/camel-net-utils.h"
#include "camel/camel-private.h"
#include "camel/camel-sasl.h"
#include "camel/camel-session.h"
#include "camel/camel-stream-buffer.h"
#include "camel/camel-stream-fs.h"
#include "camel/camel-stream-process.h"
#include "camel/camel-stream.h"
#include "camel/camel-string-utils.h"
#include "camel/camel-tcp-stream-raw.h"
#include "camel/camel-tcp-stream-ssl.h"
#include "camel/camel-url.h"
#include "camel/camel-utf8.h"

#include "camel-imap-command.h"
#include "camel-imap-folder.h"
#include "camel-imap-message-cache.h"
#include "camel-imap-store-summary.h"
#include "camel-imap-store.h"
#include "camel-imap-summary.h"
#include "camel-imap-utils.h"

#define d(x)

/* Specified in RFC 2060 */
#define IMAP_PORT "143"
#define IMAPS_PORT "993"

#ifdef G_OS_WIN32
/* The strtok() in Microsoft's C library is MT-safe (but still uses
 * only one buffer pointer per thread, but for the use of strtok_r()
 * here that's enough).
 */
#define strtok_r(s,sep,lasts) (*(lasts)=strtok((s),(sep)))
#endif

static CamelDiscoStoreClass *parent_class = NULL;

static char imap_tag_prefix = 'A';

static void construct (CamelService *service, CamelSession *session,
		       CamelProvider *provider, CamelURL *url,
		       CamelException *ex);

static int imap_setv (CamelObject *object, CamelException *ex, CamelArgV *args);
static int imap_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args);

static char *imap_get_name (CamelService *service, gboolean brief);

static gboolean can_work_offline (CamelDiscoStore *disco_store);
static gboolean imap_connect_online (CamelService *service, CamelException *ex);
static gboolean imap_connect_offline (CamelService *service, CamelException *ex);
static gboolean imap_disconnect_online (CamelService *service, gboolean clean, CamelException *ex);
static gboolean imap_disconnect_offline (CamelService *service, gboolean clean, CamelException *ex);
static void imap_noop (CamelStore *store, CamelException *ex);
static CamelFolder *imap_get_junk(CamelStore *store, CamelException *ex);
static CamelFolder *imap_get_trash(CamelStore *store, CamelException *ex);
static GList *query_auth_types (CamelService *service, CamelException *ex);
static guint hash_folder_name (gconstpointer key);
static gint compare_folder_name (gconstpointer a, gconstpointer b);
static CamelFolder *get_folder_online (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex);
static CamelFolder *get_folder_offline (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex);

static CamelFolderInfo *create_folder (CamelStore *store, const char *parent_name, const char *folder_name, CamelException *ex);
static void             delete_folder (CamelStore *store, const char *folder_name, CamelException *ex);
static void             rename_folder (CamelStore *store, const char *old_name, const char *new_name, CamelException *ex);
static CamelFolderInfo *get_folder_info_online (CamelStore *store,
						const char *top,
						guint32 flags,
						CamelException *ex);
static CamelFolderInfo *get_folder_info_offline (CamelStore *store,
						 const char *top,
						 guint32 flags,
						 CamelException *ex);
static gboolean folder_subscribed (CamelStore *store, const char *folder_name);
static void subscribe_folder (CamelStore *store, const char *folder_name,
			      CamelException *ex);
static void unsubscribe_folder (CamelStore *store, const char *folder_name,
				CamelException *ex);

static void get_folders_sync(CamelImapStore *imap_store, const char *pattern, CamelException *ex);

static void imap_folder_effectively_unsubscribed(CamelImapStore *imap_store, const char *folder_name, CamelException *ex);
static gboolean imap_check_folder_still_extant (CamelImapStore *imap_store, const char *full_name,  CamelException *ex);
static void imap_forget_folder(CamelImapStore *imap_store, const char *folder_name, CamelException *ex);
static void imap_set_server_level (CamelImapStore *store);

static gboolean imap_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, CamelException *ex);

static void
camel_imap_store_class_init (CamelImapStoreClass *camel_imap_store_class)
{
	CamelObjectClass *camel_object_class =
		CAMEL_OBJECT_CLASS (camel_imap_store_class);
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_imap_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_imap_store_class);
	CamelDiscoStoreClass *camel_disco_store_class =
		CAMEL_DISCO_STORE_CLASS (camel_imap_store_class);

	parent_class = CAMEL_DISCO_STORE_CLASS (camel_type_get_global_classfuncs (camel_disco_store_get_type ()));

	/* virtual method overload */
	camel_object_class->setv = imap_setv;
	camel_object_class->getv = imap_getv;

	camel_service_class->construct = construct;
	camel_service_class->query_auth_types = query_auth_types;
	camel_service_class->get_name = imap_get_name;

	camel_store_class->hash_folder_name = hash_folder_name;
	camel_store_class->compare_folder_name = compare_folder_name;
	camel_store_class->create_folder = create_folder;
	camel_store_class->delete_folder = delete_folder;
	camel_store_class->rename_folder = rename_folder;
	camel_store_class->free_folder_info = camel_store_free_folder_info_full;
	camel_store_class->folder_subscribed = folder_subscribed;
	camel_store_class->subscribe_folder = subscribe_folder;
	camel_store_class->unsubscribe_folder = unsubscribe_folder;
	camel_store_class->noop = imap_noop;
	camel_store_class->get_trash = imap_get_trash;
	camel_store_class->get_junk = imap_get_junk;
	camel_store_class->can_refresh_folder = imap_can_refresh_folder;

	camel_disco_store_class->can_work_offline = can_work_offline;
	camel_disco_store_class->connect_online = imap_connect_online;
	camel_disco_store_class->connect_offline = imap_connect_offline;
	camel_disco_store_class->disconnect_online = imap_disconnect_online;
	camel_disco_store_class->disconnect_offline = imap_disconnect_offline;
	camel_disco_store_class->get_folder_online = get_folder_online;
	camel_disco_store_class->get_folder_offline = get_folder_offline;
	camel_disco_store_class->get_folder_resyncing = get_folder_online;
	camel_disco_store_class->get_folder_info_online = get_folder_info_online;
	camel_disco_store_class->get_folder_info_offline = get_folder_info_offline;
	camel_disco_store_class->get_folder_info_resyncing = get_folder_info_online;
}

static gboolean
free_key (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	return TRUE;
}

static void
camel_imap_store_finalize (CamelObject *object)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (object);
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (object);

	/* This frees current_folder, folders, authtypes, streams, and namespace. */
	camel_service_disconnect((CamelService *)imap_store, TRUE, NULL);

	if (imap_store->summary) {
		camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		camel_object_unref(imap_store->summary);
	}

	if (imap_store->base_url)
		g_free (imap_store->base_url);
	if (imap_store->storage_path)
		g_free (imap_store->storage_path);

	if (disco->diary) {
		camel_object_unref (disco->diary);
		disco->diary = NULL;
	}

	g_free (imap_store->custom_headers);
}

static void
camel_imap_store_init (gpointer object, gpointer klass)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (object);

	imap_store->istream = NULL;
	imap_store->ostream = NULL;

	imap_store->dir_sep = '\0';
	imap_store->current_folder = NULL;
	imap_store->connected = FALSE;
	imap_store->preauthed = FALSE;
	((CamelStore *)imap_store)->flags |= CAMEL_STORE_SUBSCRIPTIONS;

	imap_store->tag_prefix = imap_tag_prefix++;
	if (imap_tag_prefix > 'Z')
		imap_tag_prefix = 'A';
}

CamelType
camel_imap_store_get_type (void)
{
	static CamelType camel_imap_store_type = CAMEL_INVALID_TYPE;

	if (camel_imap_store_type == CAMEL_INVALID_TYPE)	{
		camel_imap_store_type =
			camel_type_register (CAMEL_DISCO_STORE_TYPE,
					     "CamelImapStore",
					     sizeof (CamelImapStore),
					     sizeof (CamelImapStoreClass),
					     (CamelObjectClassInitFunc) camel_imap_store_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_imap_store_init,
					     (CamelObjectFinalizeFunc) camel_imap_store_finalize);
	}

	return camel_imap_store_type;
}

static void
construct (CamelService *service, CamelSession *session,
	   CamelProvider *provider, CamelURL *url,
	   CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (service);
	CamelStore *store = CAMEL_STORE (service);
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (service);
	char *tmp, *path;
	CamelURL *summary_url;

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set (ex))
		return;

	imap_store->storage_path = camel_session_get_storage_path (session, service, ex);
	if (!imap_store->storage_path)
		return;

	/* FIXME */
	imap_store->base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
								   CAMEL_URL_HIDE_PARAMS |
								   CAMEL_URL_HIDE_AUTH));

	imap_store->parameters = 0;
	if (camel_url_get_param (url, "use_lsub"))
		imap_store->parameters |= IMAP_PARAM_SUBSCRIPTIONS;
	if (camel_url_get_param (url, "override_namespace") && camel_url_get_param (url, "namespace")) {
		imap_store->parameters |= IMAP_PARAM_OVERRIDE_NAMESPACE;
		g_free(imap_store->namespace);
		imap_store->namespace = g_strdup (camel_url_get_param (url, "namespace"));
	}
	if (camel_url_get_param (url, "check_all"))
		imap_store->parameters |= IMAP_PARAM_CHECK_ALL;
	if (camel_url_get_param (url, "filter")) {
		imap_store->parameters |= IMAP_PARAM_FILTER_INBOX;
		store->flags |= CAMEL_STORE_FILTER_INBOX;
	}
	if (camel_url_get_param (url, "filter_junk"))
		imap_store->parameters |= IMAP_PARAM_FILTER_JUNK;
	if (camel_url_get_param (url, "filter_junk_inbox"))
		imap_store->parameters |= IMAP_PARAM_FILTER_JUNK_INBOX;

	imap_store->headers = IMAP_FETCH_MAILING_LIST_HEADERS;
	if (camel_url_get_param (url, "all_headers"))
		imap_store->headers = IMAP_FETCH_ALL_HEADERS;
	else if (camel_url_get_param (url, "basic_headers"))
		imap_store->headers = IMAP_FETCH_MINIMAL_HEADERS;

	if (camel_url_get_param (url, "imap_custom_headers")) {
		imap_store->custom_headers = g_strdup(camel_url_get_param (url, "imap_custom_headers"));
	}


	/* setup journal*/
	path = g_strdup_printf ("%s/journal", imap_store->storage_path);
	disco_store->diary = camel_disco_diary_new (disco_store, path, ex);
	g_free (path);

	/* setup/load the store summary */
	tmp = alloca(strlen(imap_store->storage_path)+32);
	sprintf(tmp, "%s/.ev-store-summary", imap_store->storage_path);
	imap_store->summary = camel_imap_store_summary_new();
	camel_store_summary_set_filename((CamelStoreSummary *)imap_store->summary, tmp);
	summary_url = camel_url_new(imap_store->base_url, NULL);
	camel_store_summary_set_uri_base((CamelStoreSummary *)imap_store->summary, summary_url);
	camel_url_free(summary_url);
	if (camel_store_summary_load((CamelStoreSummary *)imap_store->summary) == 0) {
		CamelImapStoreSummary *is = imap_store->summary;

		if (is->namespace) {
			/* if namespace has changed, clear folder list */
			if (imap_store->namespace && strcmp(imap_store->namespace, is->namespace->full_name) != 0) {
				camel_store_summary_clear((CamelStoreSummary *)is);
			} else {
				imap_store->namespace = g_strdup(is->namespace->full_name);
				imap_store->dir_sep = is->namespace->sep;
			}
		}

		imap_store->capabilities = is->capabilities;
		imap_set_server_level(imap_store);
	}
}

static int
imap_setv (CamelObject *object, CamelException *ex, CamelArgV *args)
{
	CamelImapStore *store = (CamelImapStore *) object;
	guint32 tag, flags;
	int i;

	for (i = 0; i < args->argc; i++) {
		tag = args->argv[i].tag;

		/* make sure this is an arg we're supposed to handle */
		if ((tag & CAMEL_ARG_TAG) <= CAMEL_IMAP_STORE_ARG_FIRST ||
		    (tag & CAMEL_ARG_TAG) >= CAMEL_IMAP_STORE_ARG_FIRST + 100)
			continue;

		switch (tag) {
		case CAMEL_IMAP_STORE_NAMESPACE:
			if (strcmp (store->namespace, args->argv[i].ca_str) != 0) {
				g_free (store->namespace);
				store->namespace = g_strdup (args->argv[i].ca_str);
				/* the current imap code will need to do a reconnect for this to take effect */
				/*reconnect = TRUE;*/
			}
			break;
		case CAMEL_IMAP_STORE_OVERRIDE_NAMESPACE:
			flags = args->argv[i].ca_int ? IMAP_PARAM_OVERRIDE_NAMESPACE : 0;
			flags |= (store->parameters & ~IMAP_PARAM_OVERRIDE_NAMESPACE);

			if (store->parameters != flags) {
				store->parameters = flags;
				/* the current imap code will need to do a reconnect for this to take effect */
				/*reconnect = TRUE;*/
			}
			break;
		case CAMEL_IMAP_STORE_CHECK_ALL:
			flags = args->argv[i].ca_int ? IMAP_PARAM_CHECK_ALL : 0;
			flags |= (store->parameters & ~IMAP_PARAM_CHECK_ALL);
			store->parameters = flags;
			/* no need to reconnect for this option to take effect... */
			break;
		case CAMEL_IMAP_STORE_FILTER_INBOX:
			flags = args->argv[i].ca_int ? IMAP_PARAM_FILTER_INBOX : 0;
			flags |= (store->parameters & ~IMAP_PARAM_FILTER_INBOX);
			store->parameters = flags;
			/* no need to reconnect for this option to take effect... */
			break;
		case CAMEL_IMAP_STORE_FILTER_JUNK:
			flags = args->argv[i].ca_int ? IMAP_PARAM_FILTER_JUNK : 0;
			store->parameters = flags | (store->parameters & ~IMAP_PARAM_FILTER_JUNK);
			break;
		case CAMEL_IMAP_STORE_FILTER_JUNK_INBOX:
			flags = args->argv[i].ca_int ? IMAP_PARAM_FILTER_JUNK_INBOX : 0;
			store->parameters = flags | (store->parameters & ~IMAP_PARAM_FILTER_JUNK_INBOX);
			break;
		default:
			/* error?? */
			continue;
		}

		/* let our parent know that we've handled this arg */
		camel_argv_ignore (args, i);
	}

	/* FIXME: if we need to reconnect for a change to take affect,
           we need to do it here... or, better yet, somehow chain it
           up to CamelService's setv implementation. */

	return CAMEL_OBJECT_CLASS (parent_class)->setv (object, ex, args);
}

static int
imap_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelImapStore *store = (CamelImapStore *) object;
	guint32 tag;
	int i;

	for (i = 0; i < args->argc; i++) {
		tag = args->argv[i].tag;

		/* make sure this is an arg we're supposed to handle */
		if ((tag & CAMEL_ARG_TAG) <= CAMEL_IMAP_STORE_ARG_FIRST ||
		    (tag & CAMEL_ARG_TAG) >= CAMEL_IMAP_STORE_ARG_FIRST + 100)
			continue;

		switch (tag) {
		case CAMEL_IMAP_STORE_NAMESPACE:
			*args->argv[i].ca_str = store->namespace;
			break;
		case CAMEL_IMAP_STORE_OVERRIDE_NAMESPACE:
			*args->argv[i].ca_int = store->parameters & IMAP_PARAM_OVERRIDE_NAMESPACE ? TRUE : FALSE;
			break;
		case CAMEL_IMAP_STORE_CHECK_ALL:
			*args->argv[i].ca_int = store->parameters & IMAP_PARAM_CHECK_ALL ? TRUE : FALSE;
			break;
		case CAMEL_IMAP_STORE_FILTER_INBOX:
			*args->argv[i].ca_int = store->parameters & IMAP_PARAM_FILTER_INBOX ? TRUE : FALSE;
			break;
		case CAMEL_IMAP_STORE_FILTER_JUNK:
			*args->argv[i].ca_int = store->parameters & IMAP_PARAM_FILTER_JUNK ? TRUE : FALSE;
			break;
		case CAMEL_IMAP_STORE_FILTER_JUNK_INBOX:
			*args->argv[i].ca_int = store->parameters & IMAP_PARAM_FILTER_JUNK_INBOX ? TRUE : FALSE;
			break;
		default:
			/* error? */
			break;
		}
	}

	return CAMEL_OBJECT_CLASS (parent_class)->getv (object, ex, args);
}

static char *
imap_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("IMAP server %s"), service->url->host);
	else
		return g_strdup_printf (_("IMAP service for %s on %s"),
					service->url->user, service->url->host);
}

static void
imap_set_server_level (CamelImapStore *store)
{
	if (store->capabilities & IMAP_CAPABILITY_IMAP4REV1) {
		store->server_level = IMAP_LEVEL_IMAP4REV1;
		store->capabilities |= IMAP_CAPABILITY_STATUS;
	} else if (store->capabilities & IMAP_CAPABILITY_IMAP4)
		store->server_level = IMAP_LEVEL_IMAP4;
	else
		store->server_level = IMAP_LEVEL_UNKNOWN;
}

static struct {
	const char *name;
	guint32 flag;
} capabilities[] = {
	{ "IMAP4",		IMAP_CAPABILITY_IMAP4 },
	{ "IMAP4REV1",		IMAP_CAPABILITY_IMAP4REV1 },
	{ "STATUS",		IMAP_CAPABILITY_STATUS },
	{ "NAMESPACE",		IMAP_CAPABILITY_NAMESPACE },
	{ "UIDPLUS",		IMAP_CAPABILITY_UIDPLUS },
	{ "LITERAL+",		IMAP_CAPABILITY_LITERALPLUS },
	{ "STARTTLS",           IMAP_CAPABILITY_STARTTLS },
	{ "XGWEXTENSIONS",      IMAP_CAPABILITY_XGWEXTENSIONS },
	{ "XGWMOVE",            IMAP_CAPABILITY_XGWMOVE },
	{ "LOGINDISABLED",      IMAP_CAPABILITY_LOGINDISABLED },
	{ "QUOTA",              IMAP_CAPABILITY_QUOTA },
	{ NULL, 0 }
};

static void
parse_capability(CamelImapStore *store, char *capa)
{
	char *lasts;
	int i;

	for (capa = strtok_r (capa, " ", &lasts); capa; capa = strtok_r (NULL, " ", &lasts)) {
		if (!strncmp (capa, "AUTH=", 5)) {
			g_hash_table_insert (store->authtypes,
					     g_strdup (capa + 5),
					     GINT_TO_POINTER (1));
			continue;
		}
		for (i = 0; capabilities[i].name; i++) {
			if (g_ascii_strcasecmp (capa, capabilities[i].name) == 0) {
				store->capabilities |= capabilities[i].flag;
				break;
			}
		}
	}
}

static gboolean
imap_get_capability (CamelService *service, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelImapResponse *response;
	char *result;

	/* Find out the IMAP capabilities */
	/* We assume we have utf8 capable search until a failed search tells us otherwise */
	store->capabilities = IMAP_CAPABILITY_utf8_search;
	store->authtypes = g_hash_table_new (g_str_hash, g_str_equal);
	response = camel_imap_command (store, NULL, ex, "CAPABILITY");
	if (!response)
		return FALSE;
	result = camel_imap_response_extract (store, response, "CAPABILITY ", ex);
	if (!result)
		return FALSE;

	/* Skip over "* CAPABILITY ". */
	parse_capability(store, result+13);
	g_free (result);

	/* dunno why the groupwise guys didn't just list this in capabilities */
	if (store->capabilities & IMAP_CAPABILITY_XGWEXTENSIONS) {
		/* not critical if this fails */
		response = camel_imap_command (store, NULL, NULL, "XGWEXTENSIONS");
		if (response && (result = camel_imap_response_extract (store, response, "XGWEXTENSIONS ", NULL))) {
			parse_capability(store, result+16);
			g_free (result);
		}
	}

	imap_set_server_level (store);

	if (store->summary->capabilities != store->capabilities) {
		store->summary->capabilities = store->capabilities;
		camel_store_summary_touch((CamelStoreSummary *)store->summary);
		camel_store_summary_save((CamelStoreSummary *)store->summary);
	}

	return TRUE;
}

enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS
};

#ifdef HAVE_SSL
#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#endif

static gboolean
connect_to_server (CamelService *service, struct addrinfo *ai, int ssl_mode, CamelException *ex)
{
	CamelImapStore *store = (CamelImapStore *) service;
	CamelImapResponse *response;
	CamelStream *tcp_stream;
	CamelSockOptData sockopt;
	gboolean force_imap4 = FALSE;
	gboolean clean_quit = TRUE;
	char *buf;

	if (ssl_mode != MODE_CLEAR) {
#ifdef HAVE_SSL
		if (ssl_mode == MODE_TLS)
			tcp_stream = camel_tcp_stream_ssl_new_raw (service->session, service->url->host, STARTTLS_FLAGS);
		else
			tcp_stream = camel_tcp_stream_ssl_new (service->session, service->url->host, SSL_PORT_FLAGS);
#else
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
			_("Could not connect to %s: %s"),
			service->url->host, _("SSL unavailable"));

		return FALSE;

#endif /* HAVE_SSL */
	} else
		tcp_stream = camel_tcp_stream_raw_new ();

	if (camel_tcp_stream_connect ((CamelTcpStream *) tcp_stream, ai) == -1) {
		if (errno == EINTR)
			camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
					     _("Connection cancelled"));
		else
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Could not connect to %s: %s"),
					      service->url->host,
					      g_strerror (errno));

		camel_object_unref (tcp_stream);

		return FALSE;
	}

	store->ostream = tcp_stream;
	store->istream = camel_stream_buffer_new (tcp_stream, CAMEL_STREAM_BUFFER_READ);

	store->connected = TRUE;
	store->preauthed = FALSE;
	store->command = 0;

	/* Disable Nagle - we send a lot of small requests which nagle slows down */
	sockopt.option = CAMEL_SOCKOPT_NODELAY;
	sockopt.value.no_delay = TRUE;
	camel_tcp_stream_setsockopt((CamelTcpStream *)tcp_stream, &sockopt);

	/* Set keepalive - needed for some hosts/router configurations, we're idle a lot */
	sockopt.option = CAMEL_SOCKOPT_KEEPALIVE;
	sockopt.value.keep_alive = TRUE;
	camel_tcp_stream_setsockopt((CamelTcpStream *)tcp_stream, &sockopt);

	/* Read the greeting, if any, and deal with PREAUTH */
	if (camel_imap_store_readline (store, &buf, ex) < 0) {
		if (store->istream) {
			camel_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			camel_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;

		return FALSE;
	}

	if (!strncmp(buf, "* PREAUTH", 9))
		store->preauthed = TRUE;

	if (strstr (buf, "Courier-IMAP") || getenv("CAMEL_IMAP_BRAINDAMAGED")) {
		/* Courier-IMAP is braindamaged. So far this flag only
		 * works around the fact that Courier-IMAP is known to
		 * give invalid BODY responses seemingly because its
		 * MIME parser sucks. In any event, we can't rely on
		 * them so we always have to request the full messages
		 * rather than getting individual parts. */
		store->braindamaged = TRUE;
	} else if (strstr (buf, "WEB.DE") || strstr (buf, "Mail2World")) {
		/* This is a workaround for servers which advertise
		 * IMAP4rev1 but which can sometimes subtly break in
		 * various ways if we try to use IMAP4rev1 queries.
		 *
		 * WEB.DE: when querying for HEADER.FIELDS.NOT, it
		 * returns an empty literal for the headers. Many
		 * complaints about empty message-list fields on the
		 * mailing lists and probably a few bugzilla bugs as
		 * well.
		 *
		 * Mail2World (aka NamePlanet): When requesting
		 * message info's, it ignores the fact that we
		 * requested BODY.PEEK[HEADER.FIELDS.NOT (RECEIVED)]
		 * and so the responses are incomplete. See bug #58766
		 * for details.
		 **/
		force_imap4 = TRUE;
	}

	g_free (buf);

	/* get the imap server capabilities */
	if (!imap_get_capability (service, ex)) {
		if (store->istream) {
			camel_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			camel_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;
		return FALSE;
	}

	if (force_imap4) {
		store->capabilities &= ~IMAP_CAPABILITY_IMAP4REV1;
		store->server_level = IMAP_LEVEL_IMAP4;
	}

	if (ssl_mode != MODE_TLS) {
		/* we're done */
		return TRUE;
	}

#ifdef HAVE_SSL
	/* as soon as we send a STARTTLS command, all hope is lost of a clean QUIT if problems arise */
	clean_quit = FALSE;
	
	if (!(store->capabilities & IMAP_CAPABILITY_STARTTLS)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
			_("Failed to connect to IMAP server %s in secure mode: %s"),
			service->url->host, _("STARTTLS not supported"));

		goto exception;
	}

	response = camel_imap_command (store, NULL, ex, "STARTTLS");
	if (!response) {
		camel_object_unref (store->istream);
		camel_object_unref (store->ostream);
		store->istream = store->ostream = NULL;
		return FALSE;
	}

	camel_imap_response_free_without_processing (store, response);

	/* Okay, now toggle SSL/TLS mode */
	if (camel_tcp_stream_ssl_enable_ssl (CAMEL_TCP_STREAM_SSL (tcp_stream)) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Failed to connect to IMAP server %s in secure mode: %s"),
				      service->url->host, _("SSL negotiations failed"));
		goto exception;
	}
#else
	camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
			      _("Failed to connect to IMAP server %s in secure mode: %s"),
			      service->url->host, _("SSL is not available in this build"));
	goto exception;
#endif /* HAVE_SSL */

	/* rfc2595, section 4 states that after a successful STLS
           command, the client MUST discard prior CAPA responses */
	if (!imap_get_capability (service, ex)) {
		if (store->istream) {
			camel_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			camel_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;

		return FALSE;
	}

	if (store->capabilities & IMAP_CAPABILITY_LOGINDISABLED ) {
		clean_quit = TRUE;
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
			_("Failed to connect to IMAP server %s in secure mode: %s"),
			service->url->host, _("Unknown error"));
		goto exception;
	}

	return TRUE;

exception:

	if (clean_quit && store->connected) {
		/* try to disconnect cleanly */
		response = camel_imap_command (store, NULL, ex, "LOGOUT");
		if (response)
			camel_imap_response_free_without_processing (store, response);
	}

	if (store->istream) {
		camel_object_unref (store->istream);
		store->istream = NULL;
	}

	if (store->ostream) {
		camel_object_unref (store->ostream);
		store->ostream = NULL;
	}

	store->connected = FALSE;

	return FALSE;
}

#ifndef G_OS_WIN32

/* Using custom commands to connect to IMAP servers is not supported on Win32 */

static gboolean
connect_to_server_process (CamelService *service, const char *cmd, CamelException *ex)
{
	CamelImapStore *store = (CamelImapStore *) service;
	CamelStream *cmd_stream;
	int ret, i = 0;
	char *buf;
	char *cmd_copy;
	char *full_cmd;
	char *child_env[7];

	/* Put full details in the environment, in case the connection
	   program needs them */
	buf = camel_url_to_string(service->url, 0);
	child_env[i++] = g_strdup_printf("URL=%s", buf);
	g_free(buf);

	child_env[i++] = g_strdup_printf("URLHOST=%s", service->url->host);
	if (service->url->port)
		child_env[i++] = g_strdup_printf("URLPORT=%d", service->url->port);
	if (service->url->user)
		child_env[i++] = g_strdup_printf("URLUSER=%s", service->url->user);
	if (service->url->passwd)
		child_env[i++] = g_strdup_printf("URLPASSWD=%s", service->url->passwd);
	if (service->url->path)
		child_env[i++] = g_strdup_printf("URLPATH=%s", service->url->path);
	child_env[i] = NULL;

	/* Now do %h, %u, etc. substitution in cmd */
	buf = cmd_copy = g_strdup(cmd);

	full_cmd = g_strdup("");

	for(;;) {
		char *pc;
		char *tmp;
		char *var;
		int len;

		pc = strchr(buf, '%');
	ignore:
		if (!pc) {
			tmp = g_strdup_printf("%s%s", full_cmd, buf);
			g_free(full_cmd);
			full_cmd = tmp;
			break;
		}

		len = pc - buf;

		var = NULL;

		switch(pc[1]) {
		case 'h':
			var = service->url->host;
			break;
		case 'u':
			var = service->url->user;
			break;
		}
		if (!var) {
			/* If there wasn't a valid %-code, with an actual
			   variable to insert, pretend we didn't see the % */
			pc = strchr(pc + 1, '%');
			goto ignore;
		}
		tmp = g_strdup_printf("%s%.*s%s", full_cmd, len, buf, var);
		g_free(full_cmd);
		full_cmd = tmp;
		buf = pc + 2;
	}

	g_free(cmd_copy);

	cmd_stream = camel_stream_process_new ();

	ret = camel_stream_process_connect (CAMEL_STREAM_PROCESS(cmd_stream),
					    full_cmd, (const char **)child_env);

	while (i)
		g_free(child_env[--i]);

	if (ret == -1) {
		if (errno == EINTR)
			camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
					     _("Connection cancelled"));
		else
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Could not connect with command \"%s\": %s"),
					      full_cmd, g_strerror (errno));

		camel_object_unref (cmd_stream);
		g_free (full_cmd);
		return FALSE;
	}
	g_free (full_cmd);

	store->ostream = cmd_stream;
	store->istream = camel_stream_buffer_new (cmd_stream, CAMEL_STREAM_BUFFER_READ);

	store->connected = TRUE;
	store->preauthed = FALSE;
	store->command = 0;

	/* Read the greeting, if any, and deal with PREAUTH */
	if (camel_imap_store_readline (store, &buf, ex) < 0) {
		if (store->istream) {
			camel_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			camel_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;
		return FALSE;
	}

	if (!strncmp(buf, "* PREAUTH", 9))
		store->preauthed = TRUE;
	g_free (buf);

	/* get the imap server capabilities */
	if (!imap_get_capability (service, ex)) {
		if (store->istream) {
			camel_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			camel_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;
		return FALSE;
	}

	return TRUE;

}

#endif

static struct {
	char *value;
	char *serv;
	char *port;
	int mode;
} ssl_options[] = {
	{ "",              "imaps", IMAPS_PORT, MODE_SSL   },  /* really old (1.x) */
	{ "always",        "imaps", IMAPS_PORT, MODE_SSL   },
	{ "when-possible", "imap",  IMAP_PORT,  MODE_TLS   },
	{ "never",         "imap",  IMAP_PORT,  MODE_CLEAR },
	{ NULL,            "imap",  IMAP_PORT,  MODE_CLEAR },
};

static gboolean
connect_to_server_wrapper (CamelService *service, CamelException *ex)
{
	const char *ssl_mode;
	struct addrinfo hints, *ai;
	int mode, ret, i;
	char *serv;
	const char *port;

#ifndef G_OS_WIN32
	const char *command;

	if (camel_url_get_param(service->url, "use_command")
	    && (command = camel_url_get_param(service->url, "command")))
		return connect_to_server_process(service, command, ex);
#endif

	if ((ssl_mode = camel_url_get_param (service->url, "use_ssl"))) {
		for (i = 0; ssl_options[i].value; i++)
			if (!strcmp (ssl_options[i].value, ssl_mode))
				break;
		mode = ssl_options[i].mode;
		serv = ssl_options[i].serv;
		port = ssl_options[i].port;
	} else {
		mode = MODE_CLEAR;
		serv = "imap";
		port = IMAP_PORT;
	}

	if (service->url->port) {
		serv = g_alloca (16);
		sprintf (serv, "%d", service->url->port);
		port = NULL;
	}

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	ai = camel_getaddrinfo(service->url->host, serv, &hints, ex);
	if (ai == NULL && port != NULL && camel_exception_get_id(ex) != CAMEL_EXCEPTION_USER_CANCEL) {
		camel_exception_clear (ex);
		ai = camel_getaddrinfo(service->url->host, port, &hints, ex);
	}

	if (ai == NULL)
		return FALSE;

	ret = connect_to_server (service, ai, mode, ex);

	camel_freeaddrinfo (ai);

	return ret;
}

extern CamelServiceAuthType camel_imap_password_authtype;

static GList *
query_auth_types (CamelService *service, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelServiceAuthType *authtype;
	GList *sasl_types, *t, *next;
	gboolean connected;

	if (!camel_disco_store_check_online (CAMEL_DISCO_STORE (store), ex))
		return NULL;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	connected = store->istream != NULL && store->connected;
	if (!connected)
		connected = connect_to_server_wrapper (service, ex);
	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
	if (!connected)
		return NULL;

	sasl_types = camel_sasl_authtype_list (FALSE);
	for (t = sasl_types; t; t = next) {
		authtype = t->data;
		next = t->next;

		if (!g_hash_table_lookup (store->authtypes, authtype->authproto)) {
			sasl_types = g_list_remove_link (sasl_types, t);
			g_list_free_1 (t);
		}
	}

	return g_list_prepend (sasl_types, &camel_imap_password_authtype);
}

/* folder_name is path name */
static CamelFolderInfo *
imap_build_folder_info(CamelImapStore *imap_store, const char *folder_name)
{
	CamelURL *url;
	const char *name;
	CamelFolderInfo *fi;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup(folder_name);
	fi->unread = -1;
	fi->total = -1;

	url = camel_url_new (imap_store->base_url, NULL);
	g_free (url->path);
	url->path = g_strdup_printf ("/%s", folder_name);
	fi->uri = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
	camel_url_free(url);
	name = strrchr (fi->full_name, '/');
	if (name == NULL)
		name = fi->full_name;
	else
		name++;
	if (!g_ascii_strcasecmp (fi->full_name, "INBOX"))
		fi->name = g_strdup (_("Inbox"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Drafts"))
		fi->name = g_strdup (_("Drafts"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Sent"))
		fi->name = g_strdup (_("Sent"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Templates"))
		fi->name = g_strdup (_("Templates"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Trash"))
		fi->name = g_strdup (_("Trash"));
	else
		fi->name = g_strdup (name);

	return fi;
}

static void
imap_folder_effectively_unsubscribed(CamelImapStore *imap_store,
				     const char *folder_name, CamelException *ex)
{
	CamelFolderInfo *fi;
	CamelStoreInfo *si;

	si = camel_store_summary_path((CamelStoreSummary *)imap_store->summary, folder_name);
	if (si) {
		if (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) {
			si->flags &= ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
			camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}

	if (imap_store->renaming) {
		/* we don't need to emit a "folder_unsubscribed" signal
                   if we are in the process of renaming folders, so we
                   are done here... */
		return;

	}

	fi = imap_build_folder_info(imap_store, folder_name);
	camel_object_trigger_event (CAMEL_OBJECT (imap_store), "folder_unsubscribed", fi);
	camel_folder_info_free (fi);
}

static void
imap_forget_folder (CamelImapStore *imap_store, const char *folder_name, CamelException *ex)
{
	CamelFolderSummary *summary;
	CamelImapMessageCache *cache;
	char *summary_file, *state_file;
	char *journal_file;
	char *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	const char *name;

	name = strrchr (folder_name, imap_store->dir_sep);
	if (name)
		name++;
	else
		name = folder_name;

	storage_path = g_strdup_printf ("%s/folders", imap_store->storage_path);
	folder_dir = imap_path_to_physical (storage_path, folder_name);
	g_free (storage_path);
	if (g_access (folder_dir, F_OK) != 0) {
		g_free (folder_dir);
		goto event;
	}

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	summary = camel_imap_summary_new (NULL, summary_file);
	if (!summary) {
		g_free (summary_file);
		g_free (folder_dir);
		goto event;
	}
	camel_object_unref (summary);

	g_unlink (summary_file);
	g_free (summary_file);

	summary_file = g_strdup_printf ("%s/summary-meta", folder_dir);
	summary = camel_imap_summary_new (NULL, summary_file);
	if (!summary) {
		g_free (summary_file);
		g_free (folder_dir);
		goto event;
	}

	cache = camel_imap_message_cache_new (folder_dir, summary, ex);
	if (cache)
		camel_imap_message_cache_clear (cache);

	camel_object_unref (cache);
	camel_object_unref (summary);

	g_unlink (summary_file);
	g_free (summary_file);

	journal_file = g_strdup_printf ("%s/journal", folder_dir);
	g_unlink (journal_file);
	g_free (journal_file);

	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	g_unlink (state_file);
	g_free (state_file);

	state_file = g_strdup_printf("%s/subfolders", folder_dir);
	g_rmdir(state_file);
	g_free(state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

 event:

	camel_store_summary_remove_path((CamelStoreSummary *)imap_store->summary, folder_name);
	camel_store_summary_save((CamelStoreSummary *)imap_store->summary);

	fi = imap_build_folder_info(imap_store, folder_name);
	camel_object_trigger_event (CAMEL_OBJECT (imap_store), "folder_deleted", fi);
	camel_folder_info_free (fi);
}

static gboolean
imap_check_folder_still_extant (CamelImapStore *imap_store, const char *full_name,
				CamelException *ex)
{
	CamelImapResponse *response;

	response = camel_imap_command (imap_store, NULL, ex, "LIST \"\" %F",
				       full_name);

	if (response) {
		gboolean stillthere = response->untagged->len != 0;

		camel_imap_response_free_without_processing (imap_store, response);

		return stillthere;
	}

	/* if the command was rejected, there must be some other error,
	   assume it worked so we dont blow away the folder unecessarily */
	return TRUE;
}

#if 0
/* This is a little 'hack' to avoid the deadlock conditions that would otherwise
   ensue when calling camel_folder_refresh_info from inside a lock */
/* NB: on second thougts this is probably not entirely safe, but it'll do for now */
/* No, its definetly not safe.  So its been changed to copy the folders first */
/* the alternative is to:
   make the camel folder->lock recursive (which should probably be done)
   or remove it from camel_folder_refresh_info, and use another locking mechanism */
/* also see get_folder_info_online() for the same hack repeated */
static void
imap_store_refresh_folders (CamelImapStore *store, CamelException *ex)
{
	GPtrArray *folders;
	int i;

	folders = camel_object_bag_list(CAMEL_STORE (store)->folders);

	for (i = 0; i <folders->len; i++) {
		CamelFolder *folder = folders->pdata[i];

		/* NB: we can have vtrash folders also in our store ... bit hacky */
		if (!CAMEL_IS_IMAP_FOLDER(folder)) {
			camel_object_unref(folder);
			continue;
		}

		CAMEL_IMAP_FOLDER (folder)->need_rescan = TRUE;
		if (!camel_exception_is_set(ex))
			CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(folder))->refresh_info(folder, ex);

		if (camel_exception_is_set (ex) &&
		    imap_check_folder_still_extant (store, folder->full_name, ex) == FALSE) {
			gchar *namedup;

			/* the folder was deleted (may happen when we come back online
			 * after being offline */

			namedup = g_strdup (folder->full_name);
			camel_object_unref(folder);
			imap_folder_effectively_unsubscribed (store, namedup, ex);
			imap_forget_folder (store, namedup, ex);
			g_free (namedup);
		} else
			camel_object_unref(folder);
	}

	g_ptr_array_free (folders, TRUE);
}
#endif

static gboolean
try_auth (CamelImapStore *store, const char *mech, CamelException *ex)
{
	CamelSasl *sasl;
	CamelImapResponse *response;
	char *resp;
	char *sasl_resp;

	response = camel_imap_command (store, NULL, ex, "AUTHENTICATE %s", mech);
	if (!response)
		return FALSE;

	sasl = camel_sasl_new ("imap", mech, CAMEL_SERVICE (store));
	while (!camel_sasl_authenticated (sasl)) {
		resp = camel_imap_response_extract_continuation (store, response, ex);
		if (!resp)
			goto lose;

		sasl_resp = camel_sasl_challenge_base64 (sasl, imap_next_word (resp), ex);
		g_free (resp);
		if (!sasl_resp || camel_exception_is_set (ex))
			goto break_and_lose;

		response = camel_imap_command_continuation (store, sasl_resp, strlen (sasl_resp), ex);
		g_free (sasl_resp);
		if (!response)
			goto lose;
	}

	resp = camel_imap_response_extract_continuation (store, response, NULL);
	if (resp) {
		/* Oops. SASL claims we're done, but the IMAP server
		 * doesn't think so...
		 */
		g_free (resp);
		goto lose;
	}

	camel_object_unref (sasl);

	return TRUE;

 break_and_lose:
	/* Get the server out of "waiting for continuation data" mode. */
	response = camel_imap_command_continuation (store, "*", 1, NULL);
	if (response)
		camel_imap_response_free (store, response);

 lose:
	if (!camel_exception_is_set (ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
				     _("Bad authentication response from server."));
	}

	camel_object_unref (sasl);

	return FALSE;
}

static gboolean
imap_auth_loop (CamelService *service, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelSession *session = camel_service_get_session (service);
	CamelServiceAuthType *authtype = NULL;
	CamelImapResponse *response;
	char *errbuf = NULL;
	gboolean authenticated = FALSE;
	const char *auth_domain;

	auth_domain = camel_url_get_param (service->url, "auth-domain");

	if (store->preauthed) {
		if (camel_verbose_debug)
			fprintf(stderr, "Server %s has preauthenticated us.\n",
				service->url->host);
		return TRUE;
	}

	if (service->url->authmech) {
		if (!g_hash_table_lookup (store->authtypes, service->url->authmech)) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
				_("IMAP server %s does not support requested "
				  "authentication type %s"),
				service->url->host,
				service->url->authmech);
			return FALSE;
		}

		authtype = camel_sasl_authtype (service->url->authmech);
		if (!authtype) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					      _("No support for authentication type %s"),
					      service->url->authmech);
			return FALSE;
		}

		if (!authtype->need_password) {
			authenticated = try_auth (store, authtype->authproto, ex);
			if (!authenticated)
				return FALSE;
		}
	}

	while (!authenticated) {
		if (errbuf) {
			/* We need to un-cache the password before prompting again */
			camel_session_forget_password (session, service, auth_domain, "password", ex);
			g_free (service->url->passwd);
			service->url->passwd = NULL;
		}

		if (!service->url->passwd) {
			char *base_prompt;
			char *full_prompt;

			base_prompt = camel_session_build_password_prompt (
				"IMAP", service->url->user, service->url->host);

			if (errbuf != NULL)
				full_prompt = g_strconcat (errbuf, base_prompt, NULL);
			else
				full_prompt = g_strdup (base_prompt);

			service->url->passwd = camel_session_get_password (
				session, service, auth_domain, full_prompt,
				"password", CAMEL_SESSION_PASSWORD_SECRET, ex);

			g_free (base_prompt);
			g_free (full_prompt);
			g_free (errbuf);
			errbuf = NULL;

			if (!service->url->passwd) {
				camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
					_("You did not enter a password."));
				return FALSE;
			}
		}

		if (!store->connected) {
			/* Some servers (eg, courier) will disconnect on
			 * a bad password. So reconnect here. */
			if (!connect_to_server_wrapper (service, ex))
				return FALSE;
		}

		if (authtype)
			authenticated = try_auth (store, authtype->authproto, ex);
		else {
			response = camel_imap_command (store, NULL, ex,
						       "LOGIN %S %S",
						       service->url->user,
						       service->url->passwd);
			if (response) {
				camel_imap_response_free (store, response);
				authenticated = TRUE;
			}
		}
		if (!authenticated) {
			if (camel_exception_get_id(ex) == CAMEL_EXCEPTION_USER_CANCEL)
				return FALSE;

			errbuf = g_markup_printf_escaped (
				_("Unable to authenticate to IMAP server.\n%s\n\n"),
				camel_exception_get_description (ex));
			camel_exception_clear (ex);
		}
	}

	return TRUE;
}

static gboolean
can_work_offline (CamelDiscoStore *disco_store)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (disco_store);

	return camel_store_summary_count((CamelStoreSummary *)store->summary) != 0;
}

static gboolean
imap_connect_online (CamelService *service, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelImapResponse *response;
	/*struct _namespaces *namespaces;*/
	char *result, *name;
	size_t len;
	CamelImapStoreNamespace *ns;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	if (!connect_to_server_wrapper (service, ex) ||
	    !imap_auth_loop (service, ex)) {
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		camel_service_disconnect (service, TRUE, NULL);
		return FALSE;
	}

	/* Get namespace and hierarchy separator */
	if ((store->capabilities & IMAP_CAPABILITY_NAMESPACE) &&
	    !(store->parameters & IMAP_PARAM_OVERRIDE_NAMESPACE)) {
		response = camel_imap_command (store, NULL, ex, "NAMESPACE");
		if (!response)
			goto done;

		result = camel_imap_response_extract (store, response, "NAMESPACE", ex);
		if (!result)
			goto done;

#if 0
		/* new code... */
		namespaces = imap_parse_namespace_response (result);
		imap_namespaces_destroy (namespaces);
		/* end new code */
#endif

		name = camel_strstrcase (result, "NAMESPACE ((");
		if (name) {
			char *sep;

			name += 12;
			store->namespace = imap_parse_string ((const char **) &name, &len);
			if (name && *name++ == ' ') {
				sep = imap_parse_string ((const char **) &name, &len);
				if (sep) {
					store->dir_sep = *sep;
					g_free (sep);
				}
			}
		}
		g_free (result);
	}

	if (!store->namespace)
		store->namespace = g_strdup ("");

	if (!store->dir_sep) {
		if (store->server_level >= IMAP_LEVEL_IMAP4REV1) {
			/* This idiom means "tell me the hierarchy separator
			 * for the given path, even if that path doesn't exist.
			 */
			response = camel_imap_command (store, NULL, ex,
						       "LIST %G \"\"",
						       store->namespace);
		} else {
			/* Plain IMAP4 doesn't have that idiom, so we fall back
			 * to "tell me about this folder", which will fail if
			 * the folder doesn't exist (eg, if namespace is "").
			 */
			response = camel_imap_command (store, NULL, ex,
						       "LIST \"\" %G",
						       store->namespace);
		}
		if (!response)
			goto done;

		result = camel_imap_response_extract (store, response, "LIST", NULL);
		if (result) {
			imap_parse_list_response (store, result, NULL, &store->dir_sep, NULL);
			g_free (result);
		}

		if (!store->dir_sep)
			store->dir_sep = '/';	/* Guess */

	}

	/* canonicalize the namespace to end with dir_sep */
	len = strlen (store->namespace);
	if (len && store->namespace[len - 1] != store->dir_sep) {
		gchar *tmp;

		tmp = g_strdup_printf ("%s%c", store->namespace, store->dir_sep);
		g_free (store->namespace);
		store->namespace = tmp;
	}

	ns = camel_imap_store_summary_namespace_new(store->summary, store->namespace, store->dir_sep);
	camel_imap_store_summary_namespace_set(store->summary, ns);

	if ((store->parameters & IMAP_PARAM_SUBSCRIPTIONS)
	    && camel_store_summary_count((CamelStoreSummary *)store->summary) == 0) {
		CamelStoreInfo *si;
		char *pattern;

		get_folders_sync(store, store->namespace, ex);
		if (camel_exception_is_set(ex))
			goto done;
		pattern = imap_concat(store, store->namespace, "*");
		get_folders_sync(store, pattern, ex);
		g_free (pattern);
		if (camel_exception_is_set(ex))
			goto done;

		/* Make sure INBOX is present/subscribed */
		si = camel_store_summary_path((CamelStoreSummary *)store->summary, "INBOX");
		if (si == NULL || (si->flags & CAMEL_FOLDER_SUBSCRIBED) == 0) {
			response = camel_imap_command (store, NULL, ex, "SUBSCRIBE INBOX");
			if (response != NULL) {
				camel_imap_response_free (store, response);
			}
			if (si)
				camel_store_summary_info_free((CamelStoreSummary *)store->summary, si);
			if (camel_exception_is_set(ex))
				goto done;
			get_folders_sync(store, "INBOX", ex);
		}

		store->refresh_stamp = time(NULL);
	}

done:
	/* save any changes we had */
	camel_store_summary_save((CamelStoreSummary *)store->summary);

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	if (camel_exception_is_set (ex))
		camel_service_disconnect (service, TRUE, NULL);

	return !camel_exception_is_set (ex);
}

static gboolean
imap_connect_offline (CamelService *service, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (service);

	if (!disco_store->diary)
		return FALSE;

	store->connected = !camel_exception_is_set (ex);

	return store->connected;
}

static gboolean
imap_disconnect_offline (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);

	if (store->istream) {
		camel_stream_close(store->istream);
		camel_object_unref(store->istream);
		store->istream = NULL;
	}

	if (store->ostream) {
		camel_stream_close(store->ostream);
		camel_object_unref(store->ostream);
		store->ostream = NULL;
	}

	store->connected = FALSE;
	if (store->current_folder) {
		camel_object_unref (store->current_folder);
		store->current_folder = NULL;
	}

	if (store->authtypes) {
		g_hash_table_foreach_remove (store->authtypes,
					     free_key, NULL);
		g_hash_table_destroy (store->authtypes);
		store->authtypes = NULL;
	}

	if (store->namespace && !(store->parameters & IMAP_PARAM_OVERRIDE_NAMESPACE)) {
		g_free (store->namespace);
		store->namespace = NULL;
	}

	return TRUE;
}

static gboolean
imap_disconnect_online (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelImapResponse *response;

	if (store->connected && clean) {
		response = camel_imap_command (store, NULL, NULL, "LOGOUT");
		camel_imap_response_free (store, response);
	}

	imap_disconnect_offline (service, clean, ex);

	return TRUE;
}


static gboolean
imap_summary_is_dirty (CamelFolderSummary *summary)
{
	CamelImapMessageInfo *info;
	int max, i;
	int found = FALSE;

	max = camel_folder_summary_count (summary);
	for (i = 0; i < max && !found; i++) {
		info = (CamelImapMessageInfo *)camel_folder_summary_index (summary, i);
		if (info) {
			found = info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED;
			camel_message_info_free(info);
		}
	}

	return FALSE;
}

static void
imap_noop (CamelStore *store, CamelException *ex)
{
	CamelImapStore *imap_store = (CamelImapStore *) store;
	CamelImapResponse *response;
	CamelFolder *current_folder;

	CAMEL_SERVICE_REC_LOCK (imap_store, connect_lock);

	if (!camel_imap_store_connected(imap_store, ex))
		goto done;

	current_folder = imap_store->current_folder;
	if (current_folder && imap_summary_is_dirty (current_folder->summary)) {
		/* let's sync the flags instead.  NB: must avoid folder lock */
		((CamelFolderClass *)((CamelObject *)current_folder)->klass)->sync(current_folder, FALSE, ex);
	} else {
		response = camel_imap_command (imap_store, NULL, ex, "NOOP");
		if (response)
			camel_imap_response_free (imap_store, response);
	}
done:
	CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
}

static CamelFolder *
imap_get_trash(CamelStore *store, CamelException *ex)
{
	CamelFolder *folder = CAMEL_STORE_CLASS(parent_class)->get_trash(store, ex);

	if (folder) {
		char *state = g_build_filename(((CamelImapStore *)store)->storage_path, "system", "Trash.cmeta", NULL);

		camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state, NULL);
		g_free(state);
		/* no defaults? */
		camel_object_state_read(folder);
	}

	return folder;
}

static CamelFolder *
imap_get_junk(CamelStore *store, CamelException *ex)
{
	CamelFolder *folder = CAMEL_STORE_CLASS(parent_class)->get_junk(store, ex);

	if (folder) {
		char *state = g_build_filename(((CamelImapStore *)store)->storage_path, "system", "Junk.cmeta", NULL);

		camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state, NULL);
		g_free(state);
		/* no defaults? */
		camel_object_state_read(folder);
	}

	return folder;
}

static guint
hash_folder_name (gconstpointer key)
{
	if (g_ascii_strcasecmp (key, "INBOX") == 0)
		return g_str_hash ("INBOX");
	else
		return g_str_hash (key);
}

static gint
compare_folder_name (gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp (b, "INBOX") == 0)
		bname = "INBOX";
	return g_str_equal (aname, bname);
}

struct imap_status_item {
	struct imap_status_item *next;
	char *name;
	guint32 value;
};

static void
imap_status_item_free (struct imap_status_item *items)
{
	struct imap_status_item *next;

	while (items != NULL) {
		next = items->next;
		g_free (items->name);
		g_free (items);
		items = next;
	}
}

static struct imap_status_item *
get_folder_status (CamelImapStore *imap_store, const char *folder_name, const char *type)
{
	struct imap_status_item *items, *item, *tail;
	CamelImapResponse *response;
	char *status, *name, *p;

	/* FIXME: we assume the server is STATUS-capable */

	response = camel_imap_command (imap_store, NULL, NULL,
				       "STATUS %F (%s)",
				       folder_name,
				       type);

	if (!response) {
		CamelException ex;

		camel_exception_init (&ex);
		if (imap_check_folder_still_extant (imap_store, folder_name, &ex) == FALSE) {
			imap_folder_effectively_unsubscribed (imap_store, folder_name, &ex);
			imap_forget_folder (imap_store, folder_name, &ex);
		}
		camel_exception_clear (&ex);
		return NULL;
	}

	if (!(status = camel_imap_response_extract (imap_store, response, "STATUS", NULL)))
		return NULL;

	p = status + strlen ("* STATUS ");
	while (*p == ' ')
		p++;

	/* skip past the mailbox string */
	if (*p == '"') {
		p++;
		while (*p != '\0') {
			if (*p == '"' && p[-1] != '\\') {
				p++;
				break;
			}

			p++;
		}
	} else {
		while (*p != ' ')
			p++;
	}

	while (*p == ' ')
		p++;

	if (*p++ != '(') {
		g_free (status);
		return NULL;
	}

	while (*p == ' ')
		p++;

	if (*p == ')') {
		g_free (status);
		return NULL;
	}

	items = NULL;
	tail = (struct imap_status_item *) &items;

	do {
		name = p;
		while (*p != ' ')
			p++;

		item = g_malloc (sizeof (struct imap_status_item));
		item->next = NULL;
		item->name = g_strndup (name, p - name);
		item->value = strtoul (p, &p, 10);

		tail->next = item;
		tail = item;

		while (*p == ' ')
			p++;
	} while (*p != ')');

	g_free (status);

	return items;
}

static CamelFolder *
get_folder_online (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	CamelFolder *new_folder;
	char *folder_dir, *storage_path;

	/* Try to get it locally first, if it is, then the client will
	   force a select when necessary */
	new_folder = get_folder_offline(store, folder_name, flags, ex);
	if (new_folder)
		return new_folder;
	camel_exception_clear(ex);

	CAMEL_SERVICE_REC_LOCK(imap_store, connect_lock);

	if (!camel_imap_store_connected(imap_store, ex)) {
		CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
		return NULL;
	}

	if (!g_ascii_strcasecmp (folder_name, "INBOX"))
		folder_name = "INBOX";

	if (imap_store->current_folder) {
		camel_object_unref (imap_store->current_folder);
		imap_store->current_folder = NULL;
	}

	response = camel_imap_command (imap_store, NULL, ex, "SELECT %F", folder_name);
	if (!response) {
		char *folder_real, *parent_name, *parent_real;
		const char *c;

		if (camel_exception_get_id(ex) == CAMEL_EXCEPTION_USER_CANCEL) {
			CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
			return NULL;
		}

		camel_exception_clear (ex);

		if (!(flags & CAMEL_STORE_FOLDER_CREATE)) {
			CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
			camel_exception_setv (ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
					      _("No such folder %s"), folder_name);
			return NULL;
		}

		parent_name = strrchr(folder_name, '/');
		c = parent_name ? parent_name+1 : folder_name;
		while (*c && *c != imap_store->dir_sep && !strchr ("#%*", *c))
			c++;

		if (*c != '\0') {
			CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
			camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_PATH,
					      _("The folder name \"%s\" is invalid because it contains the character \"%c\""),
					      folder_name, *c);
			return NULL;
		}

		if (parent_name) {
			parent_name = g_strndup (folder_name, parent_name - folder_name);
			parent_real = camel_imap_store_summary_path_to_full (imap_store->summary, parent_name, imap_store->dir_sep);
		} else {
			parent_real = NULL;
		}

		if (parent_real != NULL) {
			gboolean need_convert = FALSE;
			char *resp, *thisone;
			guint32 flags;
			int i;

			if (!(response = camel_imap_command (imap_store, NULL, ex, "LIST \"\" %G", parent_real))) {
				CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
				g_free (parent_name);
				g_free (parent_real);
				return NULL;
			}

			/* FIXME: does not handle unexpected circumstances very well */
			for (i = 0; i < response->untagged->len; i++) {
				resp = response->untagged->pdata[i];

				if (!imap_parse_list_response (imap_store, resp, &flags, NULL, &thisone))
					continue;

				if (!strcmp (parent_name, thisone)) {
					if (flags & CAMEL_FOLDER_NOINFERIORS)
						need_convert = TRUE;
				}

				g_free (thisone);
			}

			camel_imap_response_free (imap_store, response);

			/* if not, check if we can delete it and recreate it */
			if (need_convert) {
				struct imap_status_item *items, *item;
				guint32 messages = 0;
				CamelException lex;
				char *name;

				item = items = get_folder_status (imap_store, parent_name, "MESSAGES");
				while (item != NULL) {
					if (!g_ascii_strcasecmp (item->name, "MESSAGES")) {
						messages = item->value;
						break;
					}

					item = item->next;
				}

				imap_status_item_free (items);

				if (messages > 0) {
					camel_exception_set (ex, CAMEL_EXCEPTION_FOLDER_INVALID_STATE,
							     _("The parent folder is not allowed to contain subfolders"));
					CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				}

				/* delete the old parent and recreate it */
				camel_exception_init (&lex);
				delete_folder (store, parent_name, &lex);
				if (camel_exception_is_set (&lex)) {
					CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
					camel_exception_xfer (ex, &lex);
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				}

				/* add the dirsep to the end of parent_name */
				name = g_strdup_printf ("%s%c", parent_real, imap_store->dir_sep);
				response = camel_imap_command (imap_store, NULL, ex, "CREATE %G",
							       name);
				g_free (name);

				if (!response) {
					CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				} else
					camel_imap_response_free (imap_store, response);
			}

			g_free (parent_real);
		}

		g_free (parent_name);

		folder_real = camel_imap_store_summary_path_to_full(imap_store->summary, folder_name, imap_store->dir_sep);
		response = camel_imap_command (imap_store, NULL, ex, "CREATE %G", folder_real);
		if (response) {
			camel_imap_store_summary_add_from_full(imap_store->summary, folder_real, imap_store->dir_sep);

			camel_imap_response_free (imap_store, response);

			response = camel_imap_command (imap_store, NULL, NULL, "SELECT %F", folder_name);
		}
		g_free(folder_real);
		if (!response) {
			CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
			return NULL;
		}
	} else if (flags & CAMEL_STORE_FOLDER_EXCL) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot create folder `%s': folder exists."),
				      folder_name);

		camel_imap_response_free_without_processing (imap_store, response);

		CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);

		return NULL;
	}

	storage_path = g_strdup_printf("%s/folders", imap_store->storage_path);
	folder_dir = imap_path_to_physical (storage_path, folder_name);
	g_free(storage_path);
	new_folder = camel_imap_folder_new (store, folder_name, folder_dir, ex);
	g_free (folder_dir);
	if (new_folder) {
		CamelException local_ex;

		imap_store->current_folder = new_folder;
		camel_object_ref (new_folder);
		camel_exception_init (&local_ex);
		camel_imap_folder_selected (new_folder, response, &local_ex);

		if (camel_exception_is_set (&local_ex)) {
			camel_exception_xfer (ex, &local_ex);
			camel_object_unref (imap_store->current_folder);
			imap_store->current_folder = NULL;
			camel_object_unref (new_folder);
			new_folder = NULL;
		}
	}
	camel_imap_response_free_without_processing (imap_store, response);

	CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);

	return new_folder;
}

static CamelFolder *
get_folder_offline (CamelStore *store, const char *folder_name,
		    guint32 flags, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelFolder *new_folder = NULL;
	CamelStoreInfo *si;

	si = camel_store_summary_path((CamelStoreSummary *)imap_store->summary, folder_name);
	if (si) {
		char *folder_dir, *storage_path;

		/* Note: Although the INBOX is defined to be case-insensitive in the IMAP RFC
		 * it is still up to the server how to acutally name it in a LIST response. Since
		 * we stored the name as the server provided it us in the summary we take that name
		 * to look up the folder. 
		 * But for the on-disk cache we do always capitalize the Inbox no matter what the
		 * server provided.
		 */
		if (!g_ascii_strcasecmp (folder_name, "INBOX"))
			folder_name = "INBOX";
	
		storage_path = g_strdup_printf("%s/folders", imap_store->storage_path);
		folder_dir = imap_path_to_physical (storage_path, folder_name);
		g_free(storage_path);
		new_folder = camel_imap_folder_new (store, folder_name, folder_dir, ex);
		g_free(folder_dir);

		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	} else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				      _("No such folder %s"), folder_name);
	}

	return new_folder;
}

static void
delete_folder (CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;

	CAMEL_SERVICE_REC_LOCK (imap_store, connect_lock);

	if (!camel_imap_store_connected(imap_store, ex))
		goto fail;

	/* make sure this folder isn't currently SELECTed */
	response = camel_imap_command (imap_store, NULL, ex, "SELECT INBOX");
	if (!response)
		goto fail;

	camel_imap_response_free_without_processing (imap_store, response);
	if (imap_store->current_folder)
		camel_object_unref (imap_store->current_folder);
	/* no need to actually create a CamelFolder for INBOX */
	imap_store->current_folder = NULL;

	response = camel_imap_command(imap_store, NULL, ex, "DELETE %F", folder_name);
	if (response) {
		camel_imap_response_free (imap_store, response);
		imap_forget_folder (imap_store, folder_name, ex);
	}
fail:
	CAMEL_SERVICE_REC_UNLOCK(imap_store, connect_lock);
}

static void
manage_subscriptions (CamelStore *store, const char *old_name, gboolean subscribe)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelStoreInfo *si;
	int olen = strlen(old_name);
	const char *path;
	int i, count;

	count = camel_store_summary_count((CamelStoreSummary *)imap_store->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index((CamelStoreSummary *)imap_store->summary, i);
		if (si) {
			path = camel_store_info_path(imap_store->summary, si);
			if (strncmp(path, old_name, olen) == 0) {
				if (subscribe)
					subscribe_folder(store, path, NULL);
				else
					unsubscribe_folder(store, path, NULL);
			}
			camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
		}
	}
}

static void
rename_folder_info (CamelImapStore *imap_store, const char *old_name, const char *new_name)
{
	int i, count;
	CamelStoreInfo *si;
	int olen = strlen(old_name);
	const char *path;
	char *npath, *nfull;

	count = camel_store_summary_count((CamelStoreSummary *)imap_store->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index((CamelStoreSummary *)imap_store->summary, i);
		if (si == NULL)
			continue;
		path = camel_store_info_path(imap_store->summary, si);
		if (strncmp(path, old_name, olen) == 0) {
			if (strlen(path) > olen)
				npath = g_strdup_printf("%s/%s", new_name, path+olen+1);
			else
				npath = g_strdup(new_name);
			nfull = camel_imap_store_summary_path_to_full(imap_store->summary, npath, imap_store->dir_sep);

			/* workaround for broken server (courier uses '.') that doesn't rename
			   subordinate folders as required by rfc 2060 */
			if (imap_store->dir_sep == '.') {
				CamelImapResponse *response;

				response = camel_imap_command (imap_store, NULL, NULL, "RENAME %F %G", path, nfull);
				if (response)
					camel_imap_response_free (imap_store, response);
			}

			camel_store_info_set_string((CamelStoreSummary *)imap_store->summary, si, CAMEL_STORE_INFO_PATH, npath);
			camel_store_info_set_string((CamelStoreSummary *)imap_store->summary, si, CAMEL_IMAP_STORE_INFO_FULL_NAME, nfull);

			camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
			g_free(nfull);
			g_free(npath);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}
}

static void
rename_folder (CamelStore *store, const char *old_name, const char *new_name_in, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	char *oldpath, *newpath, *storage_path;

	CAMEL_SERVICE_REC_LOCK (imap_store, connect_lock);

	if (!camel_imap_store_connected(imap_store, ex))
		goto fail;

	/* make sure this folder isn't currently SELECTed - it's
           actually possible to rename INBOX but if you do another
           INBOX will immediately be created by the server */
	response = camel_imap_command (imap_store, NULL, ex, "SELECT INBOX");
	if (!response)
		goto fail;

	camel_imap_response_free_without_processing (imap_store, response);
	if (imap_store->current_folder)
		camel_object_unref (imap_store->current_folder);
	/* no need to actually create a CamelFolder for INBOX */
	imap_store->current_folder = NULL;

	imap_store->renaming = TRUE;
	if (imap_store->parameters & IMAP_PARAM_SUBSCRIPTIONS)
		manage_subscriptions(store, old_name, FALSE);

	response = camel_imap_command (imap_store, NULL, ex, "RENAME %F %F", old_name, new_name_in);
	if (!response) {
		if (imap_store->parameters & IMAP_PARAM_SUBSCRIPTIONS)
			manage_subscriptions(store, old_name, TRUE);
		goto fail;
	}

	camel_imap_response_free (imap_store, response);

	/* rename summary, and handle broken server */
	rename_folder_info(imap_store, old_name, new_name_in);

	if (imap_store->parameters & IMAP_PARAM_SUBSCRIPTIONS)
		manage_subscriptions(store, new_name_in, TRUE);

	storage_path = g_strdup_printf("%s/folders", imap_store->storage_path);
	oldpath = imap_path_to_physical (storage_path, old_name);
	newpath = imap_path_to_physical (storage_path, new_name_in);
	g_free(storage_path);

	/* So do we care if this didn't work?  Its just a cache? */
	if (g_rename (oldpath, newpath) == -1) {
		g_warning ("Could not rename message cache '%s' to '%s': %s: cache reset",
			   oldpath, newpath, strerror (errno));
	}

	g_free (oldpath);
	g_free (newpath);
fail:
	imap_store->renaming = FALSE;
	CAMEL_SERVICE_REC_UNLOCK(imap_store, connect_lock);
}

static CamelFolderInfo *
create_folder (CamelStore *store, const char *parent_name,
	       const char *folder_name, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	char *full_name, *resp, *thisone, *parent_real, *real_name;
	CamelImapResponse *response;
	CamelException internal_ex;
	CamelFolderInfo *root = NULL;
	gboolean need_convert;
	int i = 0, flags;
	const char *c;

	if (!camel_disco_store_check_online (CAMEL_DISCO_STORE (store), ex))
		return NULL;
	if (!parent_name)
		parent_name = "";

	c = folder_name;
	while (*c && *c != imap_store->dir_sep && !strchr ("#%*", *c))
		c++;

	if (*c != '\0') {
		camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_PATH,
				      _("The folder name \"%s\" is invalid because it contains the character \"%c\""),
				      folder_name, *c);
		return NULL;
	}

	/* check if the parent allows inferiors */

	/* FIXME: use storesummary directly */
	parent_real = camel_imap_store_summary_full_from_path(imap_store->summary, parent_name);
	if (parent_real == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_STATE,
				     _("Unknown parent folder: %s"), parent_name);
		return NULL;
	}

	need_convert = FALSE;
	response = camel_imap_command (imap_store, NULL, ex, "LIST \"\" %G",
				       parent_real);
	if (!response) /* whoa, this is bad */ {
		g_free(parent_real);
		return NULL;
	}

	/* FIXME: does not handle unexpected circumstances very well */
	for (i = 0; i < response->untagged->len && !need_convert; i++) {
		resp = response->untagged->pdata[i];

		if (!imap_parse_list_response (imap_store, resp, &flags, NULL, &thisone))
			continue;

		if (strcmp (thisone, parent_name) == 0) {
			if (flags & CAMEL_FOLDER_NOINFERIORS)
				need_convert = TRUE;
		}

		g_free(thisone);
	}

	camel_imap_response_free (imap_store, response);

	camel_exception_init (&internal_ex);

	/* if not, check if we can delete it and recreate it */
	if (need_convert) {
		struct imap_status_item *items, *item;
		guint32 messages = 0;
		char *name;

		item = items = get_folder_status (imap_store, parent_name, "MESSAGES");
		while (item != NULL) {
			if (!g_ascii_strcasecmp (item->name, "MESSAGES")) {
				messages = item->value;
				break;
			}

			item = item->next;
		}

		imap_status_item_free (items);

		if (messages > 0) {
			camel_exception_set (ex, CAMEL_EXCEPTION_FOLDER_INVALID_STATE,
					     _("The parent folder is not allowed to contain subfolders"));
			g_free(parent_real);
			return NULL;
		}

		/* delete the old parent and recreate it */
		delete_folder (store, parent_name, &internal_ex);
		if (camel_exception_is_set (&internal_ex)) {
			camel_exception_xfer (ex, &internal_ex);
			return NULL;
		}

		/* add the dirsep to the end of parent_name */
		name = g_strdup_printf ("%s%c", parent_real, imap_store->dir_sep);
		response = camel_imap_command (imap_store, NULL, ex, "CREATE %G",
					       name);
		g_free (name);

		if (!response) {
			g_free(parent_real);
			return NULL;
		} else
			camel_imap_response_free (imap_store, response);

		root = imap_build_folder_info(imap_store, parent_name);
	}

	/* ok now we can create the folder */
	real_name = camel_imap_store_summary_path_to_full(imap_store->summary, folder_name, imap_store->dir_sep);
	full_name = imap_concat (imap_store, parent_real, real_name);
	g_free(real_name);
	response = camel_imap_command (imap_store, NULL, ex, "CREATE %G", full_name);

	if (response) {
		CamelImapStoreInfo *si;
		CamelFolderInfo *fi;

		camel_imap_response_free (imap_store, response);

		si = camel_imap_store_summary_add_from_full(imap_store->summary, full_name, imap_store->dir_sep);
		camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		fi = imap_build_folder_info(imap_store, camel_store_info_path(imap_store->summary, si));
		fi->flags |= CAMEL_FOLDER_NOCHILDREN;
		if (root) {
			root->child = fi;
			fi->parent = root;
		} else {
			root = fi;
		}
		camel_object_trigger_event (CAMEL_OBJECT (store), "folder_created", root);
	} else if (root) {
		/* need to re-recreate the folder we just deleted */
		camel_object_trigger_event (CAMEL_OBJECT (store), "folder_created", root);
		camel_folder_info_free(root);
		root = NULL;
	}

	g_free (full_name);
	g_free(parent_real);

	return root;
}

static CamelFolderInfo *
parse_list_response_as_folder_info (CamelImapStore *imap_store,
				    const char *response)
{
	CamelFolderInfo *fi;
	int flags;
	char sep, *dir, *path;
	CamelURL *url;
	CamelImapStoreInfo *si;
	guint32 newflags;

	if (!imap_parse_list_response (imap_store, response, &flags, &sep, &dir))
		return NULL;

	/* FIXME: should use imap_build_folder_info, note the differences with param setting tho */

	si = camel_imap_store_summary_add_from_full(imap_store->summary, dir, sep?sep:'/');
	g_free(dir);
	if (si == NULL)
		return NULL;

	newflags = (si->info.flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) | (flags & ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED);
	if (si->info.flags != newflags) {
		si->info.flags = newflags;
		camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
	}

	flags = (flags & ~CAMEL_FOLDER_SUBSCRIBED) | (si->info.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED);

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup(camel_store_info_path(imap_store->summary, si));
	if (!g_ascii_strcasecmp(fi->full_name, "inbox")) {
		flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_TYPE_INBOX;
		fi->name = g_strdup (_("Inbox"));
	} else
		fi->name = g_strdup(camel_store_info_name(imap_store->summary, si));

	/* HACK: some servers report noinferiors for all folders (uw-imapd)
	   We just translate this into nochildren, and let the imap layer enforce
	   it.  See create folder */
	if (flags & CAMEL_FOLDER_NOINFERIORS)
		flags = (flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;
	fi->flags = flags;

	url = camel_url_new (imap_store->base_url, NULL);
	path = alloca(strlen(fi->full_name)+2);
	sprintf(path, "/%s", fi->full_name);
	camel_url_set_path(url, path);

	if (flags & CAMEL_FOLDER_NOSELECT || fi->name[0] == 0)
		camel_url_set_param (url, "noselect", "yes");
	fi->uri = camel_url_to_string (url, 0);
	camel_url_free (url);

	fi->total = -1;
	fi->unread = -1;

	return fi;
}

static int imap_match_pattern(char dir_sep, const char *pattern, const char *name)
{
	char p, n;

	p = *pattern++;
	n = *name++;
	while (n && p) {
		if (n == p) {
			p = *pattern++;
			n = *name++;
		} else if (p == '%') {
			if (n != dir_sep) {
				n = *name++;
			} else {
				p = *pattern++;
			}
		} else if (p == '*') {
			return TRUE;
		} else
			return FALSE;
	}

	return n == 0 && (p == '%' || p == 0);
}

/* imap needs to treat inbox case insensitive */
/* we'll assume the names are normalised already */
static guint folder_hash(const void *ap)
{
	const char *a = ap;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		a = "INBOX";

	return g_str_hash(a);
}

static int folder_eq(const void *ap, const void *bp)
{
	const char *a = ap;
	const char *b = bp;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		a = "INBOX";
	if (g_ascii_strcasecmp(b, "INBOX") == 0)
		b = "INBOX";

	return g_str_equal(a, b);
}

static void
get_folders_free(void *k, void *v, void *d)
{
	camel_folder_info_free(v);
}

static void
get_folders_sync(CamelImapStore *imap_store, const char *pattern, CamelException *ex)
{
	CamelImapResponse *response;
	CamelFolderInfo *fi, *hfi;
	char *list;
	int i, count, j;
	GHashTable *present;
	CamelStoreInfo *si;

	/* We do a LIST followed by LSUB, and merge the results.  LSUB may not be a strict
	   subset of LIST for some servers, so we can't use either or separately */
	present = g_hash_table_new(folder_hash, folder_eq);

	for (j=0;j<2;j++) {
		response = camel_imap_command (imap_store, NULL, ex,
					       "%s \"\" %G", j==1 ? "LSUB" : "LIST",
					       pattern);
		if (!response)
			goto fail;

		for (i = 0; i < response->untagged->len; i++) {
			list = response->untagged->pdata[i];
			fi = parse_list_response_as_folder_info (imap_store, list);
			if (fi) {
				hfi = g_hash_table_lookup(present, fi->full_name);
				if (hfi == NULL) {
					if (j == 1) {
						fi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
						if ((fi->flags & (CAMEL_IMAP_FOLDER_MARKED | CAMEL_IMAP_FOLDER_UNMARKED)))
							imap_store->capabilities |= IMAP_CAPABILITY_useful_lsub;
					}
					g_hash_table_insert(present, fi->full_name, fi);
				} else {
					if (j == 1)
						hfi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
					camel_folder_info_free(fi);
				}
			}
		}
		camel_imap_response_free (imap_store, response);
	}

	/* Sync summary to match */

	/* FIXME: we need to emit folder_create/subscribed/etc events for any new folders */
	count = camel_store_summary_count((CamelStoreSummary *)imap_store->summary);

	for (i=0;i<count;i++) {
		si = camel_store_summary_index((CamelStoreSummary *)imap_store->summary, i);
		if (si == NULL)
			continue;

		if (imap_match_pattern(imap_store->dir_sep, pattern, camel_imap_store_info_full_name(imap_store->summary, si))) {
			if ((fi = g_hash_table_lookup(present, camel_store_info_path(imap_store->summary, si))) != NULL) {
				if (((fi->flags ^ si->flags) & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)) {
					si->flags = (si->flags & ~CAMEL_FOLDER_SUBSCRIBED) | (fi->flags & CAMEL_FOLDER_SUBSCRIBED);
					camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
				}
			} else {
				camel_store_summary_remove((CamelStoreSummary *)imap_store->summary, si);
				count--;
				i--;
			}
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}
fail:
	g_hash_table_foreach(present, get_folders_free, NULL);
	g_hash_table_destroy(present);
}

#if 0
static void
dumpfi(CamelFolderInfo *fi)
{
	int depth;
	CamelFolderInfo *n = fi;

	if (fi == NULL)
		return;

	depth = 0;
	while (n->parent) {
		depth++;
		n = n->parent;
	}

	while (fi) {
		printf("%-40s %-30s %*s\n", fi->path, fi->full_name, depth*2+strlen(fi->url), fi->url);
		if (fi->child)
			dumpfi(fi->child);
		fi = fi->sibling;
	}
}
#endif

static void
fill_fi(CamelStore *store, CamelFolderInfo *fi, guint32 flags)
{
	CamelFolder *folder;

	folder = camel_object_bag_peek(store->folders, fi->full_name);
	if (folder) {
		fi->unread = camel_folder_get_unread_message_count(folder);
		fi->total = camel_folder_get_message_count(folder);
		camel_object_unref(folder);
	}
}

struct _refresh_msg {
	CamelSessionThreadMsg msg;

	CamelStore *store;
	CamelException ex;
};

static void
refresh_refresh(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _refresh_msg *m = (struct _refresh_msg *)msg;
	CamelImapStore *store = (CamelImapStore *)m->store;

	CAMEL_SERVICE_REC_LOCK(m->store, connect_lock);

	if (!camel_imap_store_connected((CamelImapStore *)m->store, &m->ex))
		goto done;

	if (store->namespace && store->namespace[0]) {
		char *pattern;

		get_folders_sync(store, "INBOX", &m->ex);
		if (camel_exception_is_set(&m->ex))
			goto done;
		get_folders_sync(store, store->namespace, &m->ex);
		if (camel_exception_is_set(&m->ex))
			goto done;
		pattern = imap_concat(store, store->namespace, "*");
		get_folders_sync(store, pattern, &m->ex);
		g_free(pattern);
	} else {
		get_folders_sync((CamelImapStore *)m->store, "*", &m->ex);
	}
	camel_store_summary_save((CamelStoreSummary *)((CamelImapStore *)m->store)->summary);
done:
	CAMEL_SERVICE_REC_UNLOCK(m->store, connect_lock);
}

static void
refresh_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _refresh_msg *m = (struct _refresh_msg *)msg;

	camel_object_unref(m->store);
	camel_exception_clear(&m->ex);
}

static CamelSessionThreadOps refresh_ops = {
	refresh_refresh,
	refresh_free,
};

static CamelFolderInfo *
get_folder_info_online (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelFolderInfo *tree = NULL;

	/* If we have a list of folders already, use that, but if we haven't
	   updated for a while, then trigger an asynchronous rescan.  Otherwise
	   we update the list first, and then build it from that */

	if (top == NULL)
		top = "";

	if (camel_debug("imap:folder_info"))
		printf("get folder info online\n");

	if ((flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)
	    && camel_store_summary_count((CamelStoreSummary *)imap_store->summary) > 0) {
		time_t now;
		int ref;

		now = time(NULL);
		ref = now > imap_store->refresh_stamp+60*60*1;
		if (ref) {
			CAMEL_SERVICE_REC_LOCK(store, connect_lock);
			ref = now > imap_store->refresh_stamp+60*60*1;
			if (ref) {
				struct _refresh_msg *m;

				imap_store->refresh_stamp = now;

				m = camel_session_thread_msg_new(((CamelService *)store)->session, &refresh_ops, sizeof(*m));
				m->store = store;
				camel_object_ref(store);
				camel_exception_init(&m->ex);
				camel_session_thread_queue(((CamelService *)store)->session, &m->msg, 0);
			}
			CAMEL_SERVICE_REC_UNLOCK(store, connect_lock);
		}
	} else {
		char *pattern;
		int i;

		CAMEL_SERVICE_REC_LOCK(store, connect_lock);

		if (!camel_imap_store_connected((CamelImapStore *)store, ex))
			goto fail;

		if (top[0] == 0) {
			if (imap_store->namespace && imap_store->namespace[0]) {
				get_folders_sync(imap_store, "INBOX", ex);
				if (camel_exception_is_set(ex))
					goto fail;

				i = strlen(imap_store->namespace)-1;
				pattern = g_alloca(i+5);
				strcpy(pattern, imap_store->namespace);
				while (i>0 && pattern[i] == imap_store->dir_sep)
					pattern[i--] = 0;
				i++;
			} else {
				pattern = g_alloca(2);
				pattern[0] = '*';
				pattern[1] = 0;
				i=0;
			}
		} else {
			char *name;

			name = camel_imap_store_summary_full_from_path(imap_store->summary, top);
			if (name == NULL)
				name = camel_imap_store_summary_path_to_full(imap_store->summary, top, imap_store->dir_sep);

			i = strlen(name);
			pattern = g_alloca(i+5);
			strcpy(pattern, name);
			g_free(name);
		}

		get_folders_sync(imap_store, pattern, ex);
		if (camel_exception_is_set(ex))
			goto fail;
		if (pattern[0] != '*' && imap_store->dir_sep) {
			pattern[i] = imap_store->dir_sep;
			pattern[i+1] = (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)?'*':'%';
			pattern[i+2] = 0;
			get_folders_sync(imap_store, pattern, ex);
		}
		camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		CAMEL_SERVICE_REC_UNLOCK(store, connect_lock);
	}

	tree = get_folder_info_offline(store, top, flags, ex);
	return tree;

fail:
	CAMEL_SERVICE_REC_UNLOCK(store, connect_lock);
	return NULL;
}

static CamelFolderInfo *
get_folder_info_offline (CamelStore *store, const char *top,
			 guint32 flags, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	gboolean include_inbox = FALSE;
	CamelFolderInfo *fi;
	GPtrArray *folders;
	char *pattern, *name;
	int i;

	if (camel_debug("imap:folder_info"))
		printf("get folder info offline\n");

	/* FIXME: obey other flags */

	folders = g_ptr_array_new ();

	if (top == NULL || top[0] == '\0') {
		include_inbox = TRUE;
		top = "";
	}

	/* get starting point */
	if (top[0] == 0) {
		if (imap_store->namespace && imap_store->namespace[0]) {
			name = g_strdup(imap_store->summary->namespace->full_name);
			top = imap_store->summary->namespace->path;
		} else
			name = g_strdup("");
	} else {
		name = camel_imap_store_summary_full_from_path(imap_store->summary, top);
		if (name == NULL)
			name = camel_imap_store_summary_path_to_full(imap_store->summary, top, imap_store->dir_sep);
	}

	pattern = imap_concat(imap_store, name, "*");

	/* folder_info_build will insert parent nodes as necessary and mark
	 * them as noselect, which is information we actually don't have at
	 * the moment. So let it do the right thing by bailing out if it's
	 * not a folder we're explicitly interested in. */

	for (i=0;i<camel_store_summary_count((CamelStoreSummary *)imap_store->summary);i++) {
		CamelStoreInfo *si = camel_store_summary_index((CamelStoreSummary *)imap_store->summary, i);

		if (si == NULL)
			continue;

		if ((!strcmp(name, camel_imap_store_info_full_name(imap_store->summary, si))
		     || imap_match_pattern(imap_store->dir_sep, pattern, camel_imap_store_info_full_name(imap_store->summary, si))
		     || (include_inbox && !g_ascii_strcasecmp (camel_imap_store_info_full_name(imap_store->summary, si), "INBOX")))
		    && ((imap_store->parameters & IMAP_PARAM_SUBSCRIPTIONS) == 0
			|| (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) == 0
			|| (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED))) {

			fi = imap_build_folder_info(imap_store, camel_store_info_path((CamelStoreSummary *)imap_store->summary, si));
			fi->unread = si->unread;
			fi->total = si->total;
			fi->flags = si->flags;
			/* HACK: some servers report noinferiors for all folders (uw-imapd)
			   We just translate this into nochildren, and let the imap layer enforce
			   it.  See create folder */
			if (fi->flags & CAMEL_FOLDER_NOINFERIORS)
				fi->flags = (fi->flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;

			/* blah, this gets lost somewhere, i can't be bothered finding out why */
			if (!g_ascii_strcasecmp(fi->full_name, "inbox"))
				fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_INBOX;

			if (si->flags & CAMEL_FOLDER_NOSELECT) {
				CamelURL *url = camel_url_new(fi->uri, NULL);

				camel_url_set_param (url, "noselect", "yes");
				g_free(fi->uri);
				fi->uri = camel_url_to_string (url, 0);
				camel_url_free (url);
			} else {
				fill_fi((CamelStore *)imap_store, fi, 0);
			}
			if (!fi->child)
				fi->flags |= CAMEL_FOLDER_NOCHILDREN;
			g_ptr_array_add (folders, fi);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}
	g_free(pattern);

	fi = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	g_free(name);

	return fi;
}

static gboolean
folder_subscribed (CamelStore *store, const char *folder_name)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelStoreInfo *si;
	int truth = FALSE;

	si = camel_store_summary_path((CamelStoreSummary *)imap_store->summary, folder_name);
	if (si) {
		truth = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}

	return truth;
}

/* Note: folder_name must match a folder as listed with get_folder_info() -> full_name */
static void
subscribe_folder (CamelStore *store, const char *folder_name,
		  CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	CamelFolderInfo *fi;
	CamelStoreInfo *si;

	CAMEL_SERVICE_REC_LOCK(store, connect_lock);

	if (!camel_imap_store_connected (imap_store, ex))
		goto done;

	response = camel_imap_command (imap_store, NULL, ex,
				       "SUBSCRIBE %F", folder_name);
	if (!response)
		goto done;
	camel_imap_response_free (imap_store, response);

	si = camel_store_summary_path((CamelStoreSummary *)imap_store->summary, folder_name);
	if (si) {
		if ((si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) == 0) {
			si->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
			camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}

	if (imap_store->renaming) {
		/* we don't need to emit a "folder_subscribed" signal
                   if we are in the process of renaming folders, so we
                   are done here... */
		goto done;
	}

	fi = imap_build_folder_info(imap_store, folder_name);
	fi->flags |= CAMEL_FOLDER_NOCHILDREN;

	camel_object_trigger_event (CAMEL_OBJECT (store), "folder_subscribed", fi);
	camel_folder_info_free (fi);
done:
	CAMEL_SERVICE_REC_UNLOCK(store, connect_lock);
}

static void
unsubscribe_folder (CamelStore *store, const char *folder_name,
		    CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;

	CAMEL_SERVICE_REC_LOCK(store, connect_lock);

	if (!camel_imap_store_connected (imap_store, ex))
		goto done;

	response = camel_imap_command (imap_store, NULL, ex,
				       "UNSUBSCRIBE %F", folder_name);
	if (!response)
		goto done;
	camel_imap_response_free (imap_store, response);

	imap_folder_effectively_unsubscribed (imap_store, folder_name, ex);
done:
	CAMEL_SERVICE_REC_UNLOCK(store, connect_lock);
}

#if 0
static gboolean
folder_flags_have_changed (CamelFolder *folder)
{
	CamelMessageInfo *info;
	int i, max;

	max = camel_folder_summary_count (folder->summary);
	for (i = 0; i < max; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		if (!info)
			continue;
		if (info->flags & CAMEL_MESSAGE_FOLDER_FLAGGED) {
			return TRUE;
		}
	}

	return FALSE;
}
#endif

/* Use this whenever you need to ensure you're both connected and
   online. */
gboolean
camel_imap_store_connected (CamelImapStore *store, CamelException *ex)
{
	/* This looks stupid ... because it is.

	   camel-service-connect will return OK if we connect in 'offline mode',
	   which isn't what we want at all.  So we have to recheck we actually
	   did connect anyway ... */

	if (store->istream != NULL
	    || (camel_disco_store_check_online((CamelDiscoStore *)store, ex)
		&& camel_service_connect((CamelService *)store, ex)
		&& store->istream != NULL))
		return TRUE;

	if (!camel_exception_is_set(ex))
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				     _("You must be working online to complete this operation"));

	return FALSE;
}


/* FIXME: please god, when will the hurting stop? Thus function is so
   fucking broken it's not even funny. */
ssize_t
camel_imap_store_readline (CamelImapStore *store, char **dest, CamelException *ex)
{
	CamelStreamBuffer *stream;
	char linebuf[1024] = {0};
	GByteArray *ba;
	ssize_t nread;

	g_return_val_if_fail (CAMEL_IS_IMAP_STORE (store), -1);
	g_return_val_if_fail (dest, -1);

	*dest = NULL;

	/* Check for connectedness. Failed (or cancelled) operations will
	 * close the connection. We can't expect a read to have any
	 * meaning if we reconnect, so always set an exception.
	 */

	if (!camel_imap_store_connected (store, ex))
		return -1;

	stream = CAMEL_STREAM_BUFFER (store->istream);

	ba = g_byte_array_new ();
	while ((nread = camel_stream_buffer_gets (stream, linebuf, sizeof (linebuf))) > 0) {
		g_byte_array_append (ba, (const guint8 *) linebuf, nread);
		if (linebuf[nread - 1] == '\n')
			break;
	}

	if (nread <= 0) {
		if (errno == EINTR)
			camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL, _("Operation cancelled"));
		else
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Server unexpectedly disconnected: %s"),
					      g_strerror (errno));

		camel_service_disconnect (CAMEL_SERVICE (store), FALSE, NULL);
		g_byte_array_free (ba, TRUE);
		return -1;
	}

	if (camel_verbose_debug) {
		fprintf (stderr, "received: ");
		fwrite (ba->data, 1, ba->len, stderr);
	}

	/* camel-imap-command.c:imap_read_untagged expects the CRLFs
           to be stripped off and be nul-terminated *sigh* */
	nread = ba->len - 1;
	ba->data[nread] = '\0';
	if (ba->data[nread - 1] == '\r') {
		ba->data[nread - 1] = '\0';
		nread--;
	}

	*dest = ba->data;
	g_byte_array_free (ba, FALSE);

	return nread;
}

static gboolean
imap_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, CamelException *ex)
{
	gboolean res;

	res = CAMEL_STORE_CLASS(parent_class)->can_refresh_folder (store, info, ex) ||
	      (camel_url_get_param (((CamelService *)store)->url, "check_all") != NULL);

	if (!res && !camel_exception_is_set (ex)) {
		CamelFolder *folder;

		folder = camel_store_get_folder (store, info->full_name, 0, ex);
		if (folder && CAMEL_IS_IMAP_FOLDER (folder))
			res = CAMEL_IMAP_FOLDER (folder)->check_folder;
	}

	return res;
}

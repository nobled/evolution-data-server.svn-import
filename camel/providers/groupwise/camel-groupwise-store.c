/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-store.c : class for an groupwise store */

/*
 *  Authors:
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *  parthasarathi susarla <sparthasarathi@novell.com>
 *
 *  Copyright 2004 Novell, Inc.
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
 *
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "camel/camel-session.h"
#include "camel/camel-debug.h"
#include "camel/camel-i18n.h"
#include "camel/camel-types.h"
#include "camel/camel-folder.h" 
#include "camel/camel-private.h"
#include "camel/camel-net-utils.h"

#include "camel-groupwise-store.h"
#include "camel-groupwise-summary.h"
#include "camel-groupwise-store-summary.h"
#include "camel-groupwise-folder.h"
#include "camel-groupwise-utils.h"

#define d(x) 
#define CURSOR_ITEM_LIMIT 100
#define JUNK_ENABLE 1
#define JUNK_PERSISTENCE 14


struct _CamelGroupwiseStorePrivate {
	char *server_name;
	char *port;
	char *user;
	char *use_ssl;

	char *base_url;
	char *storage_path;

	GHashTable *id_hash; /*get names from ids*/
	GHashTable *name_hash;/*get ids from names*/
	GHashTable *parent_hash;
	EGwConnection *cnc;
};

static CamelOfflineStoreClass *parent_class = NULL;

extern CamelServiceAuthType camel_groupwise_password_authtype; /*for the query_auth_types function*/
CamelFolderInfo *convert_to_folder_info (CamelGroupwiseStore *store, EGwContainer *container, const char *url, CamelException *ex);
static void groupwise_folders_sync (CamelGroupwiseStore *store, CamelException *ex);
static int match_path(const char *path, const char *name);



static void
groupwise_store_construct (CamelService *service, CamelSession *session,
			   CamelProvider *provider, CamelURL *url,
			   CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelStore *store = CAMEL_STORE (service);
	const char *property_value;
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	char *path = NULL;
	
	d("in groupwise store constrcut\n");

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set (ex))
		return;
	
	if (!(url->host || url->user)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID,
				     _("Host or user not available in url"));
	}

	//store->flags = 0; 
	groupwise_store->list_loaded = FALSE;
	
	/*storage path*/
	priv->storage_path = camel_session_get_storage_path (session, service, ex);
	if (!priv->storage_path)
		return;
	
	/*store summary*/
	path = g_alloca (strlen (priv->storage_path) + 32);
	sprintf (path, "%s/.summary", priv->storage_path);
	groupwise_store->summary = camel_groupwise_store_summary_new ();
	camel_store_summary_set_filename ((CamelStoreSummary *)groupwise_store->summary, path);
	camel_store_summary_touch ((CamelStoreSummary *)groupwise_store->summary);
	camel_store_summary_load ((CamelStoreSummary *) groupwise_store->summary);
	
	/*host and user*/
	priv->server_name = g_strdup (url->host);
	priv->user = g_strdup (url->user);

	/*base url*/
	priv->base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
						       CAMEL_URL_HIDE_PARAMS   |
						       CAMEL_URL_HIDE_AUTH)  );

	/*soap port*/
	property_value =  camel_url_get_param (url, "soap_port");
	if (property_value == NULL)
		priv->port = g_strdup ("7191");
	else if(strlen(property_value) == 0)
		priv->port = g_strdup ("7191");
	else
		priv->port = g_strdup (property_value);

	/*filter*/
	if (camel_url_get_param (url, "filter")) 
		store->flags |= CAMEL_STORE_FILTER_INBOX;
	
	/*Hash Table*/	
	priv->id_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->name_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->parent_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/*ssl*/
	priv->use_ssl = g_strdup (camel_url_get_param (url, "use_ssl"));

	store->flags &= ~CAMEL_STORE_VJUNK;
	store->flags &= ~CAMEL_STORE_VTRASH;
}

static guint
groupwise_hash_folder_name (gconstpointer key)
{
	return g_str_hash (key);
}

static gint
groupwise_compare_folder_name (gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	return g_str_equal (aname, bname);
}

static gboolean
groupwise_auth_loop (CamelService *service, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelSession *session = camel_service_get_session (service);
	CamelStore *store = CAMEL_STORE (service);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	char *errbuf = NULL;
	gboolean authenticated = FALSE;
	char *uri;

	CAMEL_SERVICE_ASSERT_LOCKED (groupwise_store, connect_lock);
	if (priv->use_ssl && !g_str_equal (priv->use_ssl, "never")) 
		uri = g_strconcat ("https://", priv->server_name, ":", priv->port, "/soap", NULL);
	else 
		uri = g_strconcat ("http://", priv->server_name, ":", priv->port, "/soap", NULL);
	service->url->passwd = NULL;

	while (!authenticated) {
		if (errbuf) {
			/* We need to un-cache the password before prompting again */
			camel_session_forget_password (session, service, "Groupwise", "password", ex);
			g_free (service->url->passwd);
			service->url->passwd = NULL;
		}
		
		if (!service->url->passwd && !(store->flags & CAMEL_STORE_PROXY)) {
			char *prompt;
			
			prompt = g_strdup_printf (_("%sPlease enter the GroupWise "
						    "password for %s@%s"),
						  errbuf ? errbuf : "",
						  service->url->user,
						  service->url->host);
			service->url->passwd =
				camel_session_get_password (session, service, "Groupwise",
							    prompt, "password", CAMEL_SESSION_PASSWORD_SECRET, ex);
			g_free (prompt);
			g_free (errbuf);
			errbuf = NULL;
			
			if (!service->url->passwd) {
				camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
						     _("You didn't enter a password."));
				return FALSE;
			}
		}
		
		
				
		priv->cnc = e_gw_connection_new (uri, priv->user, service->url->passwd);
		if (!E_IS_GW_CONNECTION(priv->cnc) && priv->use_ssl && g_str_equal (priv->use_ssl, "when-possible")) {
			char *http_uri = g_strconcat ("http://", uri + 8, NULL);
			priv->cnc = e_gw_connection_new (http_uri, priv->user, service->url->passwd);
			g_free (http_uri);
		}
		if (!E_IS_GW_CONNECTION(priv->cnc)) {
			errbuf = g_strdup_printf (_("Unable to authenticate "
					    "to GroupWise server."));
						  
			camel_exception_clear (ex);
		} else 
			authenticated = TRUE;
		
	}
	
	return TRUE;
}

static gboolean
check_for_connection (CamelService *service, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	struct addrinfo hints, *ai;

	memset (&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	ai = camel_getaddrinfo(priv->server_name, "groupwise", &hints, ex);
	if (ai == NULL && priv->port != NULL && camel_exception_get_id(ex) != CAMEL_EXCEPTION_USER_CANCEL) {
		camel_exception_clear (ex);
		ai = camel_getaddrinfo(priv->server_name, priv->port, &hints, ex);
	}
	if (ai == NULL)
		return FALSE;

	camel_freeaddrinfo (ai);

	return TRUE;

}

static gboolean
groupwise_connect (CamelService *service, CamelException *ex)
{
	CamelGroupwiseStore *store = CAMEL_GROUPWISE_STORE (service);
	CamelGroupwiseStorePrivate *priv = store->priv;
	CamelGroupwiseStoreNamespace *ns;
	CamelSession *session = service->session;

	d("in groupwise store connect\n");
	
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL && 
	     (service->status == CAMEL_SERVICE_DISCONNECTED)) 
		return TRUE;
	
	CAMEL_SERVICE_LOCK (service, connect_lock);
	
	if (priv->cnc) {
		CAMEL_SERVICE_UNLOCK (service, connect_lock);
		return TRUE;
	}

	if (!check_for_connection (service, ex) || !groupwise_auth_loop (service, ex)) {
		CAMEL_SERVICE_UNLOCK (service, connect_lock);
		camel_service_disconnect (service, TRUE, NULL);
		return FALSE;
	}
	
	service->status = CAMEL_SERVICE_CONNECTED;

	if (!e_gw_connection_get_version (priv->cnc)) {
		camel_session_alert_user(session, 
				CAMEL_SESSION_ALERT_WARNING, 
				_("Some features may not work correctly with your current server version"),
				FALSE);

	}

	ns = camel_groupwise_store_summary_namespace_new (store->summary, priv->storage_path, '/');
	camel_groupwise_store_summary_namespace_set (store->summary, ns);

	if (camel_store_summary_count ((CamelStoreSummary *)store->summary) == 0) {
		/*XXX: Have to sync up folders here*/
		groupwise_folders_sync (store, ex);
		if (camel_exception_is_set (ex)) {
			camel_store_summary_save ((CamelStoreSummary *) store->summary);
			CAMEL_SERVICE_UNLOCK (service, connect_lock);
			camel_service_disconnect (service, TRUE, NULL);
			return FALSE;
		}
		/*Settting the refresh stamp to the current time*/
		store->refresh_stamp = time (0);
	}

	camel_store_summary_save ((CamelStoreSummary *) store->summary);

	CAMEL_SERVICE_UNLOCK (service, connect_lock);
	if (E_IS_GW_CONNECTION (priv->cnc)) {
		return TRUE;
	}

	return FALSE;

}

static gboolean
groupwise_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	
	CAMEL_SERVICE_LOCK (groupwise_store, connect_lock);
	if (groupwise_store->priv && groupwise_store->priv->cnc) {
		g_object_unref (groupwise_store->priv->cnc);
		groupwise_store->priv->cnc = NULL;
	}
	CAMEL_SERVICE_UNLOCK (groupwise_store, connect_lock);
	
	return TRUE;
}

static  GList*
groupwise_store_query_auth_types (CamelService *service, CamelException *ex)
{
	GList *auth_types = NULL;
	
	d("in query auth types\n");
	auth_types = g_list_prepend (auth_types,  &camel_groupwise_password_authtype);
	return auth_types;
}

static gboolean
groupwise_is_system_folder (const char *folder_name)
{
	if (!strcmp (folder_name, "Mailbox") ||
	    !strcmp (folder_name, "Trash") ||
	    !strcmp (folder_name, "Junk Mail") ||
	    !strcmp (folder_name, "Sent Items") ||
	    !strcmp (folder_name, "Cabinet") ||
	    !strcmp (folder_name, "Documents") )
		return TRUE;
	else
		return FALSE;
}

/*Build/populate CamelFolderInfo structure based on the imap_build_folder_info function*/
static CamelFolderInfo *
groupwise_build_folder_info(CamelGroupwiseStore *gw_store, const char *parent_name, const char *folder_name)
{
	CamelURL *url;
	const char *name;
	CamelFolderInfo *fi;
	CamelGroupwiseStorePrivate *priv = gw_store->priv;

	fi = g_malloc0(sizeof(*fi));
	
	fi->unread = -1;
	fi->total = -1;

	if (parent_name) {
		if (strlen(parent_name) > 0) 
			fi->full_name = g_strconcat(parent_name, "/", folder_name, NULL);
		else
			fi->full_name = g_strdup (folder_name);
	} else 
		fi->full_name = g_strdup(folder_name);
 
	url = camel_url_new(priv->base_url,NULL);
	g_free(url->path);
	url->path = g_strdup_printf("/%s", fi->full_name);
	fi->uri = camel_url_to_string(url,CAMEL_URL_HIDE_ALL);
	camel_url_free(url);

	name = strrchr(fi->full_name,'/');
	if(name == NULL)
		name = fi->full_name;
	else
		name++;
	
	fi->name = g_strdup(name);
	return fi;
}

static void
groupwise_forget_folder (CamelGroupwiseStore *gw_store, const char *folder_name, CamelException *ex)
{
	CamelFolderSummary *summary;
	CamelGroupwiseStorePrivate *priv = gw_store->priv;
	char *summary_file, *state_file;
	char *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	const char *name;

	
	name = folder_name;

	storage_path = g_strdup_printf ("%s/folders", priv->storage_path);
	folder_dir = g_strdup(e_path_to_physical (storage_path,folder_name));

	if (g_access(folder_dir, F_OK) != 0) {
		g_free(folder_dir);
		return;
	}

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	summary = camel_groupwise_summary_new(NULL,summary_file);
	if(!summary) {
		g_free(summary_file);
		g_free(folder_dir);
		return;
	}

	camel_object_unref (summary);
	g_unlink (summary_file);
	g_free (summary_file);


	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	g_unlink (state_file);
	g_free (state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

	camel_store_summary_remove_path ( (CamelStoreSummary *)gw_store->summary, folder_name);
	camel_store_summary_save ( (CamelStoreSummary *)gw_store->summary);

	fi = groupwise_build_folder_info(gw_store, NULL, folder_name);
	camel_object_trigger_event (CAMEL_OBJECT (gw_store), "folder_deleted", fi);
	camel_folder_info_free (fi);
}

static CamelFolder *
groupwise_get_folder_from_disk (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate *priv = gw_store->priv;
	CamelFolder *folder;
	char *folder_dir, *storage_path;

	storage_path = g_strdup_printf("%s/folders", priv->storage_path);
	folder_dir = e_path_to_physical (storage_path, folder_name);
	g_free(storage_path);
	if (!folder_dir || g_access (folder_dir, F_OK) != 0) {
		g_free (folder_dir);
		camel_exception_setv (ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				_("No such folder %s"), folder_name);
		return NULL;
	}

	folder = camel_gw_folder_new (store, folder_name, folder_dir, ex);
	g_free (folder_dir);

	return folder;
}

static CamelFolder * 
groupwise_get_folder (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate *priv = gw_store->priv;
	CamelFolder *folder;
	CamelGroupwiseSummary *summary;
	char *container_id, *folder_dir, *storage_path;
	EGwConnectionStatus status;
	GList *list = NULL;
	gboolean done = FALSE;
	const char *position = E_GW_CURSOR_POSITION_END; 
	int count = 0, cursor, summary_count = 0;
	CamelStoreInfo *si = NULL;
	guint total;
	
	folder = groupwise_get_folder_from_disk (store, folder_name, flags, ex);
	if (folder) {
		camel_object_ref (folder);
		return folder;
	}

	camel_exception_clear (ex);

	CAMEL_SERVICE_LOCK (gw_store, connect_lock);

	if (!camel_groupwise_store_connected ((CamelGroupwiseStore *)store, ex)) {
		CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
		return NULL;
	}
	
	if (gw_store->current_folder) {
		camel_object_unref (gw_store->current_folder);
		gw_store->current_folder = NULL;
	}

	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!groupwise_connect (CAMEL_SERVICE(store), ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
			CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
			return NULL;
		}
	}

	container_id = 	g_strdup (g_hash_table_lookup (priv->name_hash, folder_name));


	storage_path = g_strdup_printf("%s/folders", priv->storage_path);
	folder_dir = e_path_to_physical (storage_path, folder_name);
	g_free(storage_path);
	folder = camel_gw_folder_new (store, folder_name, folder_dir, ex);
	if (!folder) {
		CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
		g_free (folder_dir);
		g_free (container_id);
		return NULL;
	}
	g_free (folder_dir);

	si = camel_store_summary_path ((CamelStoreSummary *)gw_store->summary, folder_name);
	if (si) {
		camel_object_get (folder, NULL, CAMEL_FOLDER_TOTAL, &total, NULL);
		camel_store_summary_info_free ((CamelStoreSummary *)(gw_store)->summary, si);
		g_print ("TOTAL:%d\n\n", total);
	}

	summary = (CamelGroupwiseSummary *) folder->summary;

	summary_count = camel_folder_summary_count (folder->summary);
	if(!summary_count || !summary->time_string) {
		g_print ("\n\n** %s **: No summary as yet : using get cursor request\n\n", folder->name);

		status = e_gw_connection_create_cursor (priv->cnc, container_id, 
				"peek id recipient attachments distribution subject status options priority startDate created delivered size",
				NULL,
				&cursor);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
			g_free (container_id);
			return NULL;
		}

		camel_operation_start (NULL, _("Fetching summary information for new messages in %s"), folder->name);
		camel_folder_summary_clear (folder->summary);

		while (!done) {
			status = e_gw_connection_read_cursor (priv->cnc, container_id, 
							      cursor, FALSE, 
							      CURSOR_ITEM_LIMIT, position, &list);
			if (status != E_GW_CONNECTION_STATUS_OK) {
				CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
				e_gw_connection_destroy_cursor (priv->cnc, container_id, cursor);
				camel_folder_summary_clear (folder->summary);
				camel_folder_summary_save (folder->summary);
				camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
				camel_operation_end (NULL);
				g_free (container_id);
				return NULL;
			}
			
			count += g_list_length (list);
		
			if (total > 0)
				camel_operation_progress (NULL, (100*count)/total);
			gw_update_summary (folder, list,  ex);
			
			if (!list)
				done = TRUE;
			g_list_foreach (list, (GFunc)g_object_unref, NULL);
			g_list_free (list);
			list = NULL;
			position = E_GW_CURSOR_POSITION_CURRENT;
      		}

		e_gw_connection_destroy_cursor (priv->cnc, container_id, cursor);

		camel_operation_end (NULL);
	} 

	if (done) {
		if (summary->time_string)
			g_free (summary->time_string);
		summary->time_string = g_strdup (e_gw_connection_get_server_time (priv->cnc));
	}

	camel_folder_summary_save (folder->summary);

	gw_store->current_folder = folder;
	camel_object_ref (folder);

	g_free (container_id);
	CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);

	return folder;
}

CamelFolderInfo *
convert_to_folder_info (CamelGroupwiseStore *store, EGwContainer *container, const char *url, CamelException *ex)
{
	const char *name = NULL, *id = NULL, *parent = NULL;
	char *par_name = NULL;
	CamelFolderInfo *fi;
	CamelGroupwiseStoreInfo *si = NULL;
	CamelGroupwiseStorePrivate *priv = store->priv;
	EGwContainerType type;

	name = e_gw_container_get_name (container);
	id = e_gw_container_get_id (container);
	type = e_gw_container_get_container_type (container);
		
	fi = g_new0 (CamelFolderInfo, 1);

	if (type == E_GW_CONTAINER_TYPE_INBOX)
		fi->flags |= CAMEL_FOLDER_TYPE_INBOX;
	if (type == E_GW_CONTAINER_TYPE_TRASH)
		fi->flags |= CAMEL_FOLDER_TYPE_TRASH;
	if (type == E_GW_CONTAINER_TYPE_SENT)
		fi->flags |= CAMEL_FOLDER_TYPE_SENT;

	if ( (type == E_GW_CONTAINER_TYPE_INBOX) ||
		(type == E_GW_CONTAINER_TYPE_SENT) ||
		(type == E_GW_CONTAINER_TYPE_DOCUMENTS) ||
		(type == E_GW_CONTAINER_TYPE_QUERY) ||
		(type == E_GW_CONTAINER_TYPE_CHECKLIST) ||
		(type == E_GW_CONTAINER_TYPE_DRAFT) ||
		(type == E_GW_CONTAINER_TYPE_CABINET) ||
		(type == E_GW_CONTAINER_TYPE_JUNK) ||
		(type == E_GW_CONTAINER_TYPE_TRASH) ) 
		fi->flags |= CAMEL_FOLDER_SYSTEM;
	/*
	   parent_hash contains the "parent id <-> container id" combination. So we form
	   the path for the full name in camelfolder info by looking up the hash table until
	   NULL is found
	 */

	parent = e_gw_container_get_parent_id (container);
	par_name = g_hash_table_lookup (priv->id_hash, parent);

	if (par_name != NULL) {
		gchar *temp_parent = NULL, *temp = NULL;
		gchar *str = g_strconcat (par_name, "/", name, NULL);

		fi->name = g_strdup (name);

		temp_parent = g_hash_table_lookup (priv->parent_hash, parent);
		while (temp_parent) {
			temp = g_hash_table_lookup (priv->id_hash, temp_parent );
			if (temp == NULL) {
				break;
			}	
			str = g_strconcat ( temp, "/", str, NULL);

			temp_parent = g_hash_table_lookup (priv->parent_hash, temp_parent);

		} 
		fi->full_name = g_strdup (str);
		fi->uri = g_strconcat (url, str, NULL);
		g_free (str);
	}
	else {
		fi->name =  g_strdup (name);
		fi->full_name = g_strdup (name);
		fi->uri = g_strconcat (url, "", name, NULL);
	}

	si = camel_groupwise_store_summary_add_from_full (store->summary, fi->full_name, '/');
	if (si == NULL) {
		camel_folder_info_free (fi);
		return NULL;
	}

	/*name_hash returns the container id given the name */
	g_hash_table_insert (priv->name_hash, g_strdup(fi->full_name), g_strdup(id));

	if (e_gw_container_get_is_shared_to_me (container))
		fi->flags |= CAMEL_FOLDER_SHARED_TO_ME;

	if (e_gw_container_get_is_shared_by_me (container))
		fi->flags |= CAMEL_FOLDER_SHARED_BY_ME;

	if (type == E_GW_CONTAINER_TYPE_INBOX) {
		fi->total = -1;
		fi->unread = -1;
	} else	if (type == E_GW_CONTAINER_TYPE_TRASH) {
		fi->total = e_gw_container_get_total_count (container);
		fi->unread = 0;
	}else {
		fi->total = e_gw_container_get_total_count (container);
		fi->unread = e_gw_container_get_unread_count (container);
	}
	si->info.total = fi->total;
	si->info.unread = fi->unread;
	si->info.flags = fi->flags;
	/*refresh info*/
	if (store->current_folder 
	    && !strcmp (store->current_folder->full_name, fi->full_name)
	    && type != E_GW_CONTAINER_TYPE_INBOX) {
		CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS (store->current_folder))->refresh_info(store->current_folder, ex);
	}
	return fi;
}

static void
get_folders_free (void *k, void *v, void *d)
{
	CamelFolderInfo *fi = v;
	camel_folder_info_free (fi);
}

static void
groupwise_folders_sync (CamelGroupwiseStore *store, CamelException *ex)
{
	CamelGroupwiseStorePrivate  *priv = store->priv;
	int status;
	GList *folder_list = NULL, *temp_list = NULL, *list = NULL;
	char *url, *temp_url;
	CamelFolderInfo *info = NULL, *hfi = NULL;
	GHashTable *present;
	CamelStoreInfo *si = NULL;
	int count, i;

	status = e_gw_connection_get_container_list (priv->cnc, "folders", &folder_list);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_warning ("Could not get folder list..\n");
		return;
	}

	temp_list = folder_list;
	list = folder_list;

	url = camel_url_to_string (CAMEL_SERVICE(store)->url,
				   (CAMEL_URL_HIDE_PASSWORD|
				    CAMEL_URL_HIDE_PARAMS|
				    CAMEL_URL_HIDE_AUTH) );

	if ( url[strlen(url) - 1] != '/') {
		temp_url = g_strconcat (url, "/", NULL);
		g_free ((char *)url);
		url = temp_url;
	}
	
	/*populate the hash table for finding the mapping from container id <-> folder name*/
	for (;temp_list != NULL ; temp_list = g_list_next (temp_list) ) {
		const char *name, *id, *parent;
		name = e_gw_container_get_name (E_GW_CONTAINER (temp_list->data));
		id = e_gw_container_get_id(E_GW_CONTAINER(temp_list->data));
		parent = e_gw_container_get_parent_id (E_GW_CONTAINER(temp_list->data));

		if (e_gw_container_is_root (E_GW_CONTAINER(temp_list->data))) {
			store->root_container = g_strdup (id);
			continue;
		}

		/*id_hash returns the name for a given container id*/
		g_hash_table_insert (priv->id_hash, g_strdup(id), g_strdup(name)); 
		/*parent_hash returns the parent container id, given an id*/
		g_hash_table_insert (priv->parent_hash, g_strdup(id), g_strdup(parent));
	}

	present = g_hash_table_new (g_str_hash, g_str_equal);

	for (;folder_list != NULL; folder_list = g_list_next (folder_list)) {
		EGwContainerType type;
		EGwContainer *container = E_GW_CONTAINER (folder_list->data);
		
		type = e_gw_container_get_container_type (container);

		if (e_gw_container_is_root(container))
			continue;
		if ( (type == E_GW_CONTAINER_TYPE_CALENDAR) || (type == E_GW_CONTAINER_TYPE_CONTACTS) )
			continue;

		info = convert_to_folder_info (store, E_GW_CONTAINER (folder_list->data), (const char *)url, ex);
		if (info) {
			hfi = g_hash_table_lookup (present, info->full_name);
			if (hfi == NULL)
				g_hash_table_insert (present, info->full_name, info);
			else {
				camel_folder_info_free (info);
				info = NULL;
			}
		}
	}
	
	g_free ((char *)url);
	e_gw_connection_free_container_list (list);
	count = camel_store_summary_count ((CamelStoreSummary *)store->summary);

	count = camel_store_summary_count ((CamelStoreSummary *)store->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index ((CamelStoreSummary *)store->summary, i);
		if (si == NULL)
			continue;

		info = g_hash_table_lookup (present, camel_store_info_path (store->summary, si));
		if (info != NULL) {
			camel_store_summary_touch ((CamelStoreSummary *)store->summary);
		} else {
			camel_store_summary_remove ((CamelStoreSummary *)store->summary, si);
			count--;
			i--;
		}
		camel_store_summary_info_free ((CamelStoreSummary *)store->summary, si);
	}

	g_hash_table_foreach (present, get_folders_free, NULL);
	g_hash_table_destroy (present);
}

static CamelFolderInfo *
groupwise_get_folder_info_offline (CamelStore *store, const char *top,
			 guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelFolderInfo *fi;
	GPtrArray *folders;
	char *path, *name;
	int i;

	folders = g_ptr_array_new ();

	if (top == NULL)
		top = "";

	/* get starting point */
	if (top[0] == 0) {
			name = g_strdup("");
	} else {
		name = camel_groupwise_store_summary_full_from_path(groupwise_store->summary, top);
		if (name == NULL)
			name = camel_groupwise_store_summary_path_to_full(groupwise_store->summary, top, '/');
	}

	path = gw_concat (name, "*");

	for (i=0;i<camel_store_summary_count((CamelStoreSummary *)groupwise_store->summary);i++) {
		CamelStoreInfo *si = camel_store_summary_index((CamelStoreSummary *)groupwise_store->summary, i);

		if (si == NULL) 
			continue;
		
		if ( !strcmp(name, camel_groupwise_store_info_full_name (groupwise_store->summary, si))
		     || match_path (path, camel_groupwise_store_info_full_name (groupwise_store->summary, si))) {
			fi = groupwise_build_folder_info(groupwise_store, NULL, camel_store_info_path((CamelStoreSummary *)groupwise_store->summary, si));
			fi->unread = si->unread;
			fi->total = si->total;
			fi->flags = si->flags;
			g_ptr_array_add (folders, fi);
		}
		camel_store_summary_info_free((CamelStoreSummary *)groupwise_store->summary, si);
	}

	g_free(name);
	g_free (path);
	fi = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	return fi;
}

/*** Thread stuff for refreshing folder tree begins ***/
struct _store_refresh_msg {
	CamelSessionThreadMsg msg;

	CamelStore *store;
	CamelException ex;
};

static void
store_refresh_refresh (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _store_refresh_msg *m = (struct _store_refresh_msg *)msg;
	CamelGroupwiseStore *groupwise_store = (CamelGroupwiseStore *)m->store;
	
	CAMEL_SERVICE_LOCK (m->store, connect_lock);
	if (!camel_groupwise_store_connected ((CamelGroupwiseStore *)m->store, &m->ex))
		goto done;
	/*Get the folder list and save it here*/
	groupwise_folders_sync (groupwise_store, &m->ex);
	if (camel_exception_is_set (&m->ex))
		goto done;
	camel_store_summary_save ((CamelStoreSummary *)groupwise_store->summary);
done:
	CAMEL_SERVICE_UNLOCK (m->store, connect_lock);
}

static void
store_refresh_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _store_refresh_msg *m = (struct _store_refresh_msg *)msg;

	camel_object_unref (m->store);
	camel_exception_clear (&m->ex);
}

static CamelSessionThreadOps store_refresh_ops = {
	store_refresh_refresh,
	store_refresh_free,
};

/*** Thread stuff ends ***/

static CamelFolderInfo *
groupwise_get_folder_info (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	CamelFolderInfo *info = NULL;
	char *top_folder = NULL;
	
	if (top) {
		top_folder = g_hash_table_lookup (priv->name_hash, top);
		/* 'top' is a valid path, but doesnt have a container id
		 *  return NULL */
/*		if (!top_folder) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					_("You must be working online to complete this operation"));
			return NULL;
		}*/
	}

	if (top && groupwise_is_system_folder (top)) 
		return groupwise_build_folder_info (groupwise_store, NULL, top );

	/*
	 * Thanks to Michael, for his cached folders implementation in IMAP
	 * is used as is here.
	 */
	if ((groupwise_store->list_loaded == TRUE) && camel_store_summary_count((CamelStoreSummary *)groupwise_store->summary) > 0) {
		/*Load from cache*/
		/*
		time_t now;
		int ref;

		now = time (0);
		ref = now > groupwise_store->refresh_stamp+60*60*1;
		if (ref) {
			CAMEL_SERVICE_LOCK (store, connect_lock);
			ref = now > groupwise_store->refresh_stamp+60*60*1;
			if (ref) {
				struct _store_refresh_msg *m;
				groupwise_store->refresh_stamp = now;
				m = camel_session_thread_msg_new (((CamelService *)store)->session, &store_refresh_ops, sizeof(*m));
				m->store = store;
				camel_object_ref (store);
				camel_exception_init (&m->ex);
				camel_session_thread_queue (((CamelService *)store)->session, &m->msg, 0);
			}
			CAMEL_SERVICE_UNLOCK (store, connect_lock);
		}*/
		groupwise_folders_sync (groupwise_store, ex);
	}

	CAMEL_SERVICE_LOCK (store, connect_lock);
	if ((groupwise_store->list_loaded == FALSE) && camel_groupwise_store_connected ((CamelGroupwiseStore *)store, ex)) {
		groupwise_store->list_loaded = TRUE;
		groupwise_folders_sync (groupwise_store, ex);
		if (camel_exception_is_set (ex)) {
			CAMEL_SERVICE_UNLOCK (store, connect_lock);
			return NULL;
		}
		camel_store_summary_touch ((CamelStoreSummary *)groupwise_store->summary);
		camel_store_summary_save ((CamelStoreSummary *)groupwise_store->summary);
	}
	CAMEL_SERVICE_UNLOCK (store, connect_lock);

	//camel_exception_clear (ex);
	info = groupwise_get_folder_info_offline (store, top, flags, ex);
	return info;
}

/* To create a junk mail folder in case  we want it and it isn't there*/
CamelFolderInfo *
create_junk_folder (CamelStore *store)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	CamelFolderInfo *root = NULL;
	char *parent_name, *folder_name, *child_container_id, *parent_id;
	int status;

	parent_name = "";
	folder_name = "Junk Mail";
	parent_id = "";
	/* TODO: check for offlining*/
		
	CAMEL_SERVICE_LOCK (store, connect_lock);
	status = e_gw_connection_modify_junk_settings (priv->cnc, JUNK_ENABLE, 0, 0,  JUNK_PERSISTENCE);
	if (status == E_GW_CONNECTION_STATUS_OK) {
		root = groupwise_build_folder_info(groupwise_store, parent_name, folder_name);
		camel_store_summary_save((CamelStoreSummary *)groupwise_store->summary);
		
		child_container_id = e_gw_connection_get_container_id (priv->cnc, "Junk Mail");
		if (!child_container_id)
			g_warning("failed to retrieve id for junk folder");
		
		g_hash_table_insert (priv->id_hash, g_strdup(child_container_id), g_strdup(folder_name)); 
		g_hash_table_insert (priv->name_hash, g_strdup(folder_name), g_strdup(child_container_id));
		g_hash_table_insert (priv->parent_hash, g_strdup(child_container_id), g_strdup(parent_id));
		camel_object_trigger_event (CAMEL_OBJECT (store), "folder_created", root);
	}
	CAMEL_SERVICE_UNLOCK (store, connect_lock);

	return root;
}

static CamelFolderInfo*
groupwise_create_folder(CamelStore *store,
		const char *parent_name,
		const char *folder_name,
		CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	CamelFolderInfo *root = NULL;
	char *parent_id , *child_container_id;
	int status;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot create GroupWise folders in offline mode."));
		return NULL;
	}
	
	if(parent_name == NULL) {
		parent_name = "";
		if (groupwise_is_system_folder (folder_name)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, NULL);
			return NULL;
		}
	}

	if (parent_name && (strlen(parent_name) > 0) ) {
		if (strcmp (parent_name, "Cabinet") && groupwise_is_system_folder (parent_name)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_FOLDER_INVALID_STATE, _("The parent folder is not allowed to contain subfolders"));
			return NULL;
		}
		parent_id = g_hash_table_lookup (priv->name_hash, parent_name);
	} else
		parent_id = "";

	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!groupwise_connect (CAMEL_SERVICE(store), ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
			return NULL;
		}
	}
	CAMEL_SERVICE_LOCK (store, connect_lock);
	status = e_gw_connection_create_folder(priv->cnc,parent_id,folder_name, &child_container_id);
	if (status == E_GW_CONNECTION_STATUS_OK) {
		root = groupwise_build_folder_info(groupwise_store, parent_name,folder_name);
		camel_store_summary_save((CamelStoreSummary *)groupwise_store->summary);

		g_hash_table_insert (priv->id_hash, g_strdup(child_container_id), g_strdup(folder_name)); 
		g_hash_table_insert (priv->name_hash, g_strdup(root->full_name), g_strdup(child_container_id));
		g_hash_table_insert (priv->parent_hash, g_strdup(child_container_id), g_strdup(parent_id));

		camel_object_trigger_event (CAMEL_OBJECT (store), "folder_created", root);
	}
	CAMEL_SERVICE_UNLOCK (store, connect_lock);
	return root;
}

static void 
groupwise_delete_folder(CamelStore *store,
				   const char *folder_name,
				   CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	EGwConnectionStatus status;
	const char * container; 
	
	CAMEL_SERVICE_LOCK (store, connect_lock);
	
	if (!camel_groupwise_store_connected ((CamelGroupwiseStore *)store, ex)) {
		CAMEL_SERVICE_UNLOCK (store, connect_lock);
		return;
	}

	container = g_hash_table_lookup (priv->name_hash, folder_name);

	status = e_gw_connection_remove_item (priv->cnc, container, container);

	if (status == E_GW_CONNECTION_STATUS_OK) {
		if (groupwise_store->current_folder)
			camel_object_unref (groupwise_store->current_folder);

		groupwise_forget_folder(groupwise_store,folder_name,ex);

		g_hash_table_remove (priv->id_hash, container);
		g_hash_table_remove (priv->name_hash, folder_name);
		
		g_hash_table_remove (priv->parent_hash, container);
	}
	CAMEL_SERVICE_UNLOCK (store, connect_lock);
}

static void 
groupwise_rename_folder(CamelStore *store,
		        const char *old_name,
			const char *new_name,
			CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	char *oldpath, *newpath, *storepath;
	const char *container_id;
	char *temp_new = NULL;
	
	if (groupwise_is_system_folder (old_name)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot rename Groupwise folder `%s' to `%s'"),
				      old_name, new_name);
		return;
	}

	CAMEL_SERVICE_LOCK (groupwise_store, connect_lock);
	
	if (!camel_groupwise_store_connected ((CamelGroupwiseStore *)store, ex)) {
		CAMEL_SERVICE_UNLOCK (groupwise_store, connect_lock);
		return;
	}
	
	container_id = camel_groupwise_store_container_id_lookup (groupwise_store, old_name);
	temp_new = strrchr (new_name, '/');
	if (temp_new) 
		temp_new++;
	else
		temp_new = (char *)new_name;
	
	if (!container_id || e_gw_connection_rename_folder (priv->cnc, container_id , temp_new) != E_GW_CONNECTION_STATUS_OK)
	{
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot rename Groupwise folder `%s' to `%s'"),
				      old_name, new_name);
		CAMEL_SERVICE_UNLOCK (groupwise_store, connect_lock);
		return;
	}

	g_hash_table_replace (priv->id_hash, g_strdup(container_id), g_strdup(temp_new));

	g_hash_table_insert (priv->name_hash, g_strdup(new_name), g_strdup(container_id));
	g_hash_table_remove (priv->name_hash, old_name);
	/*FIXME:Update all the id in the parent_hash*/

	storepath = g_strdup_printf ("%s/folders", priv->storage_path);
	oldpath = e_path_to_physical (storepath, old_name);
	newpath = e_path_to_physical (storepath, new_name);
	g_free (storepath);

	/*XXX: make sure the summary is also renamed*/
	if (g_rename (oldpath, newpath) == -1) {
		g_warning ("Could not rename message cache '%s' to '%s': %s: cache reset",
				oldpath, newpath, strerror (errno));
	}

	g_free (oldpath);
	g_free (newpath);
	CAMEL_SERVICE_UNLOCK (groupwise_store, connect_lock);
}

char * 
groupwise_get_name(CamelService *service, gboolean brief) 
{
	if(brief) 
		return g_strdup_printf(_("GroupWise server %s"), service->url->host);
	else
		return g_strdup_printf(_("GroupWise service for %s on %s"), 
				       service->url->user, service->url->host);
}

const char *
camel_groupwise_store_container_id_lookup (CamelGroupwiseStore *gw_store, const char *folder_name)
{
	CamelGroupwiseStorePrivate *priv = gw_store->priv;

	return g_hash_table_lookup (priv->name_hash, folder_name);
}

const char *
camel_groupwise_store_folder_lookup (CamelGroupwiseStore *gw_store, const char *container_id)
{
	CamelGroupwiseStorePrivate *priv = gw_store->priv;

	return g_hash_table_lookup (priv->id_hash, container_id);
}


EGwConnection *
cnc_lookup (CamelGroupwiseStorePrivate *priv)
{
	return priv->cnc;
}

char *
storage_path_lookup (CamelGroupwiseStorePrivate *priv)
{
	return priv->storage_path;
}

const char *
groupwise_base_url_lookup (CamelGroupwiseStorePrivate *priv)
{
	return priv->base_url;
}

static CamelFolder *
groupwise_get_trash (CamelStore *store, CamelException *ex)
{
	CamelFolder *folder = camel_store_get_folder(store, "Trash", 0, ex);
	if (folder) {
		 char *state = g_build_filename(((CamelGroupwiseStore *)store)->priv->storage_path, "folders", "Trash", "cmeta", NULL);

		camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state, NULL);
		g_free(state);
		camel_object_state_read(folder);

		return folder;
	} else 
		return NULL;
}

/*
 * Function to check if we are both connected and are _actually_
 * online. Based on an equivalient function in IMAP
 */ 
gboolean
camel_groupwise_store_connected (CamelGroupwiseStore *store, CamelException *ex)
{
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL
	    && camel_service_connect ((CamelService *)store, ex))
		return TRUE;

	if (!camel_exception_is_set (ex))
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				_("You must be working online to complete this operation"));

	return FALSE;
}

static int 
match_path(const char *path, const char *name)
{
	char p, n;

	p = *path++;
	n = *name++;
	while (n && p) {
		if (n == p) {
			p = *path++;
			n = *name++;
		} else if (p == '%') {
			if (n != '/') {
				n = *name++;
			} else {
				p = *path++;
			}
		} else if (p == '*') {
			return TRUE;
		} else
			return FALSE;
	}

	return n == 0 && (p == '%' || p == 0);
}

/* GObject Init and finalise methods */
static void
camel_groupwise_store_class_init (CamelGroupwiseStoreClass *camel_groupwise_store_class)
{
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_groupwise_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_groupwise_store_class);
	
	parent_class = CAMEL_OFFLINE_STORE_CLASS (camel_type_get_global_classfuncs (camel_offline_store_get_type ()));
	
	camel_service_class->construct = groupwise_store_construct;
	camel_service_class->query_auth_types = groupwise_store_query_auth_types;
	camel_service_class->get_name = groupwise_get_name;
	camel_service_class->connect = groupwise_connect;
	camel_service_class->disconnect = groupwise_disconnect;
	
	camel_store_class->hash_folder_name = groupwise_hash_folder_name;
	camel_store_class->compare_folder_name = groupwise_compare_folder_name;
	
	camel_store_class->get_folder = groupwise_get_folder;
	camel_store_class->create_folder = groupwise_create_folder;
	camel_store_class->delete_folder = groupwise_delete_folder;
	camel_store_class->rename_folder = groupwise_rename_folder;
	camel_store_class->get_folder_info = groupwise_get_folder_info;
	camel_store_class->get_trash = groupwise_get_trash;
}


/*This frees the private structure*/
static void
camel_groupwise_store_finalize (CamelObject *object)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (object);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	
	g_print ("camel_groupwise_store_finalize\n");
	if (groupwise_store->summary) {
		camel_store_summary_save ((CamelStoreSummary *)groupwise_store->summary);
		camel_object_unref (groupwise_store->summary);
	}
	
	if (priv) {
		if (priv->user) {
			g_free (priv->user);
			priv->user = NULL;
		}
		if (priv->server_name) {
			g_free (priv->server_name);
			priv->server_name = NULL;
		}
		if (priv->port) {
			g_free (priv->port);
			priv->port = NULL;
		}
		if (priv->use_ssl) {
			g_free (priv->use_ssl);
			priv->use_ssl = NULL;
		}
		if (priv->base_url) {
			g_free (priv->base_url);
			priv->base_url = NULL;
		}
		
		if (E_IS_GW_CONNECTION (priv->cnc)) {
			g_object_unref (priv->cnc);
			priv->cnc = NULL;
		}

		if (priv->storage_path)
			g_free(priv->storage_path);

		if(groupwise_store->root_container)
			g_free (groupwise_store->root_container);
		
		if (priv->id_hash)
			g_hash_table_destroy (priv->id_hash);

		if (priv->name_hash)
			g_hash_table_destroy (priv->name_hash);

		if (priv->parent_hash)
			g_hash_table_destroy (priv->parent_hash);

		g_free (groupwise_store->priv);
		groupwise_store->priv = NULL;
	}

}

static void
camel_groupwise_store_init (gpointer object, gpointer klass)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (object);
	CamelGroupwiseStorePrivate *priv = g_new0 (CamelGroupwiseStorePrivate, 1);
	
	d("in groupwise store init\n");
	priv->server_name = NULL;
	priv->port = NULL;
	priv->use_ssl = NULL;
	priv->user = NULL;
	priv->cnc = NULL;
	groupwise_store->priv = priv;
	
}

CamelType
camel_groupwise_store_get_type (void)
{
	static CamelType camel_groupwise_store_type = CAMEL_INVALID_TYPE;
	
	if (camel_groupwise_store_type == CAMEL_INVALID_TYPE)	{
		camel_groupwise_store_type =
			camel_type_register (camel_offline_store_get_type (),
					     "CamelGroupwiseStore",
					     sizeof (CamelGroupwiseStore),
					     sizeof (CamelGroupwiseStoreClass),
					     (CamelObjectClassInitFunc) camel_groupwise_store_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_groupwise_store_init,
					     (CamelObjectFinalizeFunc) camel_groupwise_store_finalize);
	}
	
	return camel_groupwise_store_type;
}

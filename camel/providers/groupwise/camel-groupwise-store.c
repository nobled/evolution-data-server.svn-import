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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "camel-groupwise-store.h"
#include "camel-groupwise-summary.h"
#include "camel-groupwise-store-summary.h"
#include "camel-groupwise-folder.h"
#include "camel-groupwise-utils.h"

#include "camel-session.h"
#include "camel-debug.h"
#include "camel-i18n.h"
#include "camel-types.h"
#include "camel-folder.h" 
#include "camel-private.h"

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
	priv->id_hash = g_hash_table_new (g_str_hash, g_str_equal);
	priv->name_hash = g_hash_table_new (g_str_hash, g_str_equal);
	priv->parent_hash = g_hash_table_new (g_str_hash, g_str_equal);

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
groupwise_connect (CamelService *service, CamelException *ex)
{
	CamelGroupwiseStore *store = CAMEL_GROUPWISE_STORE (service);
	CamelGroupwiseStorePrivate *priv = store->priv;
	CamelGroupwiseStoreNamespace *ns;
	CamelSession *session = service->session;

	d("in groupwise store connect\n");
	
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return TRUE;
	
	CAMEL_SERVICE_LOCK (service, connect_lock);
	
	if (priv->cnc) {
		CAMEL_SERVICE_UNLOCK (service, connect_lock);
		return TRUE;
	}

	if (!groupwise_auth_loop (service, ex)) {
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

	camel_store_summary_save ((CamelStoreSummary *) store->summary);

	ns = camel_groupwise_store_summary_namespace_new (store->summary, priv->storage_path, '/');
	camel_groupwise_store_summary_namespace_set (store->summary, ns);
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
	if (groupwise_store->priv->cnc) {
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

	if (access(folder_dir, F_OK) != 0) {
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
	unlink (summary_file);
	g_free (summary_file);


	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	unlink (state_file);
	g_free (state_file);

	rmdir (folder_dir);
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
	if (!folder_dir || access (folder_dir, F_OK) != 0) {
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
	CamelSession *session = camel_service_get_session (CAMEL_SERVICE (store));
	CamelFolder *folder;
	CamelGroupwiseSummary *summary;
	char *container_id, *folder_dir, *storage_path;
	EGwConnectionStatus status;
	GList *list = NULL;
	gboolean done = FALSE;
	const char *position = E_GW_CURSOR_POSITION_END; 
	int count = 0, cursor, summary_count = 0;
	
	g_print ("get_folder: %s\n", folder_name);
	folder = groupwise_get_folder_from_disk (store, folder_name, flags, ex);
	if (folder) {
		camel_object_ref (folder);
		return folder;
	}

	camel_exception_clear (ex);

	CAMEL_SERVICE_LOCK (gw_store, connect_lock);
	
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		folder = groupwise_get_folder_from_disk (store, folder_name, flags, ex);
		if (folder) {
			CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
			return folder;
		} else {
			CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
			return NULL;
		}
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

	summary = (CamelGroupwiseSummary *) folder->summary;

	summary_count = camel_folder_summary_count (folder->summary);
	if(!summary_count || !summary->time_string) {
		g_print ("\n\n** %s **: No summary as yet : using get cursor request\n\n", folder->name);

		status = e_gw_connection_create_cursor (priv->cnc, container_id, 
				"peek id recipient attachments distribution created subject status options priority",
				NULL,
				&cursor);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
			return NULL;
		}

		camel_operation_start (NULL, _("Fetching summary information for new messages"));
		camel_folder_summary_clear (folder->summary);

		while (!done) {
			int temp = 0;
			status = e_gw_connection_read_cursor (priv->cnc, container_id, 
							      cursor, FALSE, 
							      CURSOR_ITEM_LIMIT, position, &list);
			if (status != E_GW_CONNECTION_STATUS_OK) {
				CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);
				camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
				camel_operation_end (NULL);
				g_free (container_id);
				return NULL;
			}
			
			count += g_list_length (list);
		
			if (count >= 100)
				temp = count/100;
			camel_operation_progress (NULL, temp);
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

static gboolean
get_one_folder_offline (const char *physical_path, const char *path, gpointer data)
{
	GPtrArray *folders = data;
	CamelGroupwiseStore *groupwise_store = folders->pdata[0];
	CamelFolderInfo *fi;
		
	if (*path != '/')
		return TRUE;
	
	fi = groupwise_build_folder_info(groupwise_store, NULL, path+1);
	if (!strcmp (fi->full_name, "Mailbox"))
		fi->flags |= CAMEL_FOLDER_TYPE_INBOX;
	if (!strcmp (fi->full_name, "Junk Mail"))
		fi->flags |= CAMEL_FOLDER_TYPE_JUNK;
	g_ptr_array_add (folders, fi);
	return TRUE;
}

static CamelFolderInfo *
groupwise_get_folder_info_offline (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelFolderInfo *fi;
	GPtrArray *folders;
	char *storage_path;

	/* FIXME: obey other flags */

	folders = g_ptr_array_new ();

	/* A kludge to avoid having to pass a struct to the callback */
	g_ptr_array_add (folders, groupwise_store);
	storage_path = g_strdup_printf("%s/folders", groupwise_store->priv->storage_path);
	if (!e_path_find_folders (storage_path, get_one_folder_offline, folders)) {
		if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get folder info in offline mode."));
		fi = NULL;
	} else {
		g_ptr_array_remove_index_fast (folders, 0);
		fi = camel_folder_info_build (folders, top, '/', TRUE);
	}
	g_free(storage_path);

	g_ptr_array_free (folders, TRUE);
	return fi;
       
}

static CamelFolderInfo *
groupwise_get_folder_info (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	CamelSession *session;
	int status;
	GPtrArray *folders;
	GList *folder_list = NULL, *temp_list = NULL;
	const char *url, *top_folder, *temp_url;
	char *folder_real = NULL;
	CamelFolderInfo *info = NULL;
	struct _store_folder_refresh *msg;
	
	if (top && groupwise_is_system_folder (top)) 
		return groupwise_build_folder_info (groupwise_store, NULL, top );

	CAMEL_SERVICE_LOCK (store, connect_lock);

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		CAMEL_SERVICE_UNLOCK (store, connect_lock);
		return groupwise_get_folder_info_offline (store, top, flags, ex);
	}

	session = camel_service_get_session (CAMEL_SERVICE (store));
	
	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!groupwise_connect (CAMEL_SERVICE(store), ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
			CAMEL_SERVICE_UNLOCK (store, connect_lock);
			return NULL;
		}
	}
	if (top == NULL) {
		top_folder = "folders";
		top = "";
	} else {
		top_folder = g_hash_table_lookup (priv->name_hash, top);
		/* 'top' is a valid path, but doesnt have a container id
		 *  return NULL */
		if (!top_folder)
			return NULL;
	}

	status = e_gw_connection_get_container_list (priv->cnc, top_folder, &folder_list);
	if (status != E_GW_CONNECTION_STATUS_OK ) {
		CAMEL_SERVICE_UNLOCK (store, connect_lock);
		g_warning ("Network Unavailable....\n");
		/*FIX ME set the camel exception id*/
		return NULL;
	}
	
	temp_list = folder_list;
	
	folders = g_ptr_array_new();
	
	url = camel_url_to_string (CAMEL_SERVICE(groupwise_store)->url,
				   (CAMEL_URL_HIDE_PASSWORD|
				    CAMEL_URL_HIDE_PARAMS|
				    CAMEL_URL_HIDE_AUTH) );

	if ( url[strlen(url) - 1] != '/') {
		temp_url = g_strconcat (url, "/", NULL);
		g_free ((char *)url);
		url = temp_url;
	}
	/*Populate the hash table for finding the mapping from container id <-> folder name*/
	for (;temp_list != NULL ; temp_list = g_list_next (temp_list) ) {
		const char *name, *id, *parent;
		name = e_gw_container_get_name (E_GW_CONTAINER (temp_list->data));
		id = e_gw_container_get_id(E_GW_CONTAINER(temp_list->data));
		parent = e_gw_container_get_parent_id (E_GW_CONTAINER(temp_list->data));

		if (e_gw_container_is_root (E_GW_CONTAINER(temp_list->data))) {
			groupwise_store->root_container = g_strdup (id);
			continue;
		}

		/*id_hash returns the name for a given container id*/
		g_hash_table_insert (priv->id_hash, g_strdup(id), g_strdup(name)); 
		/*parent_hash returns the parent container id, given an id*/
		g_hash_table_insert (priv->parent_hash, g_strdup(id), g_strdup(parent));
	}
	
	for (; folder_list != NULL; folder_list = g_list_next(folder_list)) {
		CamelFolderInfo *fi;
		const char *parent;
		gchar *par_name = NULL;
		EGwContainer *container = E_GW_CONTAINER (folder_list->data);
		EGwContainerType type = e_gw_container_get_container_type (container);
		const char *name = e_gw_container_get_name (container);
		const char *id = e_gw_container_get_id (container);

		if (e_gw_container_is_root (container)) 
			continue;
		if ( (type == E_GW_CONTAINER_TYPE_CALENDAR) || (type == E_GW_CONTAINER_TYPE_CONTACTS) )
			continue;

		fi = g_new0 (CamelFolderInfo, 1);

		if (type == E_GW_CONTAINER_TYPE_INBOX)
			fi->flags |= CAMEL_FOLDER_TYPE_INBOX;
		if (type == E_GW_CONTAINER_TYPE_TRASH)
			fi->flags |= CAMEL_FOLDER_TYPE_TRASH;

		if ( (type == E_GW_CONTAINER_TYPE_INBOX) ||
		     (type == E_GW_CONTAINER_TYPE_OUTBOX) ||
		     (type == E_GW_CONTAINER_TYPE_DOCUMENTS) ||
		     (type == E_GW_CONTAINER_TYPE_QUERY) ||
		     (type == E_GW_CONTAINER_TYPE_CHECKLIST) ||
		     (type == E_GW_CONTAINER_TYPE_DRAFT) ||
		     (type == E_GW_CONTAINER_TYPE_CABINET) ||
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
			fi->name =  fi->full_name = g_strdup (name);
			fi->uri = g_strconcat (url, "", name, NULL);
		}
		/*name_hash returns the container id given the name */
		g_hash_table_insert (priv->name_hash, g_strdup(fi->full_name), g_strdup(id));

		if (!strcmp (fi->full_name, "Junk Mail"))
			fi->flags |= CAMEL_FOLDER_TYPE_JUNK;

		/*XXX: Remove this condition check when server has a fix to show mails in shared-folder*/
		if (getenv("SHARED_FOLDER")) {
			if (e_gw_container_get_is_shared_to_me (container))
				fi->flags |= CAMEL_FOLDER_SHARED_TO_ME;

			if (e_gw_container_get_is_shared_by_me (container))
				fi->flags |= CAMEL_FOLDER_SHARED_BY_ME;
		}

		if (type == E_GW_CONTAINER_TYPE_INBOX) {
			fi->total = -1;
			fi->unread = -1;
		}else {
			fi->total = e_gw_container_get_total_count (container);
			fi->unread = e_gw_container_get_unread_count (container);
		}
		/*refresh info*/
		if (groupwise_store->current_folder &&
		strcmp (groupwise_store->current_folder->full_name, fi->full_name) == 0) {
			CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS (groupwise_store->current_folder))->refresh_info(groupwise_store->current_folder, ex);
		}
		
		g_ptr_array_add (folders, fi);
		name = NULL;
	}

	/*Thread stuff starts*/
/*	msg = camel_session_thread_msg_new (session, &refresh_ops, sizeof(*msg));
	msg->store = store;
	camel_session_thread_queue (session, &msg->msg, 0);*/
	/*Thread stuff ends*/
	g_free ((char *)url);
	if ( (top != NULL) && (folders->len == 0)) {
		CAMEL_SERVICE_UNLOCK (store, connect_lock);
		return groupwise_build_folder_info (groupwise_store, NULL, top );

	}
	info = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);

	folder_real = camel_groupwise_store_summary_path_to_full(groupwise_store->summary, top, '/');
	camel_groupwise_store_summary_add_from_full(groupwise_store->summary, folder_real, '/');
	camel_store_summary_save ((CamelStoreSummary *)groupwise_store->summary);
	CAMEL_SERVICE_UNLOCK (store, connect_lock);

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

	if (parent_name && (strlen(parent_name) > 0) ) 
		parent_id = g_hash_table_lookup (priv->name_hash, parent_name);
	else
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
	
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot delete GroupWise folders in offline mode."));
		return;
	}
	
	container = g_hash_table_lookup (priv->name_hash, folder_name);

	CAMEL_SERVICE_LOCK (store, connect_lock);

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

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot rename GroupWise folders in offline mode."));
		return;
	}
	CAMEL_SERVICE_LOCK (groupwise_store, connect_lock);
	
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
		CAMEL_SERVICE_UNLOCK (store, connect_lock);
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
	if (rename (oldpath, newpath) == -1) {
		g_warning ("Could not rename message cache '%s' to '%s': %s: cache reset",
				oldpath, newpath, strerror (errno));
	}

	g_free (oldpath);
	g_free (newpath);
	CAMEL_SERVICE_UNLOCK (store, connect_lock);
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


static void
free_hash (gpointer key, gpointer value, gpointer data)
{
	if (value)
		g_free (value);
	if (key)
		g_free (key);
}

static CamelFolder *
groupwise_get_trash (CamelStore *store, CamelException *ex)
{
	CamelFolder *folder =  groupwise_get_folder (store, "Trash", 0, ex);
	if (folder) {
		g_print ("Trash Folder exists\n");
		return folder;
	} else {
		g_print ("Trash Folder does not\n");
		return NULL;
	}
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
		
		if (priv->id_hash) {
			g_hash_table_foreach (priv->id_hash, free_hash, NULL);
			g_hash_table_destroy (priv->id_hash);
		}
		if (priv->name_hash) {
			g_hash_table_foreach (priv->name_hash, free_hash, NULL);
			g_hash_table_destroy (priv->name_hash);
		}
		if (priv->parent_hash) {
			g_hash_table_foreach (priv->parent_hash, free_hash, NULL);
			g_hash_table_destroy (priv->parent_hash);
		}
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

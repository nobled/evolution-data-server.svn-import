/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <glib.h>
#include <libsoup/soup-misc.h>
#include "e-gw-item.h"
#include "e-gw-connection.h"
#include "e-gw-message.h"

struct _EGwItemPrivate {
	EGwItemType item_type;
	char *container;
	GList *category_list; /*list of category ids*/

	/* properties */
	char *id;
	char *creation_date;
	char *start_date;
	char *end_date;
	char *due_date;
	char *completed_date;
	gboolean completed;
	gboolean is_allday_event;
	char *subject;
	char *message;
	char *classification;
	char *accept_level;
	char *priority;
	char *task_priority;
	char *place;
	GSList *recipient_list;
	GSList *recurrence_dates;
	int trigger; /* alarm */
	EGwItemOrganizer *organizer;

	/*properties for mail*/
	char *from ;
	char *to ;
	char *content_type ;
	int item_status;
	/*Attachments*/
	GSList *attach_list ;

	/* properties for tasks/calendars */
	char *icalid;
	/* if the self is not the organizer of the item, the 
	 * status is not reflected in the recipientStatus.
	 * Hence it should be gleaned from the 'status' element
	 * of the Mail, the parent item.*/
	guint32 self_status;
	/* properties for category items*/
	char *category_name;

	/* properties for contacts */
	FullName *full_name;
	GList *email_list;
	GList *im_list;
	GHashTable *simple_fields;
	GList *member_list;
	GHashTable *addresses;

	/***** Send Options *****/
	
	gboolean set_sendoptions;
	/* Reply Request */
	char *reply_within;
	gboolean reply_request_set;
	
	/* Status Tracking through sent Item */
	EGwItemTrack track_info;
	gboolean autodelete;

	/* Return Notification */
	EGwItemReturnNotify notify_completed;
	EGwItemReturnNotify notify_accepted;
	EGwItemReturnNotify notify_declined;
	EGwItemReturnNotify notify_opened;
	EGwItemReturnNotify notify_deleted;

	/* Expiration Date */
	char *expires;

	/* Delay delivery */
	char *delay_until;

	/* changes */
	GHashTable *additions;
	GHashTable *updates;
	GHashTable *deletions;	
};

static GObjectClass *parent_class = NULL;

static void
free_recipient (EGwItemRecipient *recipient, gpointer data)
{
	g_free (recipient->email);
	g_free (recipient->display_name);
	g_free (recipient);
}

static void 
free_postal_address (gpointer  postal_address)
{
	PostalAddress *address;
	address = (PostalAddress *) postal_address;
	if (address) {
		g_free (address->street_address);
		g_free (address->location);
		g_free(address->city);
		g_free(address->country);
		g_free(address->state);
		g_free(address->postal_code);
		g_free(address);
	}
}

static void 
free_full_name (gpointer full_name)
{
	FullName *name = (FullName *) full_name;
	g_free (name->name_prefix);
	g_free (name->first_name);
	g_free (name->middle_name);
	g_free (name->last_name);
	g_free (name->name_suffix);
	g_free (name);

}

static void 
free_string (gpointer s, gpointer data)
{
	if (s)
		free (s);
}

static void
free_attach (gpointer s, gpointer data) 
{
	EGwItemAttachment *attach = (EGwItemAttachment *) s ;
	if (attach) {
		if (attach->id) 
			g_free (attach->id), attach->id = NULL ;
		if (attach->name)
			g_free (attach->name), attach->name = NULL ;
		if (attach->contentType)
			g_free (attach->contentType), attach->contentType = NULL ;
		if (attach->date)
			g_free (attach->date), attach->date = NULL ;
		if (attach->data)
			g_free (attach->data), attach->data = NULL ;
	
		g_free(attach) ;
	}
	
}
static void 
free_member (gpointer member, gpointer data)
{
	EGroupMember *group_member = (EGroupMember *) member;
	if (group_member->id)
		g_free (group_member->id);
	if (group_member->email)
		g_free (group_member->email);
	if (group_member->name)
		g_free (group_member->name);
	g_free (group_member);
}

static void 
free_im_address ( gpointer address, gpointer data)
{
	IMAddress *im_address;
	im_address = (IMAddress *) address;
	
	if (im_address) {
		if (im_address->service)
			g_free (im_address->service);
		if (im_address->address)
			g_free (im_address->address);
		g_free (im_address);
	}
}

static void 
free_changes ( GHashTable *changes)
{
	gpointer value;
	if (!changes)
		return;
	value = g_hash_table_lookup (changes, "full_name");
	if (value)
		free_full_name (value);
	value = g_hash_table_lookup (changes, "email");
	if (value)
		g_list_free ((GList*) value);
	value = g_hash_table_lookup (changes, "ims");
	if (value)
		g_list_free ((GList*) value);
	value = g_hash_table_lookup (changes, "Home");
	if (value)
		free_postal_address (value);
	value = g_hash_table_lookup (changes, "Office");
	if (value)
		free_postal_address (value);
	g_hash_table_destroy (changes);
}
static void
e_gw_item_dispose (GObject *object)
{
	EGwItem *item = (EGwItem *) object;
	EGwItemPrivate *priv;

	g_return_if_fail (E_IS_GW_ITEM (item));

	priv = item->priv;
	if (priv) {
		if (priv->container) {
			g_free (priv->container);
			priv->container = NULL;
		}

		if (priv->id) {
			g_free (priv->id);
			priv->id = NULL;
		}

		if (priv->subject) {
			g_free (priv->subject);
			priv->subject = NULL;
		}

		if (priv->message) {
			g_free (priv->message);
			priv->message = NULL;
		}

		if (priv->classification) {
			g_free (priv->classification);
			priv->classification = NULL;
		}

		if (priv->accept_level) {
			g_free (priv->accept_level);
			priv->accept_level = NULL;
		}

		if (priv->priority) {
			g_free (priv->priority);
			priv->priority = NULL;
		}
			
		if (priv->task_priority) {
			g_free (priv->task_priority);
			priv->task_priority = NULL;
		}
		
		if (priv->place) {
			g_free (priv->place);
			priv->place = NULL;
		}

		if (priv->from) {
			g_free (priv->from) ;
			priv->from = NULL ;
		}

		if (priv->to) {
			g_free (priv->to) ;
			priv->to = NULL ;
		}
		
		if (priv->content_type) {
			g_free (priv->content_type) ;
			priv->to = NULL ;
		}

		if (priv->icalid) {
			g_free (priv->icalid);
			priv->icalid = NULL;
		}

		if (priv->reply_within) {
			g_free (priv->reply_within);
			priv->reply_within = NULL;
		}

		if (priv->expires) {
			g_free (priv->expires);
			priv->expires = NULL;
		}

		if (priv->delay_until) {
			g_free (priv->delay_until);
			priv->delay_until = NULL;
		}

		if (priv->recipient_list) {
			g_slist_foreach (priv->recipient_list, (GFunc) free_recipient, NULL);
			g_slist_free (priv->recipient_list);
			priv->recipient_list = NULL;
		}	
		if (priv->organizer) {
			g_free (priv->organizer->display_name);
			g_free (priv->organizer->email);
			priv->organizer = NULL;
		}
		if (priv->recurrence_dates) {
			g_slist_foreach (priv->recurrence_dates, free_string, NULL);
			g_slist_free (priv->recurrence_dates);
			priv->recurrence_dates = NULL;
		}
		if (priv->full_name) {
			free_full_name (priv->full_name);
			priv->full_name = NULL;
			}
		if (priv->simple_fields)
			g_hash_table_destroy (priv->simple_fields);
		if (priv->addresses)
			g_hash_table_destroy (priv->addresses);
		if (priv->email_list) {
			g_list_foreach (priv->email_list,  free_string , NULL);
			g_list_free (priv->email_list);
			priv->email_list = NULL;
		}
		if (priv->member_list) {
			g_list_foreach (priv->member_list,  free_member, NULL);
			g_list_free (priv->member_list);
			priv->member_list = NULL;
		}

		if (priv->im_list) {
			g_list_foreach (priv->im_list, free_im_address, NULL);
			g_list_free (priv->im_list);
			priv->im_list = NULL;
		}
		if (priv->category_list) {
			g_list_foreach (priv->category_list,  free_string, NULL);
			g_list_free (priv->category_list);
			priv->category_list = NULL;
		}
		if(priv->attach_list) {
			g_slist_foreach (priv->attach_list, free_attach, NULL); 
			g_slist_free (priv->attach_list) ;
			priv->attach_list = NULL ;
		}
		if (priv->category_name) {
			g_free (priv->category_name);
			priv->category_name = NULL;
		}
		free_changes (priv->additions);
		free_changes (priv->deletions);
		free_changes (priv->updates);
		
	}

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_gw_item_finalize (GObject *object)
{
	EGwItem *item = (EGwItem *) object;
	EGwItemPrivate *priv;

	g_return_if_fail (E_IS_GW_ITEM (item));

	priv = item->priv;

	/* clean up */
	g_free (priv);
	item->priv = NULL;

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_gw_item_class_init (EGwItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_gw_item_dispose;
	object_class->finalize = e_gw_item_finalize;
}

static void
e_gw_item_init (EGwItem *item, EGwItemClass *klass)
{
	EGwItemPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EGwItemPrivate, 1);
	priv->item_type = E_GW_ITEM_TYPE_UNKNOWN;
	priv->creation_date = NULL;
	priv->start_date = NULL;
	priv->end_date = NULL; 
	priv->due_date = NULL; 
	priv->completed_date = NULL;
	priv->trigger = 0;
	priv->recipient_list = NULL;
	priv->organizer = NULL;
	priv->recurrence_dates = NULL;
	priv->completed = FALSE;
	priv->is_allday_event = FALSE;
	priv->im_list = NULL;
	priv->email_list = NULL;
	priv->member_list = NULL;
	priv->category_list = NULL;
	priv->reply_within = NULL;
	priv->reply_request_set = FALSE;
	priv->autodelete = FALSE;
	priv->set_sendoptions = FALSE;
	priv->expires = NULL;
	priv->delay_until = NULL;
	priv->attach_list = NULL ;
	priv->simple_fields = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
	priv->full_name = g_new0(FullName, 1);
	priv->addresses = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, free_postal_address);
	priv->additions = g_hash_table_new(g_str_hash, g_str_equal);
	priv->updates =   g_hash_table_new (g_str_hash, g_str_equal);
	priv->deletions = g_hash_table_new (g_str_hash, g_str_equal);
	priv->self_status = 0;
	item->priv = priv;
	
	
}

GType
e_gw_item_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EGwItemClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_gw_item_class_init,
                        NULL, NULL,
                        sizeof (EGwItem),
                        0,
                        (GInstanceInitFunc) e_gw_item_init
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EGwItem", &info, 0);
	}

	return type;
}

EGwItem *
e_gw_item_new_empty (void)
{
	return g_object_new (E_TYPE_GW_ITEM, NULL);
}

static void 
set_recipient_list_from_soap_parameter (EGwItem *item, SoupSoapParameter *param)
{
        SoupSoapParameter *param_recipient;
        char *email, *cn;
	EGwItemRecipient *recipient;
	GList *email_list;

	email_list = e_gw_item_get_email_list (item);
	
        for (param_recipient = soup_soap_parameter_get_first_child_by_name (param, "recipient");
	     param_recipient != NULL;
	     param_recipient = soup_soap_parameter_get_next_child_by_name (param_recipient, "recipient")) {
                SoupSoapParameter *subparam;

		recipient = g_new0 (EGwItemRecipient, 1);	
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "email");
                if (subparam) {
                        email = soup_soap_parameter_get_string_value (subparam);
                        if (email)
                                recipient->email = email;
                }        
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "displayName");
                if (subparam) {
                        cn = soup_soap_parameter_get_string_value (subparam);
                        if (cn)
                                recipient->display_name = cn;
                }
                
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "distType");
                if (subparam) {
                        const char *dist_type;
                        dist_type = soup_soap_parameter_get_string_value (subparam);
                        if (!strcmp (dist_type, "TO")) 
                                recipient->type = E_GW_ITEM_RECIPIENT_TO;
                        else if (!strcmp (dist_type, "CC"))
                                recipient->type = E_GW_ITEM_RECIPIENT_CC;
			else if (!strcmp (dist_type, "BC"))
				recipient->type = E_GW_ITEM_RECIPIENT_BC;
                        else
				recipient->type = E_GW_ITEM_RECIPIENT_NONE;
                }
		/*FIXME  gw recipientTypes need to be added after the server is fixed. */

		
		/* update Recipient Status
		 look for accepted/declined and update the item else set it
		 to none. */
		subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "recipientStatus");
                if (subparam) {
                        if (soup_soap_parameter_get_first_child_by_name (subparam, "deleted"))
				recipient->status = E_GW_ITEM_STAT_DECLINED;
			if (soup_soap_parameter_get_first_child_by_name (subparam, "declined"))
				recipient->status = E_GW_ITEM_STAT_DECLINED;
			else if (soup_soap_parameter_get_first_child_by_name (subparam, "accepted")) 
				recipient->status = E_GW_ITEM_STAT_ACCEPTED;
			else 	
				recipient->status = E_GW_ITEM_STAT_NONE;
			subparam = soup_soap_parameter_get_first_child_by_name (subparam, "completed");
			if (subparam) {
				char *formatted_date, *value; 
				value = soup_soap_parameter_get_string_value (subparam);
				formatted_date = e_gw_connection_format_date_string (value);
				e_gw_item_set_completed_date (item, formatted_date);
				g_free (value);
				g_free (formatted_date);
			}
                }
		else {
			/* if recipientStatus is not provided, use the
			 * self_status, obtained from the mail properties. */
			if (!strcmp ((const char *)email_list->data, recipient->email)) 
				recipient->status = item->priv->self_status & (E_GW_ITEM_STAT_DECLINED | 
										E_GW_ITEM_STAT_ACCEPTED);
			else
				recipient->status = E_GW_ITEM_STAT_NONE;
		}
		
                item->priv->recipient_list = g_slist_append (item->priv->recipient_list, recipient);
        }        
}

char*
e_gw_item_get_field_value (EGwItem *item, char *field_name)
{
	gpointer value;

	g_return_val_if_fail (field_name != NULL, NULL);
	g_return_val_if_fail (E_IS_GW_ITEM(item), NULL);
	
	if (item->priv->simple_fields == NULL)
		return NULL;
       
	value =  (char *) g_hash_table_lookup (item->priv->simple_fields, field_name);
	if (value)
		return value;
			
	return NULL;
}

void 
e_gw_item_set_field_value (EGwItem *item, char *field_name, char* field_value)
{
	g_return_if_fail (field_name != NULL);
	g_return_if_fail (field_name != NULL);
	g_return_if_fail (E_IS_GW_ITEM(item));
	
	if (item->priv->simple_fields != NULL)
		g_hash_table_insert (item->priv->simple_fields, field_name, g_strdup (field_value));

}
guint32 
e_gw_item_get_item_status (EGwItem *item)
{

	return item->priv->self_status;
}
GList * 
e_gw_item_get_email_list (EGwItem *item)
{
	return item->priv->email_list;


}

void 
e_gw_item_set_email_list (EGwItem *item, GList* email_list)     
{
	item->priv->email_list = email_list;
}

GList * 
e_gw_item_get_im_list (EGwItem *item)

{
	return item->priv->im_list;
}

void 
e_gw_item_set_im_list (EGwItem *item, GList *im_list)
{
	item->priv->im_list = im_list;
}
FullName*
e_gw_item_get_full_name (EGwItem *item)
{
	return item->priv->full_name;
}

void 
e_gw_item_set_full_name (EGwItem *item, FullName *full_name)
{	
	item->priv->full_name = full_name;
}

GList *
e_gw_item_get_member_list (EGwItem *item)
{
	return item->priv->member_list;
}

void 
e_gw_item_set_member_list (EGwItem *item, GList *list)
{
	item->priv->member_list = list;

}

void 
e_gw_item_set_address (EGwItem *item, char *address_type, PostalAddress *address)
{
	if (address_type && address)
		g_hash_table_insert (item->priv->addresses, address_type, address);

}

PostalAddress *e_gw_item_get_address (EGwItem *item, char *address_type)
{
	return (PostalAddress *) g_hash_table_lookup (item->priv->addresses, address_type);
}

void 
e_gw_item_set_categories (EGwItem *item, GList *category_list)
{
	item->priv->category_list = category_list;

}

GList*
e_gw_item_get_categories (EGwItem *item)
{
	return item->priv->category_list;
}

void 
e_gw_item_set_category_name (EGwItem *item, char *category_name)
{
	item->priv->category_name = category_name;
}

char*
e_gw_item_get_category_name (EGwItem *item)
{
	return item->priv->category_name;
}

void e_gw_item_set_change (EGwItem *item, EGwItemChangeType change_type, char *field_name, gpointer field_value)
{
	GHashTable *hash_table;
	EGwItemPrivate *priv;

	priv = item->priv;
	hash_table = NULL;
	switch (change_type) {
	case E_GW_ITEM_CHANGE_TYPE_ADD :
		hash_table = priv->additions;
		break;
	case E_GW_ITEM_CHANGE_TYPE_UPDATE :
		hash_table = priv->updates;
		break;
	case E_GW_ITEM_CHANGE_TYPE_DELETE :
		hash_table = priv->deletions;
		break;
	case E_GW_ITEM_CHNAGE_TYPE_UNKNOWN :
		hash_table = NULL;
		break;
	
	}

	if (hash_table)
		g_hash_table_insert (hash_table, field_name, field_value);
	
}

static void 
set_common_addressbook_item_fields_from_soap_parameter (EGwItem *item, SoupSoapParameter *param)
{
	SoupSoapParameter *subparam, *category_param;
	GHashTable *simple_fields;
	char *value;
	EGwItemPrivate *priv;
	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}
	priv = item->priv;
	simple_fields = priv->simple_fields;

	subparam = soup_soap_parameter_get_first_child_by_name(param, "id");
	if(subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		g_hash_table_insert (simple_fields, "id", value);
		item->priv->id = g_strdup (value);
	}
	value = NULL;
	subparam = soup_soap_parameter_get_first_child_by_name (param, "comment");
	if(subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			g_hash_table_insert (simple_fields , "comment", value);
	}
	value = NULL;
	subparam = soup_soap_parameter_get_first_child_by_name(param, "name");
	if(subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			g_hash_table_insert (simple_fields, "name", value);
	}
	value = NULL;
	subparam = soup_soap_parameter_get_first_child_by_name (param, "categories");
	if (subparam) {
		for (category_param = soup_soap_parameter_get_first_child_by_name (subparam, "category");
		     category_param != NULL;
		     category_param = soup_soap_parameter_get_next_child_by_name (category_param, "category")) {

			value = soup_soap_parameter_get_string_value (category_param);
			if (value) {
				char **components = g_strsplit (value, "@", -1);
				g_free (value);
				value = components[0];
				priv->category_list = g_list_append (priv->category_list, g_strdup (value));
				g_strfreev(components);
				
			}
				
		}
	}


}

static void 
set_postal_address_from_soap_parameter (PostalAddress *address, SoupSoapParameter *param)
{
	SoupSoapParameter *subparam;
	char *value;

	subparam= soup_soap_parameter_get_first_child_by_name (param, "streetAddress");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->street_address = value;
	}
	
	subparam = soup_soap_parameter_get_first_child_by_name (param, "location");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		
		if (value)
			address->location = value;
	}
	
	subparam = soup_soap_parameter_get_first_child_by_name (param, "city");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->city = value;
	}
	
	subparam = soup_soap_parameter_get_first_child_by_name (param, "state");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->state = value;
	}
	
	subparam = soup_soap_parameter_get_first_child_by_name (param, "postalCode");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->postal_code = value;
	}
	
	subparam = soup_soap_parameter_get_first_child_by_name (param, "country");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->country = value;
	}
	
}

static void 
set_contact_fields_from_soap_parameter (EGwItem *item, SoupSoapParameter *param)
{
	char *value;
        char *type;
	char *primary_email;
	SoupSoapParameter *subparam;
	SoupSoapParameter *temp;
	SoupSoapParameter *second_level_child;
	GHashTable *simple_fields;
	FullName *full_name ;
	PostalAddress *address;

	value = NULL;
	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}
	set_common_addressbook_item_fields_from_soap_parameter (item, param);
	simple_fields = item->priv->simple_fields;
	full_name = item->priv->full_name;
	if (full_name) {
		subparam = soup_soap_parameter_get_first_child_by_name (param, "fullName");
		if (subparam) {
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "namePrefix"); 
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->name_prefix = value;
			}
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "firstName"); 
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->first_name = value;
			}
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "middleName"); 
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->middle_name = value;
			}
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "lastName"); 
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->last_name = value;
			}
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "nameSuffix"); 
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->name_suffix = value;
			}
		}
	}
	subparam = soup_soap_parameter_get_first_child_by_name(param, "emailList"); 
	if (subparam) {
		primary_email = NULL;
		value = soup_soap_parameter_get_property(subparam, "primary");
		if (value) {	 
			primary_email = value;
			item->priv->email_list = g_list_append (item->priv->email_list, g_strdup (primary_email));
		}
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp)) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value && (!primary_email ||  !g_str_equal (primary_email, value)))
				item->priv->email_list = g_list_append (item->priv->email_list, value);
		}		
	}
	
	subparam =  soup_soap_parameter_get_first_child_by_name(param, "imList");
	if(subparam) {
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp))
			{
				IMAddress *im_address = g_new0(IMAddress, 1);
				im_address->address = im_address->service = NULL;
				second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "service");
				if (second_level_child) {
					value = soup_soap_parameter_get_string_value (second_level_child);
					if (value )
						im_address->service = value;
				}
				second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "address");
				if (second_level_child) {
					value = soup_soap_parameter_get_string_value (second_level_child);
					if (value)
						im_address->address = value;
				}
				if (im_address->service && im_address->address)
					item->priv->im_list = g_list_append (item->priv->im_list, im_address);
				else 
					free_im_address (im_address, NULL);					
				
			}
	}
	
	
	subparam =  soup_soap_parameter_get_first_child_by_name(param, "phoneList");
	if(subparam) {
		g_hash_table_insert (simple_fields, "default_phone", soup_soap_parameter_get_property(subparam, "default"));
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp))
			{
				type =  soup_soap_parameter_get_property (temp, "type");
				value = soup_soap_parameter_get_string_value (temp);
				g_hash_table_insert (item->priv->simple_fields, g_strconcat("phone_", type, NULL) , value);
				g_free (type);
			}
	}
	subparam =  soup_soap_parameter_get_first_child_by_name(param, "personalInfo");
	if(subparam) {
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "birthday");
		if(temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value)
				g_hash_table_insert (simple_fields, "birthday", value);
			
		}
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "website");
		if(temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value)
				g_hash_table_insert (simple_fields, "website", value);
		}
	}

	subparam =  soup_soap_parameter_get_first_child_by_name(param, "officeInfo");
	if (subparam) {
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "organization");
		if(temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if(value)
				g_hash_table_insert (simple_fields, "organization", value);
			value = soup_soap_parameter_get_property(temp, "uid");
			if (value)
				g_hash_table_insert (simple_fields, "organization_id", value);
		}
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "department");
		if(temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if(value)
				g_hash_table_insert (simple_fields, "department", value);
		}
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "title");
		if(temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if(value)
				g_hash_table_insert (simple_fields, "title", value);
		}
			
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "addressList");
	if (subparam) {
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp)) {
			
			address = g_new0 (PostalAddress, 1);
			set_postal_address_from_soap_parameter (address, temp);
			value = soup_soap_parameter_get_property(temp, "type");
			if (value)
				g_hash_table_insert (item->priv->addresses, value, address);
			 			
			
		}
		
	}
	
}
static void 
set_group_fields_from_soap_parameter (EGwItem *item, SoupSoapParameter *param)
{

	char *value;
	SoupSoapParameter *subparam, *temp, *second_level_child;
	GHashTable *simple_fields;
	
	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}

	set_common_addressbook_item_fields_from_soap_parameter (item, param);
	simple_fields = item->priv->simple_fields;

	/* set name as the ful name also , as it is needed for searching*/
	value = g_hash_table_lookup (simple_fields, "name");
	if (value) 
		item->priv->full_name->first_name = g_strdup (value);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "members");
	if (subparam) {
		char *id, *email;
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp)) {
			id = email = NULL;
			second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "email"); 
			if (second_level_child)
				email = soup_soap_parameter_get_string_value (second_level_child);
			second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "id");
			if (second_level_child)
				id = soup_soap_parameter_get_string_value (second_level_child);
			
			if (id && email) {
				EGroupMember *member = g_new0 (EGroupMember, 1);
				member->id = id;
				member->email = email;
				second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "name");
				member->name =  soup_soap_parameter_get_string_value (second_level_child); 
				item->priv->member_list = g_list_append (item->priv->member_list, member);
			}
			      
			
		}
	}
	
	
}

static void 
set_resource_fields_from_soap_parameter (EGwItem *item, SoupSoapParameter *param)
{

	char *value;
	SoupSoapParameter *subparam;
	GHashTable *simple_fields;
	
	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}

	set_common_addressbook_item_fields_from_soap_parameter (item, param);
	simple_fields = item->priv->simple_fields;

	/* set name as the ful name also , as it is needed for searching*/
	value = g_hash_table_lookup (simple_fields, "name");
	if (value) 
		item->priv->full_name->first_name = g_strdup (value);
		
	subparam = soup_soap_parameter_get_first_child_by_name (param, "phone");
	if(subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if(value)
			g_hash_table_insert (simple_fields, "default_phone", value);
	}
	subparam = soup_soap_parameter_get_first_child_by_name (param, "email");
	if(subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if(value)
			item->priv->email_list = g_list_append (item->priv->email_list, value);
	}
	
}

static void 
set_organization_fields_from_soap_parameter (EGwItem *item, SoupSoapParameter *param)
{

	char *value;
	SoupSoapParameter *subparam;
	PostalAddress *address;
	GHashTable *simple_fields;
	
	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}
	set_common_addressbook_item_fields_from_soap_parameter (item, param);
	simple_fields = item->priv->simple_fields;

	/* set name as the ful name also , as it is needed for searching*/
	value = g_hash_table_lookup (simple_fields, "name");
	if (value) 
		item->priv->full_name->first_name = g_strdup (value);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "phone");
	if(subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if(value)
			g_hash_table_insert (simple_fields, "default_phone", value);
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "fax");
	if(subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if(value)
			g_hash_table_insert (simple_fields, "phone_Fax", value);
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "address");
	if (subparam) {
		address = g_new0 (PostalAddress, 1);
		set_postal_address_from_soap_parameter (address, subparam);
		g_hash_table_insert (item->priv->addresses, "Office", address);
		
	}

       	subparam = soup_soap_parameter_get_first_child_by_name (param, "website");
	if(subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if(value)
			g_hash_table_insert (simple_fields, "website", value);
	}


}

static void 
append_postal_address_to_soap_message (SoupSoapMessage *msg, PostalAddress *address, char *address_type)
{
	soup_soap_message_start_element (msg, "address", NULL, NULL);
	soup_soap_message_add_attribute (msg, "type", address_type, NULL, NULL);
	if (address->street_address)
		e_gw_message_write_string_parameter (msg, "streetAddress", NULL, address->street_address);
	if (address->location)
		e_gw_message_write_string_parameter (msg, "location", NULL, address->location);
	if (address->city)
		e_gw_message_write_string_parameter (msg, "city", NULL, address->city);
	if (address->state)
		e_gw_message_write_string_parameter (msg, "state", NULL, address->state);
	if (address->postal_code)
		e_gw_message_write_string_parameter (msg, "postalCode", NULL, address->postal_code);
	if (address->country)
		e_gw_message_write_string_parameter (msg, "country", NULL, address->country);
	soup_soap_message_end_element(msg);

}

static void
append_common_addressbook_item_fields_to_soap_message (GHashTable *simple_fields, GList *category_list,  SoupSoapMessage *msg)
{
	char * value;
	
	value =  g_hash_table_lookup (simple_fields, "name");
	if (value)
		e_gw_message_write_string_parameter (msg, "name", NULL, value);

	soup_soap_message_start_element (msg, "categories", NULL, NULL);
	if (category_list && category_list->data) 
		soup_soap_message_add_attribute (msg, "types:primary", category_list->data, NULL, NULL);
	for (; category_list != NULL; category_list = g_list_next (category_list)) 
		if (category_list->data) {
			e_gw_message_write_string_parameter (msg, "category", NULL, category_list->data);
		}
	soup_soap_message_end_element (msg);

	value = g_hash_table_lookup (simple_fields, "comment");
	if(value) 
		e_gw_message_write_string_parameter (msg, "comment", NULL, value);

}

static void 
append_full_name_to_soap_message (FullName *full_name, char *display_name, SoupSoapMessage *msg)
{
	g_return_if_fail (full_name != NULL);
	soup_soap_message_start_element (msg, "fullName", NULL, NULL);
	if (display_name)
		e_gw_message_write_string_parameter (msg, "displayName", NULL, display_name);
	if (full_name->name_prefix)
		e_gw_message_write_string_parameter (msg, "namePrefix", NULL, full_name->name_prefix);
	if (full_name->first_name)
		e_gw_message_write_string_parameter (msg, "firstName", NULL, full_name->first_name);
       	if (full_name->middle_name)
		e_gw_message_write_string_parameter (msg, "middleName", NULL, full_name->middle_name);
	if (full_name->last_name)
		e_gw_message_write_string_parameter (msg, "lastName", NULL, full_name->last_name) ;
	if (full_name->name_suffix)
		e_gw_message_write_string_parameter (msg, "nameSuffix", NULL, full_name->name_suffix);
	soup_soap_message_end_element (msg);

}

static void 
append_email_list_soap_message (GList *email_list, SoupSoapMessage *msg)
{
	g_return_if_fail (email_list != NULL);

	soup_soap_message_start_element (msg, "emailList", NULL, NULL);
	soup_soap_message_add_attribute (msg, "primary", email_list->data, NULL, NULL);
	for (; email_list != NULL; email_list = g_list_next (email_list)) 
		if(email_list->data) 
			e_gw_message_write_string_parameter (msg, "email", NULL, email_list->data);
	soup_soap_message_end_element (msg);

}

static void 
append_im_list_to_soap_message (GList *ims, SoupSoapMessage *msg)
{
	IMAddress *address;
	g_return_if_fail (ims != NULL);

	soup_soap_message_start_element (msg, "imList", NULL, NULL);
	for (; ims != NULL; ims = g_list_next (ims)) {
		soup_soap_message_start_element (msg, "im", NULL, NULL);
		address = (IMAddress *) ims->data;
		e_gw_message_write_string_parameter (msg, "service", NULL, address->service);
		e_gw_message_write_string_parameter (msg, "address", NULL, address->address);
		soup_soap_message_end_element (msg);
	}
	soup_soap_message_end_element (msg);

}
static void
append_phone_list_to_soap_message (GHashTable *simple_fields, SoupSoapMessage *msg)
{
	char *value;

	g_return_if_fail (simple_fields != NULL);

	soup_soap_message_start_element (msg, "phoneList", NULL, NULL);
	value = g_hash_table_lookup (simple_fields, "default_phone");
	if (value) 
		soup_soap_message_add_attribute (msg, "default", value, NULL, NULL);
	value = g_hash_table_lookup (simple_fields, "phone_Office");
	if (value) 
		e_gw_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Office");
	value = g_hash_table_lookup (simple_fields, "phone_Home");
	if (value) 
		e_gw_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Home");
	value = g_hash_table_lookup (simple_fields, "phone_Pager");
	if (value) 
		e_gw_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Pager");
	value = g_hash_table_lookup (simple_fields, "phone_Mobile");
	if (value) 
		e_gw_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Mobile");
	value = g_hash_table_lookup (simple_fields, "phone_Fax");
	if (value) 
		e_gw_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Fax");

	soup_soap_message_end_element (msg);

}

static void 
append_office_info_to_soap_message (GHashTable *simple_fields, SoupSoapMessage *msg)
{
	char *value;
	char *org_name;
	g_return_if_fail (simple_fields != NULL);

	soup_soap_message_start_element (msg, "officeInfo", NULL, NULL);
	value = g_hash_table_lookup (simple_fields, "organization_id");
	org_name = g_hash_table_lookup (simple_fields, "organization");
	if (value && org_name) 
		e_gw_message_write_string_parameter_with_attribute (msg, "organization", NULL, org_name, "uid", value);
	else if(org_name)
		e_gw_message_write_string_parameter (msg, "organization", NULL, org_name);
	value = g_hash_table_lookup (simple_fields, "department");
	if (value)
		e_gw_message_write_string_parameter (msg, "department", NULL, value);
	
		value = g_hash_table_lookup (simple_fields, "title");
	if (value)
		e_gw_message_write_string_parameter (msg, "title", NULL, value);

	value = g_hash_table_lookup (simple_fields, "website");
	if (value)
		e_gw_message_write_string_parameter (msg, "website", NULL, value);
	soup_soap_message_end_element (msg);

}

static void 
append_personal_info_to_soap_message (GHashTable *simple_fields, SoupSoapMessage *msg)
{
	char *value;

	g_return_if_fail (simple_fields != NULL);
	
	soup_soap_message_start_element (msg, "personalInfo", NULL, NULL);
	value =  g_hash_table_lookup (simple_fields, "birthday");
	if(value)
		e_gw_message_write_string_parameter (msg, "birthday", NULL, value);
	value =  g_hash_table_lookup (simple_fields, "website");
	if(value)
		e_gw_message_write_string_parameter (msg, "website",NULL,  value);
	
	soup_soap_message_end_element (msg);

}

static void
append_contact_fields_to_soap_message (EGwItem *item, SoupSoapMessage *msg)
{
	char * value;
	GHashTable *simple_fields;
	FullName *full_name;
	PostalAddress *postal_address;
	
	simple_fields = item->priv->simple_fields;
	value = g_hash_table_lookup (simple_fields, "id");
	if (value)
		e_gw_message_write_string_parameter (msg, "id", NULL, value);
	
	if (item->priv->container)
		e_gw_message_write_string_parameter (msg, "container", NULL, item->priv->container);

	append_common_addressbook_item_fields_to_soap_message (simple_fields, item->priv->category_list, msg);
	value =  g_hash_table_lookup (simple_fields, "name");
	
	full_name = item->priv->full_name;
       
	if (full_name)
		append_full_name_to_soap_message (full_name, value, msg); 
	
	if (item->priv->email_list)
		append_email_list_soap_message (item->priv->email_list, msg);
	
	if (item->priv->im_list)
		append_im_list_to_soap_message (item->priv->im_list, msg);
	
	if (simple_fields)
		append_phone_list_to_soap_message (simple_fields, msg);
		
	soup_soap_message_start_element (msg, "addressList", NULL, NULL);
	postal_address = g_hash_table_lookup (item->priv->addresses, "Home");
	if (postal_address)
		append_postal_address_to_soap_message (msg, postal_address, "Home");
	postal_address = g_hash_table_lookup (item->priv->addresses, "Office");
	if (postal_address)
		append_postal_address_to_soap_message (msg, postal_address, "Office");
	soup_soap_message_end_element (msg);
	append_office_info_to_soap_message (simple_fields, msg);
	append_personal_info_to_soap_message (simple_fields, msg);

}

static void 
append_group_fields_to_soap_message (EGwItem *item, SoupSoapMessage *msg)
{
	GHashTable *simple_fields;
	GList *members;

	simple_fields = item->priv->simple_fields;
	append_common_addressbook_item_fields_to_soap_message (simple_fields, item->priv->category_list, msg);
	if (item->priv->container)
                e_gw_message_write_string_parameter (msg, "container", NULL, item->priv->container);
	soup_soap_message_start_element (msg, "members", NULL, NULL);
	members = g_list_copy (item->priv->member_list);
	for (; members != NULL; members = g_list_next (members)) {
		EGroupMember *member = (EGroupMember *) members->data;
		soup_soap_message_start_element (msg, "member", NULL, NULL);
		e_gw_message_write_string_parameter (msg, "id", NULL, member->id);
		e_gw_message_write_string_parameter (msg, "distType", NULL, "TO");
		e_gw_message_write_string_parameter (msg, "itemType", NULL, "Contact");
		soup_soap_message_end_element(msg);

	}
	soup_soap_message_end_element (msg);
	
}

EGwItem *
e_gw_item_new_from_soap_parameter (const char *email, const char *container, SoupSoapParameter *param)
{
	EGwItem *item;
        char *item_type;
	SoupSoapParameter *subparam, *child, *category_param, *attachment_param, *notification_param ;
	gboolean is_group_item = TRUE;
	GList *user_email = NULL;
	
	g_return_val_if_fail (param != NULL, NULL);

	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return NULL;
	}

	item = g_object_new (E_TYPE_GW_ITEM, NULL);
	item_type = soup_soap_parameter_get_property (param, "type");
	if (!g_ascii_strcasecmp (item_type, "Mail"))
		item->priv->item_type = E_GW_ITEM_TYPE_MAIL ;
	else if (!g_ascii_strcasecmp (item_type, "Appointment"))
		item->priv->item_type = E_GW_ITEM_TYPE_APPOINTMENT;
	else if (!g_ascii_strcasecmp (item_type, "Task"))
		item->priv->item_type = E_GW_ITEM_TYPE_TASK;
	else if (!g_ascii_strcasecmp (item_type, "Contact") ) {
		item->priv->item_type = E_GW_ITEM_TYPE_CONTACT;
		set_contact_fields_from_soap_parameter (item, param);
		return item;
	} 
	else if (!g_ascii_strcasecmp (item_type, "Organization")) {

		item->priv->item_type =  E_GW_ITEM_TYPE_ORGANISATION;
		set_organization_fields_from_soap_parameter (item, param);
		return item;
	}
		
	else if (!g_ascii_strcasecmp (item_type, "Resource")) {
		
		item->priv->item_type = E_GW_ITEM_TYPE_CONTACT;
		set_resource_fields_from_soap_parameter (item, param);
		return item;
	}
		 
	else if (!g_ascii_strcasecmp (item_type, "Group")) {
		item->priv->item_type = E_GW_ITEM_TYPE_GROUP;
		set_group_fields_from_soap_parameter (item, param);
		return item;
	}
	
	else {
		g_free (item_type);
		g_object_unref (item);
		return NULL;
	}

	g_free (item_type);

	item->priv->container = g_strdup (container);
	/* set the email of the user */
	user_email = g_list_append (user_email, g_strdup (email));
	e_gw_item_set_email_list (item, user_email);

	/* If the parameter consists of changes - populate deltas */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "changes");
	if (subparam) {
		SoupSoapParameter *changes = subparam;
		subparam = soup_soap_parameter_get_first_child_by_name (changes, "add");
		if (!subparam)
			subparam = soup_soap_parameter_get_first_child_by_name (changes, "delete");
		if (!subparam)
			subparam = soup_soap_parameter_get_first_child_by_name (changes, "update");
	}
	else subparam = param; /* The item is a complete one, not a delta  */
	/*If its a notification - reset type*/
	notification_param = soup_soap_parameter_get_first_child_by_name (param,"notification") ;
	if (notification_param) {
			item->priv->item_type = E_GW_ITEM_TYPE_NOTIFICATION;
	}
	
	/* now add all properties to the private structure */
	for (child = soup_soap_parameter_get_first_child (subparam);
	     child != NULL;
	     child = soup_soap_parameter_get_next_child (child)) {
		const char *name;
		char *value;

		name = soup_soap_parameter_get_name (child);

		if (!g_ascii_strcasecmp (name, "acceptLevel"))
			item->priv->accept_level = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "class")) {
			item->priv->classification = soup_soap_parameter_get_string_value (child);

		} else if (!g_ascii_strcasecmp (name, "completed")) {
			value = soup_soap_parameter_get_string_value (child);
			if (!g_ascii_strcasecmp (value, "1"))
				item->priv->completed = TRUE;
			else
				item->priv->completed = FALSE;
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "allDayEvent")) {
			value = soup_soap_parameter_get_string_value (child);
			if (!g_ascii_strcasecmp (value, "1"))
				item->priv->is_allday_event = TRUE;
			else
				item->priv->is_allday_event = FALSE;
			g_free (value);	

		} else if (!g_ascii_strcasecmp (name, "status")) {
			SoupSoapParameter *status_param;
			const char *status_name;
			item->priv->self_status = 0;
			for (status_param = soup_soap_parameter_get_first_child (child); status_param != NULL;
			     status_param = soup_soap_parameter_get_next_child (status_param)) {
				status_name = soup_soap_parameter_get_name (status_param);

				if (!strcmp (status_name, "accepted"))
					item->priv->self_status |= E_GW_ITEM_STAT_ACCEPTED;
				else if (!strcmp (status_name, "declined"))
					item->priv->self_status |= E_GW_ITEM_STAT_DECLINED;
				else if (!strcmp (status_name, "deleted"))
					item->priv->self_status |= E_GW_ITEM_STAT_DECLINED;
				else if (!strcmp (status_name, "read")) 
					item->priv->self_status |= E_GW_ITEM_STAT_READ;
				else if (!strcmp (status_name, "opened"))
					item->priv->self_status |= E_GW_ITEM_STAT_OPENED;
				else if (!strcmp (status_name, "completed"))
					item->priv->self_status |= E_GW_ITEM_STAT_COMPLETED;
				else if (!strcmp (status_name, "forwarded"))
					item->priv->self_status |= E_GW_ITEM_STAT_FORWARDED;
				else if (!strcmp (status_name, "replied"))
					item->priv->self_status |= E_GW_ITEM_STAT_REPLIED;
				else if (!strcmp (status_name, "delegated"))
					item->priv->self_status |= E_GW_ITEM_STAT_DELEGATED;
			}
				
		} else if (!g_ascii_strcasecmp (name, "created")) {
			char *formatted_date;
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_gw_connection_format_date_string (value);
			e_gw_item_set_creation_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);

		} else if (!g_ascii_strcasecmp (name, "distribution")) {
			SoupSoapParameter *tp;

			tp = soup_soap_parameter_get_first_child_by_name (child, "recipients");
			if (tp) {
				g_slist_foreach (item->priv->recipient_list, (GFunc) free_recipient, NULL);
				item->priv->recipient_list = NULL;
				set_recipient_list_from_soap_parameter (item, tp);
			}
			tp = soup_soap_parameter_get_first_child_by_name (child, "from");
			if (tp && is_group_item) {
				SoupSoapParameter *subparam;
				EGwItemOrganizer *organizer = g_new0 (EGwItemOrganizer, 1);

				subparam = soup_soap_parameter_get_first_child_by_name (tp, "displayName");
				if (subparam) 
					organizer->display_name = soup_soap_parameter_get_string_value (subparam);
				subparam = soup_soap_parameter_get_first_child_by_name (tp, "email");
				if (subparam) 
					organizer->email = soup_soap_parameter_get_string_value (subparam);
				e_gw_item_set_organizer (item, organizer);
			}

		} else if (!g_ascii_strcasecmp (name, "dueDate")) {
			char *formatted_date; 
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_gw_connection_format_date_string (value);
			e_gw_item_set_due_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);
		
		} else if (!g_ascii_strcasecmp (name, "endDate")) {
			char *formatted_date; 
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_gw_connection_format_date_string (value);
			e_gw_item_set_end_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date); 
		
		} else if (!g_ascii_strcasecmp (name, "to")) {
			char *to ;
			to = soup_soap_parameter_get_string_value (child) ;
			e_gw_item_set_to (item,to) ;

		} else if (!g_ascii_strcasecmp (name, "iCalId"))
			item->priv->icalid = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "categories")) { 
			for (category_param = soup_soap_parameter_get_first_child_by_name (child, "category");
		     	     category_param != NULL;
		    	     category_param = soup_soap_parameter_get_next_child_by_name (category_param, "category")) {

					value = soup_soap_parameter_get_string_value (category_param);
					if (value) {
						char **components = g_strsplit (value, "@", -1);
						g_free (value);
						value = components[0];
						item->priv->category_list = g_list_append (item->priv->category_list, g_strdup (value));
						g_strfreev(components);
					}
			  }

		} else if (!g_ascii_strcasecmp (name, "id"))
			item->priv->id = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "message")) {
			SoupSoapParameter *part;
			int len;
			char *msg;
			char *length;
			
			part = soup_soap_parameter_get_first_child_by_name (child, "part");
			msg = soup_soap_parameter_get_string_value (part);
			length = soup_soap_parameter_get_property (part, "length"); 

			if (msg && length) {
				len = atoi (length);
				item->priv->message = soup_base64_decode  (msg, &len);
				item->priv->content_type = soup_soap_parameter_get_property (part, "contentType") ;
			}

			g_free (length);
			g_free (msg);
			
		} else if (!g_ascii_strcasecmp (name, "attachments")) {
			for (attachment_param = soup_soap_parameter_get_first_child_by_name(child,"attachment") ;
			     attachment_param != NULL ;
			     attachment_param = soup_soap_parameter_get_next_child_by_name (attachment_param, "attachment")) {
			     	
				SoupSoapParameter *temp ;
				EGwItemAttachment *attach = g_new0 (EGwItemAttachment, 1) ;
				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "id") ;
				if (temp)	
					attach->id = soup_soap_parameter_get_string_value (temp), printf ("||| attach id:%s |||\n",attach->id) ;
				
				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "name") ;
				if (temp)
					attach->name = soup_soap_parameter_get_string_value (temp) ;

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "contentType") ;
				if (temp)
					attach->contentType = soup_soap_parameter_get_string_value (temp) ;
				
				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "size") ;
				if (temp)
					attach->size = atoi (soup_soap_parameter_get_string_value (temp)) ;

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "date") ;
				if (temp)
					attach->date = soup_soap_parameter_get_string_value (temp) ;

				
				item->priv->attach_list = g_slist_append (item->priv->attach_list, attach) ;
			}
			     
		} else if (!g_ascii_strcasecmp (name, "place"))
			item->priv->place = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "taskPriority")) {
			e_gw_item_set_task_priority (item, soup_soap_parameter_get_string_value (child));
		}

		else if (!g_ascii_strcasecmp (name, "startDate")) {
			char *formatted_date;
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_gw_connection_format_date_string (value);
			e_gw_item_set_start_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);

		} else if (!g_ascii_strcasecmp (name, "subject"))
			item->priv->subject = soup_soap_parameter_get_string_value (child);
		else if (!g_ascii_strcasecmp (name, "source")) {
			char *value;
			value = soup_soap_parameter_get_string_value (child);
			if (!strcmp (value, "personal"))
				is_group_item = FALSE;
			g_free (value);
		}
			
		else if (!g_ascii_strcasecmp (name, "alarm")) {
			const char *enabled;
			enabled = soup_soap_parameter_get_property (child, "enabled");
			if (!g_ascii_strcasecmp (enabled, "true") ) {
				char *value;
				value = soup_soap_parameter_get_string_value (child);
				/* convert it into integer */
				item->priv->trigger = atoi (value);
				g_free (value);
			}
		}
		
	}

	return item;
}

EGwItemType
e_gw_item_get_item_type (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_GW_ITEM_TYPE_UNKNOWN);

	return item->priv->item_type;
}

void
e_gw_item_set_item_type (EGwItem *item, EGwItemType new_type)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->item_type = new_type;
}

const char *
e_gw_item_get_container_id (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->container;
}

void
e_gw_item_set_container_id (EGwItem *item, const char *new_id)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->container)
		g_free (item->priv->container);
	item->priv->container = g_strdup (new_id);
}

const char *
e_gw_item_get_icalid (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->icalid;
}

void
e_gw_item_set_icalid (EGwItem *item, const char *new_icalid)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->icalid)
		g_free (item->priv->icalid);
	item->priv->icalid = g_strdup (new_icalid);
}

const char *
e_gw_item_get_id (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->id;
}

void
e_gw_item_set_id (EGwItem *item, const char *new_id)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->id)
		g_free (item->priv->id);
	item->priv->id = g_strdup (new_id);
}

char *
e_gw_item_get_creation_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->creation_date;
}

void
e_gw_item_set_creation_date (EGwItem *item, const char *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->creation_date)
		g_free (item->priv->creation_date);
	item->priv->creation_date = g_strdup (new_date);
}

char *
e_gw_item_get_start_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->start_date;
}

void
e_gw_item_set_start_date (EGwItem *item, const char *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->start_date)
		g_free (item->priv->start_date);
	item->priv->start_date = g_strdup (new_date);
}

char *
e_gw_item_get_end_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->end_date;
}

void
e_gw_item_set_end_date (EGwItem *item, const char *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->end_date)
		g_free (item->priv->end_date);
	item->priv->end_date = g_strdup (new_date);
}

char *
e_gw_item_get_due_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->due_date;
}

void
e_gw_item_set_due_date (EGwItem *item, const char *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->due_date)
		g_free (item->priv->due_date);
	item->priv->due_date = g_strdup (new_date);
}

char *
e_gw_item_get_completed_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->completed_date;
}

void 
e_gw_item_set_completed_date (EGwItem *item, const char *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->completed_date)
		g_free (item->priv->completed_date);
	item->priv->completed_date = g_strdup (new_date);
}

const char *
e_gw_item_get_subject (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->subject;
}

void
e_gw_item_set_subject (EGwItem *item, const char *new_subject)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->subject)
		g_free (item->priv->subject);
	item->priv->subject = g_strdup (new_subject);
}

const char *
e_gw_item_get_message (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->message;
}

void
e_gw_item_set_message (EGwItem *item, const char *new_message)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->message)
		g_free (item->priv->message);
	item->priv->message = g_strdup (new_message);
}

const char *
e_gw_item_get_place (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->place;
}

void
e_gw_item_set_place (EGwItem *item, const char *new_place)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->place)
		g_free (item->priv->place);
	item->priv->place = g_strdup (new_place);
}

const char *
e_gw_item_get_classification (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->classification;
}

void
e_gw_item_set_classification (EGwItem *item, const char *new_class)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->classification)
		g_free (item->priv->classification);
	item->priv->classification = g_strdup (new_class);
}

gboolean
e_gw_item_get_completed (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->completed;
}

void
e_gw_item_set_completed (EGwItem *item, gboolean new_completed)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->completed = new_completed;
}

gboolean 
e_gw_item_get_is_allday_event (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->is_allday_event;
}

void 
e_gw_item_set_is_allday_event (EGwItem *item, gboolean allday_event)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->is_allday_event = allday_event;
}

const char *
e_gw_item_get_accept_level (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->accept_level;
}

void
e_gw_item_set_accept_level (EGwItem *item, const char *new_level)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->accept_level)
		g_free (item->priv->accept_level);
	item->priv->accept_level = g_strdup (new_level);
}

const char *
e_gw_item_get_priority (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->priority;
}

void
e_gw_item_set_priority (EGwItem *item, const char *new_priority)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->priority)
		g_free (item->priv->priority);
	item->priv->priority = g_strdup (new_priority);
}

const char *
e_gw_item_get_task_priority (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->task_priority;
}

void
e_gw_item_set_task_priority (EGwItem *item, const char *new_priority)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->task_priority)
		g_free (item->priv->task_priority);
	item->priv->task_priority = g_strdup (new_priority);
}
GSList *
e_gw_item_get_recipient_list (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->recipient_list;
}

void
e_gw_item_set_recipient_list (EGwItem  *item, GSList *new_recipient_list)
{
	/* free old list and set a new one*/
	g_slist_foreach (item->priv->recipient_list, (GFunc) free_recipient, NULL);
	g_slist_free (item->priv->recipient_list);
	item->priv->recipient_list = new_recipient_list;
}

EGwItemOrganizer *
e_gw_item_get_organizer (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->organizer;
}

void
e_gw_item_set_attach_id_list (EGwItem *item, GSList *attach_list)
{
	g_return_if_fail (E_IS_GW_ITEM (item)) ;
	if (attach_list) {
		g_slist_foreach (item->priv->attach_list, (GFunc)free_attach, NULL) ;
		g_slist_free (item->priv->attach_list) ;
	}
	item->priv->attach_list = attach_list ;
}

GSList *
e_gw_item_get_attach_id_list (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL) ;
	return item->priv->attach_list ;
}

void
e_gw_item_set_organizer (EGwItem  *item, EGwItemOrganizer *organizer)
{
	/* free organizer */ 
	g_free (item->priv->organizer);
	item->priv->organizer = organizer;
}

GSList *
e_gw_item_get_recurrence_dates (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->recurrence_dates;
}

void
e_gw_item_set_recurrence_dates (EGwItem  *item, GSList *new_recurrence_dates)
{
	/* free old list and set a new one*/
	g_slist_foreach (item->priv->recurrence_dates, free_string, NULL);
	/*free the list */
	g_slist_free (item->priv->recurrence_dates);
	item->priv->recurrence_dates = new_recurrence_dates;
}

int
e_gw_item_get_trigger (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), 0);

	return item->priv->trigger;
}

void
e_gw_item_set_trigger (EGwItem *item, int trigger)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->trigger = trigger;
}


void 
e_gw_item_set_to (EGwItem *item, const char *to)
{
	g_return_if_fail (E_IS_GW_ITEM (item));
	item->priv->to = g_strdup (to) ; 
}

const char *
e_gw_item_get_to (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM(item), NULL) ;
	return item->priv->to ;
}

const char *
e_gw_item_get_msg_content_type (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL) ;
	return item->priv->content_type ;
}

void
e_gw_item_set_sendoptions (EGwItem *item, gboolean set)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->set_sendoptions = set;
}

void
e_gw_item_set_reply_request (EGwItem *item, gboolean set)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->reply_request_set = set;
}

gboolean
e_gw_item_get_reply_request (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->reply_request_set;
}

void
e_gw_item_set_reply_within (EGwItem *item, char *reply_within)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->reply_within = g_strdup (reply_within);
}

char *
e_gw_item_get_reply_within (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->reply_within;
}

void
e_gw_item_set_track_info (EGwItem *item, EGwItemTrack track_info)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->track_info = track_info;
}

EGwItemTrack
e_gw_item_get_track_info (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_GW_ITEM_NONE);

	return item->priv->track_info;
}


void
e_gw_item_set_autodelete (EGwItem *item, gboolean set)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->autodelete = set;
}

gboolean
e_gw_item_get_autodelete (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->autodelete;
}

void 
e_gw_item_set_notify_completed (EGwItem *item, EGwItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_completed = notify;
}

EGwItemReturnNotify 
e_gw_item_get_notify_completed (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_completed;
}

void 
e_gw_item_set_notify_accepted (EGwItem *item, EGwItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_accepted = notify;
}

EGwItemReturnNotify 
e_gw_item_get_notify_accepted (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_accepted;
}

void 
e_gw_item_set_notify_declined (EGwItem *item, EGwItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_declined = notify;
}

EGwItemReturnNotify 
e_gw_item_get_notify_declined (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_declined;
}

void 
e_gw_item_set_notify_opened (EGwItem *item, EGwItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_opened = notify;
}

EGwItemReturnNotify 
e_gw_item_get_notify_opened (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_opened;
}

void 
e_gw_item_set_notify_deleted (EGwItem *item, EGwItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_deleted = notify;
}

EGwItemReturnNotify 
e_gw_item_get_notify_deleted (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_deleted;
}

void
e_gw_item_set_expires (EGwItem *item, char *expires)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->expires = g_strdup (expires);
}

char *
e_gw_item_get_expires (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->expires;
}

void
e_gw_item_set_delay_until (EGwItem *item, char *delay_until)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->delay_until = g_strdup (delay_until);
}

char *
e_gw_item_get_delay_until (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->delay_until;
}

static void
add_return_notification (SoupSoapMessage *msg, char *option, EGwItemReturnNotify value)
{
	soup_soap_message_start_element (msg, option, NULL, NULL);

	switch (value) {
		case E_GW_ITEM_NOTIFY_MAIL:
			e_gw_message_write_string_parameter (msg, "mail", NULL, "1");
			break;
		case E_GW_ITEM_NOTIFY_NONE:
			e_gw_message_write_string_parameter (msg, "mail", NULL, "0");
	}
	
	soup_soap_message_end_element (msg);	
}

static void 
append_gw_item_options (SoupSoapMessage *msg, EGwItem *item) 
{
	EGwItemPrivate *priv;

	priv = item->priv;
	
	soup_soap_message_start_element (msg, "options", NULL, NULL);
	
	/* Priority */
	e_gw_message_write_string_parameter (msg, "priority", NULL, priv->priority ? priv->priority : "");

	/* Expiration date */
	e_gw_message_write_string_parameter (msg, "expires", NULL, priv->expires ? priv->expires : "");
	
	/* Delay delivery */
	e_gw_message_write_string_parameter (msg, "delayDeliveryUntil", NULL, priv->delay_until ? priv->delay_until : "");

	soup_soap_message_end_element (msg);
}

static void
add_distribution_to_soap_message (EGwItem *item, SoupSoapMessage *msg)
{
	GSList *rl;
	EGwItemPrivate *priv;
	EGwItemOrganizer *organizer;
	GSList *recipient_list;

	priv = item->priv;
	organizer = priv->organizer;
	recipient_list = priv->recipient_list;
	
	/* start distribution element */
	soup_soap_message_start_element (msg, "distribution", NULL, NULL);
	if (organizer) {
		soup_soap_message_start_element (msg, "from", NULL, NULL);
		e_gw_message_write_string_parameter (msg, "displayName", NULL,
				organizer->display_name ? organizer->display_name : "");
		e_gw_message_write_string_parameter (msg, "email", NULL, 
				organizer->email ? organizer->email : "");
		
		soup_soap_message_end_element (msg);
	}
	/* start recipients */
	soup_soap_message_start_element (msg, "recipients", NULL, NULL);
	/* add each recipient */
	for (rl = recipient_list; rl != NULL; rl = rl->next) {
		char *dist_type;
		char *status;

		EGwItemRecipient *recipient = (EGwItemRecipient *) rl->data;
		
		soup_soap_message_start_element (msg, "recipient", NULL, NULL);
		e_gw_message_write_string_parameter (msg, "displayName", NULL, recipient->display_name ? recipient->display_name : "");
		e_gw_message_write_string_parameter (msg, "email", NULL, recipient->email ? recipient->email : "");
		if (recipient->type == E_GW_ITEM_RECIPIENT_TO)
			dist_type = "TO";
		else if (recipient->type == E_GW_ITEM_RECIPIENT_CC)
			dist_type = "CC";
		else 
			dist_type ="";
		e_gw_message_write_string_parameter (msg, "distType", NULL, dist_type);
		/* add recip_status */
		if (recipient->status == E_GW_ITEM_STAT_ACCEPTED)
			status = "accepted";
		else if (recipient->status == E_GW_ITEM_STAT_DECLINED)
			status = "declined";
		else
			status = "";
		e_gw_message_write_string_parameter (msg, "recipientStatus", NULL, status);
		
		soup_soap_message_end_element (msg);		
	}
	
	soup_soap_message_end_element (msg);
	
	if (priv->set_sendoptions) {	
		soup_soap_message_start_element (msg, "sendoptions", NULL, NULL);
		
		if (priv->reply_request_set) {
			
			soup_soap_message_start_element (msg, "requestReply", NULL, NULL);
			
			if (priv->reply_within)
				e_gw_message_write_string_parameter (msg, "withinNDays", NULL, priv->reply_within);
			
			soup_soap_message_end_element (msg);
		}
	
		soup_soap_message_start_element (msg, "statusTracking", NULL, NULL);
		
		soup_soap_message_add_attribute (msg, "autoDelete", priv->autodelete ? "1" : "0", NULL, NULL);
	
		switch (priv->track_info) {
			case E_GW_ITEM_DELIVERED : soup_soap_message_write_string (msg, "Delivered");
				 break;
			case E_GW_ITEM_DELIVERED_OPENED : soup_soap_message_write_string (msg, "DeliveredAndOpened");
				 break;
			case E_GW_ITEM_ALL : soup_soap_message_write_string (msg, "All");
				 break;
			default: soup_soap_message_write_string (msg, "None");
		}
		
		soup_soap_message_end_element (msg);
	
		soup_soap_message_start_element (msg, "notification", NULL, NULL);
		switch (priv->item_type) {
		
		/* TODO Uncomment this after the completed element is available in shemas */
		case E_GW_ITEM_TYPE_TASK :
	//		add_return_notification (msg, "completed", priv->notify_completed);
			
		case E_GW_ITEM_TYPE_APPOINTMENT:
			add_return_notification (msg, "accepted", priv->notify_accepted);
			add_return_notification (msg, "declined", priv->notify_declined);
			add_return_notification (msg, "opened", priv->notify_opened);
			break;
		
		default:
			add_return_notification (msg, "opened", priv->notify_opened);
			add_return_notification (msg, "deleted", priv->notify_deleted);
		}
		soup_soap_message_end_element (msg);

		soup_soap_message_end_element (msg);
	}

	soup_soap_message_end_element (msg);
}

static void
add_attachment_to_soap_message(EGwItemAttachment *attachment, SoupSoapMessage *msg)
{
	char *size ;
	soup_soap_message_start_element (msg, "attachment", NULL, NULL) ;

	/*id*/
	if (attachment->id)
		e_gw_message_write_string_parameter (msg, "id", NULL, attachment->id) ;
	else
		e_gw_message_write_string_parameter (msg, "id", NULL, "") ;
	/*name*/
	e_gw_message_write_string_parameter (msg, "name", NULL, attachment->name) ;
	/*content type*/
	e_gw_message_write_string_parameter (msg, "contentType", NULL, attachment->contentType) ;
	/*size*/
	size = g_strdup_printf ("%d", attachment->size) ;
	e_gw_message_write_string_parameter (msg, "size", NULL, size) ;
	g_free (size) ;
	/*date*/
	if (attachment->date) 
		e_gw_message_write_string_parameter (msg, "date", NULL, attachment->date) ;	
	else
		e_gw_message_write_string_parameter (msg, "date", NULL, "") ;	

	/*data*/
	soup_soap_message_start_element (msg, "data", NULL, NULL) ;
	soup_soap_message_add_attribute (msg, "contentId", attachment->id, NULL, NULL);
	soup_soap_message_add_attribute (msg, "contentType", attachment->contentType, NULL, NULL) ;
	soup_soap_message_add_attribute (msg, "length", attachment->size, NULL, NULL) ;
	soup_soap_message_write_string (msg, attachment->data) ;
	soup_soap_message_end_element (msg) ;
	
	soup_soap_message_end_element (msg) ;
}

static void 
e_gw_item_set_calendar_item_elements (EGwItem *item, SoupSoapMessage *msg)
{
	EGwItemPrivate *priv = item->priv;
	char *dtstring;

	if (priv->id)
		e_gw_message_write_string_parameter (msg, "id", NULL, priv->id);
	if (priv->container)
		e_gw_message_write_string_parameter (msg, "container", NULL, priv->container);

	if (priv->classification)
		e_gw_message_write_string_parameter (msg, "class", NULL, priv->classification);
	else
		e_gw_message_write_string_parameter (msg, "class", NULL, "");

	e_gw_message_write_string_parameter (msg, "subject", NULL, priv->subject ? priv->subject : "");

	if (priv->recipient_list != NULL) {
		add_distribution_to_soap_message (item, msg);
		if (priv->set_sendoptions)
			append_gw_item_options (msg, item);
	}

	soup_soap_message_start_element (msg, "message", NULL, NULL);
	if (priv->message) {
		char *str;
		
		str = soup_base64_encode (priv->message, strlen (priv->message));
		dtstring = g_strdup_printf ("%d", strlen (str));
		soup_soap_message_add_attribute (msg, "length", dtstring, NULL, NULL);
		g_free (dtstring);
		soup_soap_message_write_string (msg, str);
		g_free (str);
	} else {
		soup_soap_message_add_attribute (msg, "length", "0", NULL, NULL);
		soup_soap_message_write_string (msg, "");
	}

	soup_soap_message_end_element (msg);

	if (priv->start_date) {
		e_gw_message_write_string_parameter (msg, "startDate", NULL, 
				priv->start_date);
	}

	if (priv->category_list) {
			soup_soap_message_start_element (msg, "categories", NULL, NULL);
			
			if (priv->category_list && priv->category_list->data) 
				soup_soap_message_add_attribute (msg, "types:primary", priv->category_list->data, NULL, NULL);
			
			for (; priv->category_list != NULL; priv->category_list = g_list_next (priv->category_list)) 
				if (priv->category_list->data) {
					e_gw_message_write_string_parameter (msg, "category", NULL, priv->category_list->data);
				}
			soup_soap_message_end_element (msg);
		}
	
	/* handle recurrences */
	if (item->priv->recurrence_dates) {
		GSList *date;
		soup_soap_message_start_element (msg, "rdate", NULL, NULL);
		for (date = item->priv->recurrence_dates; date != NULL; date = g_slist_next (date)) {
			e_gw_message_write_string_parameter (msg, "date", NULL, (char *) date->data);
		}
		soup_soap_message_end_element (msg);
	}
	else {
		/*the icalid is fed to the server only if we are not saving
		 * recurring items */
		e_gw_message_write_string_parameter (msg, "iCalId", NULL, priv->icalid ? priv->icalid : "");
	}
}

gboolean
e_gw_item_append_to_soap_message (EGwItem *item, SoupSoapMessage *msg)
{
	EGwItemPrivate *priv;
	char *alarm;
	GSList *attach_list = NULL ;

	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), FALSE);

	priv = item->priv;

	soup_soap_message_start_element (msg, "item", "types", NULL);

	switch (priv->item_type) {
	case E_GW_ITEM_TYPE_MAIL :
		soup_soap_message_add_attribute (msg, "type", "Mail", "xsi", NULL);

		/*The subject*/
		if (priv->subject)
			e_gw_message_write_string_parameter (msg, "subject", NULL, priv->subject) ;
		/*distribution*/
		add_distribution_to_soap_message(item, msg) ;
		
		if (priv->set_sendoptions) {
			/* item options */
			append_gw_item_options (msg, item);
		}
		
		/*message*/
		soup_soap_message_start_element (msg, "message", NULL, NULL);
		if (priv->message) {
			char *str ;
			char *str_len ;

			str = soup_base64_encode (priv->message, strlen (priv->message));
			str_len = g_strdup_printf ("%d", strlen (str));
			soup_soap_message_add_attribute (msg, "length", str_len, NULL, NULL);
			g_free (str_len);
			soup_soap_message_write_string (msg, str);
			g_free (str);
		} else {
			soup_soap_message_add_attribute (msg, "length", "0", NULL, NULL);
			soup_soap_message_write_string (msg, "");
		}

		soup_soap_message_end_element (msg);

		/*attachments*/
		soup_soap_message_start_element (msg, "attachments", NULL, NULL) ;
		attach_list = e_gw_item_get_attach_id_list (item) ;
		if (attach_list) {
			GSList *al ;
			for (al = attach_list ; al != NULL ;  al = al->next) {
				EGwItemAttachment *attachment = (EGwItemAttachment *)al->data ;
				add_attachment_to_soap_message (attachment, msg) ;
			}
		}
		soup_soap_message_end_element (msg) ;

		break;

	case E_GW_ITEM_TYPE_APPOINTMENT :
		soup_soap_message_add_attribute (msg, "type", "Appointment", "xsi", NULL);

		/* Calendar Item properties. */
		e_gw_item_set_calendar_item_elements (item, msg);

		/* Appointment specific properties */
		if (priv->end_date) {
			e_gw_message_write_string_parameter (msg, "endDate", NULL, priv->end_date);
		} else
			e_gw_message_write_string_parameter (msg, "endDate", NULL, "");

		e_gw_message_write_string_parameter (msg, "acceptLevel", NULL, priv->accept_level ? priv->accept_level : "");
		if (priv->is_allday_event)
			e_gw_message_write_string_parameter (msg, "allDayEvent", NULL, "1");
		else
			e_gw_message_write_string_parameter (msg, "allDayEvent", NULL ,"0");


		if (priv->trigger != 0) {
			alarm = g_strdup_printf ("%d", priv->trigger);
			e_gw_message_write_string_parameter_with_attribute (msg, "alarm", NULL, alarm, "enabled", "true");
			g_free (alarm);
		}
		e_gw_message_write_string_parameter (msg, "place", NULL, priv->place ? priv->place : "");
		
		break;

	case E_GW_ITEM_TYPE_TASK :
		soup_soap_message_add_attribute (msg, "type", "Task", "xsi", NULL);

		/* Calendar Item properties. */
		e_gw_item_set_calendar_item_elements (item, msg);

		/* Task specific properties */
		if (priv->due_date) {
			e_gw_message_write_string_parameter (msg, "dueDate", NULL, priv->due_date);
		} else
			e_gw_message_write_string_parameter (msg, "dueDate", NULL, "");

		e_gw_message_write_string_parameter (msg, "taskPriority", NULL, priv->task_priority ? priv->task_priority : "");
		if (priv->completed)
			e_gw_message_write_string_parameter (msg, "completed", NULL, "1");
		else
			e_gw_message_write_string_parameter (msg, "completed", NULL, "0");

		break;
	case E_GW_ITEM_TYPE_CONTACT :
		soup_soap_message_add_attribute (msg, "type", "Contact", "xsi", NULL);
		append_contact_fields_to_soap_message (item, msg);
		soup_soap_message_end_element(msg);
		return TRUE;
        case E_GW_ITEM_TYPE_GROUP :
		soup_soap_message_add_attribute (msg, "type", "Group", "xsi", NULL);
		append_group_fields_to_soap_message (item, msg);
		soup_soap_message_end_element(msg); 
		return TRUE;
	case E_GW_ITEM_TYPE_ORGANISATION :
		soup_soap_message_add_attribute (msg, "type", "Organization", "xsi", NULL); 
		append_contact_fields_to_soap_message (item, msg);
		soup_soap_message_end_element(msg); 
		return TRUE;
	case E_GW_ITEM_TYPE_CATEGORY :
		soup_soap_message_add_attribute (msg, "type", "Category", "xsi", NULL);
		e_gw_message_write_string_parameter (msg, "name", NULL, item->priv->category_name);
		soup_soap_message_end_element(msg); 
		return TRUE;
	default :
		g_warning (G_STRLOC ": Unknown type for item");
		return FALSE;
	}

	soup_soap_message_end_element (msg);

	return TRUE;
}


static void
append_contact_changes_to_soap_message (EGwItem *item, SoupSoapMessage *msg, int change_type)
{
	GHashTable *changes;
	EGwItemPrivate *priv;
	FullName *full_name;
	char *value;
	GList *list;
	PostalAddress *postal_address;

	priv = item->priv;
	changes = NULL;
	switch (change_type) {
	case E_GW_ITEM_CHANGE_TYPE_ADD :
		changes = priv->additions;
		soup_soap_message_start_element (msg, "add", NULL, NULL);
		break;
	case E_GW_ITEM_CHANGE_TYPE_UPDATE :
		changes = priv->updates;
		soup_soap_message_start_element (msg, "update", NULL, NULL);
		break;
	case E_GW_ITEM_CHANGE_TYPE_DELETE :
		soup_soap_message_start_element (msg, "delete", NULL, NULL);
		changes = priv->deletions;
		break;
	
	}
	if (!changes)
		return;
	list = g_hash_table_lookup (changes, "categories");
	append_common_addressbook_item_fields_to_soap_message (changes, list, msg);
	full_name = g_hash_table_lookup (changes, "full_name");
	value = g_hash_table_lookup (changes, "name");
	if (full_name) 
		append_full_name_to_soap_message (full_name, value, msg);
	list = g_hash_table_lookup (changes, "email");
	if (list)
		append_email_list_soap_message (list, msg);
	list = g_hash_table_lookup (changes, "ims");
	if (list)
		append_im_list_to_soap_message (list, msg);
	append_phone_list_to_soap_message (changes, msg);

	soup_soap_message_start_element (msg, "addressList", NULL, NULL);
	postal_address = g_hash_table_lookup (changes, "Home");
	if (postal_address)
		append_postal_address_to_soap_message (msg, postal_address, "Home");
	postal_address = g_hash_table_lookup (changes, "Office");
	if (postal_address)
		append_postal_address_to_soap_message (msg, postal_address, "Office");
	soup_soap_message_end_element (msg);
	
	append_office_info_to_soap_message (changes, msg);
	append_personal_info_to_soap_message (changes, msg);

	soup_soap_message_end_element (msg);

}

static void
append_event_changes_to_soap_message (EGwItem *item, SoupSoapMessage *msg, int change_type)
{
	GHashTable *changes;
	EGwItemPrivate *priv;

	priv = item->priv;
	changes = NULL;
	switch (change_type) {
	case E_GW_ITEM_CHANGE_TYPE_ADD :
		changes = priv->additions;
		if (!changes)
			return;
		soup_soap_message_start_element (msg, "add", NULL, NULL);
		break;
	case E_GW_ITEM_CHANGE_TYPE_UPDATE :
		changes = priv->updates;
		if (!changes)
			return;
		soup_soap_message_start_element (msg, "update", NULL, NULL);
		break;
	case E_GW_ITEM_CHANGE_TYPE_DELETE :
		changes = priv->deletions;
		if (!changes)
			return;
		soup_soap_message_start_element (msg, "delete", NULL, NULL);
		break;
	}
	if (g_hash_table_lookup (changes, "categories")){
		GList *list;	
		list = g_hash_table_lookup (changes, "categories");

		soup_soap_message_start_element (msg, "categories", NULL, NULL);
		if (list != NULL && list->data) 
			soup_soap_message_add_attribute (msg, "types:primary",list->data, NULL, NULL);
			for (; list != NULL; list = g_list_next (list)) 
				if (list->data) {
					e_gw_message_write_string_parameter (msg, "category", NULL, list->data);
			}
		soup_soap_message_end_element (msg);
		g_list_free (list);
	}
	if (g_hash_table_lookup (changes, "subject"))
		e_gw_message_write_string_parameter (msg, "subject", NULL, priv->subject ? priv->subject : "");
	if (g_hash_table_lookup (changes, "start_date")) {
		if (priv->start_date) {
			e_gw_message_write_string_parameter (msg, "startDate", NULL, priv->start_date); 
		}
	}
	if (g_hash_table_lookup (changes, "end_date")) {
		if (priv->end_date) {
			e_gw_message_write_string_parameter (msg, "endDate", NULL, priv->end_date);
		}
	}
	if (g_hash_table_lookup (changes, "allDayEvent"))
		e_gw_message_write_string_parameter (msg, "allDayEvent", NULL, priv->is_allday_event ? "1" : "0");
	if (g_hash_table_lookup (changes, "message")) {
		soup_soap_message_start_element (msg, "message", NULL, NULL);
		if (priv->message) {
			char *str, *message;

			str = soup_base64_encode (priv->message, strlen (priv->message));
			message = g_strdup_printf ("%d", strlen (str));
			soup_soap_message_add_attribute (msg, "length", message, NULL, NULL);
			g_free (message);
			soup_soap_message_write_string (msg, str);
			g_free (str);
		} else {
			soup_soap_message_add_attribute (msg, "length", "0", NULL, NULL);
			soup_soap_message_write_string (msg, "");
		}

		soup_soap_message_end_element (msg);
	}
	if (g_hash_table_lookup (changes, "classification"))
		e_gw_message_write_string_parameter (msg, "class", NULL, priv->classification);
	if (g_hash_table_lookup (changes, "task_priority")) {
		e_gw_message_write_string_parameter (msg, "taskPriority", NULL, priv->task_priority);
	}
	if (g_hash_table_lookup (changes, "accept_level"))
		e_gw_message_write_string_parameter (msg, "acceptLevel", NULL, priv->accept_level ? priv->accept_level : "");
	if (g_hash_table_lookup (changes, "place"))
		e_gw_message_write_string_parameter (msg, "place", NULL, priv->place ? priv->place : "");
	if (g_hash_table_lookup (changes, "alarm")) {
		if (priv->trigger != 0) {
			char *alarm = g_strdup_printf ("%d", priv->trigger);
			e_gw_message_write_string_parameter_with_attribute (msg, "alarm", NULL, alarm, "enabled", "true");
			g_free (alarm);
		}
		else
			e_gw_message_write_string_parameter_with_attribute (msg, "alarm", NULL, "0", "enabled", "false");
	}
	if (g_hash_table_lookup (changes, "completed"))
		e_gw_message_write_string_parameter (msg, "completed", NULL, priv->completed ? "1" : "0");
	if (g_hash_table_lookup (changes, "due_date"))
		e_gw_message_write_string_parameter (msg, "dueDate", NULL, priv->due_date);
	soup_soap_message_end_element (msg);

}

gboolean 
e_gw_item_append_changes_to_soap_message (EGwItem *item, SoupSoapMessage *msg)
{
	EGwItemPrivate *priv;
	char *value;
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), FALSE);

	priv = item->priv;

	soup_soap_message_start_element (msg, "updates", NULL, NULL);

	switch (priv->item_type) {
	case E_GW_ITEM_TYPE_CONTACT :
	case E_GW_ITEM_TYPE_ORGANISATION :
		append_contact_changes_to_soap_message (item, msg, E_GW_ITEM_CHANGE_TYPE_ADD);
		append_contact_changes_to_soap_message (item, msg, E_GW_ITEM_CHANGE_TYPE_UPDATE);
		append_contact_changes_to_soap_message (item, msg, E_GW_ITEM_CHANGE_TYPE_DELETE);
		soup_soap_message_end_element(msg); 
		return TRUE;
        case E_GW_ITEM_TYPE_GROUP :
		soup_soap_message_start_element (msg, "update", NULL, NULL);
		value = g_hash_table_lookup (item->priv->simple_fields, "name");
		if (value)
			e_gw_message_write_string_parameter (msg, "name", NULL, value);
		soup_soap_message_end_element (msg);
		soup_soap_message_end_element(msg); 
		return TRUE;
	case E_GW_ITEM_TYPE_APPOINTMENT:
	case E_GW_ITEM_TYPE_TASK :
		append_event_changes_to_soap_message (item, msg, E_GW_ITEM_CHANGE_TYPE_ADD);
		append_event_changes_to_soap_message (item, msg, E_GW_ITEM_CHANGE_TYPE_UPDATE);
		append_event_changes_to_soap_message (item, msg, E_GW_ITEM_CHANGE_TYPE_DELETE);
		soup_soap_message_end_element(msg); 
		return TRUE;
	default :
		g_warning (G_STRLOC ": Unknown type for item");
		return FALSE;
	}

}

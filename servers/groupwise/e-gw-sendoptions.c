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
#include "e-gw-connection.h"
#include "e-gw-sendoptions.h"

struct _EGwSendOptionsPrivate {
	EGwSendOptionsGeneral *gopts;
	EGwSendOptionsStatusTracking *mopts;
	EGwSendOptionsStatusTracking *copts;
	EGwSendOptionsStatusTracking *topts;
};

static GObjectClass *parent_class = NULL;

static gboolean e_gw_sendoptions_store_settings (SoupSoapParameter *param, EGwSendOptions *opts);
static void e_gw_sendoptions_init (GObject *object);
static void e_gw_sendoptions_class_init (GObjectClass *klass);
static void e_gw_sendoptions_dispose (GObject *object);
static void e_gw_sendoptions_finalize (GObject *object);

EGwSendOptionsGeneral*
e_gw_sendoptions_get_general_options (EGwSendOptions *opts) 
{
	g_return_val_if_fail (opts != NULL || E_IS_GW_SENDOPTIONS (opts), NULL);

	return opts->priv->gopts;
}

EGwSendOptionsStatusTracking*
e_gw_sendoptions_get_status_tracking_options (EGwSendOptions *opts, char *type)
{
	g_return_val_if_fail (opts != NULL || E_IS_GW_SENDOPTIONS (opts), NULL);
	g_return_val_if_fail (type != NULL, NULL);

	if (!strcasecmp (type, "mail"))
		return opts->priv->mopts;
	else if (!strcasecmp (type, "calendar"))
		return opts->priv->copts;
	else if (!strcasecmp (type, "task"))
		return opts->priv->topts;
	else
		return NULL;
}

static void
e_gw_sendoptions_dispose (GObject *object)
{
	EGwSendOptions *opts = (EGwSendOptions *) object;

	g_return_if_fail (E_IS_GW_SENDOPTIONS (opts));

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_gw_sendoptions_finalize (GObject *object) 
{
	EGwSendOptions *opts = (EGwSendOptions *) object;
	EGwSendOptionsPrivate *priv;

	g_return_if_fail (E_IS_GW_SENDOPTIONS (opts));

	priv = opts->priv;
	
	if (priv->gopts) {
		g_free (priv->gopts);
		priv->gopts = NULL;
	}

	if (priv->mopts) {
		g_free (priv->mopts);
		priv->mopts = NULL;
	}

	if (priv->copts) {
		g_free (priv->copts);
		priv->copts = NULL;
	}

	if (priv->topts) {
		g_free (priv->topts);
		priv->topts = NULL;
	}

	if (priv) {
		g_free (priv);
		opts->priv = NULL;
	}

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_gw_sendoptions_class_init (GObjectClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_gw_sendoptions_dispose;
	object_class->finalize = e_gw_sendoptions_finalize;
}

static void
e_gw_sendoptions_init (GObject *object)
{		
	EGwSendOptions *opts;
	EGwSendOptionsPrivate *priv;

	opts = E_GW_SENDOPTIONS (object);

	/* allocate internal structure */
	priv = g_new0 (EGwSendOptionsPrivate, 1);
	priv->gopts = g_new0 (EGwSendOptionsGeneral, 1);
	priv->mopts = g_new0 (EGwSendOptionsStatusTracking, 1);
	priv->copts = g_new0 (EGwSendOptionsStatusTracking, 1);
	priv->topts = g_new0 (EGwSendOptionsStatusTracking, 1);
	opts->priv = priv;
}

GType
e_gw_sendoptions_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EGwSendOptionsClass),
                        NULL,
                        NULL,
                        (GClassInitFunc) e_gw_sendoptions_class_init,
                        NULL, NULL,
                        sizeof (EGwSendOptions),
                        0,
                        (GInstanceInitFunc) e_gw_sendoptions_init,
			NULL
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EGwSendOptions", &info, 0);
	}

	return type;
}

static void
parse_status_tracking_options (SoupSoapParameter *group_param, guint i, EGwSendOptionsStatusTracking *sopts)
{
	SoupSoapParameter *subparam, *field_param, *val_param;

	for (subparam = soup_soap_parameter_get_first_child_by_name(group_param, "setting") ;
			     subparam != NULL ;
			     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "setting")) {

		char *field = NULL, *val = NULL;
		field_param = soup_soap_parameter_get_first_child_by_name (subparam, "field");
		val_param = soup_soap_parameter_get_first_child_by_name (subparam, "value");
		
		if (field_param)
			field = soup_soap_parameter_get_string_value (field_param);
		else
			continue;

		if (!g_ascii_strcasecmp (field + i, "StatusInfo")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val) {
				sopts->tracking_enabled = TRUE;
				if (!strcmp (val, "Delivered"))
					sopts->track_when = E_GW_DELIVERED;
				if (!strcmp (val, "DeliveredAndOpened"))
					sopts->track_when = E_GW_DELIVERED_OPENED;
				if (!strcmp (val, "All"))
					sopts->track_when = E_GW_ALL;
				if (!strcmp (val, "None"))
					sopts->tracking_enabled = FALSE;
			} else
				sopts->tracking_enabled = FALSE;
			
		} else if (!g_ascii_strcasecmp (field + i, "ReturnOpen")) {
			val_param = soup_soap_parameter_get_first_child_by_name (val_param, "mail");
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val && !strcmp (val, "1")) {
				sopts->opened = E_GW_RETURN_NOTIFY_MAIL;
			} else
				sopts->opened = E_GW_RETURN_NOTIFY_NONE;
			

		} else if (!g_ascii_strcasecmp (field + i, "ReturnDelete")) {
			val_param = soup_soap_parameter_get_first_child_by_name (val_param, "mail");
			
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val && !strcmp (val, "1")) {
				sopts->declined = E_GW_RETURN_NOTIFY_MAIL;
			} else
				sopts->declined = E_GW_RETURN_NOTIFY_NONE;

		} else if (!g_ascii_strcasecmp (field + i, "ReturnAccept")) {
			val_param = soup_soap_parameter_get_first_child_by_name (val_param, "mail");
			
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val && !strcmp (val, "1")) {
				sopts->accepted = E_GW_RETURN_NOTIFY_MAIL;
			} else
				sopts->accepted = E_GW_RETURN_NOTIFY_NONE;


		} else if (!g_ascii_strcasecmp (field + i, "ReturnCompleted")) {
			val_param = soup_soap_parameter_get_first_child_by_name (val_param, "mail");
			
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val && !strcmp (val, "1")) {
				sopts->completed = E_GW_RETURN_NOTIFY_MAIL;
			} else
				sopts->completed = E_GW_RETURN_NOTIFY_NONE;

		} 			
		g_free (field);
		g_free (val);		
	}	
}

/* These are not actually general Options. These can be configured seperatly for
   each component. Since win32 shows them as general options, we too do the same 
   way. So the Options are take from the mail setttings */
	
static void 
parse_general_options (SoupSoapParameter *group_param, EGwSendOptionsGeneral *gopts) 
{
	SoupSoapParameter *subparam, *field_param, *val_param;

	for (subparam = soup_soap_parameter_get_first_child_by_name(group_param, "setting") ;
			     subparam != NULL ;
			     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "setting")) {
		char *field = NULL, *val = NULL;
		field_param = soup_soap_parameter_get_first_child_by_name (subparam, "field");
		val_param = soup_soap_parameter_get_first_child_by_name (subparam, "value");
		
		if (field_param)
			field = soup_soap_parameter_get_string_value (field_param);
		else
			continue;

		if (!g_ascii_strcasecmp (field, "mailPriority")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val) {
				if (!strcasecmp (val, "High"))
					gopts->priority = E_GW_PRIORITY_HIGH;
				else if (!strcasecmp (val, "Standard")) {
					gopts->priority = E_GW_PRIORITY_STANDARD;
				} else if (!strcasecmp (val, "Low"))
					gopts->priority = E_GW_PRIORITY_LOW;
				else
					gopts->priority = E_GW_PRIORITY_UNDEFINED;
					
			} else
				gopts->priority = E_GW_PRIORITY_UNDEFINED;
		} else if (!g_ascii_strcasecmp (field, "mailReplyRequested")) {
		       if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

	       		if (val) {
		 		/* TODO What will be the soap response when the val is not None */
				if (!strcasecmp (val, "None"))
					gopts->reply_enabled = FALSE;
				else {
					gint i = atoi (val);							
					if (i < 0)
						gopts->reply_convenient = TRUE;
					else
						gopts->reply_within = i;

					gopts->reply_enabled = TRUE;		
				}
			}
		} else if (! g_ascii_strcasecmp (field, "mailExpireDays")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val) {
				gopts->expiration_enabled = TRUE;
				gopts->expire_after = atoi (val);
			} else
				gopts->expiration_enabled = FALSE;
		}
		g_free (field);
		g_free (val);
	}
}

/* These settings are common to all components */

static void
parse_advanced_settings (SoupSoapParameter *group_param, EGwSendOptionsGeneral *gopts) 
{
	SoupSoapParameter *subparam, *field_param, *val_param;

	for (subparam = soup_soap_parameter_get_first_child_by_name(group_param, "setting") ;
			     subparam != NULL ;
			     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "setting")) {
		char *field = NULL, *val = NULL;
		field_param = soup_soap_parameter_get_first_child_by_name (subparam, "field");
		val_param = soup_soap_parameter_get_first_child_by_name (subparam, "value");
		
		if (field_param)
			field = soup_soap_parameter_get_string_value (field_param);
		else
			continue;

		if (!g_ascii_strcasecmp (field, "delayDelivery")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);
			if (val) {
				gint i = atoi (val);
				if (i > 0 ) {
					gopts->delay_enabled = TRUE;
					gopts->delay_until = i;
				} else 
					gopts->delay_enabled = FALSE;
			} else
				gopts->delay_enabled = FALSE;
		}
	}
}

/* TODO have to handle the locked settings */
static gboolean
e_gw_sendoptions_store_settings (SoupSoapParameter *param, EGwSendOptions *opts)
{
	SoupSoapParameter *group_param;
	EGwSendOptionsPrivate *priv;

	priv = opts->priv;
	
	for (group_param = soup_soap_parameter_get_first_child_by_name(param, "group") ;
			     group_param != NULL ;
			     group_param = soup_soap_parameter_get_next_child_by_name (group_param, "group")) {
		char *temp = NULL;

		temp = soup_soap_parameter_get_property (group_param, "type");
		
	        if (!temp) 
			continue;

		if (!g_ascii_strcasecmp (temp, "MailMessageSettings")) {
			parse_status_tracking_options (group_param, 4, priv->mopts);
			parse_general_options (group_param, priv->gopts);
		}	
	  	
		temp = soup_soap_parameter_get_property (group_param, "AppointmentMessageSettings");
	        if (!g_ascii_strcasecmp (temp, "AppointmentMessageSettings")) { 
			parse_status_tracking_options (group_param, 11, priv->copts);
		}
	        if (!g_ascii_strcasecmp (temp, "TaskMessageSettings")) 
			parse_status_tracking_options (group_param, 4, priv->topts);

	        if (!g_ascii_strcasecmp (temp, "AdvancedSettings")) 
			parse_advanced_settings (group_param, priv->gopts);

		g_free (temp);
	}
	return TRUE;
}

EGwSendOptions *
e_gw_sendoptions_new_from_soap_parameter (SoupSoapParameter *param) 
{
	EGwSendOptions *opts;
	
	g_return_val_if_fail (param != NULL, NULL);

	opts = g_object_new (E_TYPE_GW_SENDOPTIONS, NULL);

	if (!e_gw_sendoptions_store_settings (param, opts)){
		g_object_unref (opts);
		return NULL;
	}
	
	return opts;
}



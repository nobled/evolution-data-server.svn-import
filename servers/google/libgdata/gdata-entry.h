/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *  Jason Willis <zenbrother@gmail.com>
 *
 * Copyright 2007, Novell, Inc.
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
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, 
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef _GDATA_ENTRY_H_
#define _GDATA_ENTRY_H_

#include <glib.h>
#include <glib-object.h>


/* LibXML2 includes */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>  
#include <libxml/xpathInternals.h>


G_BEGIN_DECLS


#define GDATA_TYPE_ENTRY           (gdata_entry_get_type())
#define GDATA_ENTRY(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), GDATA_TYPE_ENTRY, GDataEntry))
#define GDATA_ENTRY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GDATA_TYPE_ENTRY, GDataEntryClass))
#define GDATA_IS_ENTRY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj), GDATA_TYPE_ENTRY))
#define GDATA_IS_ENTRY_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE((klass), GDATA_TYPE_ENTRY))
#define GDATA_ENTRY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GDATA_TYPE_ENTRY, GDataEntryClass))

typedef struct _GDataEntry GDataEntry;
typedef struct _GDataEntryClass GDataEntryClass;
typedef struct _GDataEntryPrivate GDataEntryPrivate;


struct _GDataEntry {

  GObject parent;

  /* private */
  GDataEntryPrivate *priv;
};

struct _GDataEntryClass {
  
  GObjectClass parent_class;
  /* class members */

};

/* Should be moved under extensions */
typedef struct _Attendee Attendee;
struct _Attendee {
	gchar *attendee_email;
	gchar *attendee_rel;
	gchar *attendee_value;
	gchar *attendee_status;
	gchar *attendee_type;
};

GType gdata_entry_get_type(void);


/***** API  *****/

GDataEntry * gdata_entry_new(void);

GDataEntryPrivate * gdata_entry_get_private (GDataEntry *entry);

GDataEntry * gdata_entry_new_from_xml(const gchar *entryXML);

GDataEntry * gdata_entry_new_from_xmlptr (xmlDocPtr doc, xmlNodePtr cur);

gchar * gdata_entry_generate_xml (GDataEntry *entry);

gchar * gdata_entry_get_id(GDataEntry *entry);

gchar * gdata_entry_get_content(GDataEntry *entry);

gchar * gdata_entry_get_description (GDataEntry *entry);

gchar * gdata_entry_get_copyright (GDataEntry *entry);

gchar * gdata_entry_get_title (GDataEntry *entry);

GSList * gdata_entry_get_authors (GDataEntry *entry);

GSList * gdata_entry_get_links (GDataEntry *links);

gchar * gdata_entry_get_start_time (GDataEntry *entry);

gchar * gdata_entry_get_end_time (GDataEntry *entry);

gchar * gdata_entry_get_location (GDataEntry *entry);

gchar * gdata_entry_get_status (GDataEntry *entry);

gchar * gdata_entry_get_edit_link (GDataEntry *entry);

GSList * gdata_entry_get_categories (GDataEntry *entry);

gchar * gdata_entry_get_start_date (GDataEntry *entry);

gchar * gdata_entry_get_end_date (GDataEntry *entry);

gchar * 
gdata_entry_get_visibility (GDataEntry *entry);

gchar * 
gdata_entry_get_transparency (GDataEntry *entry);

GSList *
gdata_entry_get_attendee_list (GDataEntry *entry);

void gdata_entry_set_author (GDataEntry *entry, GSList *author);

void gdata_entry_set_categories (GDataEntry *entry, GSList *categories);

void gdata_entry_set_title (GDataEntry *entry, const gchar *title);

void gdata_entry_set_content (GDataEntry *entry, const gchar *content);

void gdata_entry_set_links (GDataEntry *entry, GSList *links);

void gdata_entry_set_status (GDataEntry *entry, const gchar *status);

void gdata_entry_set_send_notification (GDataEntry *entry, const gchar *sendNotification);

void gdata_entry_set_reminder (GDataEntry *entry, const gchar *reminder);

void gdata_entry_set_start_time (GDataEntry *entry, const gchar *start_time);

void gdata_entry_set_end_time (GDataEntry *entry, const gchar *end_time);

void gdata_entry_set_transparency (GDataEntry *entry, const gchar *transparency);

void gdata_entry_set_location (GDataEntry *entry, const gchar *location);

void gdata_entry_create_authors (GDataEntry *entry,const gchar *name, const gchar *email);

void gdata_entry_create_categories (GDataEntry *entry, const gchar *scheme, const gchar *label , const gchar *term);

GSList * gdata_entries_new_from_xml (const gchar *feedXML, const gint length);

void
gdata_entry_set_id (GDataEntry *entry, gchar *id);

gboolean gdata_entry_is_recurrent (GDataEntry *entry);

void
gdata_entry_set_attendee_list (GDataEntry *entry, GSList *attendee);
#endif






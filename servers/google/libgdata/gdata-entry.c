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


#include <config.h>

/* LibXML2 includes */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>  
#include <libxml/xpathInternals.h>

#include <string.h>
#include <gdata-entry.h>

#define GDATA_ENTRY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GDATA_TYPE_ENTRY, GDataEntryPrivate))

struct _GDataEntryAuthor {
	gchar *email;
	gchar *name;
	gchar *uri;
};
typedef struct _GDataEntryAuthor GDataEntryAuthor;

struct _GDataEntryCategory {
	gchar *label;
	gchar *scheme;
	gchar *scheme_prefix;
	gchar *scheme_suffix;
	gchar *term;
};
typedef struct _GDataEntryCategory GDataEntryCategory;

struct _GDataEntryLink {
	gchar *href;
	gint  length;
	gchar *rel;
	gchar *title;
	gchar *type;
};
typedef struct _GDataEntryLink GDataEntryLink;

struct _GDataEntryPrivate {
	GSList *authors;
	GSList *categories;
	GSList *links;
	GHashTable *field_table;
	GSList *attendees;
	gchar *location;
	gchar *content;
        gchar *title;
	gchar *reminder;
	gchar *status;
	gchar *visibility;
	gchar *start_time;
	gchar *end_time;
	gchar *send_notification;
	gchar *transparency;
	gchar *id;

	gboolean entry_needs_update;
	gboolean has_attendees;
	gchar *entry_xml;

	gboolean dispose_has_run;
	gboolean is_recurrent;
};

static void gdata_entry_set_xml (GDataEntry *entry, const gchar *xml);

static void destroy_authors (gpointer data, gpointer user_data)
{
	GDataEntryAuthor *author = (GDataEntryAuthor *)data;
	if (author->email != NULL)
		g_free(author->email);

	if (author->name != NULL)
		g_free(author->name);

	if (author->uri != NULL)
		g_free(author->uri);

	g_free(author);
}

static void destroy_categories (gpointer data, gpointer user_data)
{
	GDataEntryCategory *category = (GDataEntryCategory *)data;
	if (category->label != NULL)
		g_free(category->label);

	if (category->scheme != NULL)
		g_free(category->scheme);

	if (category->scheme_prefix != NULL)
		g_free(category->scheme_prefix);

	if (category->scheme_suffix != NULL)
		g_free(category->scheme_suffix);

	if (category->term != NULL)
		g_free(category->term);

	g_free(category);
}

static void destroy_links (gpointer data, gpointer user_data)
{
	GDataEntryLink *link = (GDataEntryLink *)data;
	if (link->href != NULL)
		g_free(link->href);

	if (link->rel != NULL)
		g_free(link->rel);

	if (link->title != NULL)
		g_free(link->title);

	if (link->type != NULL)
		g_free(link->type);

	g_free(link);
}

enum {
	PROP_0,
};

static void gdata_entry_init (GTypeInstance *instance,
		gpointer      g_class)
{
	GDataEntryPrivate *priv;
	GDataEntry *self = (GDataEntry *)instance;

	/* Private data set by g_type_class_add_private */
	priv = GDATA_ENTRY_GET_PRIVATE(self);
	priv->dispose_has_run = FALSE;
	priv->authors = NULL;
	priv->links = NULL;
	priv->categories = NULL;
	priv->content = NULL;
	priv->title = NULL;
	priv->field_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	priv->entry_needs_update = FALSE;
	priv->entry_xml = NULL;
	priv->is_recurrent = FALSE;
}

static void gdata_entry_dispose (GObject *obj)
{
	GObjectClass *parent_class;
	GDataEntryClass *klass;
	GDataEntry *self = GDATA_ENTRY(obj);
	GDataEntryPrivate *priv = GDATA_ENTRY_GET_PRIVATE(self);

	if (priv->dispose_has_run) {
		/* Don't run dispose twice */
		return;
	}
	priv->dispose_has_run = TRUE;
	/* Chain up to the parent class */
	klass = GDATA_ENTRY_CLASS(g_type_class_peek(GDATA_TYPE_ENTRY));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	parent_class->dispose(obj);
}

static void gdata_entry_finalize (GObject *obj)
{
	GDataEntryPrivate *priv;
	GDataEntry *self = GDATA_ENTRY(obj);
	GObjectClass *parent_class;
	GDataEntryClass *klass;

	priv = GDATA_ENTRY_GET_PRIVATE(self);
	if (priv->authors != NULL) {
		g_slist_foreach(priv->authors, (GFunc) destroy_authors, NULL);
		g_slist_free(priv->authors);
	}

	if (priv->links != NULL) {
		g_slist_foreach(priv->links, (GFunc) destroy_links, NULL);
		g_slist_free(priv->links);
	}

	if (priv->categories != NULL) {
		g_slist_foreach(priv->categories, (GFunc) destroy_categories, NULL);
		g_slist_free(priv->categories);
	}

	if (priv->field_table != NULL)
		g_hash_table_destroy(priv->field_table);

	if (priv->entry_xml != NULL)
		g_free(priv->entry_xml);

	/* Chain up to the parent class */
	klass = GDATA_ENTRY_CLASS(g_type_class_peek(GDATA_TYPE_ENTRY));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	parent_class->finalize(obj);
}

static void gdata_entry_get_property (GObject *obj,
		guint    property_id,
		GValue  *value,
		GParamSpec *pspec)
{
	GDataEntryPrivate *priv;

	priv = GDATA_ENTRY_GET_PRIVATE(obj);
}

static void gdata_entry_set_property (GObject *obj,
		guint    property_id,
		const GValue *value,
		GParamSpec   *pspec)
{
	GDataEntryPrivate *priv;
	GDataEntry *self = (GDataEntry *) obj;

	priv = GDATA_ENTRY_GET_PRIVATE(self);
}

static void gdata_entry_class_init (gpointer g_class,
		gpointer g_class_data)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
	GDataEntryClass *klass = GDATA_ENTRY_CLASS(g_class);

	g_type_class_add_private(klass, sizeof (GDataEntryPrivate));
	gobject_class->set_property = gdata_entry_set_property;
	gobject_class->get_property = gdata_entry_get_property;
	gobject_class->dispose  = gdata_entry_dispose;
	gobject_class->finalize = gdata_entry_finalize;
}

GType gdata_entry_get_type (void)
{
	static GType type = 0;
	if (type == 0) {
		static const GTypeInfo info = {
			sizeof (GDataEntryClass),
			NULL, /* base init */
			NULL, /* base finalize */
			gdata_entry_class_init, /* class init */
			NULL, /* class finalize */
			NULL, /* class data */
			sizeof (GDataEntry),
			0, /* n_preallocs */
			gdata_entry_init /* instance init */
		};
		type = g_type_register_static(G_TYPE_OBJECT,"GDataEntryType", &info,0);
	}

	return type;
}


/*** API ***/
static GDataEntryAuthor *
xmlnode_to_author (xmlDocPtr doc, xmlNodePtr cur) 
{
	GDataEntryAuthor *author;
	xmlChar *value;

	author = g_new0(GDataEntryAuthor, 1);
	author->email = NULL;
	author->name  = NULL;
	author->uri   = NULL;

	cur = cur->xmlChildrenNode;
	while (cur != NULL) {
		if (!xmlStrcmp(cur->name, (xmlChar *)"email")) {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			author->email = g_strdup((gchar *)value);
			xmlFree(value);
		}

		if (!xmlStrcmp(cur->name, (xmlChar *)"name")) {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			author->name = g_strdup((gchar *)value);
			xmlFree(value);
		}

		if (!xmlStrcmp(cur->name, (xmlChar *)"uri")) {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			author->uri = g_strdup((gchar *)value);
			xmlFree(value);
		}
		cur = cur->next;
	}

	return author;
}

static GDataEntryLink *
xmlnode_to_link (xmlDocPtr doc, xmlNodePtr cur) 
{
	GDataEntryLink *link;
	xmlChar *value;

	link = g_new0(GDataEntryLink, 1);
	link->href = NULL;
	link->rel = NULL;
	link->title = NULL;
	link->type = NULL;

	value = xmlGetProp(cur, (xmlChar *)"href");
		if (value) {
			link->href = g_strdup((gchar *)value);
			xmlFree(value);
		}

	value = xmlGetProp(cur, (xmlChar *)"rel");
		if (value) {
			link->rel = g_strdup((gchar *)value);
			xmlFree(value);
		}

	value = xmlGetProp(cur, (xmlChar *)"title");
		if (value) {
			link->title = g_strdup((gchar *)value);
			xmlFree(value);
		}

	value = xmlGetProp(cur, (xmlChar *)"type");
		if (value) {
			link->type = g_strdup((gchar *)value);
			xmlFree(value);
		}

	return link;
}

static GDataEntryCategory *
xmlnode_to_category (xmlDocPtr doc, xmlNodePtr cur) 
{
	GDataEntryCategory *category;
	xmlChar *value;

	category = g_new0(GDataEntryCategory, 1);
	category->label = NULL;
	category->scheme = NULL;
	category->scheme_prefix = NULL;
	category->scheme_suffix = NULL;
	category->term = NULL;

	value = xmlGetProp(cur, (xmlChar *)"label");
	if (value) {
		category->label = g_strdup((gchar *)value);
		xmlFree(value);
	}

	value = xmlGetProp(cur, (xmlChar *)"scheme");
	if (value) {
		category->scheme = g_strdup((gchar *)value);
		xmlFree(value);
	}

	value = xmlGetProp(cur, (xmlChar *)"term");
	if (value) {
		category->term = g_strdup((gchar *)value);
		xmlFree(value);
	}

	return category;
}

Attendee *
xmlnode_to_attendee (xmlDocPtr doc, xmlNodePtr cur)
{
	Attendee *attendee;
	xmlChar *value;

	attendee = g_new0 (Attendee, 1);
	
	while (cur != NULL) {	
		value = xmlGetProp(cur, (xmlChar *)"email");
		if (value) {
			attendee->attendee_email = g_strdup((gchar *)value);
			xmlFree(value);
		}	
		
		value = xmlGetProp(cur, (xmlChar *)"rel");
	
		if (value) {
			attendee->attendee_rel = g_strdup((gchar *)value);
			xmlFree (value);
		}

		value= xmlGetProp(cur, (xmlChar *)"valueString");
		if (value) {
			attendee->attendee_value = g_strdup((gchar *)value);
			xmlFree (value);
		}
		
		cur = cur->next;
	}	
	return attendee;
}

static xmlNodePtr 
entry_to_xmlnode (GDataEntry *entry)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	gchar *xmlEntry;

	xmlEntry = gdata_entry_generate_xml (entry);

	/* FIXME 3rd argument could carry a better name */
	doc = xmlReadMemory (xmlEntry, strlen(xmlEntry), "feeds.xml", NULL, 0);
	cur = xmlDocGetRootElement (doc);
	
	return cur;
}

static xmlNodePtr 
author_to_xmlnode (GDataEntryAuthor *author) 
{

	xmlNodePtr author_node;

	author_node = xmlNewNode(NULL, (xmlChar *)"author");
	if (author->email)
		xmlNewChild (author_node, NULL, (xmlChar *)"email", (xmlChar *)author->email);

	if (author->name)
		xmlNewChild (author_node, NULL, (xmlChar *)"name", (xmlChar *)author->name);

	return author_node;
}

static xmlNodePtr 
link_to_xmlnode (GDataEntryLink *link)
{
	xmlNodePtr link_node;
	link_node = xmlNewNode(NULL, (xmlChar *)"link");

	if (link->href) {
		xmlSetProp (link_node, (xmlChar *)"href", (xmlChar *)link->href);
	}

	if (link->rel) {
		xmlSetProp (link_node, (xmlChar *)"rel", (xmlChar *)link->rel);
	}

	if (link->title) {
		xmlSetProp (link_node, (xmlChar *)"title", (xmlChar *)link->title);
	}

	if (link->type) {
		xmlSetProp (link_node, (xmlChar *)"type", (xmlChar *)link->type);
	}

	return link_node;
}

static xmlNodePtr 
category_to_xmlnode (GDataEntryCategory *category)
{
	xmlNodePtr category_node;
	category_node = xmlNewNode(NULL, (xmlChar *)"category");

	if (category->label) {
		xmlSetProp (category_node, (xmlChar *)"label", (xmlChar *)category->label);
	}

	if (category->scheme) {
		xmlSetProp (category_node, (xmlChar *)"scheme", (xmlChar *)category->scheme);
	}

	if (category->term) {
		xmlSetProp (category_node, (xmlChar *)"term", (xmlChar *)category->term);
	}

	return category_node;
}

/**
 * gdata_entry_new:
 * Creates a new #GDataEntry.
 **/
GDataEntry * 
gdata_entry_new (void)
{
	return g_object_new(GDATA_TYPE_ENTRY, NULL);
}

/**
 * gdata_entry_get_private:
 * @entry: A #GDataEntry object
 * Returns a #GDataEntryPrivate object
 **/ 
GDataEntryPrivate * 
gdata_entry_get_private (GDataEntry *entry)
{
	GDataEntryPrivate *priv;
	priv = GDATA_ENTRY_GET_PRIVATE (entry);

	return priv;	
}
/**
 * gdata_entry_new_from_xmlptr: 
 * @doc: A xml document pointer
 * @ptr: A xml Node pointer
 **/
GDataEntry * 
gdata_entry_new_from_xmlptr (xmlDocPtr doc, xmlNodePtr cur)
{
	GDataEntry *entry;
	GDataEntryPrivate *priv;
	xmlChar *value = NULL;
	xmlOutputBufferPtr buf;
	gchar *xmlString;

	g_return_val_if_fail(doc != NULL, NULL);
	g_return_val_if_fail(cur != NULL, NULL);

	if (xmlStrcmp(cur->name, (xmlChar *)"entry")) {
		return NULL;
	}

	entry = gdata_entry_new (); 
	priv  = GDATA_ENTRY_GET_PRIVATE(entry);
	buf = xmlAllocOutputBuffer (NULL);

	if (buf == NULL) { 
		xmlString = NULL;
	}
	else {
		xmlNodeDumpOutput (buf, NULL, cur, 0, 1, NULL);
		xmlOutputBufferFlush (buf);

		if (buf->conv == NULL)
			xmlString = g_strdup ((gchar *)buf->buffer->content);
		else	
			xmlString = g_strdup ((gchar *)buf->conv->content);
		xmlOutputBufferClose (buf);
	}

	priv->entry_xml = g_strdup (xmlString);
	cur = cur->xmlChildrenNode;
	while (cur != NULL) {

		if (!xmlStrcmp(cur->name, (xmlChar *)"author")) {
			priv->authors = g_slist_prepend(priv->authors, xmlnode_to_author(doc, cur));
		} 
		else if (!xmlStrcmp(cur->name, (xmlChar *)"link")) {
			priv->links = g_slist_prepend(priv->links, xmlnode_to_link(doc, cur));
		}
		else if (!xmlStrcmp(cur->name, (xmlChar *)"category")) {
			priv->categories = g_slist_prepend(priv->categories, xmlnode_to_category(doc, cur));
		}
		else if (!xmlStrcmp (cur->name, (xmlChar *)"where")) {
			priv->location = (gchar *)xmlGetProp (cur, (xmlChar *)"valueString");	
		}
		else if (!xmlStrcmp (cur->name, (xmlChar *)"eventStatus")) {
			priv->status = (gchar *)xmlGetProp (cur, (xmlChar *)"value");
		}
		else if (!xmlStrcmp (cur->name, (xmlChar *)"visibility")) {
			priv->visibility = (gchar *)xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
		}
		else if (!xmlStrcmp (cur->name, (xmlChar *)"when")) {
			priv->start_time = (gchar *)xmlGetProp (cur, (xmlChar *)"startTime");
			priv->end_time = (gchar *)xmlGetProp (cur, (xmlChar *)"endTime");
		}
		else if (!xmlStrcmp (cur->name, (xmlChar *)"recurrence")) {
			priv->is_recurrent = TRUE;
		}
		
		else if (!xmlStrcmp (cur->name, (xmlChar *)"who")) {
			priv->attendees = g_slist_prepend (priv->attendees, xmlnode_to_attendee (doc, cur));		
			priv->has_attendees = TRUE;
		}
		else if (!xmlStrcmp (cur->name, (xmlChar *)"sendEventNotifications")) {
			priv->send_notification =(gchar *)xmlGetProp (cur, (xmlChar *)"value");
		}
		else if (!xmlStrcmp (cur->name, (xmlChar *)"comments")) {
			/*FIXME Call _comment_to_xml_node */
		}

		else {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			g_hash_table_insert(priv->field_table, g_strdup((gchar *)cur->name), 
					g_strdup((gchar *)value));
			xmlFree(value);
		}
		cur = cur->next;
	}
	
	xmlFree(value);
	return entry;
}

/**
 * gdata_entries_new_from_xml: 
 * @feed_xml: A xml tree.
 * @length: length of feed_xml
 * Returns the list of all the entries in a feed.
 **/
/*Returns all the entries from the feed */
GSList * 
gdata_entries_new_from_xml (const gchar *feed_xml, const gint length)
{
	GSList *list;
	xmlNodePtr cur;
	xmlDocPtr doc;

	list = NULL;

	g_return_val_if_fail(feed_xml != NULL && *feed_xml != '\0', NULL);

	doc = xmlReadMemory (feed_xml, strlen(feed_xml), "feed.xml", NULL, 0);
	if (doc == NULL) 
		return NULL;

	cur = xmlDocGetRootElement (doc);
	if (cur == NULL) {
		xmlFree (doc);
	}
	
	cur = cur->xmlChildrenNode;
	while (cur != NULL) 	
	{
		if (!xmlStrcmp (cur->name, (xmlChar *)"entry")) {
			list = g_slist_prepend (list, gdata_entry_new_from_xmlptr (doc, cur));
		}
		cur = cur->next;
	}

	/* Free them */
	xmlFreeDoc (doc);
	xmlFreeNode (cur);

	if (list == NULL)
		g_slist_free (list);

	return list;
}

/** 
 * gdata_entry_new_from_xml: 
 * @entry_xml: the xml tree
 * Returns a GDataEntry object:
 **/
GDataEntry *
gdata_entry_new_from_xml (const gchar *entry_xml)
{
	GDataEntry *entry = NULL;
	xmlDocPtr doc;
	xmlNodePtr cur;

	g_return_val_if_fail (entry_xml != NULL && *entry_xml != '\0', NULL);	
	doc = xmlReadMemory (entry_xml, strlen(entry_xml), "feed.xml", NULL, 0);

	if (doc == NULL)
		return NULL;

	cur = xmlDocGetRootElement (doc);
	if (cur == NULL) 
		xmlFree (doc);

	while (cur != NULL) {
		if (!xmlStrcmp (cur->name,(xmlChar *)"entry")) 
			entry = gdata_entry_new_from_xmlptr (doc, cur);
		cur = cur->next;
	}
	/*Free them */
	xmlFreeDoc (doc);
	xmlFreeNode (cur);

	if (!GDATA_IS_ENTRY(entry))
		return NULL;

	return entry;
}

/**
 *gdata_entry_generate_xml:
 *@entry: A GDataEntry Object 
 *Returns the xml content:
 **/
gchar * 
gdata_entry_generate_xml (GDataEntry *entry)
{
	GDataEntryPrivate *priv;
	GSList *list;
	xmlChar *xmlString;
	xmlNsPtr ns;
	xmlNodePtr cur, cur_child, root;
	xmlDocPtr doc;
	gint xml_buffer_size;

	g_return_val_if_fail(entry !=NULL, NULL);
	g_return_val_if_fail(GDATA_IS_ENTRY (entry), NULL);

	priv = GDATA_ENTRY_GET_PRIVATE(entry);

	if (!(priv->entry_xml == NULL || priv->entry_needs_update == TRUE))
		return priv->entry_xml;

	/* Construct DOM tree */
	doc = xmlNewDoc ((xmlChar *)"1.0");
	root = xmlNewDocNode (doc, NULL, (xmlChar *)"entry", NULL);

	xmlSetProp (root, (xmlChar *)"xmlns", (xmlChar *)"http://www.w3.org/2005/Atom");
	ns =  xmlNewNs (root, (xmlChar *)"http://schemas.google.com/g/2005", (xmlChar *)"gd");
	xmlDocSetRootElement (doc, root);
	cur = root;

	list = priv->categories;
	while (list) {
		cur_child = category_to_xmlnode(list->data);
		xmlAddChild(root, cur_child);
		list = g_slist_next(list);
	}

	list = priv->authors;
	while (list) {
		cur_child = author_to_xmlnode(list->data);
		xmlAddChild(root, cur_child);
		list = g_slist_next(list);
	}

	list = priv->links;
	while (list) {
		cur_child = link_to_xmlnode(list->data);
		xmlAddChild(root, cur_child);
		list = g_slist_next(list);
	}

	if (priv->status) {
		cur_child = xmlNewChild(root, NULL, (xmlChar *)"eventStatus", NULL);
		xmlSetNs (cur_child, xmlNewNs (cur_child, NULL, (xmlChar *)"gd"));
		xmlSetProp (cur_child, (xmlChar *)"value", (xmlChar *)priv->status);
	}

	if (priv->start_time || priv->end_time || priv->reminder) {
		cur_child = xmlNewChild(root, NULL, (xmlChar *)"when", NULL);
		xmlSetNs (cur_child, xmlNewNs (cur_child, NULL, (xmlChar *)"gd"));
		xmlSetProp (cur_child, (xmlChar *)"startTime", (xmlChar *)priv->start_time);
		xmlSetProp (cur_child, (xmlChar *)"endTime", (xmlChar *)priv->end_time);	

		if (priv->reminder) {		
			cur = cur_child;
			cur_child = xmlNewChild(cur, NULL, (xmlChar *)"reminder", (xmlChar *)"");
			xmlSetNs (cur_child, xmlNewNs (cur_child, NULL, (xmlChar *)"gd"));		  
			xmlSetProp (cur_child->xmlChildrenNode, (xmlChar *)"minutes", (xmlChar *)priv->reminder);
		} 
	}

	if (priv->location ) {
		cur_child = xmlNewChild(root, NULL, (xmlChar *)"where", NULL);
		xmlSetNs (cur_child, xmlNewNs (cur_child, NULL, (xmlChar *)"gd"));
		xmlSetProp (cur_child, (xmlChar *)"valueString", (xmlChar *)priv->location);
	}

	if (priv->content) {
		cur_child = xmlNewChild(root, NULL, (xmlChar *)"content", (xmlChar *)priv->content);
		xmlSetProp (cur_child, (xmlChar *)"type", (xmlChar *)"text");
	}

	if (priv->title) {
		cur_child = xmlNewChild(root, NULL, (xmlChar *)"title", (xmlChar *)priv->title);
		xmlSetProp (cur_child, (xmlChar *)"type", (xmlChar *)"text");
	}

	/*
	   if (priv->field_table) {
	   g_hash_table_foreach (priv->field_table,(GHFunc) _build_hash_table_entries, &root);
	   }
	 */

	xmlDocDumpMemory(doc, &xmlString, &xml_buffer_size);
	priv->entry_xml = g_strdup((gchar *)xmlString);

	xmlFree(xmlString);
	xmlFreeDoc(doc);
 
	return priv->entry_xml;		 
}

/* Builds the DOM tree for the entries stored in hash table */
static void
build_hash_table_entries (gchar *key, gchar *value, xmlNode **cur)
{
	xmlNode *ptr;
	ptr = *cur;

	/* Iterates from a node pointer , till it reaches NULL , to append other nodes */
	while (TRUE) {

		if (ptr->next == NULL) {
			/* FIXME: Will we be needing , these nodes of entries when building them ? */
			if (!g_ascii_strcasecmp (key, "published") || !g_ascii_strcasecmp (key, "id") || !g_ascii_strcasecmp (key, "updated"))
				break;

			ptr->next = xmlNewNode (NULL, (xmlChar *)key);
			(value) ? xmlNodeSetContent (ptr->next, (xmlChar *)value) : xmlNodeSetContent (ptr->next, (xmlChar *)"");
			break;
		}
		else 
			ptr = ptr->next;
	}
}

/** 
 * gdata_entry_get_id:
 * @entry: A GDataEntry object
 * Returns the id of the Entry: 
 **/
gchar * 
gdata_entry_get_id (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail(entry != NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);

	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return g_hash_table_lookup (priv->field_table, "id");	  
}

/** 
 * gdata_entry_get_visibility:
 * @entry: A GDataEntry object
 * Returns the visibility of the Entry: 
 **/
gchar * 
gdata_entry_get_visibility (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail(entry != NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);

	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return priv->visibility;
}

/** 
 * gdata_entry_get_content:
 * @entry: A #GDataEntry object
 * Returns the content of the Entry/Event. 
 **/
gchar * 
gdata_entry_get_content (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry !=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return g_hash_table_lookup (priv->field_table, "content");
}

/** 
 * gdata_entry_get_description:
 * @entry: A #GDataEntry object
 * Returns the description of the Entry. 
 **/
/* Returns the description of the entry */
gchar * 
gdata_entry_get_description (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry !=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return NULL;
}

/** 
 * gdata_entry_get_copyright:
 * @entry: A #GDataEntry object
 * Returns the copyright of the Entry
 **/
gchar * 
gdata_entry_get_copyright (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry !=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return g_hash_table_lookup (priv->field_table, "copyright");
}

/** 
 * gdata_entry_get_title:
 * @entry: A #GDataEntry object.
 * Returns the title of the Entry. 
 **/
gchar * 
gdata_entry_get_title (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry !=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return g_hash_table_lookup (priv->field_table, "title");
}

/** 
 * gdata_entry_get_authors:
 * @entry: A #GDataEntry object.
 * Returns the list of authors of entry.  
 **/
GSList * 
gdata_entry_get_authors (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return priv->authors;
}

/**
 * gdata_entry_get_links:
 * @entry: A #GDataEntry object.
 * Returns the list of links
 **/
GSList *
gdata_entry_get_links (GDataEntry *entry)
{
	GDataEntryPrivate *priv;
	
	g_return_val_if_fail (entry !=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return priv->links;
}

/** 
 * gdata_entry_get_start_time:
 * @entry: A #GDataEntry object.
 * Returns the starting time of the Event. 
 **/
gchar * 
gdata_entry_get_start_time (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry!=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY(entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	return priv->start_time;
}


/** 
 * gdata_entry_get_transparency:
 * @entry: A #GDataEntry object.
 * Returns the transparency of the Event. 
 **/
gchar * 
gdata_entry_get_transparency (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry!=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY(entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	return priv->transparency;
}

/** 
 * gdata_entry_get_end_time:
 * @entry: A #GDataEntry object.
 * Returns the ending time of the Event. 
 **/
gchar * 
gdata_entry_get_end_time (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY(entry), NULL);
		
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	return priv->end_time;
}

/** 
 * gdata_entry_get_location:
 * @entry: A #GDataEntry object.
 * Returns the location of the Event. 
 **/
gchar * 
gdata_entry_get_location (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), NULL);

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	return priv->location; 
}

/** 
 * gdata_entry_get_status:
 * @entry: A #GDataEntry object.
 * Returns the status of the Event. 
 **/
gchar * 
gdata_entry_get_status (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail(entry !=NULL, NULL);
	g_return_val_if_fail(GDATA_IS_ENTRY (entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	return priv->status;
}

/** 
 * gdata_entry_get_categories:
 * @entry: A #GDataEntry object.
 * Returns the status of the Event. 
 **/
GSList *
gdata_entry_get_categories (GDataEntry *entry)
{
	GDataEntryPrivate *priv;

	g_return_val_if_fail(entry !=NULL, NULL);
	g_return_val_if_fail(GDATA_IS_ENTRY (entry), NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	return priv->categories;
}

/**
 * gdata_entry_get_edit_link:
 * @entry: A #GDataEntry object.
 * Returns the edit link of the entry
 **/
gchar *
gdata_entry_get_edit_link (GDataEntry *entry)
{
	GSList *list;
	GDataEntryLink *link;
	gchar *edit_link = NULL;

	g_return_val_if_fail(GDATA_IS_ENTRY(entry), NULL);
	list = gdata_entry_get_links (entry);

	while (list) {
		link = list->data;
		if (!g_ascii_strcasecmp (link->rel, "edit")) {
			edit_link = g_strdup(link->href);
		}
		list = g_slist_next (list);
	}

	return edit_link;
}

/** 
 * gdata_entry_set_author:
 * @entry: A #GDataEntry object.
 * @author: A list of authors.
 * Sets the list of authors.  
 **/
void 
gdata_entry_set_author (GDataEntry *entry, GSList *author)
{
	GDataEntryPrivate *priv;

	g_return_if_fail (author !=NULL);
	g_return_if_fail (GDATA_IS_ENTRY(entry));

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->authors = author;
	priv->entry_needs_update = TRUE;
}

/** 
 * gdata_entry_set_id:
 * @entry: A #GDataEntry object.
 * @id: Id of the entry.
 * Sets the list of authors.  
 **/
void 
gdata_entry_set_id (GDataEntry *entry, gchar *id)
{
	GDataEntryPrivate *priv;

	g_return_if_fail (id !=NULL);
	g_return_if_fail (GDATA_IS_ENTRY(entry));

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->id = g_strdup(id);
	priv->entry_needs_update = TRUE;
}




/** 
 * gdata_entry_set_categories:
 * @entry: A GDataEntry object.
 * @categories: A list of categories.
 * Sets the list of categories of the Entry. 
 **/
void 
gdata_entry_set_categories (GDataEntry *entry, GSList *categories)
{
	GDataEntryPrivate *priv;
	
	g_return_if_fail(categories !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY(entry));

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->categories = categories;
	priv->entry_needs_update = TRUE;		
}

/** 
 * gdata_entry_set_title:
 * @entry: A GDataEntry object.
 * @title: title of the event.
 * Sets the title of the Entry. 
 **/
void 
gdata_entry_set_title (GDataEntry *entry, const gchar *title)
{
	GDataEntryPrivate *priv;

	g_return_if_fail(title !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY (entry));
		
	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	priv->title = g_strdup(title);
	g_hash_table_insert (priv->field_table, g_strdup("title"), g_strdup(title));
	priv->entry_needs_update = TRUE;
}

/** 
 * gdata_entry_set_content:
 * @entry: A GDataEntry object.
 * @content: The content of the event.
 * Sets the content of the Entry. 
 **/
void
gdata_entry_set_content (GDataEntry *entry, const gchar *content)
{
	GDataEntryPrivate *priv;

	g_return_if_fail(content !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY(entry));
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->content = g_strdup (content);
	priv->entry_needs_update = TRUE;
	g_hash_table_insert (priv->field_table, g_strdup("content"), g_strdup (content));
}

/** 
 * gdata_entry_set_links:
 * @entry: A GDataEntry object.
 * @links: A list of links associated.
 * Sets the links of the Event. 
 **/
void 
gdata_entry_set_links (GDataEntry *entry, GSList *links)
{
	GDataEntryPrivate *priv;

	g_return_if_fail( links !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY(entry));

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->links = links;
	priv->entry_needs_update = TRUE;
}

/** 
 * gdata_entry_set_status:
 * @entry: A GDataEntry object.
 * @status: The status of the event.
 * Sets the status of the Event. 
 **/
void 
gdata_entry_set_status (GDataEntry *entry, const gchar *status)
{
	GDataEntryPrivate *priv;

	g_return_if_fail(status!=NULL);
	g_return_if_fail(GDATA_IS_ENTRY(entry));
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->status = g_strdup (status);	
	priv->entry_needs_update = TRUE;
}

/** 
 * gdata_entry_set_sendNotification:
 * @entry: A GDataEntry object.
 * @sendNotification: Whether notification is required. 
 * Sets whether notification should be sent by the Event. 
 **/
void 
gdata_entry_set_send_notification (GDataEntry *entry, const gchar *send_notification)
{
	GDataEntryPrivate *priv;	

	g_return_if_fail (send_notification!=NULL);
	g_return_if_fail (GDATA_IS_ENTRY (entry));
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->send_notification = g_strdup(send_notification);
	priv->entry_needs_update = TRUE;
}	

/** 
 * gdata_entry_set_reminder:
 * @entry: A GDataEntry object.
 * @reminder: The reminder set.
 * Sets the reminder of the Event.
 **/
void 
gdata_entry_set_reminder (GDataEntry *entry, const gchar *reminder)
{
	GDataEntryPrivate *priv;

	g_return_if_fail(reminder !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY (entry));
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->reminder = g_strdup (reminder);
	priv->entry_needs_update = TRUE;
}

/** 
 * gdata_entry_set_start_time:
 * @entry: A GDataEntry object.
 * @start_time: The starting time of the event.
 * Sets the starting time of the Event. 
 **/
void 
gdata_entry_set_start_time (GDataEntry *entry, const gchar *start_time)
{
	GDataEntryPrivate *priv;

	g_return_if_fail(start_time !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY(entry));
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->start_time = g_strdup(start_time);
	priv->entry_needs_update = TRUE;
}	

/** 
 * gdata_entry_set_end_time:
 * @entry: A GDataEntry object.
 * @end_time: The end time of the event.
 * Sets the end time of the event.
 **/
void 
gdata_entry_set_end_time (GDataEntry *entry, const gchar *end_time)
{
	GDataEntryPrivate *priv;

	g_return_if_fail(end_time !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY (entry));
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->end_time = g_strdup(end_time);
	priv->entry_needs_update = TRUE;
}

/**
 * gdata_entry_set_transparency:
 * @entry: A GDataEntry object. 
 * @transparency: Transparency of the Entry.
 * Sets the transparency of the entry. 
 **/
void 
gdata_entry_set_transparency (GDataEntry *entry, const gchar *transparency)
{
	GDataEntryPrivate *priv;

	g_return_if_fail (transparency !=NULL);
	g_return_if_fail (GDATA_IS_ENTRY(entry));

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->transparency = g_strdup (transparency);
	priv->entry_needs_update = TRUE;
}

/**
 * gdata_entry_set_location:
 * @entry: A GDataEntry object.
 * @location: Location of the event.
 * Sets the location of the event.
 **/
void 
gdata_entry_set_location (GDataEntry *entry, const gchar *location)
{
	GDataEntryPrivate *priv;

	g_return_if_fail (location !=NULL);
	g_return_if_fail (GDATA_IS_ENTRY(entry));

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->location = g_strdup (location);
	priv->entry_needs_update = TRUE;
}

/**
 * gdata_entry_set_xml:
 * @entry: A GDataEntry object.
 * @location: xml tree of the entry.
 * Sets the xml tree of the entry.
 **/
static void
gdata_entry_set_xml (GDataEntry *entry, const gchar *xml)
{
	GDataEntryPrivate *priv;

	g_return_if_fail(GDATA_IS_ENTRY(entry));

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->entry_xml = g_strdup (xml);
	priv->entry_needs_update = TRUE;
	
}

/** 
 * gdata_entry_create_authors:
 * @entry: A GDataEntry object.
 * @name: The name of the author.
 * @email: The email of the author.
 * Sets the list of authors to the Entry.
 **/
void 
gdata_entry_create_authors (GDataEntry *entry,
			    const gchar *name, 
			    const gchar *email)
{
	GDataEntryPrivate *priv;
	GDataEntryAuthor *author;

	g_return_if_fail(name !=NULL || email !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY(entry));
	
	author = g_new0 (GDataEntryAuthor, 1);
	priv = GDATA_ENTRY_GET_PRIVATE(entry);

	author->name = g_strdup(name); 
	author->email = g_strdup(email);
	
	priv->authors =	g_slist_prepend(priv->authors, author);
	priv->entry_needs_update = TRUE;
	
}

/** 
 * gdata_entry_create_categories:
 * @entry: A GDataEntry object.
 * @scheme:Scheme.
 * @label:Label.
 * @term: Term.
 * Sets the categories of the Event.
 **/
void 
gdata_entry_create_categories (GDataEntry *entry, 
			       const gchar *scheme, 
			       const gchar *label , 
			       const gchar *term)
{
	GDataEntryPrivate *priv;
	GDataEntryCategory *category;

	g_return_if_fail(scheme !=NULL || label !=NULL || term !=NULL);
	g_return_if_fail(GDATA_IS_ENTRY (entry));

	category = g_new0(GDataEntryCategory, 1);
	priv = GDATA_ENTRY_GET_PRIVATE(entry);

	category->scheme = g_strdup(scheme);
	category->label = g_strdup(label);
	category->term = g_strdup(term);

	priv->categories = g_slist_prepend(priv->categories, category);
	priv->entry_needs_update = TRUE;
}

gboolean
gdata_entry_is_recurrent (GDataEntry *entry)
{
	GDataEntryPrivate *priv;
	
	g_return_val_if_fail (entry != NULL, 0);
	g_return_val_if_fail (GDATA_IS_ENTRY (entry), 0);
	
	priv = GDATA_ENTRY_GET_PRIVATE (entry);
	return priv->is_recurrent;
}

GSList *
gdata_entry_get_attendee_list (GDataEntry *entry)
{
	GDataEntryPrivate *priv;
	
	g_return_val_if_fail (entry != NULL, NULL);	
	g_return_val_if_fail (GDATA_IS_ENTRY(entry), NULL);

	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	return priv->attendees;
}

void
gdata_entry_set_attendee_list (GDataEntry *entry, GSList *attendees)
{
	GDataEntryPrivate *priv;

	g_return_if_fail (entry != NULL);
	g_return_if_fail (GDATA_IS_ENTRY(entry));
	g_return_if_fail (attendees != NULL);
	
	priv = GDATA_ENTRY_GET_PRIVATE(entry);
	priv->attendees = attendees;
	priv->has_attendees = TRUE;
}

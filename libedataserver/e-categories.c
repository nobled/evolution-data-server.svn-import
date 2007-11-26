/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005 Novell, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <libxml/parser.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gconf/gconf-client.h>
#include "e-categories.h"

#include "libedataserver-private.h"

typedef struct {
	char *category;
	char *icon_file;
	#ifndef EDS_DISABLE_DEPRECATED
	char *color;
	#endif
	gboolean searchable;
} CategoryInfo;

static CategoryInfo default_categories[] = {
	{ N_("Anniversary") },
	{ N_("Birthday"), "category_birthday_16.png" },
	{ N_("Business"), "category_business_16.png" },
	{ N_("Competition") },
	{ N_("Favorites"), "category_favorites_16.png" },
	{ N_("Gifts"), "category_gifts_16.png" },
	{ N_("Goals/Objectives"), "category_goals_16.png" },
	{ N_("Holiday"), "category_holiday_16.png" },
	{ N_("Holiday Cards"), "category_holiday-cards_16.png" },
	/* important people (e.g. new business partners) */
	{ N_("Hot Contacts"), "category_hot-contacts_16.png" },
	{ N_("Ideas"), "category_ideas_16.png" },
	{ N_("International"), "category_international_16.png" },
	{ N_("Key Customer"), "category_key-customer_16.png" },
	{ N_("Miscellaneous"), "category_miscellaneous_16.png" },
	{ N_("Personal"), "category_personal_16.png" },
	{ N_("Phone Calls"), "category_phonecalls_16.png" },
	{ N_("Status"), "category_status_16.png" },
	{ N_("Strategies"), "category_strategies_16.png" },
	{ N_("Suppliers"), "category_suppliers_16.png" },
	{ N_("Time & Expenses"), "category_time-and-expenses_16.png" },
	{ N_("VIP") },
	{ N_("Waiting") },
	{ NULL }
};

static gboolean initialized = FALSE;
static GHashTable *categories_table = NULL;
static gboolean save_is_pending = FALSE;
static guint idle_id = 0;

static gchar *
build_categories_filename (void)
{
	return g_build_filename (g_get_home_dir (),
		".evolution", "categories.xml", NULL);
}

static void
free_category_info (CategoryInfo *cat_info)
{
	g_free (cat_info->category);
	g_free (cat_info->icon_file);
	#ifndef EDS_DISABLE_DEPRECATED
	g_free (cat_info->color);
	#endif
	g_free (cat_info);
}

static gchar *
escape_string (const gchar *source)
{
	GString *buffer;

	buffer = g_string_sized_new (strlen (source));

	while (*source) {
		switch (*source) {
		case '<':
			g_string_append_len (buffer, "&lt;", 4);
			break;
		case '>':
			g_string_append_len (buffer, "&gt;", 4);
			break;
		case '&':
			g_string_append_len (buffer, "&amp;", 5);
			break;
		case '"':
			g_string_append_len (buffer, "&quot;", 6);
			break;
		default:
			g_string_append_c (buffer, *source);
			break;
		}
		source++;
	}

	return g_string_free (buffer, FALSE);
}

static void
hash_to_xml_string (gpointer key, gpointer value, gpointer user_data)
{
	CategoryInfo *cat_info = value;
	GString *string = user_data;
	gchar *category;

	g_string_append_len (string, "\t<category", 10);

	category = escape_string (cat_info->category);
	g_string_append_printf (string, " a=\"%s\"", category);
	g_free (category);

	if (cat_info->icon_file != NULL)
		g_string_append_printf (
			string, " icon=\"%s\"", cat_info->icon_file);

	#ifndef EDS_DISABLE_DEPRECATED
	if (cat_info->color != NULL)
		g_string_append_printf (
			string, " color=\"%s\"", cat_info->color);
	#endif

	g_string_append_printf (
		string, " searchable=\"%d\"", cat_info->searchable);

	g_string_append_len (string, "/>\n", 3);
}

static gboolean
idle_saver_cb (gpointer user_data)
{
	GString *buffer;
	gchar *contents;
	gchar *filename;
	GError *error = NULL;

	if (!save_is_pending)
		goto exit;

	filename = build_categories_filename ();

	g_debug ("Saving categories to \"%s\"", filename);

	/* build the file contents */
	buffer = g_string_new ("<categories>\n");
	g_hash_table_foreach (categories_table, hash_to_xml_string, buffer);
	g_string_append_len (buffer, "</categories>\n", 14);
	contents = g_string_free (buffer, FALSE);

	if (!g_file_set_contents (filename, contents, -1, &error)) {
		g_warning ("Unable to save categories: %s", error->message);
		g_error_free (error);
	}

	g_free (contents);
	g_free (filename);
	save_is_pending = FALSE;

exit:
	idle_id = 0;
	return FALSE;
}

static void
save_categories (void)
{
	save_is_pending = TRUE;

	if (idle_id == 0)
		idle_id = g_idle_add (idle_saver_cb, NULL);
}

static gint
parse_categories (const gchar *contents, gsize length)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	gint n_added = 0;

	doc = xmlParseMemory (contents, length);
	if (doc == NULL) {
		g_warning ("Unable to parse categories");
		return 0;
	}

	node = xmlDocGetRootElement (doc);
	if (node == NULL) {
		g_warning ("Unable to parse categories");
		xmlFreeDoc (doc);
		return 0;
	}

	for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
		xmlChar *category, *icon, *searchable;
		#ifndef EDS_DISABLE_DEPRECATED
		xmlChar *color;
		#endif

		category = xmlGetProp (node, (xmlChar *) "a");
		icon = xmlGetProp (node, (xmlChar *) "icon");
		#ifndef EDS_DISABLE_DEPRECATED
		color = xmlGetProp (node, (xmlChar *) "color");
		#endif
		searchable = xmlGetProp (node, (xmlChar *) "searchable");

		if (category != NULL) {
			e_categories_add (
				(gchar *) category,
				#ifndef EDS_DISABLE_DEPRECATED
				(gchar *) color,
				#else
				NULL,
				#endif
				(gchar *) icon,
				(searchable != NULL) &&
				strcmp ((gchar *) searchable, "0") != 0);
			n_added++;
		}

		xmlFree (category);
		xmlFree (icon);
		#ifndef EDS_DISABLE_DEPRECATED
		xmlFree (color);
		#endif
		xmlFree (searchable);
	}

	xmlFreeDoc (doc);

	return n_added;
}

static gint
load_categories (void)
{
	gchar *contents;
	gchar *filename;
	gsize length;
	gint n_added = 0;
	GError *error = NULL;

	contents = NULL;
	filename = build_categories_filename ();

	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
		goto exit;

	g_debug ("Loading categories from \"%s\"", filename);

	if (!g_file_get_contents (filename, &contents, &length, &error)) {
		g_warning ("Unable to load categories: %s", error->message);
		g_error_free (error);
		goto exit;
	}

	n_added = parse_categories (contents, length);

exit:
	g_free (contents);
	g_free (filename);

	return n_added;
}

static void
migrate_old_icon_file (gpointer key, gpointer value, gpointer user_data)
{
	CategoryInfo *info = value;
	gchar *basename;

	if (info->icon_file == NULL)
		return;

	/* We can't be sure where the old icon files were stored, but
         * a good guess is (E_DATA_SERVER_IMAGESDIR "-2.x").  Convert
         * any such paths to just E_DATA_SERVER_IMAGESDIR. */
	if (g_str_has_prefix (info->icon_file, E_DATA_SERVER_IMAGESDIR)) {
		basename = g_path_get_basename (info->icon_file);
		g_free (info->icon_file);
		info->icon_file = g_build_filename (
			E_DATA_SERVER_IMAGESDIR, basename, NULL);
		g_free (basename);
	}
}

static gboolean
migrate_old_categories (void)
{
	/* Try migrating old category settings from GConf to the new
         * category XML file.  If successful, unset the old GConf key
         * so that this is a one-time-only operation. */

	const gchar *key = "/apps/evolution/general/category_master_list";

	GConfClient *client;
	gchar *string;
	gint n_added = 0;

	client = gconf_client_get_default ();
	string = gconf_client_get_string (client, key, NULL);
	if (string == NULL || *string == '\0')
		goto exit;

	g_debug ("Loading categories from GConf key \"%s\"", key);

	n_added = parse_categories (string, strlen (string));
	if (n_added == 0)
		goto exit;

	/* default icon files are now in an unversioned directory */
	g_hash_table_foreach (categories_table, migrate_old_icon_file, NULL);

	gconf_client_unset (client, key, NULL);

exit:
	g_object_unref (client);
	g_free (string);

	return n_added;
}

static void
load_default_categories (void)
{
	CategoryInfo *cat_info = default_categories;

	/* Note: All default categories are searchable. */
	while (cat_info->category != NULL) {
		if (cat_info->icon_file != NULL)
			cat_info->icon_file = g_build_filename (E_DATA_SERVER_IMAGESDIR, cat_info->icon_file, NULL);
		e_categories_add (
			gettext (cat_info->category),
			#ifndef EDS_DISABLE_DEPRECATED
			cat_info->color,
			#else
			NULL,
			#endif
			cat_info->icon_file, TRUE);
		g_free (cat_info->icon_file);
		cat_info++;
	}
}

static void
finalize_categories (void)
{
	if (save_is_pending)
		idle_saver_cb (NULL);

	if (idle_id > 0) {
		g_source_remove (idle_id);
		idle_id = 0;
	}

	if (categories_table != NULL) {
		g_hash_table_destroy (categories_table);
		categories_table = NULL;
	}

	initialized = FALSE;
}

static void
initialize_categories (void)
{
	gint n_added;

	if (initialized)
		return;

	initialized = TRUE;

	categories_table = g_hash_table_new_full (
		g_str_hash, g_str_equal, g_free,
		(GDestroyNotify) free_category_info);

	g_atexit (finalize_categories);

	n_added = load_categories ();
	if (n_added > 0) {
		g_debug ("Loaded %d categories", n_added);
		save_is_pending = FALSE;
		return;
	}

	n_added = migrate_old_categories ();
	if (n_added > 0) {
		g_debug ("Loaded %d categories", n_added);
		save_categories ();
		return;
	}

	load_default_categories ();
	g_debug ("Loaded default categories");
	save_categories ();
}

static void
add_hash_to_list (gpointer key, gpointer value, gpointer user_data)
{
	GList **list = user_data;

	*list = g_list_prepend (*list, key);
}

/**
 * e_categories_get_list:
 *
 * Returns a sorted list of all the category names currently configured.
 *
 * Return value: a sorted GList containing the names of the categories. The
 * list should be freed using g_list_free, but the names of the categories
 * should not be touched at all, they are internal strings.
 */
GList *
e_categories_get_list (void)
{
	GList *list = NULL;

	if (!initialized)
		initialize_categories ();

	g_hash_table_foreach (categories_table, add_hash_to_list, &list);

	return g_list_sort (list, (GCompareFunc) g_utf8_collate);
}

/**
 * e_categories_add:
 * @category: name of category to add.
 * @unused: DEPRECATED! associated color. DEPRECATED!
 * @icon_file: full path of the icon associated to the category.
 * @searchable: whether the category can be used for searching in the GUI.
 *
 * Adds a new category, with its corresponding icon, to the
 * configuration database.
 */
void
e_categories_add (const char *category, const char *unused, const char *icon_file, gboolean searchable)
{
	CategoryInfo *cat_info;

	g_return_if_fail (category != NULL);

	if (!initialized)
		initialize_categories ();

	/* add the new category */
	cat_info = g_new0 (CategoryInfo, 1);
	cat_info->category = g_strdup (category);
#ifndef EDS_DISABLE_DEPRECATED
	cat_info->color = g_strdup (unused);
#endif
	cat_info->icon_file = g_strdup (icon_file);
	cat_info->searchable = searchable;

	g_hash_table_insert (categories_table, g_strdup (category), cat_info);

	save_categories ();
}

/**
 * e_categories_remove:
 * @category: category to be removed.
 *
 * Removes the given category from the configuration.
 */
void
e_categories_remove (const char *category)
{
	g_return_if_fail (category != NULL);

	if (!initialized)
		initialize_categories ();

	if (g_hash_table_remove (categories_table, category))
		save_categories ();
}

/**
 * e_categories_exist:
 * @category: category to be searched.
 *
 * Checks whether the given category is available in the configuration.
 *
 * Return value: %TRUE if the category is available, %FALSE otherwise.
 */
gboolean
e_categories_exist (const char *category)
{
	g_return_val_if_fail (category != NULL, FALSE);

	if (!initialized)
		initialize_categories ();

	return (g_hash_table_lookup (categories_table, category) != NULL);
}

#ifndef EDS_DISABLE_DEPRECATED
/**
 * e_categories_get_color_for:
 * @category: category to retrieve the color for.
 *
 * Gets the color associated with the given category.
 *
 * Return value: a string representation of the color.
 *
 * DEPRECATED!
 */
const char *
e_categories_get_color_for (const char *category)
{
	CategoryInfo *cat_info;

	g_return_val_if_fail (category != NULL, NULL);

	if (!initialized)
		initialize_categories ();

	cat_info = g_hash_table_lookup (categories_table, category);
	if (cat_info == NULL)
		return NULL;

	return cat_info->color;
}

/**
 * e_categories_set_color_for:
 * @category: category to set the color for.
 * @color: X color.
 *
 * Sets the color associated with the given category.
 *
 * DEPRECATED!
 */
void
e_categories_set_color_for (const char *category, const char *color)
{
	CategoryInfo *cat_info;

	g_return_if_fail (category != NULL);

	if (!initialized)
		initialize_categories ();

	cat_info = g_hash_table_lookup (categories_table, category);
	g_return_if_fail (cat_info != NULL);

	g_free (cat_info->color);
	cat_info->color = g_strdup (color);
	save_categories ();
}
#endif /* EDS_DISABLE_DEPRECATED */

/**
 * e_categories_get_icon_file_for:
 * @category: category to retrieve the icon file for.
 *
 * Gets the icon file associated with the given category.
 *
 * Return value: icon file name.
 */
const char *
e_categories_get_icon_file_for (const char *category)
{
	CategoryInfo *cat_info;

	g_return_val_if_fail (category != NULL, NULL);

	if (!initialized)
		initialize_categories ();

	cat_info = g_hash_table_lookup (categories_table, category);
	if (cat_info == NULL)
		return NULL;

	return cat_info->icon_file;
}

/**
 * e_categories_set_icon_file_for:
 * @category: category to set the icon file for.
 * @icon_file: icon file.
 *
 * Sets the icon file associated with the given category.
 */
void
e_categories_set_icon_file_for (const char *category, const char *icon_file)
{
	CategoryInfo *cat_info;

	g_return_if_fail (category != NULL);

	if (!initialized)
		initialize_categories ();

	cat_info = g_hash_table_lookup (categories_table, category);
	g_return_if_fail (cat_info != NULL);

	g_free (cat_info->icon_file);
	cat_info->icon_file = g_strdup (icon_file);
	save_categories ();
}

/**
 * e_categories_is_searchable:
 * @category: category name.
 *
 * Gets whether the given calendar is to be used for searches in the GUI.
 *
 * Return value; %TRUE% if the category is searchable, %FALSE% if not.
 */
gboolean
e_categories_is_searchable (const char *category)
{
	CategoryInfo *cat_info;

	g_return_val_if_fail (category != NULL, FALSE);

	if (!initialized)
		initialize_categories ();

	cat_info = g_hash_table_lookup (categories_table, category);
	if (cat_info == NULL)
		return FALSE;

	return cat_info->searchable;
}

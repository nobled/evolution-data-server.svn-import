/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   JP Rosevear (jpr@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#include <config.h>

#include "e-dbhash.h"

#include <string.h>
#include <fcntl.h>
#include "db.h"

struct _EDbHashPrivate
{
	DB *db;
};

EDbHash *
e_dbhash_new (const char *filename)
{
	EDbHash *edbh;
	DB *db;
	int rv;

	/* Attempt to open the database */
	rv = db_create (&db, NULL, 0);
	if (rv != 0) {
		return NULL;
	}

	rv = (*db->open) (db, NULL, filename, NULL, DB_HASH, 0, 0666);
	if (rv != 0) {
		rv = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_CREATE, 0666);

		if (rv != 0) {
			db->close (db, 0);
			return NULL;
		}
	}

	edbh = g_new (EDbHash, 1);
	edbh->priv = g_new (EDbHashPrivate, 1);
	edbh->priv->db = db;

	return edbh;
}

static void
string_to_dbt(const char *str, DBT *dbt)
{
	memset (dbt, 0, sizeof (DBT));
	dbt->data = (void*)str;
	dbt->size = strlen (str) + 1;
}

static void
md5_to_dbt(const guint8 str[16], DBT *dbt)
{
	memset (dbt, 0, sizeof (DBT));
	dbt->data = (void*)str;
	dbt->size = 16;
}

void
e_dbhash_add (EDbHash *edbh, const gchar *key, const gchar *data)
{
	DB *db;
	DBT dkey;
	DBT ddata;
	GChecksum *checksum;
	guint8 *digest;
	gsize length;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);
	g_return_if_fail (edbh->priv->db != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (data != NULL);

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	db = edbh->priv->db;

	/* Key dbt */
	string_to_dbt (key, &dkey);

	/* Compute MD5 checksum */
	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *) data, -1);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	/* Data dbt */
	md5_to_dbt (digest, &ddata);

	/* Add to database */
	db->put (db, NULL, &dkey, &ddata, 0);
}

void
e_dbhash_remove (EDbHash *edbh, const char *key)
{
	DB *db;
	DBT dkey;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);
	g_return_if_fail (key != NULL);

	db = edbh->priv->db;

	/* Key dbt */
	string_to_dbt (key, &dkey);

	/* Remove from database */
	db->del (db, NULL, &dkey, 0);
}

void
e_dbhash_foreach_key (EDbHash *edbh, EDbHashFunc func, gpointer user_data)
{
	DB *db;
	DBT dkey;
	DBT ddata;
	DBC *dbc;
	int db_error = 0;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);
	g_return_if_fail (func != NULL);

	db = edbh->priv->db;

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		return;
	}

	memset(&dkey, 0, sizeof(DBT));
	memset(&ddata, 0, sizeof(DBT));
	db_error = dbc->c_get(dbc, &dkey, &ddata, DB_FIRST);

	while (db_error == 0) {
		(*func) ((const char *)dkey.data, user_data);

		db_error = dbc->c_get(dbc, &dkey, &ddata, DB_NEXT);
	}
	dbc->c_close (dbc);
}

EDbHashStatus
e_dbhash_compare (EDbHash *edbh, const char *key, const char *compare_data)
{
	DB *db;
	DBT dkey;
	DBT ddata;
	guint8 compare_hash[16];
	gsize length = sizeof (compare_hash);

	g_return_val_if_fail (edbh != NULL, FALSE);
	g_return_val_if_fail (edbh->priv != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (compare_hash != NULL, FALSE);

	db = edbh->priv->db;

	/* Key dbt */
	string_to_dbt (key, &dkey);

	/* Lookup in database */
	memset (&ddata, 0, sizeof (DBT));
	db->get (db, NULL, &dkey, &ddata, 0);

	/* Compare */
	if (ddata.data) {
		GChecksum *checksum;

		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, (guchar *) compare_data, -1);
		g_checksum_get_digest (checksum, compare_hash, &length);
		g_checksum_free (checksum);

		if (memcmp (ddata.data, compare_hash, sizeof (guchar) * 16))
			return E_DBHASH_STATUS_DIFFERENT;
	} else {
		return E_DBHASH_STATUS_NOT_FOUND;
	}

	return E_DBHASH_STATUS_SAME;
}

void
e_dbhash_write (EDbHash *edbh)
{
	DB *db;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);

	db = edbh->priv->db;

	/* Flush database to disk */
	db->sync (db, 0);
}

void
e_dbhash_destroy (EDbHash *edbh)
{
	DB *db;

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);

	db = edbh->priv->db;

	/* Close datbase */
	db->close (db, 0);

	g_free (edbh->priv);
	g_free (edbh);
}

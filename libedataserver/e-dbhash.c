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
#include <db.h>
#include "md5-utils.h"

#if DB_VERSION_MAJOR != 3 || \
    DB_VERSION_MINOR != 1 || \
    DB_VERSION_PATCH != 17
#error Including wrong DB3.  Need libdb 3.1.17.
#endif

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

	int major, minor, patch;

	db_version (&major, &minor, &patch);

	if (major != 3 ||
	    minor != 1 ||
	    patch != 17) {
		g_warning ("Wrong version of libdb.");
		return NULL;
	}

	/* Attempt to open the database */
	rv = db_create (&db, NULL, 0);
	if (rv != 0) {
		return NULL;
	}

	rv = db->open (db, filename, NULL, DB_HASH, 0, 0666);
	if (rv != 0) {
		rv = db->open (db, filename, NULL, DB_HASH, DB_CREATE, 0666);

		if (rv != 0)
			return NULL;
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
md5_to_dbt(const char str[16], DBT *dbt)
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
	guchar local_hash[16];

	g_return_if_fail (edbh != NULL);
	g_return_if_fail (edbh->priv != NULL);
	g_return_if_fail (edbh->priv->db != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (data != NULL);

	db = edbh->priv->db;
	
	/* Key dbt */
	string_to_dbt (key, &dkey);

	/* Data dbt */
	md5_get_digest (data, strlen (data), local_hash);
	md5_to_dbt (local_hash, &ddata);

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
	guchar compare_hash[16];
	
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
		md5_get_digest (compare_data, strlen (compare_data), compare_hash);
		
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

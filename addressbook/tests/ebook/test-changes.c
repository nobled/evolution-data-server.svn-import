/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#define NEW_VCARD "BEGIN:VCARD\n\
X-EVOLUTION-FILE-AS:Toshok, Chris\n\
FN:Chris Toshok\n\
EMAIL;INTERNET:toshok@ximian.com\n\
ORG:Ximian, Inc.;\n\
END:VCARD"

int
main (int argc, char **argv)
{
	EBook *book;
	EContact *contact;
	GList *changes;
	GError *error = NULL;
	EBookChange *change;
	gchar *file_template;
	gchar *uri;

	g_type_init ();

	file_template = g_build_filename (g_get_tmp_dir (),
					  "change-test-XXXXXX",
					  NULL);
	g_mkstemp (file_template);

	uri = g_filename_to_uri (file_template, NULL, &error);
	if (!uri) {
		printf ("failed to convert %s to an URI: %s\n",
			file_template, error->message);
		exit (0);
	}
	g_free (file_template);

	/* create a temp addressbook in /tmp */
	printf ("loading addressbook\n");
	book = e_book_new_from_uri (uri, &error);
	if (!book) {
		printf ("failed to create addressbook: `%s': %s\n",
			uri, error->message);
		exit(0);
	}

	if (!e_book_open (book, FALSE, &error)) {
		printf ("failed to open addressbook: `%s': %s\n",
			uri, error->message);
		exit(0);
	}

	/* get an initial change set */
	if (!e_book_get_changes (book, "changeidtest", &changes, &error)) {
		printf ("failed to get changes: %s\n", error->message);
		exit(0);
	}

	/* make a change to the book */
	contact = e_contact_new_from_vcard (NEW_VCARD);
	if (!e_book_add_contact (book, contact, &error)) {
		printf ("failed to add new contact: %s\n", error->message);
		exit(0);
	}

	/* get another change set */
	if (!e_book_get_changes (book, "changeidtest", &changes, &error)) {
		printf ("failed to get second set of changes: %s\n", error->message);
		exit(0);
	}

	/* make sure that 1 change has occurred */
	if (g_list_length (changes) != 1) {
		printf ("got back %d changes, was expecting 1\n", g_list_length (changes));
		exit(0);
	}

	change = changes->data;
	if (change->change_type != E_BOOK_CHANGE_CARD_ADDED) {
		printf ("was expecting a CARD_ADDED change, but didn't get it.\n");
		exit(0);
	}

	printf ("got changed vcard back: %s\n", (char*)e_contact_get_const (change->contact, E_CONTACT_UID));

	e_book_free_change_list (changes);


	if (!e_book_remove (book, &error)) {
		printf ("failed to remove book; %s\n", error->message);
		exit(0);
	}

	g_object_unref (book);

	return 0;
}

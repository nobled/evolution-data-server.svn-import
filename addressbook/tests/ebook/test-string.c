#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <string.h>
#include <libebook/e-book.h>

#define TEST_ID "test-uid"

int
main (int argc, char **argv)
{
	EContact *contact;

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	contact = e_contact_new ();

	e_contact_set (contact, E_CONTACT_UID, TEST_ID);

	if (!strcmp (e_contact_get_const (contact, E_CONTACT_UID), TEST_ID))
	  printf ("passed\n");
	else
	  printf ("failed\n");
	
	return 0;
}

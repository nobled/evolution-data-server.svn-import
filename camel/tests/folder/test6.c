/* folder testing */

#include "camel-test.h"
#include "messages.h"

#include <camel/camel-exception.h>
#include <camel/camel-service.h>
#include <camel/camel-session.h>
#include <camel/camel-store.h>

#include <camel/camel-folder.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-mime-message.h>

/* god, who designed this horrid interface */
static char *auth_callback(CamelAuthCallbackMode mode,
			   char *data, gboolean secret,
			   CamelService *service, char *item,
			   CamelException *ex)
{
	return NULL;
}

static int regtimeout()
{
	return 1;
}

static int unregtimeout()
{
	return 1;
}

#define ARRAY_LEN(x) (sizeof(x)/sizeof(x[0]))

static char *remote_providers[] = {
	"IMAP_TEST_URL",
};

int main(int argc, char **argv)
{
	CamelSession *session;
	CamelException *ex;
	int i;
	char *path;

	camel_test_init(argc, argv);

	/* clear out any camel-test data */
	system("/bin/rm -rf /tmp/camel-test");

	ex = camel_exception_new();

	session = camel_session_new("/tmp/camel-test", auth_callback, regtimeout, unregtimeout);

	for (i=0;i<ARRAY_LEN(remote_providers);i++) {
		path = getenv(remote_providers[i]);

		if (path == NULL) {
			printf("Aborted (ignored).\n");
			printf("Set '%s', to re-run test.\n", remote_providers[i]);
			/* tells make check to ignore us in the total count */
			_exit(77);
		}
		/*camel_test_nonfatal("The IMAP code is just rooted");*/
		test_folder_message_ops(session, path, FALSE);
		/*camel_test_fatal();*/
	}

	check_unref(session, 1);
	camel_exception_free(ex);

	return 0;
}

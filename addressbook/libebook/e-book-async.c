/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include "libebook/e-book-async.h"

#define MAX_WORKERS 20
static GThreadPool *worker_pool;

static GAsyncQueue *from_worker_queue;
static GStaticMutex idle_mutex = G_STATIC_MUTEX_INIT;
static guint idle_id = -1;

typedef struct _EBookMsg EBookMsg;

typedef void (*EBookMsgHandler)(EBookMsg* msg);
typedef void (*EBookMsgDtor)(EBookMsg* msg);

struct _EBookMsg {
	EBookMsgHandler handler;
	EBookMsgDtor dtor;
};

static void
worker (gpointer data, gpointer user_data)
{
	EBookMsg *msg = data;

	msg->handler (msg);
	msg->dtor (msg);
}

static gboolean
main_thread_get_response (gpointer data)
{
	EBookMsg *msg;

	g_static_mutex_lock (&idle_mutex);

	idle_id = -1;

	while ((msg = g_async_queue_try_pop (from_worker_queue)) != NULL) {
		msg->handler (msg);
		msg->dtor (msg);
	}

	g_static_mutex_unlock (&idle_mutex);

	return FALSE;
}

static void
push_response (gpointer response)
{
	g_static_mutex_lock (&idle_mutex);

	g_async_queue_push (from_worker_queue, response);
	if (idle_id == -1)
		idle_id = g_idle_add (main_thread_get_response, NULL);

	g_static_mutex_unlock (&idle_mutex);
}

static void
e_book_msg_init (EBookMsg *msg, EBookMsgHandler handler, EBookMsgDtor dtor)
{
	msg->handler = handler;
	msg->dtor = dtor;
}

static void
init_async()
{
	static gboolean init_done = FALSE;
	if (!init_done) {
		init_done = TRUE;
		from_worker_queue = g_async_queue_new ();
		worker_pool = g_thread_pool_new (worker, NULL, MAX_WORKERS, FALSE, NULL);
	}
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	gboolean only_if_exists;
	EBookCallback open_response;
	gpointer closure;
} OpenMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookCallback open_response;
	gpointer closure;
} OpenResponse;

static void
_open_response_handler (EBookMsg *msg)
{
	OpenResponse *resp = (OpenResponse*)msg;

	resp->open_response (resp->book, resp->status, resp->closure);
}

static void
_open_response_dtor (EBookMsg *msg)
{
	OpenResponse *resp = (OpenResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_open_handler (EBookMsg *msg)
{
	OpenMsg *open_msg = (OpenMsg *)msg;
	OpenResponse *response;
	GError *error = NULL;

	response = g_new0 (OpenResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _open_response_handler, _open_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_open (open_msg->book, open_msg->only_if_exists, &error)) {
		response->status = error->code;
		g_error_free (error);
	}

	response->book = open_msg->book;
	response->open_response = open_msg->open_response;
	response->closure = open_msg->closure;

	push_response (response);
}

static void
_open_dtor (EBookMsg *msg)
{
	OpenMsg *open_msg = (OpenMsg *)msg;
	
	g_free (open_msg);
}

void
e_book_async_open (EBook                 *book,
		   gboolean               only_if_exists,
		   EBookCallback          open_response,
		   gpointer               closure)
{
	OpenMsg *msg;

	g_return_if_fail (E_IS_BOOK (book));
	g_return_if_fail (open_response != NULL);

	init_async ();

	msg = g_new (OpenMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _open_handler, _open_dtor);

	msg->book = g_object_ref (book);
	msg->only_if_exists = only_if_exists;
	msg->open_response = open_response;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookFieldsCallback cb;
	gpointer closure;
} GetFieldsMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	GList *fields;
	EBookFieldsCallback cb;
	gpointer closure;
} GetFieldsResponse;

static void
_get_fields_response_handler (EBookMsg *msg)
{
	GetFieldsResponse *resp = (GetFieldsResponse*)msg;
	GList *l;
	EList *fields = e_list_new ((EListCopyFunc) g_strdup, 
				    (EListFreeFunc) g_free,
				    NULL);

	for (l = resp->fields; l; l = l->next)
		e_list_append (fields, l->data);

	if (resp->cb)
		resp->cb (resp->book, resp->status, fields, resp->closure);

	g_object_unref (fields);
}

static void
_get_fields_response_dtor (EBookMsg *msg)
{
	GetFieldsResponse *resp = (GetFieldsResponse*)msg;

	g_list_foreach (resp->fields, (GFunc)g_free, NULL);
	g_list_free (resp->fields);
	g_object_unref (resp->book);
	g_free (resp);
}

static void
_get_fields_handler (EBookMsg *msg)
{
	GetFieldsMsg *fields_msg = (GetFieldsMsg *)msg;
	GetFieldsResponse *response;
	GError *error = NULL;

	response = g_new0 (GetFieldsResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_fields_response_handler, _get_fields_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_get_supported_fields (fields_msg->book, &response->fields, &error)) {
		response->status = error->code;
		g_error_free (error);
	}
	response->book = fields_msg->book;
	response->cb = fields_msg->cb;
	response->closure = fields_msg->closure;

	push_response (response);
}

guint
e_book_async_get_supported_fields (EBook                 *book,
				   EBookFieldsCallback    cb,
				   gpointer               closure)
{
	GetFieldsMsg *msg;

	g_return_val_if_fail (E_IS_BOOK (book), 0);

	init_async ();

	msg = g_new (GetFieldsMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_fields_handler, (EBookMsgDtor)g_free);

	msg->book = g_object_ref (book);
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);

	return 0;
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookAuthMethodsCallback cb;
	gpointer closure;
} GetMethodsMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	GList *methods;
	EBookAuthMethodsCallback cb;
	gpointer closure;
} GetMethodsResponse;

static void
_get_methods_response_handler (EBookMsg *msg)
{
	GetMethodsResponse *resp = (GetMethodsResponse*)msg;
	GList *l;
	EList *methods = e_list_new ((EListCopyFunc) g_strdup, 
				    (EListFreeFunc) g_free,
				    NULL);

	for (l = resp->methods; l; l = l->next)
		e_list_append (methods, l->data);

	if (resp->cb)
		resp->cb (resp->book, resp->status, methods, resp->closure);

	g_object_unref (methods);
}

static void
_get_methods_response_dtor (EBookMsg *msg)
{
	GetMethodsResponse *resp = (GetMethodsResponse*)msg;

	g_list_foreach (resp->methods, (GFunc)g_free, NULL);
	g_list_free (resp->methods);
	g_object_unref (resp->book);
	g_free (resp);
}

static void
_get_methods_handler (EBookMsg *msg)
{
	GetMethodsMsg *methods_msg = (GetMethodsMsg *)msg;
	GetMethodsResponse *response;
	GError *error = NULL;

	response = g_new0 (GetMethodsResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_methods_response_handler, _get_methods_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_get_supported_auth_methods (methods_msg->book, &response->methods, &error)) {
		response->status = error->code;
		g_error_free (error);
	}

	response->book = methods_msg->book;
	response->cb = methods_msg->cb;
	response->closure = methods_msg->closure;

	push_response (response);
}

guint
e_book_async_get_supported_auth_methods (EBook                    *book,
					 EBookAuthMethodsCallback  cb,
					 gpointer                  closure)
{
	GetMethodsMsg *msg;

	g_return_val_if_fail (E_IS_BOOK (book), 0);

	init_async ();

	msg = g_new (GetMethodsMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_methods_handler, (EBookMsgDtor)g_free);

	msg->book = g_object_ref (book);
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);

	return 0;
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	char *user;
	char *passwd;
	char *auth_method;
	EBookCallback cb;
	gpointer closure;
} AuthUserMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookCallback cb;
	gpointer closure;
} AuthUserResponse;

static void
_auth_user_response_handler (EBookMsg *msg)
{
	AuthUserResponse *resp = (AuthUserResponse*)msg;

	if (resp->cb)
		resp->cb (resp->book, resp->status, resp->closure);
}

static void
_auth_user_response_dtor (EBookMsg *msg)
{
	AuthUserResponse *resp = (AuthUserResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_auth_user_handler (EBookMsg *msg)
{
	AuthUserMsg *auth_msg = (AuthUserMsg *)msg;
	AuthUserResponse *response;
	GError *error = NULL;

	response = g_new0 (AuthUserResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _auth_user_response_handler, _auth_user_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_authenticate_user (auth_msg->book, auth_msg->user, auth_msg->passwd, auth_msg->auth_method, &error)) {
		response->status = error->code;
		g_error_free (error);
	}
	response->book = auth_msg->book;
	response->cb = auth_msg->cb;
	response->closure = auth_msg->closure;

	push_response (response);
}

static void
_auth_user_dtor (EBookMsg *msg)
{
	AuthUserMsg *auth_msg = (AuthUserMsg *)msg;

	g_free (auth_msg->user);
	g_free (auth_msg->passwd);
	g_free (auth_msg->auth_method);

	g_free (auth_msg);
}

/* User authentication. */
void
e_book_async_authenticate_user (EBook                 *book,
				const char            *user,
				const char            *passwd,
				const char            *auth_method,
				EBookCallback         cb,
				gpointer              closure)
{
	AuthUserMsg *msg;

	g_return_if_fail (E_IS_BOOK (book));
	g_return_if_fail (user != NULL);
	g_return_if_fail (passwd != NULL);
	g_return_if_fail (auth_method != NULL);

	init_async ();

	msg = g_new (AuthUserMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _auth_user_handler, _auth_user_dtor);

	msg->book = g_object_ref (book);
	msg->user = g_strdup (user);
	msg->passwd = g_strdup (passwd);
	msg->auth_method = g_strdup (auth_method);
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	char *id;
	EBookContactCallback cb;
	gpointer closure;
} GetContactMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EContact *contact;
	EBookStatus status;
	EBookContactCallback cb;
	gpointer closure;
} GetContactResponse;

static void
_get_contact_response_handler (EBookMsg *msg)
{
	GetContactResponse *resp = (GetContactResponse*)msg;

	if (resp->cb)
		resp->cb (resp->book, resp->status, resp->contact, resp->closure);
}

static void
_get_contact_response_dtor (EBookMsg *msg)
{
	GetContactResponse *resp = (GetContactResponse*)msg;

	g_object_unref (resp->contact);
	g_object_unref (resp->book);
	g_free (resp);
}

static void
_get_contact_handler (EBookMsg *msg)
{
	GetContactMsg *get_contact_msg = (GetContactMsg *)msg;
	GetContactResponse *response;
	GError *error = NULL;

	response = g_new0 (GetContactResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_contact_response_handler, _get_contact_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_get_contact (get_contact_msg->book, get_contact_msg->id, &response->contact, &error)) {
		response->status = error->code;
		g_error_free (error);
	}
	response->book = get_contact_msg->book;
	response->cb = get_contact_msg->cb;
	response->closure = get_contact_msg->closure;

	push_response (response);
}

static void
_get_contact_dtor (EBookMsg *msg)
{
	GetContactMsg *get_contact_msg = (GetContactMsg *)msg;

	g_free (get_contact_msg->id);
	g_free (get_contact_msg);
}

/* Fetching contacts. */
guint
e_book_async_get_contact (EBook                 *book,
			  const char            *id,
			  EBookContactCallback   cb,
			  gpointer               closure)
{
	GetContactMsg *msg;

	g_return_val_if_fail (E_IS_BOOK (book), 0);
	g_return_val_if_fail (id != NULL, 0);

	init_async ();

	msg = g_new (GetContactMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_contact_handler, _get_contact_dtor);

	msg->book = g_object_ref (book);
	msg->id = g_strdup (id);
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);

	return 0;
}



/* Deleting cards. */
gboolean
e_book_async_remove_contact (EBook                 *book,
			     EContact              *contact,
			     EBookCallback          cb,
			     gpointer               closure)
{
	const char *id;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	id = e_contact_get_const (contact, E_CONTACT_UID);

	return e_book_async_remove_contact_by_id (book, id, cb, closure);
}

gboolean
e_book_async_remove_contact_by_id (EBook                 *book,
				   const char            *id,
				   EBookCallback          cb,
				   gpointer               closure)
{
	GList *list;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	list = g_list_append (NULL, g_strdup (id));

	return e_book_async_remove_contacts (book, list, cb, closure);
}


typedef struct {
	EBookMsg msg;

	EBook *book;
	GList *id_list;
	EBookCallback cb;
	gpointer closure;
} RemoveContactsMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookCallback cb;
	gpointer closure;
} RemoveContactsResponse;

static void
_remove_contacts_response_handler (EBookMsg *msg)
{
	RemoveContactsResponse *resp = (RemoveContactsResponse*)msg;

	if (resp->cb)
		resp->cb (resp->book, resp->status, resp->closure);
}

static void
_remove_contacts_response_dtor (EBookMsg *msg)
{
	RemoveContactsResponse *resp = (RemoveContactsResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_remove_contacts_handler (EBookMsg *msg)
{
	RemoveContactsMsg *remove_contacts_msg = (RemoveContactsMsg *)msg;
	RemoveContactsResponse *response;
	GError *error = NULL;

	response = g_new0 (RemoveContactsResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _remove_contacts_response_handler, _remove_contacts_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_remove_contacts (remove_contacts_msg->book, remove_contacts_msg->id_list, &error)) {
		response->status = error->code;
		g_error_free (error);
	}
	response->book = remove_contacts_msg->book;
	response->cb = remove_contacts_msg->cb;
	response->closure = remove_contacts_msg->closure;

	push_response (response);
}

static void
_remove_contacts_dtor (EBookMsg *msg)
{
	RemoveContactsMsg *remove_contacts_msg = (RemoveContactsMsg *)msg;

	g_list_foreach (remove_contacts_msg->id_list, (GFunc)g_free, NULL);
	g_list_free (remove_contacts_msg->id_list);
	g_free (remove_contacts_msg);
}

gboolean
e_book_async_remove_contacts (EBook                 *book,
			      GList                 *id_list,
			      EBookCallback          cb,
			      gpointer               closure)
{
	RemoveContactsMsg *msg;
	GList *l;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	init_async ();

	msg = g_new (RemoveContactsMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _remove_contacts_handler, _remove_contacts_dtor);

	msg->book = g_object_ref (book);
	msg->id_list = g_list_copy (id_list);
	for (l = msg->id_list; l; l = l->next)
		l->data = g_strdup (l->data);
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);

	return 0;
}



/* Adding contacts. */
typedef struct {
	EBookMsg msg;

	EBook *book;
	EContact *contact;
	EBookIdCallback cb;
	gpointer closure;
} AddContactMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	char *id;
	EBookStatus status;
	EBookIdCallback cb;
	gpointer closure;
} AddContactResponse;

static void
_add_contact_response_handler (EBookMsg *msg)
{
	AddContactResponse *resp = (AddContactResponse*)msg;

	if (resp->cb)
		resp->cb (resp->book, resp->status, resp->id, resp->closure);
}

static void
_add_contact_response_dtor (EBookMsg *msg)
{
	AddContactResponse *resp = (AddContactResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp->id);
	g_free (resp);
}

static void
_add_contact_handler (EBookMsg *msg)
{
	AddContactMsg *add_contact_msg = (AddContactMsg *)msg;
	AddContactResponse *response;
	GError *error = NULL;

	response = g_new0 (AddContactResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _add_contact_response_handler, _add_contact_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_add_contact (add_contact_msg->book, add_contact_msg->contact, &error)) {
		response->status = error->code;
		response->id = NULL;
		g_error_free (error);
	}
	else {
		response->id = e_contact_get (add_contact_msg->contact, E_CONTACT_UID);
	}
	response->book = add_contact_msg->book;
	response->cb = add_contact_msg->cb;
	response->closure = add_contact_msg->closure;

	push_response (response);
}

static void
_add_contact_dtor (EBookMsg *msg)
{
	AddContactMsg *add_contact_msg = (AddContactMsg *)msg;

	g_object_unref (add_contact_msg->contact);
	g_free (add_contact_msg);
}

gboolean
e_book_async_add_contact (EBook                 *book,
			  EContact              *contact,
			  EBookIdCallback        cb,
			  gpointer               closure)
{
	AddContactMsg *msg;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	init_async ();

	msg = g_new (AddContactMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _add_contact_handler, _add_contact_dtor);

	msg->book = g_object_ref (book);
	msg->contact = e_contact_duplicate (contact);
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);

	return TRUE;
}



/* Modifying cards. */
typedef struct {
	EBookMsg msg;

	EBook *book;
	EContact *contact;
	EBookCallback cb;
	gpointer closure;
} CommitContactMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookCallback cb;
	gpointer closure;
} CommitContactResponse;

static void
_commit_contact_response_handler (EBookMsg *msg)
{
	CommitContactResponse *resp = (CommitContactResponse*)msg;

	if (resp->cb)
		resp->cb (resp->book, resp->status, resp->closure);
}

static void
_commit_contact_response_dtor (EBookMsg *msg)
{
	CommitContactResponse *resp = (CommitContactResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_commit_contact_handler (EBookMsg *msg)
{
	CommitContactMsg *commit_contact_msg = (CommitContactMsg *)msg;
	CommitContactResponse *response;
	GError *error = NULL;

	response = g_new0 (CommitContactResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _commit_contact_response_handler, _commit_contact_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_commit_contact (commit_contact_msg->book, commit_contact_msg->contact, &error)) {
		response->status = error->code;
		g_error_free (error);
	}
	response->book = commit_contact_msg->book;
	response->cb = commit_contact_msg->cb;
	response->closure = commit_contact_msg->closure;

	push_response (response);
}

static void
_commit_contact_dtor (EBookMsg *msg)
{
	CommitContactMsg *commit_contact_msg = (CommitContactMsg *)msg;

	g_object_unref (commit_contact_msg->contact);
	g_free (commit_contact_msg);
}


gboolean
e_book_async_commit_contact (EBook                 *book,
			     EContact              *contact,
			     EBookCallback          cb,
			     gpointer               closure)
{
	CommitContactMsg *msg;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	init_async ();

	msg = g_new (CommitContactMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _commit_contact_handler, _commit_contact_dtor);

	msg->book = g_object_ref (book);
	msg->contact = e_contact_duplicate (contact);
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);

	return TRUE;
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookQuery *query;
	GList *requested_fields;
	int max_results;
	EBookBookViewCallback cb;
	gpointer closure;
} GetBookViewMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookView *book_view;
	EBookBookViewCallback cb;
	gpointer closure;
} GetBookViewResponse;

static void
_get_book_view_response_handler (EBookMsg *msg)
{
	GetBookViewResponse *resp = (GetBookViewResponse*)msg;

	if (resp->cb)
		resp->cb (resp->book, resp->status, resp->book_view, resp->closure);
}

static void
_get_book_view_response_dtor (EBookMsg *msg)
{
	GetBookViewResponse *resp = (GetBookViewResponse*)msg;

	g_object_unref (resp->book);
	if (resp->book_view)
		g_object_unref (resp->book_view);
	g_free (resp);
}

static void
_get_book_view_handler (EBookMsg *msg)
{
	GetBookViewMsg *view_msg = (GetBookViewMsg *)msg;
	GetBookViewResponse *response;
	GError *error = NULL;

	response = g_new0 (GetBookViewResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_book_view_response_handler, _get_book_view_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_get_book_view (view_msg->book, view_msg->query, view_msg->requested_fields, view_msg->max_results, &response->book_view, &error)) {
		response->status = error->code;
		g_error_free (error);
	}
	response->book = view_msg->book;
	response->cb = view_msg->cb;
	response->closure = view_msg->closure;

	push_response (response);
}

static void
_get_book_view_dtor (EBookMsg *msg)
{
	GetBookViewMsg *view_msg = (GetBookViewMsg *)msg;
	g_list_foreach (view_msg->requested_fields, (GFunc)g_free, NULL);
	g_list_free (view_msg->requested_fields); 
	e_book_query_unref (view_msg->query);
	g_free (view_msg);
}

guint
e_book_async_get_book_view (EBook                 *book,
			    EBookQuery            *query,
			    GList                 *requested_fields,
			    int                    max_results,
			    EBookBookViewCallback  cb,
			    gpointer               closure)
{
	GetBookViewMsg *msg;
	GList *l;

	g_return_val_if_fail (E_IS_BOOK (book), 0);
	g_return_val_if_fail (query != NULL, 0);

	init_async ();

	msg = g_new (GetBookViewMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_book_view_handler, _get_book_view_dtor);

	msg->book = g_object_ref (book);
	msg->query = e_book_query_ref (query);
	msg->requested_fields = g_list_copy (requested_fields);
	for (l = msg->requested_fields; l; l = l->next)
		l->data = g_strdup (l->data);
	msg->max_results = max_results;
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);

	return 0;
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookQuery *query;
	EBookContactsCallback cb;
	gpointer closure;
} GetContactsMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	GList *contacts;
	EBookContactsCallback cb;
	gpointer closure;
} GetContactsResponse;

static void
_get_contacts_response_handler (EBookMsg *msg)
{
	GetContactsResponse *resp = (GetContactsResponse*)msg;

	if (resp->cb)
		resp->cb (resp->book, resp->status, resp->contacts, resp->closure);
}

static void
_get_contacts_response_dtor (EBookMsg *msg)
{
	GetContactsResponse *resp = (GetContactsResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_get_contacts_handler (EBookMsg *msg)
{
	GetContactsMsg *view_msg = (GetContactsMsg *)msg;
	GetContactsResponse *response;
	GError *error = NULL;

	response = g_new0 (GetContactsResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_contacts_response_handler, _get_contacts_response_dtor);

	response->status = E_BOOK_ERROR_OK;
	if (!e_book_get_contacts (view_msg->book, view_msg->query, &response->contacts, &error)) {
		response->status = error->code;
		g_error_free (error);
	}
	response->book = view_msg->book;
	response->cb = view_msg->cb;
	response->closure = view_msg->closure;

	push_response (response);
}

static void
_get_contacts_dtor (EBookMsg *msg)
{
	GetContactsMsg *view_msg = (GetContactsMsg *)msg;
	
	e_book_query_unref (view_msg->query);
	g_free (view_msg);
}

guint
e_book_async_get_contacts (EBook                 *book,
			   EBookQuery            *query,
			   EBookContactsCallback  cb,
			   gpointer              closure)
{
	GetContactsMsg *msg;

	g_return_val_if_fail (E_IS_BOOK (book), 0);
	g_return_val_if_fail (query != NULL, 0);

	init_async ();

	msg = g_new (GetContactsMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_contacts_handler, _get_contacts_dtor);

	msg->book = g_object_ref (book);
	msg->query = e_book_query_ref (query);
	msg->cb = cb;
	msg->closure = closure;

	g_thread_pool_push (worker_pool, msg, NULL);

	return 0;
}

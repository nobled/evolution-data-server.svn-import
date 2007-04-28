/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors:
 *   Michael Zucchi <notzed@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com>
 *   Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 2000, 2003 Ximian, Inc.
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#define EWOULDBLOCK EAGAIN
#endif

#include <libedataserver/e-data-server-util.h>

#include "camel-file-utils.h"
#include "camel-operation.h"
#include "camel-url.h"

#define IO_TIMEOUT (60*4)

/**
 * camel_file_util_encode_uint32:
 * @out: file to output to
 * @value: value to output
 * 
 * Utility function to save an uint32 to a file.
 * 
 * Return value: 0 on success, -1 on error.
 **/
int
camel_file_util_encode_uint32 (FILE *out, guint32 value)
{
	int i;

	for (i = 28; i > 0; i -= 7) {
		if (value >= (1 << i)) {
			unsigned int c = (value >> i) & 0x7f;
			if (fputc (c, out) == -1)
				return -1;
		}
	}
	return fputc (value | 0x80, out);
}


/**
 * camel_file_util_decode_uint32:
 * @in: file to read from
 * @dest: pointer to a variable to store the value in
 * 
 * Retrieve an encoded uint32 from a file.
 * 
 * Return value: 0 on success, -1 on error.  @*dest will contain the
 * decoded value.
 **/
int
camel_file_util_decode_uint32 (FILE *in, guint32 *dest)
{
        guint32 value = 0;
	int v;

        /* until we get the last byte, keep decoding 7 bits at a time */
        while ( ((v = fgetc (in)) & 0x80) == 0 && v!=EOF) {
                value |= v;
                value <<= 7;
        }
	if (v == EOF) {
		*dest = value >> 7;
		return -1;
	}
	*dest = value | (v & 0x7f);

        return 0;
}


/**
 * camel_file_util_encode_fixed_int32:
 * @out: file to output to
 * @value: value to output
 * 
 * Encode a gint32, performing no compression, but converting
 * to network order.
 * 
 * Return value: 0 on success, -1 on error.
 **/
int
camel_file_util_encode_fixed_int32 (FILE *out, gint32 value)
{
	guint32 save;

	save = g_htonl (value);
	if (fwrite (&save, sizeof (save), 1, out) != 1)
		return -1;
	return 0;
}


/**
 * camel_file_util_decode_fixed_int32:
 * @in: file to read from
 * @dest: pointer to a variable to store the value in
 * 
 * Retrieve a gint32.
 * 
 * Return value: 0 on success, -1 on error.
 **/
int
camel_file_util_decode_fixed_int32 (FILE *in, gint32 *dest)
{
	guint32 save;

	if (fread (&save, sizeof (save), 1, in) == 1) {
		*dest = g_ntohl (save);
		return 0;
	} else {
		return -1;
	}
}

#define CFU_ENCODE_T(type)						\
int									\
camel_file_util_encode_##type(FILE *out, type value)			\
{									\
	int i;								\
									\
	for (i = sizeof (type) - 1; i >= 0; i--) {			\
		if (fputc((value >> (i * 8)) & 0xff, out) == -1)	\
			return -1;					\
	}								\
	return 0;							\
}

#define CFU_DECODE_T(type)				\
int							\
camel_file_util_decode_##type(FILE *in, type *dest)	\
{							\
	type save = 0;					\
	int i = sizeof(type) - 1;			\
	int v = EOF;					\
							\
        while (i >= 0 && (v = fgetc (in)) != EOF) {	\
		save |= ((type)v) << (i * 8);		\
		i--;					\
	}						\
	*dest = save;					\
	if (v == EOF)					\
		return -1;				\
	return 0;					\
}


/**
 * camel_file_util_encode_time_t:
 * @out: file to output to
 * @value: value to output
 * 
 * Encode a time_t value to the file.
 * 
 * Return value: 0 on success, -1 on error.
 **/
CFU_ENCODE_T(time_t)

/**
 * camel_file_util_decode_time_t:
 * @in: file to read from
 * @dest: pointer to a variable to store the value in
 * 
 * Decode a time_t value.
 * 
 * Return value: 0 on success, -1 on error.
 **/
CFU_DECODE_T(time_t)

/**
 * camel_file_util_encode_off_t:
 * @out: file to output to
 * @value: value to output
 * 
 * Encode an off_t type.
 * 
 * Return value: 0 on success, -1 on error.
 **/
CFU_ENCODE_T(off_t)


/**
 * camel_file_util_decode_off_t:
 * @in: file to read from
 * @dest: pointer to a variable to put the value in
 * 
 * Decode an off_t type.
 * 
 * Return value: 0 on success, -1 on failure.
 **/
CFU_DECODE_T(off_t)

/**
 * camel_file_util_encode_size_t:
 * @out: file to output to
 * @value: value to output
 * 
 * Encode an size_t type.
 * 
 * Return value: 0 on success, -1 on error.
 **/
CFU_ENCODE_T(size_t)


/**
 * camel_file_util_decode_size_t:
 * @in: file to read from
 * @dest: pointer to a variable to put the value in
 * 
 * Decode an size_t type.
 * 
 * Return value: 0 on success, -1 on failure.
 **/
CFU_DECODE_T(size_t)


/**
 * camel_file_util_encode_string:
 * @out: file to output to
 * @str: value to output
 * 
 * Encode a normal string and save it in the output file.
 * 
 * Return value: 0 on success, -1 on error.
 **/
int
camel_file_util_encode_string (FILE *out, const char *str)
{
	register int len;

	if (str == NULL)
		return camel_file_util_encode_uint32 (out, 1);
	
	if ((len = strlen (str)) > 65536)
		len = 65536;
	
	if (camel_file_util_encode_uint32 (out, len+1) == -1)
		return -1;
	if (len == 0 || fwrite (str, len, 1, out) == 1)
		return 0;
	return -1;
}


/**
 * camel_file_util_decode_string:
 * @in: file to read from
 * @str: pointer to a variable to store the value in
 * 
 * Decode a normal string from the input file.
 * 
 * Return value: 0 on success, -1 on error.
 **/
int
camel_file_util_decode_string (FILE *in, char **str)
{
	guint32 len;
	register char *ret;

	if (camel_file_util_decode_uint32 (in, &len) == -1) {
		*str = NULL;
		return -1;
	}

	len--;
	if (len > 65536) {
		*str = NULL;
		return -1;
	}

	ret = g_malloc (len+1);
	if (len > 0 && fread (ret, len, 1, in) != 1) {
		g_free (ret);
		*str = NULL;
		return -1;
	}

	ret[len] = 0;
	*str = ret;
	return 0;
}

/**
 * camel_file_util_encode_fixed_string:
 * @out: file to output to
 * @str: value to output
 * @len: total-len of str to store
 * 
 * Encode a normal string and save it in the output file.
 * Unlike @camel_file_util_encode_string, it pads the
 * @str with "NULL" bytes, if @len is > strlen(str)
 * 
 * Return value: 0 on success, -1 on error.
 **/
int
camel_file_util_encode_fixed_string (FILE *out, const char *str, size_t len)
{
	char buf[len];

	/* Don't allow empty strings to be written */
	if (len < 1)
		return -1;

	/* Max size is 64K */
	if (len > 65536)
		len = 65536;
		
	memset(buf, 0x00, len);
	g_strlcpy(buf, str, len);

	if (fwrite (buf, len, 1, out) == len)
		return 0;

	return -1;
}


/**
 * camel_file_util_decode_fixed_string:
 * @in: file to read from
 * @str: pointer to a variable to store the value in
 * @len: total-len to decode.  
 * 
 * Decode a normal string from the input file.
 * 
 * Return value: 0 on success, -1 on error.
 **/
int
camel_file_util_decode_fixed_string (FILE *in, char **str, size_t len)
{
	register char *ret;

	if (len > 65536) {
		*str = NULL;
		return -1;
	}

	ret = g_malloc (len+1);
	if (len > 0 && fread (ret, len, 1, in) != 1) {
		g_free (ret);
		*str = NULL;
		return -1;
	}

	ret[len] = 0;
	*str = ret;
	return 0;
}

/**
 * camel_file_util_safe_filename:
 * @name: string to 'flattened' into a safe filename
 *
 * 'Flattens' @name into a safe filename string by hex encoding any
 * chars that may cause problems on the filesystem.
 *
 * Returns a safe filename string.
 **/
char *
camel_file_util_safe_filename (const char *name)
{
#ifdef G_OS_WIN32
	const char *unsafe_chars = "/?()'*<>:\"\\|";
#else
	const char *unsafe_chars = "/?()'*";
#endif

	if (name == NULL)
		return NULL;
	
	return camel_url_encode(name, unsafe_chars);
}


/* FIXME: poll() might be more efficient and more portable? */

/**
 * camel_read:
 * @fd: file descriptor
 * @buf: buffer to fill
 * @n: number of bytes to read into @buf
 *
 * Cancellable libc read() replacement.
 *
 * Code that intends to be portable to Win32 should call this function
 * only on file descriptors returned from open(), not on sockets.
 *
 * Returns number of bytes read or -1 on fail. On failure, errno will
 * be set appropriately.
 **/
ssize_t
camel_read (int fd, char *buf, size_t n)
{
	ssize_t nread;
	int cancel_fd;
	
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
#ifndef G_OS_WIN32
	cancel_fd = camel_operation_cancel_fd (NULL);
#else
	cancel_fd = -1;
#endif
	if (cancel_fd == -1) {
		do {
			nread = read (fd, buf, n);
		} while (nread == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
	} else {
#ifndef G_OS_WIN32
		int errnosav, flags, fdmax;
		fd_set rdset;
		
		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);
		
		do {
			struct timeval tv;
			int res;

			FD_ZERO (&rdset);
			FD_SET (fd, &rdset);
			FD_SET (cancel_fd, &rdset);
			fdmax = MAX (fd, cancel_fd) + 1;
			tv.tv_sec = IO_TIMEOUT;
			tv.tv_usec = 0;
			nread = -1;

			res = select(fdmax, &rdset, 0, 0, &tv);
			if (res == -1)
				;
			else if (res == 0)
				errno = ETIMEDOUT;
			else if (FD_ISSET (cancel_fd, &rdset)) {
				errno = EINTR;
				goto failed;
			} else {				
				do {
					nread = read (fd, buf, n);
				} while (nread == -1 && errno == EINTR);
			}
		} while (nread == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
	failed:
		errnosav = errno;
		fcntl (fd, F_SETFL, flags);
		errno = errnosav;
#endif
	}
	
	return nread;
}


/**
 * camel_write:
 * @fd: file descriptor
 * @buf: buffer to write
 * @n: number of bytes of @buf to write
 *
 * Cancellable libc write() replacement.
 *
 * Code that intends to be portable to Win32 should call this function
 * only on file descriptors returned from open(), not on sockets.
 *
 * Returns number of bytes written or -1 on fail. On failure, errno will
 * be set appropriately.
 **/
ssize_t
camel_write (int fd, const char *buf, size_t n)
{
	ssize_t w, written = 0;
	int cancel_fd;
	
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
#ifndef G_OS_WIN32
	cancel_fd = camel_operation_cancel_fd (NULL);
#else
	cancel_fd = -1;
#endif
	if (cancel_fd == -1) {
		do {
			do {
				w = write (fd, buf + written, n - written);
			} while (w == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
			if (w > 0)
				written += w;
		} while (w != -1 && written < n);
	} else {
#ifndef G_OS_WIN32
		int errnosav, flags, fdmax;
		fd_set rdset, wrset;
		
		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);
		
		fdmax = MAX (fd, cancel_fd) + 1;
		do {
			struct timeval tv;
			int res;

			FD_ZERO (&rdset);
			FD_ZERO (&wrset);
			FD_SET (fd, &wrset);
			FD_SET (cancel_fd, &rdset);
			tv.tv_sec = IO_TIMEOUT;
			tv.tv_usec = 0;			
			w = -1;

			res = select (fdmax, &rdset, &wrset, 0, &tv);
			if (res == -1) {
				if (errno == EINTR)
					w = 0;
			} else if (res == 0)
				errno = ETIMEDOUT;
			else if (FD_ISSET (cancel_fd, &rdset))
				errno = EINTR;
			else {
				do {
					w = write (fd, buf + written, n - written);
				} while (w == -1 && errno == EINTR);
				
				if (w == -1) {
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						w = 0;
				} else
					written += w;
			}
		} while (w != -1 && written < n);
		
		errnosav = errno;
		fcntl (fd, F_SETFL, flags);
		errno = errnosav;
#endif
	}
	
	if (w == -1)
		return -1;
	
	return written;
}

/**
 * camel_read_socket:
 * @fd: a socket
 * @buf: buffer to fill
 * @n: number of bytes to read into @buf
 *
 * Cancellable read() replacement for sockets. Code that intends to be
 * portable to Win32 should call this function only on sockets
 * returned from socket(), or accept().
 *
 * Returns number of bytes read or -1 on fail. On failure, errno will
 * be set appropriately. If the socket is nonblocking
 * camel_read_socket() will retry the read until it gets something.
 **/
ssize_t
camel_read_socket (int fd, char *buf, size_t n)
{
#ifndef G_OS_WIN32
	return camel_read (fd, buf, n);
#else
	ssize_t nread;
	int cancel_fd;
	
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
	cancel_fd = camel_operation_cancel_fd (NULL);

	if (cancel_fd == -1) {
		do {
			nread = recv (fd, buf, n, 0);
		} while (nread == SOCKET_ERROR && WSAGetLastError () == WSAEWOULDBLOCK);
	} else {
		int fdmax;
		fd_set rdset;
		u_long yes = 1;

		ioctlsocket (fd, FIONBIO, &yes);
		fdmax = MAX (fd, cancel_fd) + 1;
		do {
			struct timeval tv;
			int res;

			FD_ZERO (&rdset);
			FD_SET (fd, &rdset);
			FD_SET (cancel_fd, &rdset);
			tv.tv_sec = IO_TIMEOUT;
			tv.tv_usec = 0;
			nread = -1;

			res = select(fdmax, &rdset, 0, 0, &tv);
			if (res == -1)
				;
			else if (res == 0)
				errno = ETIMEDOUT;
			else if (FD_ISSET (cancel_fd, &rdset)) {
				errno = EINTR;
				goto failed;
			} else {				
				nread = recv (fd, buf, n, 0);
			}
		} while (nread == -1 && WSAGetLastError () == WSAEWOULDBLOCK);
	failed:
		;
	}
	
	return nread;
#endif
}

/**
 * camel_write_socket:
 * @fd: file descriptor
 * @buf: buffer to write
 * @n: number of bytes of @buf to write
 *
 * Cancellable write() replacement for sockets. Code that intends to
 * be portable to Win32 should call this function only on sockets
 * returned from socket() or accept().
 *
 * Returns number of bytes written or -1 on fail. On failure, errno will
 * be set appropriately.
 **/
ssize_t
camel_write_socket (int fd, const char *buf, size_t n)
{
#ifndef G_OS_WIN32
	return camel_write (fd, buf, n);
#else
	ssize_t w, written = 0;
	int cancel_fd;
	
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	cancel_fd = camel_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {
		do {
			do {
				w = send (fd, buf + written, n - written, 0);
			} while (w == SOCKET_ERROR && WSAGetLastError () == WSAEWOULDBLOCK);
			if (w > 0)
				written += w;
		} while (w != -1 && written < n);
	} else {
		int fdmax;
		fd_set rdset, wrset;
		u_long arg = 1;

		ioctlsocket (fd, FIONBIO, &arg);
		fdmax = MAX (fd, cancel_fd) + 1;
		do {
			struct timeval tv;
			int res;

			FD_ZERO (&rdset);
			FD_ZERO (&wrset);
			FD_SET (fd, &wrset);
			FD_SET (cancel_fd, &rdset);
			tv.tv_sec = IO_TIMEOUT;
			tv.tv_usec = 0;			
			w = -1;

			res = select (fdmax, &rdset, &wrset, 0, &tv);
			if (res == SOCKET_ERROR) {
				/* w still being -1 will catch this */
			} else if (res == 0)
				errno = ETIMEDOUT;
			else if (FD_ISSET (cancel_fd, &rdset))
				errno = EINTR;
			else {
				w = send (fd, buf + written, n - written, 0);
				if (w == SOCKET_ERROR) {
					if (WSAGetLastError () == WSAEWOULDBLOCK)
						w = 0;
				} else
					written += w;
			}
		} while (w != -1 && written < n);
		arg = 0;
		ioctlsocket (fd, FIONBIO, &arg);
	}
	
	if (w == -1)
		return -1;
	
	return written;
#endif
}


/**
 * camel_file_util_savename:
 * @filename: a pathname
 * 
 * Builds a pathname where the basename is of the form ".#" + the
 * basename of @filename, for instance used in a two-stage commit file
 * write.
 * 
 * Return value: The new pathname.  It must be free'd with g_free().
 **/
char *
camel_file_util_savename(const char *filename)
{
	char *dirname, *retval;

	dirname = g_path_get_dirname(filename);

	if (strcmp (dirname, ".") == 0) {
		retval = g_strconcat (".#", filename, NULL);
	} else {
		char *basename = g_path_get_basename(filename);
		char *newbasename = g_strconcat (".#", basename, NULL);

		retval = g_build_filename (dirname, newbasename, NULL);

		g_free (newbasename);
		g_free (basename);
	}
	g_free (dirname);

	return retval;
}

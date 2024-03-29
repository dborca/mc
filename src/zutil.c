/*
 * Copyright (c) 2007 Daniel Borca  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <config.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include "global.h"
#include "wtools.h"
#include "zutil.h"


#if GLIB_MAJOR_VERSION > 0
#include "../slib/array.c"
#endif


/* buffered I/O **************************************************************/


#define FILE_READ_BUF	4096
#define FILE_FLAG_TEMP	(1 << 0)
#define FILE_FLAG_COPY	(1 << 1)

#define FILE_DIRTY(fs)	\
    do {		\
	(fs)->pos = 0;	\
	(fs)->len = 0;	\
    } while (0)

struct FBUF {
    int fd;
    int pos;
    int len;
    char *buf;
    int flags;
    void *data;
    void *name;
};


FBUF *
f_dopen (int fd)
{
    FBUF *fs;

    if (fd < 0) {
	return NULL;
    }

    fs = malloc(sizeof(FBUF));
    if (fs == NULL) {
	return NULL;
    }

    fs->buf = malloc(FILE_READ_BUF);
    if (fs->buf == NULL) {
	free(fs);
	return NULL;
    }

    fs->fd = fd;
    FILE_DIRTY(fs);
    fs->flags = 0;
    fs->data = NULL;
    fs->name = NULL;

    return fs;
}


int
f_free (FBUF *fs)
{
    int rv = 0;
    if (fs->flags & FILE_FLAG_COPY) {
	rv = mc_ungetlocalcopy(fs->name, fs->data, FALSE);
	g_free(fs->data);
    }
    if (fs->flags & FILE_FLAG_TEMP) {
	rv = unlink(fs->name);
    }
    g_free(fs->name);
    free(fs->buf);
    free(fs);
    return rv;
}


FBUF *
f_temp (void)
{
    int fd;
    FBUF *fs;

    fs = f_dopen(0);
    if (fs == NULL) {
	return NULL;
    }

    fd = mc_mkstemps((char **)&fs->name, "mcdiff", NULL);
    if (fd < 0) {
	f_free(fs);
	return NULL;
    }

    fs->fd = fd;
    fs->flags = FILE_FLAG_TEMP;
    return fs;
}


FBUF *
f_open (const char *filename, int flags)
{
    int fd;
    FBUF *fs;

    fs = f_dopen(0);
    if (fs == NULL) {
	return NULL;
    }

    fs->name = g_strdup(filename);
    if (!fs->name) {
	f_free(fs);
	return NULL;
    }

    if (vfs_file_is_local(filename)) {
	fd = open(filename, flags);
    } else {
	fd = -1;
	fs->data = mc_getlocalcopy(filename);
	if (fs->data) {
	    fs->flags = FILE_FLAG_COPY;
	    fd = open(fs->data, flags);
	}
    }
    if (fd < 0) {
	f_free(fs);
	return NULL;
    }

    fs->fd = fd;
    return fs;
}


size_t
f_read (FBUF *fs, char *buf, size_t size)
{
    size_t j = 0;

    do {
	int i;

	for (i = fs->pos; j < size && i < fs->len; i++, j++) {
	    buf[j] = fs->buf[i];
	}
	fs->pos = i;

	if (j == size) {
	    break;
	}

	fs->pos = 0;
	fs->len = read(fs->fd, fs->buf, FILE_READ_BUF);
    } while (fs->len > 0);

    return j;
}


size_t
f_gets (char *buf, size_t size, FBUF *fs)
{
    size_t j = 0;

    do {
	int i;
	int stop = 0;

	for (i = fs->pos; j < size && i < fs->len && !stop; i++, j++) {
	    buf[j] = fs->buf[i];
	    if (buf[j] == '\n') {
		stop = 1;
	    }
	}
	fs->pos = i;

	if (j == size || stop) {
	    break;
	}

	fs->pos = 0;
	fs->len = read(fs->fd, fs->buf, FILE_READ_BUF);
    } while (fs->len > 0);

    return j;
}


int
f_getc (FBUF *fs)
{
    do {
	if (fs->pos < fs->len) {
	    return (unsigned char)fs->buf[fs->pos++];
	}

	fs->pos = 0;
	fs->len = read(fs->fd, fs->buf, FILE_READ_BUF);
    } while (fs->len > 0);

    return -1;
}


off_t
f_seek (FBUF *fs, off_t off, int whence)
{
    off_t rv;

    if (fs->len && whence != SEEK_END) {
	rv = lseek(fs->fd, 0, SEEK_CUR);
	if (rv != -1) {
	    if (whence == SEEK_CUR) {
		whence = SEEK_SET;
		off += rv - fs->len + fs->pos;
	    }
	    if (off - rv >= -fs->len && off - rv <= 0) {
		fs->pos = fs->len + off - rv;
		return off;
	    }
	}
    }

    rv = lseek(fs->fd, off, whence);
    if (rv != -1) {
	FILE_DIRTY(fs);
    }
    return rv;
}


ssize_t
f_write (FBUF *fs, const char *buf, size_t size)
{
    ssize_t rv = write(fs->fd, buf, size);
    if (rv >= 0) {
	FILE_DIRTY(fs);
    }
    return rv;
}


const char *
f_getname (FBUF *fs)
{
    if (fs->flags & FILE_FLAG_COPY) {
	return fs->data;
    }
    return fs->name;
}


int
f_close (FBUF *fs)
{
    int rv = -1;
    if (fs) {
	rv = close(fs->fd);
	f_free(fs);
    }
    return rv;
}


FBUF *
p_open (const char *cmd, int flags)
{
    FILE *f;
    FBUF *fs;
    const char *type = NULL;

    if (flags == O_RDONLY) {
	type = "r";
    }
    if (flags == O_WRONLY) {
	type = "w";
    }

    if (type == NULL) {
	return NULL;
    }

    fs = f_dopen(0);
    if (fs == NULL) {
	return NULL;
    }

    f = popen(cmd, type);
    if (f == NULL) {
	f_free(fs);
	return NULL;
    }

    fs->fd = fileno(f);
    fs->data = f;
    return fs;
}


int
p_close (FBUF *fs)
{
    int rv = pclose(fs->data);
    f_free(fs);
    return rv;
}


/* search ********************************************************************/


#define IS_WHOLE_OR_DONT_CARE()							\
    (!whole || (								\
     (i == 0 || strchr(wholechars, haystack[i - 1]) == NULL) &&			\
     (i + nlen == hlen || strchr(wholechars, haystack[i + nlen]) == NULL)	\
    ))

static const char *wholechars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_";


const unsigned char *
memmem_dumb (const unsigned char *haystack, size_t i, size_t hlen, const unsigned char *needle, size_t nlen, int whole)
{
    for (; i + nlen <= hlen; i++) {
	if (haystack[i] == needle[0]) {
	    size_t j;
	    for (j = 1; j < nlen; j++) {
		if (haystack[i + j] != needle[j]) {
		    break;
		}
	    }
	    if (j == nlen && IS_WHOLE_OR_DONT_CARE()) {
		return haystack + i;
	    }
	}
    }

    return NULL;
}


const unsigned char *
memmem_dumb_nocase (const unsigned char *haystack, size_t i, size_t hlen, const unsigned char *needle, size_t nlen, int whole)
{
    for (; i + nlen <= hlen; i++) {
	if (toupper(haystack[i]) == toupper(needle[0])) {
	    size_t j;
	    for (j = 1; j < nlen; j++) {
		if (toupper(haystack[i + j]) != toupper(needle[j])) {
		    break;
		}
	    }
	    if (j == nlen && IS_WHOLE_OR_DONT_CARE()) {
		return haystack + i;
	    }
	}
    }

    return NULL;
}


/* stuff *********************************************************************/


int
scan_deci (const char **str, int *n)
{
    const char *p = *str;
    char *q;
    errno = 0;
    *n = strtol(p, &q, 10);
    if (errno || p == q) {
	return -1;
    }
    *str = q;
    return 0;
}


int
get_digits (unsigned int n)
{
    int d = 1;
    while (n /= 10) {
	d++;
    }
    return d;
}

/*
 * Copyright (c) 2008 Daniel Borca  All rights reserved.
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
#include "global.h"
#include "tty.h"
#include "cmd.h"
#include "dialog.h"
#include "widget.h"
#include "color.h"
#include "help.h"
#include "key.h"
#include "layout.h"
#include "wtools.h"
#include "panel.h"		/* Needed for current_panel and other_panel */
#include "charsets.h"
#include "selcodepage.h"
#include "main.h"
#include "zutil.h"
#include "xdiff.h"
#include "ydiff.h"
#include "zdiff.h"


#ifdef USE_DIFF_VIEW

#define RECURSIVE_DEPTH 100

#define ADD_CH	'+'
#define DEL_CH	'-'
#define CHG_CH	'*'
#define EQU_CH	' '
#define ERR_CH	'!'
#define DIR_CH	'/'

#if 1
#define make_tmp_path(s1, s2)	_bufpath(buf, s1, s2)
#define make_new_path(s1, s2)	_bufpath(buf, s1, s2)
#define make_1st_path		bufpath_1st
#define make_2nd_path		bufpath_2nd
#define free_tmp_path(p)
#define free_new_path(p)
#define free_1st_path(p)
#define free_2nd_path(p)
#else
#define make_tmp_path		strpath
#define make_new_path		strpath
#define make_1st_path		strpath
#define make_2nd_path		strpath
#define free_tmp_path(p)	free(p)
#define free_new_path(p)	free(p)
#define free_1st_path(p)	free(p)
#define free_2nd_path(p)	free(p)
#endif

typedef struct DNODE {
    const struct DNODE *link;
    const struct stat *st[2];
} DNODE;

typedef int (*DFUNC) (void *ctx, int ch, const char *f1, const char *f2);

static int diff_file (const char *r0, const char *f0, const char *r1, const char *f1, int recursive, const DNODE *prev, DFUNC printer, void *ctx);

#define is_eq(c) ((c) == EQU_CH || (c) == DIR_CH)

typedef struct {
    int ch;
    char *name[2];
} LNODE;

typedef struct {
    Widget widget;

    int recursive;
    const char *dir[2];		/* filenames */
    ARRAY z;
    int ndiff;			/* number of hunks */

    int view_quit:1;		/* Quit flag */

    int height;
    int half1;
    int half2;
    int bias;
    int new_frame;
    int skip_rows;
    int skip_cols;
    int display_symbols;
    int display_numbers;
    int ord;
    int full;
    int last_found;
} WDiff;


#define OPTX 50
#define OPTY 8

static QuickWidget diffopt_widgets[] = {
    { quick_button,   6,   10, 5, OPTY, N_("&Cancel"),    0, B_CANCEL, NULL, NULL, NULL },
    { quick_button,   3,   10, 5, OPTY, N_("&OK"),        0, B_ENTER,  NULL, NULL, NULL },
    { quick_checkbox, 4, OPTX, 3, OPTY, N_("Recursi&ve"), 0, 0,        NULL, NULL, NULL },
    NULL_QuickWidget
};

static QuickDialog diffopt = {
    OPTX, OPTY, -1, -1,
    N_(" Diff Options "), "[Directory Diff Options]",
    diffopt_widgets, 0
};

#define SEARCH_DLG_WIDTH  58
#define SEARCH_DLG_HEIGHT 10

static QuickWidget search_widgets[] = {
    { quick_button,    6,               10, 7, SEARCH_DLG_HEIGHT, N_("&Cancel"),                0, B_CANCEL, NULL, NULL, NULL },
    { quick_button,    2,               10, 7, SEARCH_DLG_HEIGHT, N_("&OK"),                    0, B_ENTER,  NULL, NULL, NULL },
    { quick_checkbox, 33, SEARCH_DLG_WIDTH, 4, SEARCH_DLG_HEIGHT, N_("&Backwards"),             0, 0,        NULL, NULL, NULL },
    { quick_checkbox,  4, SEARCH_DLG_WIDTH, 6, SEARCH_DLG_HEIGHT, N_("&Regular expression"),    0, 0,        NULL, NULL, NULL },
    { quick_checkbox,  4, SEARCH_DLG_WIDTH, 5, SEARCH_DLG_HEIGHT, N_("&Whole words only"),      0, 0,        NULL, NULL, NULL },
    { quick_checkbox,  4, SEARCH_DLG_WIDTH, 4, SEARCH_DLG_HEIGHT, N_("case &Sensitive"),        0, 0,        NULL, NULL, NULL },
    { quick_input,     3, SEARCH_DLG_WIDTH, 3, SEARCH_DLG_HEIGHT, "",                          52, 0,        NULL, NULL, N_("Search") },
    { quick_label,     2, SEARCH_DLG_WIDTH, 2, SEARCH_DLG_HEIGHT, N_(" Enter search string:"),  0, 0,        NULL, NULL, NULL },
     NULL_QuickWidget
};

static QuickDialog search_input = {
    SEARCH_DLG_WIDTH, SEARCH_DLG_HEIGHT, -1, 0,
    N_("Search"), "[Input Line Keys]",
    search_widgets, 0
};

#define error_dialog(h, s) query_dialog(h, s, D_ERROR, 1, _("&Dismiss"))


/* diff parse ****************************************************************/


/**
 * Concatenate two strings to make a path.
 *
 * \param p path buffer (must be large enough)
 * \param s1 1st component
 * \param s2 2nd component (may be NULL)
 *
 * \return static path
 *
 * \note the user must not free this buffer
 */
static char *
_bufpath (char *p, const char *s1, const char *s2)
{
    if (s2 == NULL) {
	strcpy(p, s1);
    } else {
	int len = strlen(s1);
	memcpy(p, s1, len);
	p[len++] = '/';
	strcpy(p + len, s2);
    }
    return p;
}


/**
 * Concatenate two strings to make a path.
 *
 * \param s1 1st component
 * \param s2 2nd component (may be NULL)
 *
 * \return static path
 *
 * \note the user must not free this buffer
 */
static char *
bufpath_1st (const char *s1, const char *s2)
{
    static char p[3 * PATH_MAX];
    return _bufpath(p, s1, s2);
}


/**
 * Concatenate two strings to make a path.
 *
 * \param s1 1st component
 * \param s2 2nd component (may be NULL)
 *
 * \return static path
 *
 * \note the user must not free this buffer
 */
static char *
bufpath_2nd (const char *s1, const char *s2)
{
    static char p[3 * PATH_MAX];
    return _bufpath(p, s1, s2);
}


/**
 * Concatenate two strings to make a path.
 *
 * \param s1 1st component
 * \param s2 2nd component (may be NULL)
 *
 * \return allocated path
 *
 * \note the user must free this buffer
 */
static char *
strpath (const char *s1, const char *s2)
{
    char *p;
    if (s2 == NULL) {
	p = strdup(s1);
    } else {
	int len = strlen(s1);
	p = malloc(len + 1 + strlen(s2) + 1);
	if (p != NULL) {
	    memcpy(p, s1, len);
	    p[len++] = '/';
	    strcpy(p + len, s2);
	}
    }
    return p;
}


/**
 * Scan directory.
 *
 * \param a list of items to fill
 * \param pre first component of directory
 * \param name second component of directory
 *
 * \return 0 if success
 */
static int
scan_dir (ARRAY *a, const char *pre, const char *name)
{
    char buf[2 * PATH_MAX];	/* XXX for _bufpath */
    char *p;
    DIR *dir;
    int rv = 0;

    p = make_tmp_path(pre, name);
    if (p == NULL) {
	return -1;
    }

    dir = mc_opendir(p);
    if (dir == NULL) {
	free_tmp_path(p);
	return -1;
    }

    for (;;) {
	char **q;
	const struct dirent *ent;

	errno = 0;
	ent = mc_readdir(dir);
	if (ent == NULL) {
	    if (errno) {
		rv = errno;	/* XXX should we try to continue? */
	    }
	    break;
	}

	if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
	    continue;
	}

	q = arr_enlarge(a);
	if (q == NULL) {
	    rv = -1;
	    break;
	}

	*q = strdup(ent->d_name);
	if (*q == NULL) {
	    rv = -1;
	    break;
	}
    }

    mc_closedir(dir);
    free_tmp_path(p);

    return rv;
}


/**
 * Comparator for qsort.
 *
 * \param f1 1st item
 * \param f2 2nd item
 *
 * \return strcmp-like result
 */
static int
compar (const void *f1, const void *f2)
{
    return strcmp(*(const char *const *)f1, *(const char *const *)f2);
}


/**
 * Helper to free directory items.
 *
 * \param p element
 */
static void
dispose (void *p)
{
    free(*(char **)p);
}


/**
 * Compare two binary files.
 *
 * \param p0 1st filename
 * \param p1 2nd filename
 * \param st array of two stat structs
 *
 * \return 0 if files are identical, 1 if different, -1 if error
 */
static int
diff_binary (const char *p0, const char *p1, const struct stat st[2])
{
    int rv = 0;
    int fd0, fd1;
    off_t size;

    if (st[0].st_size != st[1].st_size) {
	return 1;
    }
    if (st[0].st_ino == st[1].st_ino && vfs_file_is_local(p0) && vfs_file_is_local(p1)) {
	return 0;
    }
    size = st[0].st_size;

    fd0 = mc_open(p0, O_RDONLY | O_BINARY);
    if (fd0 < 0) {
	return -1;
    }
    fd1 = mc_open(p1, O_RDONLY | O_BINARY);
    if (fd1 < 0) {
	mc_close(fd0);
	return -1;
    }

    while (size > 0) {
	char buf0[BUFSIZ], buf1[BUFSIZ];
	int n0 = mc_read(fd0, buf0, sizeof(buf0));
	int n1 = mc_read(fd1, buf1, sizeof(buf1));
	if (n0 != n1) {
	    rv = 1;
	    break;
	}
	if (n0 <= 0) {
	    rv = -1;
	    break;
	}
	if (memcmp(buf0, buf1, n0)) {
	    rv = 1;
	    break;
	}
	size -= n0;
    }

    mc_close(fd1);
    mc_close(fd0);
    return rv;
}


/**
 * Compare two subdirectories.
 *
 * \param r0 1st path component
 * \param r1 2nd path component
 * \param d common subdirectory name
 * \param recursive max allowed depth
 * \param prev top of stack of parent subdirectories
 * \param printer callback
 * \param ctx opaque object to be passed to callback
 *
 * \return 0 success, otherwise error
 */
static int
diff_dirs (const char *r0, const char *r1, const char *d, int recursive, const DNODE *prev, DFUNC printer, void *ctx)
{
    char buf[2 * PATH_MAX];	/* XXX for _bufpath */
    ARRAY a[2];
    int rv = -1;
    arr_init(&a[0], sizeof(char *), 256);
    arr_init(&a[1], sizeof(char *), 256);
    if (scan_dir(&a[0], r0, d) == 0 && scan_dir(&a[1], r1, d) == 0) {
	int i = 0;
	int j = 0;
	char **q0 = a[0].data;
	char **q1 = a[1].data;

	rv = 0;
	qsort(a[0].data, a[0].len, a[0].eltsize, compar);
	qsort(a[1].data, a[1].len, a[1].eltsize, compar);

	while (i < a[0].len || j < a[1].len) {
	    char *tmp = NULL;
	    char *f0 = NULL;
	    char *f1 = NULL;
	    int nameorder = (i >= a[0].len) ? 1 : (j >= a[1].len) ? -1 : compar(q0, q1);
	    if (nameorder <= 0) {
		f0 = *q0++;
		i++;
		if (d != NULL) {
		    tmp = f0 = make_new_path(d, f0);
		    if (tmp == NULL) {
			rv = -1;
			break;
		    }
		}
	    }
	    if (nameorder >= 0) {
		f1 = *q1++;
		j++;
		if (d != NULL) {
		    if (tmp != NULL) {
			f1 = tmp;
		    } else {
			tmp = f1 = make_new_path(d, f1);
			if (tmp == NULL) {
			    rv = -1;
			    break;
			}
		    }
		}
	    }
	    rv |= diff_file(r0, f0, r1, f1, recursive, prev, printer, ctx);
	    free_new_path(tmp);
	}
    }
    arr_free(&a[1], dispose);
    arr_free(&a[0], dispose);
    return rv;
}


/**
 * Compare two files.
 *
 * \param r0 1st path component
 * \param f0 1st file name (should be NULL initially)
 * \param r1 2nd path component
 * \param f1 2nd file name (should be NULL initially)
 * \param recursive max allowed depth
 * \param prev top of stack of parent subdirectories (should be NULL initially)
 * \param printer callback
 * \param ctx opaque object to be passed to callback
 *
 * \return 0 success, otherwise error
 *
 * \note if neither f0 nor f1 is NULL, then they must be equivalent strings.
 * \note f0 and f1 cannot be both NULL, unless prev is NULL
 */
static int
diff_file (const char *r0, const char *f0, const char *r1, const char *f1, int recursive, const DNODE *prev, DFUNC printer, void *ctx)
{
    int rv;
    char *p0, *p1;
    struct stat st[2];

    if (prev != NULL) {
	if (f0 == NULL) {
	    return printer(ctx, ADD_CH, NULL, f1);
	}
	if (f1 == NULL) {
	    return printer(ctx, DEL_CH, f0, NULL);
	}
    }
    p0 = make_1st_path(r0, f0);
    p1 = make_2nd_path(r1, f1);
    if (p0 == NULL || p1 == NULL) {
	free_2nd_path(p1);
	free_1st_path(p0);
	return -1;
    }

    if (mc_stat(p0, &st[0]) || mc_stat(p1, &st[1])) {
	free_2nd_path(p1);
	free_1st_path(p0);
	if (prev == NULL) {
	    return -1;
	}
	return printer(ctx, ERR_CH, f0, f1);
    }
    if ((st[0].st_mode & S_IFMT) != (st[1].st_mode & S_IFMT)) {
	free_2nd_path(p1);
	free_1st_path(p0);
	if (prev == NULL) {
	    return -1;
	}
	return printer(ctx, ERR_CH, f0, f1);
    }
    if (S_ISDIR(st[0].st_mode)) {
	const DNODE *n;
	int found = 0;
	for (n = prev; n != NULL; n = n->link) {
	    if (n->st[0]->st_ino == st[0].st_ino) {
		found |= 1;
		break;
	    }
	    if (n->st[1]->st_ino == st[1].st_ino) {
		found |= 2;
		break;
	    }
	}
	free_2nd_path(p1);
	free_1st_path(p0);
	if (found) {
	    return printer(ctx, ERR_CH, f0, f1);
	}
	if (prev == NULL || recursive--) {
	    DNODE node;
	    node.link = prev;
	    node.st[0] = &st[0];
	    node.st[1] = &st[1];
	    return diff_dirs(r0, r1, f0, recursive, &node, printer, ctx);
	}
	return printer(ctx, DIR_CH, f0, f1);
    }
    rv = diff_binary(p0, p1, st);
    free_2nd_path(p1);
    free_1st_path(p0);
    if (rv < 0) {
	return printer(ctx, ERR_CH, f0, f1);
    }
    if (rv == 0) {
	return printer(ctx, EQU_CH, f0, f1);
    }
    return printer(ctx, CHG_CH, f0, f1);
}


/* read line *****************************************************************/


static void
cvt_mget(const char *name, char *buf, int width, int skip)
{
    int i, j, len = strlen(name);
    for (i = skip, j = 0; i < len && j < width; j++, i++) {
	buf[j] = name[i];
    }
    for (; j < width; j++) {
	buf[j] = ' ';
    }
    buf[j] = '\0';
}


/* diff printers et al *******************************************************/


static void
free_pair (void *p)
{
    LNODE *n = p;
    if (n->name[0] != NULL) {
	free(n->name[0]);
    } else {
	free(n->name[1]);
    }
}


static int
printer (void *ctx, int ch, const char *f0, const char *f1)
{
    ARRAY *z = ctx;
    LNODE *n;
    char *p0;
    char *p1;
    if (f0 == NULL) {
	p0 = NULL;
	p1 = strdup(f1);
    } else if (f1 == NULL) {
	p0 = strdup(f0);
	p1 = NULL;
    } else {
	p0 =
	p1 = strdup(f1);
    }
    if (p0 == NULL && p1 == NULL) {
	z->error |= 2;
	return -1;
    }
    n = arr_enlarge(z);
    if (n == NULL) {
	return -1;
    }
    n->ch = ch;
    n->name[0] = p0;
    n->name[1] = p1;
    return 0;
}


static int
calc_diffs (const ARRAY *z)
{
    const LNODE *p = z->data;
    int i, ndiff = 0;
    for (i = 0; i < z->len; i++, p++) {
	if (!is_eq(p->ch) && (i == z->len - 1 || p->ch != (p + 1)->ch)) {
	    ndiff++;
	}
    }
    return ndiff;
}


static int
redo_diff (WDiff *view)
{
    int rv;
    arr_init(&view->z, sizeof(LNODE), 256);
    rv = diff_file(view->dir[0], NULL, view->dir[1], NULL, view->recursive, NULL, printer, &view->z);
    if (rv != 0 || view->z.error) {
	arr_free(&view->z, free_pair);
	return -1;
    }
    return calc_diffs(&view->z);
}


/* stuff *********************************************************************/


static int
get_line_numbers (const ARRAY *a, int ord, int pos, int *linenum, int *lineofs)
{
    *linenum = 0;
    *lineofs = 0;

    if (a->len) {
	int i;
	const LNODE *p;

	if (pos >= a->len) {
	    pos = a->len - 1;
	}

	for (i = 0, p = a->data; i <= pos; i++, p++) {
	    if (p->name[ord] != NULL) {
		(*linenum)++;
		*lineofs = 0;
	    } else {
		(*lineofs)++;
	    }
	}
    }
    return 0;
}


static int
calc_nwidth (const ARRAY *a)
{
    int l1, o1;
    int l2, o2;
    get_line_numbers(a, 0, a->len - 1, &l1, &o1);
    get_line_numbers(a, 1, a->len - 1, &l2, &o2);
    if (l1 < l2) {
	l1 = l2;
    }
    return get_digits(l1);
}


static int
find_prev_hunk (const ARRAY *a, int pos)
{
    if (pos > 0) {
	const LNODE *p = a->data;
	int ch = p[pos].ch;
	while (pos > 0 && p[pos].ch == ch) {
	    pos--;
	}
	while (pos > 0 && is_eq(p[pos].ch)) {
	    pos--;
	}
    }
    return pos;
}


static int
find_next_hunk (const ARRAY *a, int pos)
{
    if (pos < a->len) {
	const LNODE *p = a->data;
	int ch = p[pos].ch;
	while (pos < a->len && p[pos].ch == ch) {
	    pos++;
	}
	while (pos < a->len && is_eq(p[pos].ch)) {
	    pos++;
	}
    }
    return pos;
}


static int
get_short_end (const ARRAY *z, int skip_rows)
{
    int ord;
    size_t len = -1; /* XXX error, also big unsigned */
    const LNODE *p = ((LNODE *)z->data) + skip_rows;

    for (ord = 0; ord <= 1; ord++) {
	size_t l = 0;
	if (p->name[ord]) {
	    l = strlen(p->name[ord]);
	    if (l) {
		l--;
	    }
	    if (len > l) {
		len = l;
	    }
	}
    }

    return len;
}


/* view routines and callbacks ***********************************************/


static void
view_compute_split (WDiff *view, int i)
{
    view->bias += i;
    if (view->bias < 2 - view->half1) {
	view->bias = 2 - view->half1;
    }
    if (view->bias > view->half2 - 2) {
	view->bias = view->half2 - 2;
    }
}


static void
view_compute_areas (WDiff *view)
{
    view->height = LINES - 2;
    view->half1 = COLS / 2;
    view->half2 = COLS - view->half1;

    view_compute_split(view, 0);
}


static int
view_init (WDiff *view, int recursive, const char *dir1, const char *dir2)
{
    int ndiff;

    view->dir[0] = dir1;
    view->dir[1] = dir2;
    view->recursive = recursive;

    ndiff = redo_diff(view);
    if (ndiff < 0) {
	return -1;
    }

    view->ndiff = ndiff;

    view->view_quit = 0;

    view->bias = 0;
    view->new_frame = 1;
    view->skip_rows = 0;
    view->skip_cols = 0;
    view->display_symbols = 0;
    view->display_numbers = 0;
    view->ord = 0;
    view->full = 0;
    view->last_found = -1;

    view_compute_areas(view);
    return 0;
}


static int
view_reinit (WDiff *view)
{
    int recursive = view->recursive;
    int ndiff = view->ndiff;

    diffopt_widgets[2].value = recursive;
    diffopt_widgets[2].result = &recursive;

    if (quick_dialog(&diffopt) != B_CANCEL) {
	view->recursive = (recursive != 0) * RECURSIVE_DEPTH;
	arr_free(&view->z, free_pair);
	ndiff = redo_diff(view);
	if (ndiff >= 0) {
	    view->ndiff = ndiff;
	}
    }
    return ndiff;
}


static void
view_fini (WDiff *view)
{
    arr_free(&view->z, free_pair);
}


static int
view_display_file (const WDiff *view, int ord,
		   int r, int c, int height, int width)
{
    int i, j, k;
    char buf[BUFSIZ];
    const ARRAY *z = &view->z;
    int skip = view->skip_cols;
    int display_symbols = view->display_symbols;
    int display_numbers = view->display_numbers;
    const LNODE *p;

    int nwidth = display_numbers;
    int xwidth = display_symbols + display_numbers;

    if (xwidth) {
	if (xwidth > width && display_symbols) {
	    xwidth--;
	    display_symbols = 0;
	}
	if (xwidth > width && display_numbers) {
	    xwidth = width;
	    display_numbers = width;
	}

	xwidth++;

	c += xwidth;
	width -= xwidth;

	if (width < 0) {
	    width = 0;
	}
    }

    if ((int)sizeof(buf) <= width || (int)sizeof(buf) <= nwidth) {
	/* abnormal, but avoid buffer overflow */
	return -1;
    }

    for (i = view->skip_rows, j = 0, p = (LNODE *)z->data + i; i < z->len && j < height; p++, j++, i++) {
	int ch = p->ch;
	tty_setcolor(NORMAL_COLOR);
	if (p->name[ord]) {
	    if (ch == DEL_CH) {
		ch = ADD_CH;
	    }
	    if (display_symbols) {
		tty_gotoyx(r + j, c - 2);
		tty_print_char(ch);
	    }
	    if (display_numbers) {
		int linenum, lineofs;
		get_line_numbers(&view->z, ord, i, &linenum, &lineofs);
		tty_gotoyx(r + j, c - xwidth);
		snprintf(buf, display_numbers + 1, "%*d", nwidth, linenum);
		tty_print_string(buf);
	    }
	    if (ch == ADD_CH) {
		tty_setcolor(DFFADD_COLOR);
	    }
	    if (ch == CHG_CH) {
		tty_setcolor(DFFCHG_COLOR);
	    }
	    if (ch == ERR_CH) {
		tty_setcolor(STALE_LINK_COLOR);
	    }
	    if (ch == DIR_CH) {
		tty_setcolor(DIRECTORY_COLOR);
	    }
	    if (i == view->last_found) {
		tty_setcolor(MARKED_SELECTED_COLOR);
	    }
	    cvt_mget(p->name[ord], buf, width, skip);
	} else {
	    if (ch == ADD_CH) {
		ch = DEL_CH;
	    }
	    if (display_symbols) {
		tty_gotoyx(r + j, c - 2);
		tty_print_char(ch);
	    }
	    if (display_numbers) {
		tty_gotoyx(r + j, c - xwidth);
		memset(buf, ' ', display_numbers);
		buf[display_numbers] = '\0';
		tty_print_nstring(buf, display_numbers);
	    }
	    if (ch == DEL_CH) {
		tty_setcolor(DFFCHD_COLOR);	/* XXX perhaps this sucks? */
	    }
	    if (ch == CHG_CH) {
		tty_setcolor(DFFCHG_COLOR);
	    }
	    if (ch == ERR_CH) {
		tty_setcolor(STALE_LINK_COLOR);
	    }
	    if (ch == DIR_CH) {
		tty_setcolor(DIRECTORY_COLOR);
	    }
	    memset(buf, ' ', width);
	    buf[width] = '\0';
	}
	tty_gotoyx(r + j, c);
	tty_print_nstring(buf, width);
    }
    tty_setcolor(NORMAL_COLOR);
    k = width;
    if (width < xwidth - 1) {
	k = xwidth - 1;
    }
    memset(buf, ' ', k);
    buf[k] = '\0';
    for (; j < height; j++) {
	if (xwidth) {
	    tty_gotoyx(r + j, c - xwidth);
	    tty_print_nstring(buf, xwidth - 1);
	}
	tty_gotoyx(r + j, c);
	tty_print_nstring(buf, width);
    }

    return 0;
}


static void
view_status (const WDiff *view, int ord, int width, int c)
{
    int skip_rows = view->skip_rows;
    int skip_cols = view->skip_cols;

    char buf[BUFSIZ];
    int filename_width;
    int linenum, lineofs;

    tty_setcolor(SELECTED_COLOR);

    tty_gotoyx(0, c);
    get_line_numbers(&view->z, ord, skip_rows, &linenum, &lineofs);

    filename_width = width - 22;
    if (filename_width < 8) {
	filename_width = 8;
    }
    if (filename_width >= (int)sizeof(buf)) {
	/* abnormal, but avoid buffer overflow */
	filename_width = sizeof(buf) - 1;
    }
    trim(strip_home_and_password(view->dir[ord]), buf, filename_width);
    if (ord == 0) {
	tty_printf("%-*s %6d+%-4d Col %-4d ", filename_width, buf, linenum, lineofs, skip_cols);
    } else {
	tty_printf("%-*s %6d+%-4d Dif %-4d ", filename_width, buf, linenum, lineofs, view->ndiff);
    }
}


static void
view_update (WDiff *view)
{
    int height = view->height;
    int width1;
    int width2;

    int last = view->z.len - 1;

    if (view->skip_rows > last) {
	view->skip_rows = last;
    }
    if (view->skip_rows < 0) {
	view->skip_rows = 0;
    }
    if (view->skip_cols < 0) {
	view->skip_cols = 0;
    }

    if (height < 2) {
	return;
    }

    width1 = view->half1 + view->bias;
    width2 = view->half2 - view->bias;
    if (view->full) {
	width1 = COLS;
	width2 = 0;
    }

    if (view->new_frame) {
	Dlg_head *h = view->widget.parent;

	int xwidth = view->display_symbols + view->display_numbers;

	tty_setcolor(NORMAL_COLOR);
	if (width1 > 1) {
	    draw_double_box(h, 1, 0,      height, width1);
	}
	if (width2 > 1) {
	    draw_double_box(h, 1, width1, height, width2);
	}

	if (xwidth) {
	    xwidth++;
	    if (xwidth < width1 - 1) {
		tty_gotoyx(1, xwidth);
		tty_print_alt_char(ACS_TTEE);
		tty_gotoyx(height, xwidth);
		tty_print_alt_char(ACS_BTEE);
		tty_print_vline(2, xwidth, height - 2);
	    }
	    if (xwidth < width2 - 1) {
		tty_gotoyx(1, width1 + xwidth);
		tty_print_alt_char(ACS_TTEE);
		tty_gotoyx(height, width1 + xwidth);
		tty_print_alt_char(ACS_BTEE);
		tty_print_vline(2, width1 + xwidth, height - 2);
	    }
	}

	view->new_frame = 0;
    }

    if (width1 > 2) {
	view_status(view, view->ord,     width1, 0);
	view_display_file(view, view->ord,     2, 1,          height - 2, width1 - 2);
    }
    if (width2 > 2) {
	view_status(view, view->ord ^ 1, width2, width1);
	view_display_file(view, view->ord ^ 1, 2, width1 + 1, height - 2, width2 - 2);
    }
}


static void
view_redo (WDiff *view)
{
    if (view_reinit(view) < 0) {
	view->view_quit = 1;
    } else if (view->display_numbers) {
	int old = view->display_numbers;
	view->display_numbers = calc_nwidth(&view->z);
	view->new_frame = (old != view->display_numbers);
    }
}


static int
view_search_regexp (WDiff *view, regex_t *r, int back, int whole)
{
    /* XXX perform regexp-search here: return negative if cannot find */
    error_dialog(_("Search"), _(" regexp search not yet implemented "));
    return 0;
}


static const unsigned char *
search_string (const char *haystack, size_t xpos, const void *needle, size_t nlen, int whole, int ccase)
{
    size_t hlen = strlen(haystack);

    if (xpos > hlen || nlen <= 0 || haystack == NULL || needle == NULL) {
	return NULL;
    }

    /* XXX I should use strstr */
    if (ccase) {
	return memmem_dumb((const unsigned char *)haystack, xpos, hlen, needle, nlen, whole);
    } else {
	return memmem_dumb_nocase((const unsigned char *)haystack, xpos, hlen, needle, nlen, whole);
    }
}


static int
view_search_string (WDiff *view, const char *needle, int ccase, int back, int whole)
{
    size_t nlen = strlen(needle);
    size_t xpos = 0;

    int ord = view->ord;
    const ARRAY *a = &view->z;
    const LNODE *p;

    int i = view->last_found;

    if (back) {
	if (i == -1) {
	    i = view->skip_rows;
	}
	for (--i, p = (LNODE *)a->data + i; i >= 0; p--, i--) {
	    if (p->name[ord]) {
		const unsigned char *q = search_string(p->name[ord], xpos, needle, nlen, whole, ccase);
		if (q != NULL) {
		    return i;
		}
	    }
	}
    } else {
	if (i == -1) {
	    i = view->skip_rows - 1;
	}
	for (++i, p = (LNODE *)a->data + i; i < a->len; p++, i++) {
	    if (p->name[ord]) {
		const unsigned char *q = search_string(p->name[ord], xpos, needle, nlen, whole, ccase);
		if (q != NULL) {
		    return i;
		}
	    }
	}
    }

    return -1;
}


static void
view_search (WDiff *view, int again)
{
    /* XXX some statics here, to be remembered between runs */
    static char *searchopt_text = NULL;
    static int searchopt_regexp;
    static int searchopt_whole;
    static int searchopt_case;
    static int searchopt_backwards;

    static regex_t r;
    static int compiled = 0;

    if (again < 0) {
	g_free(searchopt_text);
	searchopt_text = NULL;
	if (compiled) {
	    compiled = 0;
	    regfree(&r);
	}
	return;
    }

    if (!again || searchopt_text == NULL) {
	char *tsearchopt_text;
	int tsearchopt_regexp = searchopt_regexp;
	int tsearchopt_whole = searchopt_whole;
	int tsearchopt_case = searchopt_case;
	int tsearchopt_backwards = searchopt_backwards;

	search_widgets[2].result = &tsearchopt_backwards;
	search_widgets[3].result = &tsearchopt_regexp;
	search_widgets[4].result = &tsearchopt_whole;
	search_widgets[5].result = &tsearchopt_case;
	search_widgets[6].str_result = &tsearchopt_text;
	search_widgets[6].text = searchopt_text;

	if (quick_dialog(&search_input) == B_CANCEL) {
	    return;
	}
	if (tsearchopt_text == NULL || !*tsearchopt_text) {
	    g_free(tsearchopt_text);
	    return;
	}
	g_free(searchopt_text);

	searchopt_backwards = tsearchopt_backwards;
	searchopt_regexp = tsearchopt_regexp;
	searchopt_whole = tsearchopt_whole;
	searchopt_case = tsearchopt_case;
	searchopt_text = tsearchopt_text;
    }

    if (searchopt_regexp) {
	if (compiled) {
	    compiled = 0;
	    regfree(&r);
	}
	if (regcomp(&r, searchopt_text, REG_EXTENDED | (searchopt_case ? 0 : REG_ICASE))) {
	    error_dialog(_("Error"), _(" Invalid regular expression "));
	    return;
	}
	compiled = 1;
	view->last_found = view_search_regexp(view, &r, searchopt_backwards, searchopt_whole);
    } else {
	view->last_found = view_search_string(view, searchopt_text, searchopt_case, searchopt_backwards, searchopt_whole);
    }

    if (view->last_found == -1) {
	error_dialog(_("Search"), _(" Search string not found "));
    } else {
	view->skip_rows = view->last_found;
	view_update(view);
    }
}


static void
view_search_cmd (WDiff *view)
{
    view_search(view, 0);
}


static void
view_edit (WDiff *view, int ord)
{
    const ARRAY *z = &view->z;
    const LNODE *p = z->data;
    const char *s = p[view->skip_rows].name[ord];

    if (s != NULL) {
	char buf[2 * PATH_MAX];	/* XXX for _bufpath */
	s = make_tmp_path(view->dir[ord], s);
	if (s != NULL) {
	    do_edit_at_line(s, 0);
	    free_tmp_path(s);
	    view_redo(view);
	    view_update(view);
	}
    }
}


static void
view_edit_cmd (WDiff *view)
{
    view_edit(view, view->ord);
}


static void
view_view (WDiff *view, int ord)
{
    const ARRAY *z = &view->z;
    const LNODE *p = z->data;
    const char *s = p[view->skip_rows].name[ord];

    if (s != NULL) {
	char buf[2 * PATH_MAX];	/* XXX for _bufpath */
	s = make_tmp_path(view->dir[ord], s);
	if (s != NULL) {
	    view_file_at_line(s, 0, use_internal_view, 0);
	    free_tmp_path(s);
	    view_update(view);
	}
    }
}


static void
view_view_cmd (WDiff *view)
{
    view_view(view, view->ord);
}


static void
view_goto_cmd (WDiff *view, int ord)
{
    static const char *title[2] = { " Goto line (left) ", " Goto line (right) " };
    static char prev[256];
    /* XXX some statics here, to be remembered between runs */

    int newline;
    char *input;

    input = input_dialog(_(title[ord]), _(" Enter line: "), prev);
    if (input != NULL) {
	const char *s = input;
	if (scan_deci(&s, &newline) == 0 && *s == '\0') {
	    int i = 0;
	    if (newline > 0) {
		const LNODE *p;
		int j = 0;
		ord ^= view->ord;
		for (p = view->z.data; i < view->z.len; i++, p++) {
		    if (p->name[ord] != NULL) {
			j++;
		    }
		    if (j == newline) {
			break;
		    }
		}
	    }
	    view->skip_rows = i;
	    snprintf(prev, sizeof(prev), "%d", newline);
	}
	g_free(input);
    }
}


static void
view_help_cmd (void)
{
    interactive_display(NULL, "[Directory Diff Viewer]");
}


static void
view_quit_cmd (WDiff *view)
{
    dlg_stop(view->widget.parent);
}


static void
view_labels (WDiff *view)
{
    Dlg_head *h = view->widget.parent;

    buttonbar_set_label(h, 1, Q_("ButtonBar|Help"), view_help_cmd);

    buttonbar_set_label_data(h, 3, Q_("ButtonBar|View"), (buttonbarfn)view_view_cmd, view);
    buttonbar_set_label_data(h, 4, Q_("ButtonBar|Edit"), (buttonbarfn)view_edit_cmd, view);
    buttonbar_set_label_data(h, 7, Q_("ButtonBar|Search"), (buttonbarfn)view_search_cmd, view);
    buttonbar_set_label_data(h, 10, Q_("ButtonBar|Quit"), (buttonbarfn)view_quit_cmd, view);
}


static int
view_event (Gpm_Event *event, void *x)
{
    WDiff *view = (WDiff *)x;
    int result = MOU_NORMAL;

    /* We are not interested in the release events */
    if (!(event->type & (GPM_DOWN | GPM_DRAG))) {
	return result;
    }

    /* Wheel events */
    if ((event->buttons & GPM_B_UP) && (event->type & GPM_DOWN)) {
	view->skip_rows -= 2;
	view_update(view);
	return result;
    }
    if ((event->buttons & GPM_B_DOWN) && (event->type & GPM_DOWN)) {
	view->skip_rows += 2;
	view_update(view);
	return result;
    }

    return result;
}


static cb_ret_t
view_handle_key (WDiff *view, int c)
{
    c = convert_from_input_c(c);

    /* XXX add copy/move/delete; add file masks with shell patterns */

    switch (c) {
	case 's':
	    view->display_symbols ^= 1;
	    view->new_frame = 1;
	    return MSG_HANDLED;

	case 'l':
	    view->display_numbers ^= calc_nwidth(&view->z);
	    view->new_frame = 1;
	    return MSG_HANDLED;

	case 'f':
	    view->full ^= 1;
	    view->new_frame = 1;
	    return MSG_HANDLED;

	case '=':
	    if (!view->full) {
		view->bias = 0;
		view->new_frame = 1;
	    }
	    return MSG_HANDLED;

	case '>':
	    if (!view->full) {
		view_compute_split(view, 1);
		view->new_frame = 1;
	    }
	    return MSG_HANDLED;

	case '<':
	    if (!view->full) {
		view_compute_split(view, -1);
		view->new_frame = 1;
	    }
	    return MSG_HANDLED;

	case XCTRL('u'):
	    view->ord ^= 1;
	    return MSG_HANDLED;

	case XCTRL('r'):
	    view_redo(view);
	    return MSG_HANDLED;

	case '\n':
	    view_diff_cmd(view, NULL, NULL);
	    return MSG_HANDLED;

	case 'n':
	    view->skip_rows = find_next_hunk(&view->z, view->skip_rows);
	    return MSG_HANDLED;

	case 'p':
	    view->skip_rows = find_prev_hunk(&view->z, view->skip_rows);
	    return MSG_HANDLED;

	case ALT ('l'):
	case ALT ('L'):
	    view->last_found = -1;
	    view_goto_cmd(view, (c == ALT ('L')));
	    return MSG_HANDLED;

	case KEY_BACKSPACE:
	case KEY_DC:
	    view->last_found = -1;
	    return MSG_HANDLED;

	case KEY_F(3):
	    view_view(view, view->ord);
	    return MSG_HANDLED;

	case KEY_F(13):
	    view_view(view, view->ord ^ 1);
	    return MSG_HANDLED;

	case KEY_F(4):
	    view_edit(view, view->ord);
	    return MSG_HANDLED;

	case KEY_F(14):
	    view_edit(view, view->ord ^ 1);
	    return MSG_HANDLED;

	case KEY_F(17):
	    view_search(view, 1);
	    return MSG_HANDLED;

	case KEY_HOME:
	case ALT ('<'):
	case KEY_M_CTRL | KEY_PPAGE:
	    view->last_found = -1;
	    view->skip_rows = 0;
	    return MSG_HANDLED;

	case KEY_END:
	case ALT ('>'):
	case KEY_M_CTRL | KEY_NPAGE:
	    view->last_found = -1;
	    view->skip_rows = view->z.len - 1;
	    return MSG_HANDLED;

	case KEY_UP:
	    view->skip_rows--;
	    return MSG_HANDLED;

	case KEY_DOWN:
	    view->skip_rows++;
	    return MSG_HANDLED;

	case KEY_NPAGE:
	    view->skip_rows += view->height - 2;
	    return MSG_HANDLED;

	case KEY_PPAGE:
	    view->skip_rows -= view->height - 2;
	    return MSG_HANDLED;

	case KEY_LEFT:
	    view->skip_cols--;
	    return MSG_HANDLED;

	case KEY_RIGHT:
	    view->skip_cols++;
	    return MSG_HANDLED;

	case KEY_M_CTRL | KEY_LEFT:
	    view->skip_cols -= 8;
	    return MSG_HANDLED;

	case KEY_M_CTRL | KEY_RIGHT:
	    view->skip_cols += 8;
	    return MSG_HANDLED;

	case XCTRL('a'):
	    view->skip_cols = 0;
	    return MSG_HANDLED;

	case XCTRL('e'):
	    view->skip_cols = get_short_end(&view->z, view->skip_rows);
	    return MSG_HANDLED;

	case XCTRL('o'):
	    view_other_cmd();
	    return MSG_HANDLED;

	case 'q':
	case ESC_CHAR:
	    view->view_quit = 1;
	    return MSG_HANDLED;

#ifdef HAVE_CHARSET
	case XCTRL ('t'):
	    do_select_codepage ();
	    view_update (view);
	    return MSG_HANDLED;
#endif				/* HAVE_CHARSET */
    }

    /* Key not used */
    return MSG_NOT_HANDLED;
}


static cb_ret_t
view_callback (Widget *w, widget_msg_t msg, int parm)
{
    cb_ret_t i;
    WDiff *view = (WDiff *)w;
    Dlg_head *h = view->widget.parent;

    switch (msg) {
	case WIDGET_INIT:
	    view_labels(view);
	    return MSG_HANDLED;

	case WIDGET_DRAW:
	    view->new_frame = 1;
	    view_update(view);
	    return MSG_HANDLED;

	case WIDGET_CURSOR:
	    return MSG_HANDLED;

	case WIDGET_KEY:
	    i = view_handle_key((WDiff *)view, parm);
	    if (view->view_quit)
		dlg_stop(h);
	    else {
		view_update(view);
	    }
	    return i;

	case WIDGET_IDLE:
	    return MSG_HANDLED;

	case WIDGET_FOCUS:
	    view_labels(view);
	    return MSG_HANDLED;

	case WIDGET_DESTROY:
	    return MSG_HANDLED;

	default:
	    return default_proc(msg, parm);
    }
}


static void
view_adjust_size (Dlg_head *h)
{
    WDiff *view;
    WButtonBar *bar;

    /* Look up the viewer and the buttonbar, we assume only two widgets here */
    view = (WDiff *)find_widget_type(h, view_callback);
    bar = find_buttonbar(h);
    widget_set_size(&view->widget, 0, 0, LINES, COLS);
    widget_set_size((Widget *)bar, LINES - 1, 0, 1, COLS);

    view_compute_areas(view);
}


static cb_ret_t
view_dialog_callback (Dlg_head *h, dlg_msg_t msg, int parm)
{
    switch (msg) {
	case DLG_RESIZE:
	    view_adjust_size(h);
	    return MSG_HANDLED;

	default:
	    return default_dlg_callback(h, msg, parm);
    }
}


int
zdiff_view (const char *dir1, const char *dir2)
{
    int error;
    WDiff *view;
    WButtonBar *bar;
    Dlg_head *view_dlg;

    /* Create dialog and widgets, put them on the dialog */
    view_dlg =
	create_dlg(0, 0, LINES, COLS, NULL, view_dialog_callback,
		   "[Directory Diff Viewer]", NULL, DLG_WANT_TAB);

    view = g_new0(WDiff, 1);

    init_widget(&view->widget, 0, 0, LINES - 1, COLS,
		(callback_fn)view_callback,
		(mouse_h)view_event);

    widget_want_cursor(view->widget, 0);

    bar = buttonbar_new(1);

    add_widget(view_dlg, bar);
    add_widget(view_dlg, view);

    error = view_init(view, 0, dir1, dir2);

    /* Please note that if you add another widget,
     * you have to modify view_adjust_size to
     * be aware of it
     */
    if (!error) {
	run_dlg(view_dlg);
	view_search(view, -1);
	view_fini(view);
    }
    destroy_dlg(view_dlg);

    return error;
}


static int
is_binary (const char *file)
{
    int binary = 0;
    FBUF *f = f_open(file, O_RDONLY);
    if (f != NULL) {
	size_t size = 4096;	/* HAVE_STRUCT_STAT_ST_BLKSIZE: st->st_blksize */
	char *buf = malloc(size);
	if (buf != NULL) {
	    size = f_read(f, buf, size);
	    binary = (memchr(buf, 0, size) != NULL);
	    free(buf);
	}
	f_close(f);
    }
    return binary;
}


#define GET_FILE_AND_STAMP(n)					\
    do {							\
	use_copy##n = 0;					\
	real_file##n = file##n;					\
	if (!vfs_file_is_local(file##n)) {			\
	    real_file##n = mc_getlocalcopy(file##n);		\
	    if (real_file##n != NULL) {				\
		use_copy##n = 1;				\
		if (mc_stat(real_file##n, &st##n) != 0) {	\
		    use_copy##n = -1;				\
		}						\
	    }							\
	}							\
    } while (0)
#define UNGET_FILE(n)						\
    do {							\
	if (use_copy##n) {					\
	    int changed = 0;					\
	    if (use_copy##n > 0) {				\
		time_t mtime = st##n.st_mtime;			\
		if (mc_stat(real_file##n, &st##n) == 0) {	\
		    changed = (mtime != st##n.st_mtime);	\
		}						\
	    }							\
	    mc_ungetlocalcopy(file##n, real_file##n, changed);	\
	    g_free(real_file##n);				\
	}							\
    } while (0)
#define CHECK_DIR(n)						\
    do {							\
	struct stat st##n;					\
	char *real_name##n;					\
	is_dir##n = 0;						\
	if (vfs_file_is_local(name##n)) {			\
	    rv = mc_stat(name##n, &st##n);			\
	} else {						\
	    rv = -1;						\
	    real_name##n = mc_getlocalcopy(name##n);		\
	    if (real_name##n != NULL) {				\
		rv = mc_stat(real_name##n, &st##n);		\
		mc_ungetlocalcopy(name##n, real_name##n, FALSE);\
	    }							\
	}							\
	if (rv == 0) {						\
	    is_dir##n = S_ISDIR(st##n.st_mode);			\
	}							\
    } while (0)
void
view_diff_cmd (void *obj, const char *name0, const char *name1)
{
    int rv = 0;
    char *file0 = NULL;
    char *file1 = NULL;
    WDiff *view = obj;
    int is_dir0 = 0;
    int is_dir1 = 0;

    if (view == NULL) {
	if (name0 != NULL && name1 != NULL) {
	    CHECK_DIR(0);
	    CHECK_DIR(1);
	    file0 = g_strdup(name0);
	    file1 = g_strdup(name1);
	} else {
	    const WPanel *panel0 = current_panel;
	    const WPanel *panel1 = other_panel;
	    if (get_current_index()) {
		panel0 = other_panel;
		panel1 = current_panel;
	    }
	    file0 = concat_dir_and_file(panel0->cwd, selection(panel0)->fname);
	    file1 = concat_dir_and_file(panel1->cwd, selection(panel1)->fname);
	    is_dir0 = S_ISDIR(selection(panel0)->st.st_mode) || link_isdir(selection(panel0));
	    is_dir1 = S_ISDIR(selection(panel1)->st.st_mode) || link_isdir(selection(panel1));
	}
    } else {
	int ord = view->ord;
	const ARRAY *z = &view->z;
	const LNODE *p = (LNODE *)z->data + view->skip_rows;
	if (p->name[0] == NULL || p->name[1] == NULL || p->ch == ERR_CH) {
	    return;
	}
	file0 = strpath(view->dir[ord],     p->name[ord]);
	file1 = strpath(view->dir[ord ^ 1], p->name[ord ^ 1]);
	if (p->ch == DIR_CH) {
	    is_dir0 = is_dir1 = 1;
	}
    }

    if (rv == 0) {
	rv = -1;
	if (file0 != NULL && file1 != NULL) {
	    if (is_dir0 && is_dir1) {
		rv = zdiff_view(file0, file1);
	    } else {
		if (!is_dir0 && !is_dir1) {
		    int use_copy0;
		    int use_copy1;
		    struct stat st0;
		    struct stat st1;
		    char *real_file0;
		    char *real_file1;
		    GET_FILE_AND_STAMP(0);
		    GET_FILE_AND_STAMP(1);
		    if (real_file0 != NULL && real_file1 != NULL) {
			if (is_binary(real_file0) || is_binary(real_file1)) {
			    rv = xdiff_view(real_file0, real_file1, file0, file1);
			} else {
			    rv = diff_view(real_file0, real_file1, file0, file1);
			}
		    }
		    UNGET_FILE(1);
		    UNGET_FILE(0);
		}
	    }
	}
    }

    free(file1);
    free(file0);

    if (rv != 0) {
	message (1, MSG_ERROR, _(" Error building diff "));
    }
}
#endif

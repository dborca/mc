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
#include <sys/stat.h>
#include "global.h"
#include "tty.h"
#include "cmd.h"
#include "dialog.h"
#include "widget.h"
#include "color.h"
#include "help.h"
#include "key.h"
#include "wtools.h"
#include "charsets.h"
#include "selcodepage.h"
#include "main.h"
#include "zutil.h"
#include "xdiff.h"
#include "ydiff.h"


#define ADD_CH		'+'
#define DEL_CH		'-'
#define CHG_CH		'*'
#define EQU_CH		' '

typedef struct {
    int a[2][2];
    int cmd;
} DIFFCMD;

typedef int (*DFUNC) (void *ctx, int ch, int line, off_t off, size_t sz, const char *str);

#define HDIFF_ENABLE	1
#define HDIFF_MINCTX	5
#define HDIFF_DEPTH	10

typedef struct {
    int off;
    int len;
} BRACKET[2];

typedef int PAIR[2];

#define TAB_SKIP(ts, pos)	((ts) - (pos) % (ts))

typedef enum {
    DATA_SRC_MEM = 0,
    DATA_SRC_TMP = 1,
    DATA_SRC_ORG = 2
} DSRC;

typedef struct {
    int ch;
    int line;
    union {
	off_t off;
	size_t len;
    } u;
    void *p;
    int borrow;
} DIFFLN;

typedef struct {
    FBUF *f;
    ARRAY *a, *other;
    DSRC dsrc;
} PRINTER_CTX;

typedef struct {
    Widget widget;

    const char *args;		/* Args passed to diff */
    const char *file[2];	/* filenames */
    FBUF *f[2];
    ARRAY a[2];
    ARRAY **hdiff;
    int ndiff;			/* number of hunks */
    DSRC dsrc;			/* data source: memory or temporary file */

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
    int show_cr;
    int show_hdiff;
    int tab_size;
    int ord;
    int full;
    int last_found;

    struct {
	int quality;
	int strip_trailing_cr;
	int ignore_tab_expansion;
	int ignore_space_change;
	int ignore_all_space;
	int ignore_case;
    } opt;
} WDiff;


#define OPTX 50
#define OPTY 12

static const char *quality_str[] = {
    N_("&Normal"),
    N_("&Fastest"),
    N_("&Minimal")
};

static QuickWidget diffopt_widgets[] = {
    { quick_button,   6,   10, 9, OPTY, N_("&Cancel"),                 0, B_CANCEL, NULL, NULL, NULL },
    { quick_button,   3,   10, 9, OPTY, N_("&OK"),                     0, B_ENTER,  NULL, NULL, NULL },
    { quick_radio,   34, OPTX, 4, OPTY, "",                            3, 2,        NULL, const_cast(char **, quality_str), NULL },
    { quick_checkbox, 4, OPTX, 7, OPTY, N_("strip trailing &CR"),      0, 0,        NULL, NULL, NULL },
    { quick_checkbox, 4, OPTX, 6, OPTY, N_("ignore all &Whitespace"),  0, 0,        NULL, NULL, NULL },
    { quick_checkbox, 4, OPTX, 5, OPTY, N_("ignore &Space change"),    0, 0,        NULL, NULL, NULL },
    { quick_checkbox, 4, OPTX, 4, OPTY, N_("ignore tab &Expansion"),   0, 0,        NULL, NULL, NULL },
    { quick_checkbox, 4, OPTX, 3, OPTY, N_("&Ignore case"),            0, 0,        NULL, NULL, NULL },
    NULL_QuickWidget
};

static QuickDialog diffopt = {
    OPTX, OPTY, -1, -1,
    N_(" Diff Options "), "[Diff Options]",
    diffopt_widgets, 0
};

#define SEARCH_DLG_WIDTH  58
#define SEARCH_DLG_HEIGHT 10

static QuickWidget search_widgets[] = {
    { quick_button,    6,               10, 7, SEARCH_DLG_HEIGHT, N_("&Cancel"),                0, B_CANCEL, NULL, NULL, NULL },
    { quick_button,    2,               10, 7, SEARCH_DLG_HEIGHT, N_("&OK"),                    0, B_ENTER,  NULL, NULL, NULL },
    { quick_checkbox, 33, SEARCH_DLG_WIDTH, 4, SEARCH_DLG_HEIGHT, N_("Backwards/&Up"),          0, 0,        NULL, NULL, NULL },
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
 * Parse line for diff statement.
 *
 * \param p string to parse
 * \param ops list of diff statements
 *
 * \return 0 if success, otherwise non-zero
 */
static int
scan_line (const char *p, ARRAY *ops)
{
    DIFFCMD *op;

    int f1, f2;
    int t1, t2;
    int cmd;

    int range;

    /* handle the following cases:
     *	NUMaNUM[,NUM]
     *	NUM[,NUM]cNUM[,NUM]
     *	NUM[,NUM]dNUM
     * where NUM is a positive integer
     */

    if (scan_deci(&p, &f1) != 0 || f1 < 0) {
	return -1;
    }
    f2 = f1;
    range = 0;
    if (*p == ',') {
	p++;
	if (scan_deci(&p, &f2) != 0 || f2 < f1) {
	    return -1;
	}
	range = 1;
    }

    cmd = *p++;
    if (cmd == 'a') {
	if (range) {
	    return -1;
	}
    } else if (cmd != 'c' && cmd != 'd') {
	return -1;
    }

    if (scan_deci(&p, &t1) != 0 || t1 < 0) {
	return -1;
    }
    t2 = t1;
    range = 0;
    if (*p == ',') {
	p++;
	if (scan_deci(&p, &t2) != 0 || t2 < t1) {
	    return -1;
	}
	range = 1;
    }

    if (cmd == 'd') {
	if (range) {
	    return -1;
	}
    }

    op = arr_enlarge(ops);
    if (op == NULL) {
	return -1;
    }
    op->a[0][0] = f1;
    op->a[0][1] = f2;
    op->cmd = cmd;
    op->a[1][0] = t1;
    op->a[1][1] = t2;
    return 0;
}


/**
 * Parse diff output and extract diff statements.
 *
 * \param f stream to read from
 * \param ops list of diff statements to fill
 *
 * \return positive number indicating number of hunks, otherwise negative
 */
static int
scan_diff (FBUF *f, ARRAY *ops)
{
    int sz;
    char buf[BUFSIZ];

    while ((sz = f_gets(buf, sizeof(buf) - 1, f))) {
	if (isdigit(buf[0])) {
	    if (buf[sz - 1] != '\n') {
		return -1;
	    }
	    buf[sz] = '\0';
	    if (scan_line(buf, ops) != 0) {
		return -1;
	    }
	    continue;
	}
	while (buf[sz - 1] != '\n' && (sz = f_gets(buf, sizeof(buf), f))) {
	}
    }

    return ops->len;
}


/**
 * Invoke diff and extract diff statements.
 *
 * \param args extra arguments to be passed to diff
 * \param extra more arguments to be passed to diff
 * \param file1 first file to compare
 * \param file2 second file to compare
 * \param ops list of diff statements to fill
 *
 * \return positive number indicating number of hunks, otherwise negative
 */
static int
dff_execute (const char *args, const char *extra, const char *file1, const char *file2, ARRAY *ops)
{
    static const char *opt =
	" --old-group-format='%df%(f=l?:,%dl)d%dE\n'"
	" --new-group-format='%dea%dF%(F=L?:,%dL)\n'"
	" --changed-group-format='%df%(f=l?:,%dl)c%dF%(F=L?:,%dL)\n'"
	" --unchanged-group-format=''";

    int rv;
    FBUF *f;
    char *cmd;
    int code;

    cmd = malloc(14 + strlen(args) + strlen(extra) + strlen(opt) + strlen(file1) + strlen(file2));
    if (cmd == NULL) {
	return -1;
    }
    sprintf(cmd, "diff %s %s %s \"%s\" \"%s\"", args, extra, opt, file1, file2);

    f = p_open(cmd, O_RDONLY);
    free(cmd);
    if (f == NULL) {
	return -1;
    }

    arr_init(ops, sizeof(DIFFCMD), 64);
    rv = scan_diff(f, ops);
    code = p_close(f);

    if (rv < 0 || code == -1 || !WIFEXITED(code) || WEXITSTATUS(code) == 2) {
	arr_free(ops, NULL);
	return -1;
    }

    return rv;
}


/**
 * Reparse and display file according to diff statements.
 *
 * \param ord 0 if displaying first file, 1 if displaying 2nd file
 * \param filename file name to display
 * \param ops list of diff statements
 * \param printer printf-like function to be used for displaying
 * \param ctx printer context
 *
 * \return 0 if success, otherwise non-zero
 */
static int
dff_reparse (int ord, FBUF *f, const ARRAY *ops, DFUNC printer, void *ctx)
{
    int i;
    size_t sz;
    char buf[BUFSIZ];
    int line = 0;
    off_t off = 0;
    const DIFFCMD *op;
    int eff, tee;
    int add_cmd;
    int del_cmd;

    ord &= 1;
    eff = ord;
    tee = ord ^ 1;

    add_cmd = 'a';
    del_cmd = 'd';
    if (ord) {
	add_cmd = 'd';
	del_cmd = 'a';
    }

#define F1 a[eff][0]
#define F2 a[eff][1]
#define T1 a[tee][0]
#define T2 a[tee][1]
    for (op = ops->data, i = 0; i < ops->len; i++, op++) {
	int n = op->F1 - (op->cmd != add_cmd);
	while (line < n && (sz = f_gets(buf, sizeof(buf), f))) {
	    line++;
	    printer(ctx, EQU_CH, line, off, sz, buf);
	    off += sz;
	    while (buf[sz - 1] != '\n') {
		if (!(sz = f_gets(buf, sizeof(buf), f))) {
		    printer(ctx, 0, 0, 0, 1, "\n");
		    break;
		}
		printer(ctx, 0, 0, 0, sz, buf);
		off += sz;
		if (got_interrupt()) {
		    return -1;
		}
	    }
	}
	if (line != n) {
	    return -1;
	}

	if (op->cmd == add_cmd) {
	    n = op->T2 - op->T1 + 1;
	    while (n) {
		printer(ctx, DEL_CH, 0, 0, 1, "\n");
		n--;
	    }
	}
	if (op->cmd == del_cmd) {
	    n = op->F2 - op->F1 + 1;
	    while (n && (sz = f_gets(buf, sizeof(buf), f))) {
		line++;
		printer(ctx, ADD_CH, line, off, sz, buf);
		off += sz;
		while (buf[sz - 1] != '\n') {
		    if (!(sz = f_gets(buf, sizeof(buf), f))) {
			printer(ctx, 0, 0, 0, 1, "\n");
			break;
		    }
		    printer(ctx, 0, 0, 0, sz, buf);
		    off += sz;
		    if (got_interrupt()) {
			return -1;
		    }
		}
		n--;
	    }
	    if (n) {
		return -1;
	    }
	}
	if (op->cmd == 'c') {
	    n = op->F2 - op->F1 + 1;
	    while (n && (sz = f_gets(buf, sizeof(buf), f))) {
		line++;
		printer(ctx, CHG_CH, line, off, sz, buf);
		off += sz;
		while (buf[sz - 1] != '\n') {
		    if (!(sz = f_gets(buf, sizeof(buf), f))) {
			printer(ctx, 0, 0, 0, 1, "\n");
			break;
		    }
		    printer(ctx, 0, 0, 0, sz, buf);
		    off += sz;
		    if (got_interrupt()) {
			return -1;
		    }
		}
		n--;
	    }
	    if (n) {
		return -1;
	    }
	    n = op->T2 - op->T1 - (op->F2 - op->F1);
	    while (n > 0) {
		printer(ctx, CHG_CH, 0, 0, 1, "\n");
		n--;
	    }
	}
    }
#undef T2
#undef T1
#undef F2
#undef F1

    while ((sz = f_gets(buf, sizeof(buf), f))) {
	line++;
	printer(ctx, EQU_CH, line, off, sz, buf);
	off += sz;
	while (buf[sz - 1] != '\n') {
	    if (!(sz = f_gets(buf, sizeof(buf), f))) {
		printer(ctx, 0, 0, 0, 1, "\n");
		break;
	    }
	    printer(ctx, 0, 0, 0, sz, buf);
	    off += sz;
	    if (got_interrupt()) {
		return -1;
	    }
	}
    }

    return 0;
}


/* horizontal diff ***********************************************************/


/**
 * Longest common substring.
 *
 * \param s first string
 * \param m length of first string
 * \param t second string
 * \param n length of second string
 * \param ret list of offsets for longest common substrings inside each string
 * \param min minimum length of common substrings
 *
 * \return 0 if success, nonzero otherwise
 */
static int
lcsubstr (const char *s, int m, const char *t, int n, ARRAY *ret, int min)
{
    int i, j;

    int *Lprev, *Lcurr;

    int z = 0;

    arr_init(ret, sizeof(PAIR), 4);

    if (m < min || n < min) {
	/* XXX early culling */
	return 0;
    }

    Lprev = calloc(n + 1, sizeof(int));
    if (Lprev == NULL) {
	goto err_0;
    }
    Lcurr = calloc(n + 1, sizeof(int));
    if (Lcurr == NULL) {
	goto err_1;
    }

    for (i = 0; i < m; i++) {
	int *L = Lprev;
	Lprev = Lcurr;
	Lcurr = L;
#ifdef USE_MEMSET_IN_LCS
	memset(Lcurr, 0, (n + 1) * sizeof(int));
#endif
	for (j = 0; j < n; j++) {
#ifndef USE_MEMSET_IN_LCS
	    Lcurr[j + 1] = 0;
#endif
	    if (s[i] == t[j]) {
		int v = Lprev[j] + 1;
		Lcurr[j + 1] = v;
		if (z < v) {
		    z = v;
		    arr_reset(ret);
		}
		if (z == v && z >= min) {
		    int off0 = i - z + 1;
		    int off1 = j - z + 1;
		    int k;
		    PAIR *p;
		    for (p = ret->data, k = 0; k < ret->len; k++, p++) {
			if ((*p)[0] == off0) {
			    break;
			}
			if ((*p)[1] >= off1) {
			    break;
			}
		    }
		    if (k == ret->len) {
			p = arr_enlarge(ret);
			if (p == NULL) {
			    goto err_2;
			}
			(*p)[0] = off0;
			(*p)[1] = off1;
		    }
		}
	    }
	}
    }

    free(Lcurr);
    free(Lprev);
    return z;

  err_2:
    free(Lcurr);
  err_1:
    free(Lprev);
  err_0:
    arr_free(ret, NULL);
    return -1;
}


/**
 * Scan recursively for common substrings and build ranges.
 *
 * \param s first string
 * \param t second string
 * \param bracket current limits for both of the strings
 * \param min minimum length of common substrings
 * \param hdiff list of horizontal diff ranges to fill
 * \param depth recursion depth
 *
 * \return 0 if success, nonzero otherwise
 */
static int
hdiff_multi (const char *s, const char *t, const BRACKET bracket, int min, ARRAY *hdiff, unsigned int depth)
{
    BRACKET *p;

    if (depth--) {
	ARRAY ret;
	BRACKET b;
	int len = lcsubstr(s + bracket[0].off, bracket[0].len,
			   t + bracket[1].off, bracket[1].len, &ret, min);
	if (ret.len) {
	    int k = 0;
	    const PAIR *data = ret.data;

	    b[0].off = bracket[0].off;
	    b[0].len = data[k][0];
	    b[1].off = bracket[1].off;
	    b[1].len = data[k][1];
	    hdiff_multi(s, t, b, min, hdiff, depth);

	    for (k = 0; k < ret.len - 1; k++) {
		b[0].off = bracket[0].off + data[k][0] + len;
		b[0].len = data[k + 1][0] - data[k][0] - len;
		b[1].off = bracket[1].off + data[k][1] + len;
		b[1].len = data[k + 1][1] - data[k][1] - len;
		hdiff_multi(s, t, b, min, hdiff, depth);
	    }

	    b[0].off = bracket[0].off + data[k][0] + len;
	    b[0].len = bracket[0].len - data[k][0] - len;
	    b[1].off = bracket[1].off + data[k][1] + len;
	    b[1].len = bracket[1].len - data[k][1] - len;
	    hdiff_multi(s, t, b, min, hdiff, depth);

	    arr_free(&ret, NULL);
	    return 0;
	}
    }

    p = arr_enlarge(hdiff);
    if (p == NULL) {
	return -1;
    }
    (*p)[0].off = bracket[0].off;
    (*p)[0].len = bracket[0].len;
    (*p)[1].off = bracket[1].off;
    (*p)[1].len = bracket[1].len;

    return 0;
}


/**
 * Build list of horizontal diff ranges.
 *
 * \param s first string
 * \param m length of first string
 * \param t second string
 * \param n length of second string
 * \param min minimum length of common substrings
 * \param hdiff list of horizontal diff ranges to fill
 * \param depth recursion depth
 *
 * \return 0 if success, nonzero otherwise
 */
static int
hdiff_scan (const char *s, int m, const char *t, int n, int min, ARRAY *hdiff, unsigned int depth)
{
    int i;
    BRACKET b;

    /* dumbscan (single horizontal diff) -- does not compress whitespace */

    for (i = 0; i < m && i < n && s[i] == t[i]; i++) {
    }
    for (; m > i && n > i && s[m - 1] == t[n - 1]; m--, n--) {
    }
    b[0].off = i;
    b[0].len = m - i;
    b[1].off = i;
    b[1].len = n - i;

    /* smartscan (multiple horizontal diff) */

    arr_init(hdiff, sizeof(BRACKET), 4);
    hdiff_multi(s, t, b, min, hdiff, depth);
    if (hdiff->error) {
	arr_free(hdiff, NULL);
	return -1;
    }

    return 0;
}


/* read line *****************************************************************/


/**
 * Check if character is inside horizontal diff limits.
 *
 * \param k rank of character inside line
 * \param hdiff horizontal diff structure
 * \param ord 0 if reading from first file, 1 if reading from 2nd file
 *
 * \return TRUE if inside hdiff limits, FALSE otherwise
 */
static int
is_inside (int k, ARRAY *hdiff, int ord)
{
    int i;
    BRACKET *b;
    for (b = hdiff->data, i = 0; i < hdiff->len; i++, b++) {
	int start = (*b)[ord].off;
	int end = start + (*b)[ord].len;
	if (k >= start && k < end) {
	    return 1;
	}
    }
    return 0;
}


/**
 * Copy `src' to `dst' expanding tabs.
 *
 * \param dst destination buffer
 * \param src source buffer
 * \param srcsize size of src buffer
 * \param base virtual base of this string, needed to calculate tabs
 * \param ts tab size
 *
 * \return new virtual base
 *
 * \note The procedure returns when all bytes are consumed from `src'
 */
static int
cvt_cpy (char *dst, const char *src, size_t srcsize, int base, int ts)
{
    int i;
    for (i = 0; srcsize; i++, src++, dst++, srcsize--) {
	*dst = *src;
	if (*src == '\t') {
	    int j = TAB_SKIP(ts, i + base);
	    i += j - 1;
	    while (j-- > 0) {
		*dst++ = ' ';
	    }
	    dst--;
	}
    }
    return i + base;
}


/**
 * Copy `src' to `dst' expanding tabs.
 *
 * \param dst destination buffer
 * \param dstsize size of dst buffer
 * \param[in,out] _src source buffer
 * \param srcsize size of src buffer
 * \param base virtual base of this string, needed to calculate tabs
 * \param ts tab size
 *
 * \return new virtual base
 *
 * \note The procedure returns when all bytes are consumed from `src'
 *       or `dstsize' bytes are written to `dst'
 * \note Upon return, `src' points to the first unwritten character in source
 */
static int
cvt_ncpy (char *dst, int dstsize, const char **_src, size_t srcsize, int base, int ts)
{
    int i;
    const char *src = *_src;
    for (i = 0; i < dstsize && srcsize; i++, src++, dst++, srcsize--) {
	*dst = *src;
	if (*src == '\t') {
	    int j = TAB_SKIP(ts, i + base);
	    if (j > dstsize - i) {
		j = dstsize - i;
	    }
	    i += j - 1;
	    while (j-- > 0) {
		*dst++ = ' ';
	    }
	    dst--;
	}
    }
    *_src = src;
    return i + base;
}


/**
 * Read line from memory, converting tabs to spaces and padding with spaces.
 *
 * \param src buffer to read from
 * \param srcsize size of src buffer
 * \param dst buffer to read to
 * \param dstsize size of dst buffer, excluding trailing null
 * \param skip number of characters to skip
 * \param ts tab size
 * \param show_cr show trailing carriage return as ^M
 *
 * \return negative on error, otherwise number of bytes except padding
 */
static int
cvt_mget (const char *src, size_t srcsize, char *dst, int dstsize, int skip, int ts, int show_cr)
{
    int sz = 0;
    if (src != NULL) {
	int i;
	char *tmp = dst;
	const int base = 0;
	for (i = 0; dstsize && srcsize && *src != '\n'; i++, src++, srcsize--) {
	    if (*src == '\t') {
		int j = TAB_SKIP(ts, i + base);
		i += j - 1;
		while (j-- > 0) {
		    if (skip) {
			skip--;
		    } else if (dstsize) {
			dstsize--;
			*dst++ = ' ';
		    }
		}
	    } else if (src[0] == '\r' && (srcsize == 1 || src[1] == '\n')) {
		if (!skip && show_cr) {
		    if (dstsize > 1) {
			dstsize -= 2;
			*dst++ = '^';
			*dst++ = 'M';
		    } else {
			dstsize--;
			*dst++ = '.';
		    }
		}
		break;
	    } else {
		if (skip) {
		    skip--;
		} else {
		    dstsize--;
		    *dst++ = is_printable(*src) ? *src : '.';
		}
	    }
	}
	sz = dst - tmp;
    }
    while (dstsize) {
	dstsize--;
	*dst++ = ' ';
    }
    *dst = '\0';
    return sz;
}


/**
 * Read line from memory and build attribute array.
 *
 * \param src buffer to read from
 * \param srcsize size of src buffer
 * \param dst buffer to read to
 * \param dstsize size of dst buffer, excluding trailing null
 * \param skip number of characters to skip
 * \param ts tab size
 * \param show_cr show trailing carriage return as ^M
 * \param hdiff horizontal diff structure
 * \param ord 0 if reading from first file, 1 if reading from 2nd file
 * \param att buffer of attributes
 *
 * \return negative on error, otherwise number of bytes except padding
 */
static int
cvt_mgeta (const char *src, size_t srcsize, char *dst, int dstsize, int skip, int ts, int show_cr, ARRAY *hdiff, int ord, char *att)
{
    int sz = 0;
    if (src != NULL) {
	int i, k;
	char *tmp = dst;
	const int base = 0;
	for (i = 0, k = 0; dstsize && srcsize && *src != '\n'; i++, k++, src++, srcsize--) {
	    if (*src == '\t') {
		int j = TAB_SKIP(ts, i + base);
		i += j - 1;
		while (j-- > 0) {
		    if (skip) {
			skip--;
		    } else if (dstsize) {
			dstsize--;
			*att++ = is_inside(k, hdiff, ord);
			*dst++ = ' ';
		    }
		}
	    } else if (src[0] == '\r' && (srcsize == 1 || src[1] == '\n')) {
		if (!skip && show_cr) {
		    if (dstsize > 1) {
			dstsize -= 2;
			*att++ = is_inside(k, hdiff, ord);
			*dst++ = '^';
			*att++ = is_inside(k, hdiff, ord);
			*dst++ = 'M';
		    } else {
			dstsize--;
			*att++ = is_inside(k, hdiff, ord);
			*dst++ = '.';
		    }
		}
		break;
	    } else {
		if (skip) {
		    skip--;
		} else {
		    dstsize--;
		    *att++ = is_inside(k, hdiff, ord);
		    *dst++ = is_printable(*src) ? *src : '.';
		}
	    }
	}
	sz = dst - tmp;
    }
    while (dstsize) {
	dstsize--;
	*att++ = 0;
	*dst++ = ' ';
    }
    *dst = '\0';
    return sz;
}


/**
 * Read line from file, converting tabs to spaces and padding with spaces.
 *
 * \param f file stream to read from
 * \param off offset of line inside file
 * \param dst buffer to read to
 * \param dstsize size of dst buffer, excluding trailing null
 * \param skip number of characters to skip
 * \param ts tab size
 * \param show_cr show trailing carriage return as ^M
 *
 * \return negative on error, otherwise number of bytes except padding
 */
static int
cvt_fget (FBUF *f, off_t off, char *dst, int dstsize, int skip, int ts, int show_cr)
{
    int base = 0;
    int old_base = base;
    const int amount = dstsize;

    int useful;
    int offset;

    ssize_t i;
    size_t sz;

    int lastch = '\0';

    const char *q = NULL;
    char tmp[BUFSIZ];	/* XXX capacity must be >= max{dstsize + 1, amount} */
    char cvt[BUFSIZ];	/* XXX capacity must be >= MAX_TAB_WIDTH * amount */

    if ((int)sizeof(tmp) < amount || (int)sizeof(tmp) <= dstsize || (int)sizeof(cvt) < 8 * amount) {
	/* abnormal, but avoid buffer overflow */
	memset(dst, ' ', dstsize);
	dst[dstsize] = '\0';
	return 0;
    }

    f_seek(f, off, SEEK_SET);

    while (skip > base) {
	old_base = base;
	if (!(sz = f_gets(tmp, amount, f))) {
	    break;
	}
	base = cvt_cpy(cvt, tmp, sz, old_base, ts);
	if (cvt[base - old_base - 1] == '\n') {
	    q = &cvt[base - old_base - 1];
	    base = old_base + q - cvt + 1;
	    break;
	}
    }

    useful = base - skip;
    offset = skip - old_base;

    if (useful < 0) {
	memset(dst, ' ', dstsize);
	dst[dstsize] = '\0';
	return 0;
    }

    if (useful <= dstsize) {
	if (useful) {
	    memmove(dst, cvt + offset, useful);
	}
	if (q == NULL && (sz = f_gets(tmp, dstsize - useful + 1, f))) {
	    const char *ptr = tmp;
	    useful += cvt_ncpy(dst + useful, dstsize - useful, &ptr, sz, base, ts) - base;
	    if (ptr < tmp + sz) {
		lastch = *ptr;
	    }
	}
	sz = useful;
    } else {
	memmove(dst, cvt + offset, dstsize);
	sz = dstsize;
	lastch = cvt[offset + dstsize];
    }

    dst[sz] = lastch;
    for (i = 0; i < sz && dst[i] != '\n'; i++) {
	if (dst[i] == '\r' && dst[i + 1] == '\n') {
	    if (show_cr) {
		if (i + 1 < dstsize) {
		    dst[i++] = '^';
		    dst[i++] = 'M';
		} else {
		    dst[i++] = '.';
		}
	    }
	    break;
	} else if (!is_printable(dst[i])) {
	    dst[i] = '.';
	}
    }
    for (; i < dstsize; i++) {
	dst[i] = ' ';
    }
    dst[i] = '\0';
    return sz;
}


/* diff printers et al *******************************************************/


static void
cc_free_elt (void *elt)
{
    DIFFLN *p = elt;
    if (p->p && !p->borrow) {
	free(p->p);
    }
}


static int
printer (void *ctx, int ch, int line, off_t off, size_t sz, const char *str)
{
    DIFFLN *p;
    ARRAY *a = ((PRINTER_CTX *)ctx)->a;
    ARRAY *other = ((PRINTER_CTX *)ctx)->other;
    DSRC dsrc = ((PRINTER_CTX *)ctx)->dsrc;
    if (a->error) {
	return -1;
    }
    if (ch) {
	p = arr_enlarge(a);
	if (p == NULL) {
	    return -1;
	}
	p->p = NULL;
	p->ch = ch;
	p->line = line;
	p->u.off = off;
	p->borrow = 0;
	if (dsrc == DATA_SRC_MEM && line) {
	    if (sz && str[sz - 1] == '\n') {
		sz--;
		if (ch == EQU_CH && other) {
		    const DIFFLN *q = (DIFFLN *)other->data + a->len - 1;
		    if (sz == q->u.len && !memcmp(str, q->p, sz)) {
			p->p = q->p;
			p->u.len = sz;
			p->borrow = 1;
			goto okay;
		    }
		}
	    }
	    if (sz) {
		p->p = malloc(sz);
		if (p->p == NULL) {
		    a->error = 1;
		    return -1;
		}
		memcpy(p->p, str, sz);
	    }
	    p->u.len = sz;
	}
    } else if (dsrc == DATA_SRC_MEM) {
	if (!a->len) {
	    a->error = 1;
	    return -1;
	}
	p = (DIFFLN *)a->data + a->len - 1;
	if (sz && str[sz - 1] == '\n') {
	    sz--;
	    if (p->ch == EQU_CH && other) {
		const DIFFLN *q = (DIFFLN *)other->data + a->len - 1;
		if (p->u.len + sz == q->u.len && !memcmp(str, (char *)q->p + p->u.len, sz) && !memcmp(p->p, q->p, p->u.len)) {
		    free(p->p);
		    p->p = q->p;
		    p->u.len = q->u.len;
		    p->borrow = 1;
		    goto okay;
		}
	    }
	}
	if (sz) {
	    size_t new_size = p->u.len + sz;
	    char *q = realloc(p->p, new_size);
	    if (q == NULL) {
		a->error = 1;
		return -1;
	    }
	    memcpy(q + p->u.len, str, sz);
	    p->p = q;
	}
	p->u.len += sz;
    }
  okay:
    if (dsrc == DATA_SRC_TMP && (line || !ch)) {
	FBUF *f = ((PRINTER_CTX *)ctx)->f;
	f_write(f, str, sz);
    }
    return 0;
}


static int
redo_diff (WDiff *view)
{
    FBUF *f[2], *t[2];
    ARRAY *a = view->a;

    PRINTER_CTX ctx;
    ARRAY ops;
    int ndiff;
    int rv;

    char extra[256];

    extra[0] = '\0';
    if (view->opt.quality == 2) {
	strcat(extra, " -d");
    }
    if (view->opt.quality == 1) {
	strcat(extra, " --speed-large-files");
    }
    if (view->opt.strip_trailing_cr) {
	strcat(extra, " --strip-trailing-cr");
    }
    if (view->opt.ignore_tab_expansion) {
	strcat(extra, " -E");
    }
    if (view->opt.ignore_space_change) {
	strcat(extra, " -b");
    }
    if (view->opt.ignore_all_space) {
	strcat(extra, " -w");
    }
    if (view->opt.ignore_case) {
	strcat(extra, " -i");
    }

    f[0] = f_open(view->file[0], O_RDONLY);
    f[1] = f_open(view->file[1], O_RDONLY);
    if (f[0] == NULL || f[1] == NULL) {
	goto err;
    }

    ndiff = dff_execute(view->args, extra, f_getname(f[0]), f_getname(f[1]), &ops);
    if (ndiff < 0) {
	goto err;
    }

    t[0] = NULL;
    t[1] = NULL;
    if (view->dsrc == DATA_SRC_TMP) {
	t[0] = f_temp();
	t[1] = f_temp();
	if (t[0] == NULL || t[1] == NULL) {
	    f_close(t[0]);
	    f_close(t[1]);
	    arr_free(&ops, NULL);
	    goto err;
	}
    }

    ctx.dsrc = view->dsrc;

    enable_interrupt_key();
    arr_init(&a[0], sizeof(DIFFLN), 256);
    ctx.a = &a[0];
    ctx.f = t[0];
    ctx.other = NULL;
    rv = dff_reparse(0, f[0], &ops, printer, &ctx);

    arr_init(&a[1], sizeof(DIFFLN), 256);
    ctx.a = &a[1];
    ctx.f = t[1];
    ctx.other = &a[0]; /* XXX set this to NULL to disable borrow */
    if (rv == 0) {
	rv = dff_reparse(1, f[1], &ops, printer, &ctx);
    }
    disable_interrupt_key();

    arr_free(&ops, NULL);

    if (view->dsrc != DATA_SRC_ORG) {
	f_close(f[0]);
	f_close(f[1]);
	f[0] = NULL;
	f[1] = NULL;
    }
    if (view->dsrc == DATA_SRC_TMP) {
	f[0] = t[0];
	f[1] = t[1];
    }

    if (rv || a[0].error || a[1].error || a[0].len != a[1].len) {
	arr_free(&a[0], cc_free_elt);
	arr_free(&a[1], cc_free_elt);
	goto err;
    }

    if (view->dsrc == DATA_SRC_MEM && HDIFF_ENABLE) {
	view->hdiff = calloc(a[0].len, sizeof(ARRAY *));
    }

    view->f[0] = f[0];
    view->f[1] = f[1];
    return ndiff;

err:
    f_close(f[0]);
    f_close(f[1]);
    return -1;
}


static void
destroy_hdiff (WDiff *view)
{
    if (view->hdiff != NULL) {
	int i;
	int len = view->a[0].len;
	for (i = 0; i < len; i++) {
	    ARRAY *h = view->hdiff[i];
	    if (h != NULL) {
		arr_free(h, NULL);
		free(h);
	    }
	}
	free(view->hdiff);
	view->hdiff = NULL;
    }
}


/* stuff *********************************************************************/


static int
get_line_numbers (const ARRAY *a, int pos, int *linenum, int *lineofs)
{
    const DIFFLN *p;

    *linenum = 0;
    *lineofs = 0;

    if (a->len) {
	if (pos >= a->len) {
	    pos = a->len - 1;
	}

	p = (DIFFLN *)a->data + pos;

	if (!p->line) {
	    int n;
	    for (n = pos; n > 0; n--) {
		p--;
		if (p->line) {
		    break;
		}
	    }
	    *lineofs = pos - n + 1;
	}

	*linenum = p->line;
    }
    return 0;
}


static int
calc_nwidth (const ARRAY *const a)
{
    int l1, o1;
    int l2, o2;
    get_line_numbers(&a[0], a[0].len - 1, &l1, &o1);
    get_line_numbers(&a[1], a[1].len - 1, &l2, &o2);
    if (l1 < l2) {
	l1 = l2;
    }
    return get_digits(l1);
}


static int
find_prev_hunk (const ARRAY *a, int pos)
{
#if 1
    while (pos > 0 && ((DIFFLN *)a->data)[pos].ch != EQU_CH) {
	pos--;
    }
    while (pos > 0 && ((DIFFLN *)a->data)[pos].ch == EQU_CH) {
	pos--;
    }
#else
    while (pos > 0 && ((DIFFLN *)a->data)[pos - 1].ch == EQU_CH) {
	pos--;
    }
    while (pos > 0 && ((DIFFLN *)a->data)[pos - 1].ch != EQU_CH) {
	pos--;
    }
#endif

    return pos;
}


static int
find_next_hunk (const ARRAY *a, int pos)
{
    while (pos < a->len && ((DIFFLN *)a->data)[pos].ch != EQU_CH) {
	pos++;
    }
    while (pos < a->len && ((DIFFLN *)a->data)[pos].ch == EQU_CH) {
	pos++;
    }

    return pos;
}


static int
get_short_end (const ARRAY *const a, int skip_rows, int ts, DSRC dsrc)
{
    int ord;
    size_t len = -1; /* XXX error, also big unsigned */

    if (dsrc == DATA_SRC_MEM) {
	for (ord = 0; ord <= 1; ord++) {
	    const int base = 0;
	    const DIFFLN *p = ((DIFFLN *)a[ord].data) + skip_rows;
	    const char *src = p->p;
	    size_t l = 0;
	    if (src) {
		int i;
		for (i = 0; i < p->u.len; i++) {
		    if (src[i] == '\t') {
			l += TAB_SKIP(ts, l + base);
		    } else if (src[i] == '\r' && (i + 1 == p->u.len || src[i + 1] == '\n')) {
			break;
		    } else {
			l++;
		    }
		}
		if (l) {
		    l--;
		}
		if (len > l) {
		    len = l;
		}
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
view_init (WDiff *view, const char *args, const char *file1, const char *file2, DSRC dsrc)
{
    int ndiff;

    view->args = args;
    view->file[0] = file1;
    view->file[1] = file2;
    view->hdiff = NULL;
    view->dsrc = dsrc;

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
    view->show_cr = 1;
    view->show_hdiff = 1;
    view->tab_size = 8;
    view->ord = 0;
    view->full = 0;
    view->last_found = -1;

    view->opt.quality = 0;
    view->opt.strip_trailing_cr = 0;
    view->opt.ignore_tab_expansion = 0;
    view->opt.ignore_space_change = 0;
    view->opt.ignore_all_space = 0;
    view->opt.ignore_case = 0;

    view_compute_areas(view);
    return 0;
}


static void
view_fini (WDiff *view)
{
    f_close(view->f[1]);
    f_close(view->f[0]);
    view->f[0] = NULL;
    view->f[1] = NULL;
    destroy_hdiff(view);
    arr_free(&view->a[1], cc_free_elt);
    arr_free(&view->a[0], cc_free_elt);
}


static int
view_display_file (const WDiff *view, int ord,
		   int r, int c, int height, int width)
{
    int i, j, k;
    char buf[BUFSIZ];
    FBUF *f = view->f[ord];
    const ARRAY *a = &view->a[ord];
    int skip = view->skip_cols;
    int display_symbols = view->display_symbols;
    int display_numbers = view->display_numbers;
    int show_cr = view->show_cr;
    int tab_size = view->tab_size;
    const DIFFLN *p;

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

    for (i = view->skip_rows, j = 0, p = (DIFFLN *)a->data + i; i < a->len && j < height; p++, j++, i++) {
	int ch = p->ch;
	tty_setcolor(NORMAL_COLOR);
	if (display_symbols) {
	    tty_gotoyx(r + j, c - 2);
	    tty_print_char(ch);
	}
	if (p->line) {
	    if (display_numbers) {
		tty_gotoyx(r + j, c - xwidth);
		snprintf(buf, display_numbers + 1, "%*d", nwidth, p->line);
		tty_print_string(buf);
	    }
	    if (ch == ADD_CH) {
		tty_setcolor(DFFADD_COLOR);
	    }
	    if (ch == CHG_CH) {
		tty_setcolor(DFFCHG_COLOR);
	    }
	    if (f == NULL) {
		if (i == view->last_found) {
		    tty_setcolor(MARKED_SELECTED_COLOR);
		} else if (view->show_hdiff) {
		    if (HDIFF_ENABLE && view->hdiff != NULL && view->hdiff[i] == NULL) {
			const DIFFLN *s = (DIFFLN *)view->a[0].data + i;
			const DIFFLN *q = (DIFFLN *)view->a[1].data + i;
			if (s->line && q->line && s->ch == CHG_CH) {
			    ARRAY *h = malloc(sizeof(ARRAY));
			    if (h != NULL) {
				int rv = hdiff_scan(s->p, s->u.len, q->p, q->u.len, HDIFF_MINCTX, h, HDIFF_DEPTH);
				if (rv != 0) {
				    free(h);
				    h = NULL;
				}
				view->hdiff[i] = h;
			    }
			}
		    }
		    if (view->hdiff != NULL && view->hdiff[i] != NULL) {
			char att[BUFSIZ];
			cvt_mgeta(p->p, p->u.len, buf, width, skip, tab_size, show_cr, view->hdiff[i], ord, att);
			tty_gotoyx(r + j, c);
			for (k = 0; k < width; k++) {
			    tty_setcolor(att[k] ? DFFCHH_COLOR : DFFCHG_COLOR);
			    tty_print_char(buf[k] & 0xFF);
			}
			continue;
		    } else if (ch == CHG_CH) {
			tty_setcolor(DFFCHH_COLOR);
		    }
		}
		cvt_mget(p->p, p->u.len, buf, width, skip, tab_size, show_cr);
	    } else {
		cvt_fget(f, p->u.off, buf, width, skip, tab_size, show_cr);
	    }
	} else {
	    if (display_numbers) {
		tty_gotoyx(r + j, c - xwidth);
		memset(buf, ' ', display_numbers);
		buf[display_numbers] = '\0';
		tty_print_nstring(buf, display_numbers);
	    }
	    if (ch == DEL_CH) {
		tty_setcolor(DFFDEL_COLOR);
	    }
	    if (ch == CHG_CH) {
		tty_setcolor(DFFCHD_COLOR);
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
    get_line_numbers(&view->a[ord], skip_rows, &linenum, &lineofs);

    filename_width = width - 22;
    if (filename_width < 8) {
	filename_width = 8;
    }
    if (filename_width >= (int)sizeof(buf)) {
	/* abnormal, but avoid buffer overflow */
	filename_width = sizeof(buf) - 1;
    }
    trim(strip_home_and_password(view->file[ord]), buf, filename_width);
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

    int last = view->a[0].len - 1;

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
    int ndiff = view->ndiff;

    diffopt_widgets[2].value = view->opt.quality;
    diffopt_widgets[2].result = &view->opt.quality;
    diffopt_widgets[3].result = &view->opt.strip_trailing_cr;
    diffopt_widgets[4].result = &view->opt.ignore_all_space;
    diffopt_widgets[5].result = &view->opt.ignore_space_change;
    diffopt_widgets[6].result = &view->opt.ignore_tab_expansion;
    diffopt_widgets[7].result = &view->opt.ignore_case;

    if (quick_dialog(&diffopt) != B_CANCEL) {
	view_fini(view);
	mc_setctl (view->file[0], VFS_SETCTL_STALE_DATA, NULL);
	mc_setctl (view->file[1], VFS_SETCTL_STALE_DATA, NULL);
	ndiff = redo_diff(view);
	if (ndiff >= 0) {
	    view->ndiff = ndiff;
	}
    }

    if (ndiff < 0) {
	view->view_quit = 1;
    } else if (view->display_numbers) {
	int old = view->display_numbers;
	view->display_numbers = calc_nwidth(view->a);
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
search_string (const DIFFLN *p, size_t xpos, const void *needle, size_t nlen, int whole, int ccase)
{
    const unsigned char *haystack = p->p;
    size_t hlen = p->u.len;

    if (xpos > hlen || nlen <= 0 || haystack == NULL || needle == NULL) {
	return NULL;
    }

    /* XXX I should use Boyer-Moore */
    if (ccase) {
	return memmem_dumb(haystack, xpos, hlen, needle, nlen, whole);
    } else {
	return memmem_dumb_nocase(haystack, xpos, hlen, needle, nlen, whole);
    }
}


static int
view_search_string (WDiff *view, const char *needle, int ccase, int back, int whole)
{
    size_t nlen = strlen(needle);
    size_t xpos = 0;

    int ord = view->ord;
    const ARRAY *a = &view->a[ord];
    const DIFFLN *p;

    int i = view->last_found;

    if (back) {
	if (i == -1) {
	    i = view->skip_rows;
	}
	for (--i, p = (DIFFLN *)a->data + i; i >= 0; p--, i--) {
	    const unsigned char *q = search_string(p, xpos, needle, nlen, whole, ccase);
	    if (q != NULL) {
		return i;
	    }
	}
    } else {
	if (i == -1) {
	    i = view->skip_rows - 1;
	}
	for (++i, p = (DIFFLN *)a->data + i; i < a->len; p++, i++) {
	    const unsigned char *q = search_string(p, xpos, needle, nlen, whole, ccase);
	    if (q != NULL) {
		return i;
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

    if (view->dsrc != DATA_SRC_MEM) {
	error_dialog(_("Search"), _(" Search is disabled "));
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
    int linenum, lineofs;

    if (view->dsrc == DATA_SRC_TMP) {
	error_dialog(_("Edit"), _(" Edit is disabled "));
	return;
    }

    get_line_numbers(&view->a[ord], view->skip_rows, &linenum, &lineofs);
    do_edit_at_line(view->file[ord], linenum);
    view_redo(view);
    view_update(view);
}


static void
view_edit_cmd (WDiff *view)
{
    view_edit(view, view->ord);
}


static void
view_view (WDiff *view, int ord)
{
    int linenum, lineofs;

    get_line_numbers(&view->a[ord], view->skip_rows, &linenum, &lineofs);
    view_file_at_line(view->file[ord], 0, use_internal_view, linenum);
    view_update(view);
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
		const DIFFLN *p;
		ord ^= view->ord;
		for (p = view->a[ord].data; i < view->a[ord].len; i++, p++) {
		    if (p->line == newline) {
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
    interactive_display(NULL, "[Diff Viewer]");
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

    switch (c) {
	case 's':
	    view->display_symbols ^= 1;
	    view->new_frame = 1;
	    return MSG_HANDLED;

	case 'l':
	    view->display_numbers ^= calc_nwidth(view->a);
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

	case 'c':
	    view->show_cr ^= 1;
	    return MSG_HANDLED;

	case 'h':
	    view->show_hdiff ^= 1;
	    return MSG_HANDLED;

	case '2':
	case '3':
	case '4':
	case '8':
	    view->tab_size = c - '0';
	    return MSG_HANDLED;

	case XCTRL('u'):
	    view->ord ^= 1;
	    return MSG_HANDLED;

	case XCTRL('r'):
	    view_redo(view);
	    return MSG_HANDLED;

	case 'n':
	    view->skip_rows = find_next_hunk(&view->a[0], view->skip_rows);
	    return MSG_HANDLED;

	case 'p':
	    view->skip_rows = find_prev_hunk(&view->a[0], view->skip_rows);
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
	    view->skip_rows = view->a[0].len - 1;
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
	    view->skip_cols = get_short_end(view->a, view->skip_rows, view->tab_size, view->dsrc);
	    return MSG_HANDLED;

	case XCTRL('o'):
	    view_other_cmd();
	    return MSG_HANDLED;

	case '\n':
	    return MSG_HANDLED;

	case 'x':
	    xdiff_view(view->file[0], view->file[1]);
	    return MSG_HANDLED;

	case 'q':
	case ESC_CHAR:
	    view->view_quit = 1;
	    return MSG_HANDLED;

#ifdef HAVE_CHARSET
	case XCTRL ('t'):
	    do_select_codepage ();
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
diff_view (const char *file1, const char *file2)
{
    int error;
    WDiff *view;
    WButtonBar *bar;
    Dlg_head *view_dlg;

    /* Create dialog and widgets, put them on the dialog */
    view_dlg =
	create_dlg(0, 0, LINES, COLS, NULL, view_dialog_callback,
		   "[Diff Viewer]", NULL, DLG_WANT_TAB);

    view = g_new0(WDiff, 1);

    init_widget(&view->widget, 0, 0, LINES - 1, COLS,
		(callback_fn)view_callback,
		(mouse_h)view_event);

    widget_want_cursor(view->widget, 0);

    bar = buttonbar_new(1);

    add_widget(view_dlg, bar);
    add_widget(view_dlg, view);

    error = view_init(view, "-a", file1, file2, DATA_SRC_MEM);

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

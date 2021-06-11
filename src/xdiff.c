/*
 * Copyright (c) 2009 Daniel Borca  All rights reserved.
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
#include <errno.h>
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
#include "zutil.h"
#include "ydiff.h"
#include "xdiff.h"


#define VERTICAL_SPLIT		1

#define REINIT_READ_LEFT	(1 << 0)
#define REINIT_READ_RIGHT	(1 << 1)
#define REINIT_REALLOC		(1 << 2)
#define REINIT_OPEN		(1 << 3)

#define XDIFF_IN_LEFT		(1 << 0)
#define XDIFF_IN_RIGHT		(1 << 1)
#define XDIFF_DIFFERENT		(1 << 2)

struct display_file {
    char *data;
    size_t sz;
    off_t offs;
    off_t last;
    off_t end;
    int move;
};

typedef struct {
    Widget widget;

    const char *file[2];	/* filenames */
    const char *label[2];
    FBUF *f[2];
    char *diffs;
    struct display_file df[2];
    off_t max;
    size_t pbytes;
    size_t maxmem;
    int nbytes;
    int owidth;

    int view_quit:1;		/* Quit flag */

    int size;
    int half1;
    int half2;
    int bias;
    int subtract;
    int new_frame;
    int display_numbers;
    int ord;
    int full;
    int last_found;
} WDiff;


#define OPTX 50
#define OPTY 7

static QuickWidget diffopt_widgets[] = {
    { quick_button,   6,   10, 3, OPTY, N_("&Cancel"),                 0, B_CANCEL, NULL, NULL, NULL },
    { quick_button,   3,   10, 3, OPTY, N_("&OK"),                     0, B_ENTER,  NULL, NULL, NULL },
    NULL_QuickWidget
};

static QuickDialog diffopt = {
    OPTX, OPTY, -1, -1,
    N_(" Diff Options "), "[Diff Options]",
    diffopt_widgets, 0
};


#define error_dialog(h, s) query_dialog(h, s, D_ERROR, 1, _("&Dismiss"))


/* diff engine ***************************************************************/


static int
redo_diff (WDiff *view, int flags)
{
    size_t pbytes = view->pbytes;
    if (flags & REINIT_OPEN) {
	FBUF *f[2];
	f[0] = f_open(view->file[0], O_RDONLY);
	f[1] = f_open(view->file[1], O_RDONLY);
	if (f[0] == NULL || f[1] == NULL) {
	    f_close(f[0]);
	    f_close(f[1]);
	    return -1;
	}
	view->f[0] = f[0];
	view->f[1] = f[1];
    }
    if (flags & REINIT_REALLOC) {
	if (pbytes > view->maxmem) {
	    void *p = realloc(view->diffs, 3 * pbytes);
	    if (p == NULL) {
		return -1;
	    }
	    view->diffs = p;
	    view->maxmem = pbytes;
	}
	view->df[0].data = view->diffs + pbytes;
	view->df[1].data = view->df[0].data + pbytes;
	flags = REINIT_READ_LEFT | REINIT_READ_RIGHT;
    }
    if (flags & REINIT_READ_LEFT) {
	f_seek(view->f[0], view->df[0].offs, SEEK_SET);
	view->df[0].sz = f_read(view->f[0], view->df[0].data, pbytes);
	if (view->df[0].sz) {
	    if (view->df[0].sz < pbytes) {
		view->df[0].end = view->df[0].offs + view->df[0].sz;
	    }
	}
    }
    if (flags & REINIT_READ_RIGHT) {
	f_seek(view->f[1], view->df[1].offs, SEEK_SET);
	view->df[1].sz = f_read(view->f[1], view->df[1].data, pbytes);
	if (view->df[1].sz) {
	    if (view->df[1].sz < pbytes) {
		view->df[1].end = view->df[1].offs + view->df[1].sz;
	    }
	}
    }
    if (flags & (REINIT_READ_LEFT | REINIT_READ_RIGHT)) {
	size_t i, len;
	char *data1 = view->df[0].data;
	char *data2 = view->df[1].data;
	char *diffs = view->diffs;
	size_t sz1 = view->df[0].sz;
	size_t sz2 = view->df[1].sz;
	len = sz1;
	if (len > sz2) {
	    len = sz2;
	}
	memset(diffs, 0, pbytes);
	for (i = 0; i < len; i++) {
	    diffs[i] = XDIFF_IN_LEFT | XDIFF_IN_RIGHT;
	    if (data1[i] != data2[i]) {
		diffs[i] |= XDIFF_DIFFERENT;
	    }
	}
	for (i = len; i < sz1; i++) {
	    diffs[i] = XDIFF_IN_LEFT | XDIFF_DIFFERENT;
	}
	for (i = len; i < sz2; i++) {
	    diffs[i] = XDIFF_IN_RIGHT | XDIFF_DIFFERENT;
	}
	view->max = view->df[0].end;
	if (view->max < view->df[1].end) {
	    view->max = view->df[1].end;
	}
    }
    return 0;
}


/* stuff *********************************************************************/


/**
 * Read offset from string.
 *
 * \param[in,out] str string to parse
 * \param[out] n extracted offset
 *
 * \return 0 if success, otherwise non-zero
 */
static int
scan_unsigned (const char **str, off_t *n)
{
    const char *p = *str;
    char *q;
    errno = 0;
    *n = strtoul(p, &q, 0); /* XXX strtoull */
    if (errno || p == q) {
	return -1;
    }
    *str = q;
    return 0;
}


static int
find_prev_hunk (WDiff *view)
{
    int i;
    int pbytes = view->pbytes;

    for (;;) {
	int sub;
	int sub1 = pbytes;
	int sub2 = pbytes;

	if (sub1 > view->df[0].offs) {
	    sub1 = view->df[0].offs;
	}
	if (sub2 > view->df[1].offs) {
	    sub2 = view->df[1].offs;
	}

	sub = sub1;
	if (sub > sub2) {
	    sub = sub2;
	}

	if (sub == 0) {
	    return 0;
	}

	view->df[0].offs -= sub;
	view->df[1].offs -= sub;

	redo_diff(view, REINIT_READ_LEFT | REINIT_READ_RIGHT);

	i = sub;
	while (i > 0 && !(view->diffs[i - 1] & XDIFF_DIFFERENT)) {
	    i--;
	}
	if (i > 0) {
	    break;
	}
    }

    for (;;) {
	int sub;
	int sub1 = pbytes;
	int sub2 = pbytes;

	while (i > 0 && (view->diffs[i - 1] & XDIFF_DIFFERENT)) {
	    i--;
	}
	if (i > 0) {
	    view->df[0].offs += i;
	    view->df[1].offs += i;
	    break;
	}

	if (sub1 > view->df[0].offs) {
	    sub1 = view->df[0].offs;
	}
	if (sub2 > view->df[1].offs) {
	    sub2 = view->df[1].offs;
	}

	sub = sub1;
	if (sub > sub2) {
	    sub = sub2;
	}

	if (sub == 0) {
	    return 0;
	}

	view->df[0].offs -= sub;
	view->df[1].offs -= sub;

	redo_diff(view, REINIT_READ_LEFT | REINIT_READ_RIGHT);

	i = sub;
    }
    return 0;
}


static int
find_next_hunk (WDiff *view)
{
    int i;
    int pbytes = view->pbytes;

    if (!pbytes) {
	return 0;
    }

    for (;;) {
	i = 0;
	while (i < pbytes && (view->diffs[i] == (XDIFF_DIFFERENT | XDIFF_IN_LEFT | XDIFF_IN_RIGHT))) {
	    i++;
	}
	if (i < pbytes) {
	    break;
	}
	view->df[0].offs += i;
	view->df[1].offs += i;
	redo_diff(view, REINIT_READ_LEFT | REINIT_READ_RIGHT);
    }

    for (;;) {
	while (i < pbytes && !(view->diffs[i] & XDIFF_DIFFERENT) && view->diffs[i]) {
	    i++;
	}
	view->df[0].offs += i;
	view->df[1].offs += i;
	if (i < pbytes) {
	    break;
	}
	redo_diff(view, REINIT_READ_LEFT | REINIT_READ_RIGHT);
	i = 0;
    }

    if (view->df[0].offs >= view->df[0].end && view->df[1].offs >= view->df[1].end) {
	view->df[0].offs--;
	view->df[1].offs--;
    }

    return 0;
}


static int
recalc_offset_rel(WDiff *view, struct display_file *lf, struct display_file *rf, off_t bytes)
{
    off_t off;
    if (lf->move && rf->move) {
	if (bytes < 0) {
	    off = lf->offs;
	    if (off > rf->offs) {
		off = rf->offs;
	    }
	    if (bytes < -off) {
		bytes = -off;
	    }
	} else {
	    if (lf->offs + bytes >= view->max || rf->offs + bytes >= view->max) {
		return -1;
	    }
	}
	lf->offs += bytes;
	rf->offs += bytes;
    } else if (lf->move) {
	off = lf->offs + bytes;
	if (off >= lf->end) {
	    off = lf->end - 1;
	}
	if (off < 0) {
	    off = 0;
	}
	lf->offs = off;
    } else if (rf->move) {
	off = rf->offs + bytes;
	if (off >= rf->end) {
	    off = rf->end - 1;
	}
	if (off < 0) {
	    off = 0;
	}
	rf->offs = off;
    }
    return 0;
}


static void
recalc_offset_end(WDiff *view, struct display_file *lf, struct display_file *rf, int where)
{
    off_t off;
    if (lf->move && rf->move) {
	if (where == 0) {
	    if (lf->offs == 0 || rf->offs == 0) {
		lf->offs = 0;
		rf->offs = 0;
	    } else {
		off = lf->offs;
		if (off > rf->offs) {
		    off = rf->offs;
		}
		lf->offs -= off;
		rf->offs -= off;
	    }
	} else {
	    off_t ll = lf->end - lf->offs;
	    off_t rl = rf->end - rf->offs;
	    if (ll <= 1 || rl <= 1) {
		off = view->max - lf->offs;
		if (off > view->max - rf->offs) {
		    off = view->max - rf->offs;
		}
	    } else {
		off = ll;
		if (off > rl) {
		    off = rl;
		}
		off += (ll != rl);
	    }
	    lf->offs += off - 1;
	    rf->offs += off - 1;
	}
    } else if (lf->move) {
	lf->offs = where ? lf->end - 1 : 0;
    } else if (rf->move) {
	rf->offs = where ? rf->end - 1 : 0;
    }
}


static void
recalc_offset_pos(WDiff *view, struct display_file *lf, struct display_file *rf, off_t pos)
{
    if (lf->move && rf->move) {
	if (pos != lf->offs && recalc_offset_rel(view, lf, rf, pos - lf->offs) != 0) {
	    off_t off = view->max - lf->offs;
	    if (off > view->max - rf->offs) {
		off = view->max - rf->offs;
	    }
	    lf->offs += off - 1;
	    rf->offs += off - 1;
	}
    } else if (lf->move) {
	if (pos >= lf->end) {
	    pos = lf->end -1;
	}
	if (pos < 0) {
	    pos = 0;
	}
	lf->offs = pos;
    } else if (rf->move) {
	if (pos >= rf->end) {
	    pos = rf->end -1;
	}
	if (pos < 0) {
	    pos = 0;
	}
	rf->offs = pos;
    }
}


/* view routines and callbacks ***********************************************/


static void
view_compute_split (WDiff *view, int i)
{
    view->bias += i;
#if VERTICAL_SPLIT
    if (view->bias < 2 - view->half1) {
	view->bias = 2 - view->half1;
    }
    if (view->bias > view->half2 - 2) {
	view->bias = view->half2 - 2;
    }
#else	/* !VERTICAL_SPLIT */
    if (view->bias < 1 - view->half1) {
	view->bias = 1 - view->half1;
    }
    if (view->bias > view->half2 - 1) {
	view->bias = view->half2 - 1;
    }
#endif	/* !VERTICAL_SPLIT */
}


static void
view_compute_areas (WDiff *view)
{
#if VERTICAL_SPLIT
    view->size = LINES - 2;
    view->half1 = COLS / 2;
    view->half2 = COLS - view->half1;
#else	/* !VERTICAL_SPLIT */
    int height = LINES - 3;

    view->size = COLS;
    view->half1 = height / 2;
    view->half2 = height - view->half1;
#endif	/* !VERTICAL_SPLIT */

    view_compute_split(view, 0);
}


static int
view_init (WDiff *view, const char *file1, const char *file2)
{
    int rv;

    view->file[0] = file1;
    view->file[1] = file2;

    rv = redo_diff(view, REINIT_OPEN);
    if (rv) {
	return -1;
    }

    view->pbytes = 0;
    view->maxmem = 0;
    view->diffs = NULL;
    view->df[0].data = NULL;
    view->df[1].data = NULL;
    view->df[0].move = 1;
    view->df[1].move = 1;
    view->df[0].end = f_seek(view->f[0], 0, SEEK_END);
    view->df[1].end = f_seek(view->f[1], 0, SEEK_END);
    view->max = view->df[0].end;
    if (view->max < view->df[1].end) {
	view->max = view->df[1].end;
    }

    view->view_quit = 0;

    view->bias = 0;
    view->subtract = 0;
    view->new_frame = 1;
    view->display_numbers = 1;
    view->ord = 0;
    view->full = 0;
    view->last_found = -1;

    view_compute_areas(view);
    return 0;
}


static void
view_fini (WDiff *view, int free_mem)
{
    if (free_mem) {
	free(view->diffs);
	view->diffs = NULL;
	view->maxmem = 0;
    }

    f_close(view->f[1]);
    f_close(view->f[0]);
    view->f[0] = NULL;
    view->f[1] = NULL;
}


static int
view_display_file (WDiff *view, int ord,
		   int r, int c, int height, int width,
#if !VERTICAL_SPLIT
		   int total_height,
#endif	/* !VERTICAL_SPLIT */
		   int owidth)
{
    int i, j, k;
    char buf[BUFSIZ];
    const char *data = view->df[ord].data;
    const char *diffs = view->diffs;
    off_t offset = view->df[ord].offs;
    int nbytes = view->nbytes;

    unsigned int mask = 0;

    int available = width - (nbytes * 4 + 1);
    if (owidth > available - 1) {
	owidth = available - 1;
	if (owidth < 0) {
	    owidth = 0;
	}
    }

    if (owidth > 0) {
	mask = (((1 << ((owidth - 1) * 4)) - 1) << 4) | 0xF;
	owidth++;
    }

    if ((int)sizeof(buf) <= width) {
	/* abnormal, but avoid buffer overflow */
	return -1;
    }

    for (j = 0; j < height; j++) {
	int ch;
	int stop = 1;

	tty_gotoyx(r + j, c);

	if (owidth > 0) {
	    sprintf(buf, "%0*X ", owidth - 1, (int)(offset & mask));
	    tty_setcolor(MARKED_COLOR);
	    tty_print_nstring(buf, owidth);
	}

	for (i = owidth, k = 0; k < nbytes; k++, i += 3) {
	    ch = *data++;
	    if (diffs[k] & (1 << ord)) {
		stop = 0;
		sprintf(buf + i, "%02X ", ch & 0xFF);
		ch = convert_to_display_c(ch & 0xFF);
		if (!is_printable(ch)) {
		    ch = '.';
		}
	    } else {
		buf[i + 0] = ' ';
		buf[i + 1] = ' ';
		buf[i + 2] = ' ';
		buf[i + 3] = '\0';
		ch = ' ';
	    }
	    buf[owidth + 3 * nbytes + 1 + k] = ch;
	    if (diffs[k] & XDIFF_DIFFERENT) {
		tty_setcolor(VIEW_UNDERLINED_COLOR);
	    } else {
		tty_setcolor(NORMAL_COLOR);
	    }
	    tty_print_nstring(buf + i, (i + 3 < width) ? 3 : (width - i));
	}

	tty_setcolor(NORMAL_COLOR);
	if (i < width) {
	    buf[i] = ' ';
	    tty_print_char(buf[i]);
	    i++;
	}

	for (k = 0; k < nbytes && i < width; k++, i++) {
	    if (diffs[k] & XDIFF_DIFFERENT) {
		tty_setcolor(VIEW_UNDERLINED_COLOR);
	    } else {
		tty_setcolor(NORMAL_COLOR);
	    }
	    tty_print_char(buf[i] & 0xFF);
	}

	tty_setcolor(NORMAL_COLOR);
	for (; i < width; i++) {
	    buf[i] = ' ';
	    tty_print_char(buf[i]);
	}

	buf[width] = '\0';	/* XXX we fully construct the buffer, but don't necessarily have to */

	offset += nbytes;
	if (stop) {
	    break;
	}
	diffs += nbytes;
    }
#if !VERTICAL_SPLIT
    height = total_height;
#endif	/* !VERTICAL_SPLIT */
    if (j < height) {
	memset(buf, ' ', width);
	buf[width] = '\0';
	for (; j < height; j++) {
	    tty_gotoyx(r + j, c);
	    tty_print_nstring(buf, width);
	}
    }

    return 0;
}


static void
view_status (WDiff *view, int ord, int width, int pos)
{
    char buf[BUFSIZ];
    int filename_width;
    off_t skip_offs = view->df[ord].offs;
    int moves = view->df[ord].move;

    tty_setcolor(SELECTED_COLOR);

#if VERTICAL_SPLIT
    tty_gotoyx(0, pos);
#else	/* !VERTICAL_SPLIT */
    tty_gotoyx(pos, 0);
#endif	/* !VERTICAL_SPLIT */

    filename_width = width - 22;
    if (filename_width < 8) {
	filename_width = 8;
    }
    if (filename_width >= (int)sizeof(buf)) {
	/* abnormal, but avoid buffer overflow */
	filename_width = sizeof(buf) - 1;
    }
    trim(strip_home_and_password(view->file[ord]), buf, filename_width);
    tty_printf("%-*s   %s%-16lX ", filename_width, buf, moves ? "0x" : "--", skip_offs);
}


static void
view_update (WDiff *view)
{
    int size = view->size;
    int min_size;
    int size1;
    int size2;
    int flags = 0;
    int owidth = 0;

    if (view->df[0].offs >= view->max) {
	view->df[0].offs = view->max;
    }
    if (view->df[1].offs >= view->max) {
	view->df[1].offs = view->max;
    }
    if (view->df[0].offs < 0) {
	view->df[0].offs = 0;
    }
    if (view->df[1].offs < 0) {
	view->df[1].offs = 0;
    }

    /* XXX some more sanity checks (LINES/COLS)? */
#if VERTICAL_SPLIT
    if (size < 2) {
	return;
    }

    size1 = view->half1 + view->bias;
    size2 = view->half2 - view->bias;
    if (view->full) {
	size1 = COLS;
	size2 = 0;
    }

    if (view->display_numbers) {
	owidth = 4;
    }
#else	/* !VERTICAL_SPLIT */

    size1 = view->half1 + view->bias;
    size2 = view->half2 - view->bias;
    if (view->full) {
	size1 = LINES - 2;
	size2 = 0;
    }

    min_size = size1;
    if (size2 && size2 < size1) {
	min_size = size2;
    }

    if (view->display_numbers) {
	owidth = 4;
	if (size >= 8 + 16 * 4 + 2) {
	    owidth = 8;
	}
    }
#endif	/* !VERTICAL_SPLIT */

    if (view->new_frame) {
	Dlg_head *h = view->widget.parent;

#if VERTICAL_SPLIT
	min_size = size1;
	if (size2 && size2 < size1) {
	    min_size = size2;
	}
	view->nbytes = min_size - 2 - 1;
#else	/* !VERTICAL_SPLIT */
	view->nbytes = size - 1;
#endif	/* !VERTICAL_SPLIT */
	if (owidth) {
	    if (view->nbytes - 4 >= owidth + 1) {
		view->nbytes -= owidth + 1;
	    } else if (view->nbytes - 4 > 1) {
		view->nbytes = 4;
	    }
	}
	if (view->nbytes < 0) {
	    view->nbytes = 0;	/* XXX sanity checks should prevent this */
	}
	if (view->nbytes > 0 && view->nbytes < 4) {
	    view->nbytes = 4;
	}
	view->nbytes /= 4;
	if (view->nbytes <= view->subtract) {
	    view->subtract = view->nbytes - 1;
	    if (view->subtract < 0) {
		view->subtract = 0;
	    }
	}
	view->nbytes -= view->subtract;
#if VERTICAL_SPLIT
	view->pbytes = view->nbytes * (size - 2);
#else	/* !VERTICAL_SPLIT */
	view->pbytes = view->nbytes * min_size;
#endif	/* !VERTICAL_SPLIT */
	flags = REINIT_REALLOC;

	tty_setcolor(NORMAL_COLOR);
#if VERTICAL_SPLIT
	if (size1 > 1) {
	    draw_double_box(h, 1, 0,     size, size1);
	}
	if (size2 > 1) {
	    draw_double_box(h, 1, size1, size, size2);
	}
#endif	/* VERTICAL_SPLIT */

	view->new_frame = 0;
    }

    if (view->df[0].last != view->df[0].offs || view->df[1].last != view->df[1].offs || flags) {
	if (view->df[0].last != view->df[0].offs) {
	    flags |= REINIT_READ_LEFT;
	}
	if (view->df[1].last != view->df[1].offs) {
	    flags |= REINIT_READ_RIGHT;
	}
	if (redo_diff(view, flags) < 0) {
	    view->view_quit = 1;
	    return;
	}
	view->df[0].last = view->df[0].offs;
	view->df[1].last = view->df[1].offs;
    }

#if VERTICAL_SPLIT
    if (size1 > 2) {
	view_status(view, view->ord,     size1, 0);
	view_display_file(view, view->ord,     2,         1,         size - 2, size1 - 2,        owidth);
    }
    if (size2 > 2) {
	view_status(view, view->ord ^ 1, size2, size1);
	view_display_file(view, view->ord ^ 1, 2,         size1 + 1, size - 2, size2 - 2,        owidth);
    }
#else	/* !VERTICAL_SPLIT */
    if (size1 > 0) {
	view_status(view, view->ord,     size, 0);
	view_display_file(view, view->ord,     1,         0,         min_size, size,      size1, owidth);
    }
    if (size2 > 0) {
	view_status(view, view->ord ^ 1, size, size1 + 1);
	view_display_file(view, view->ord ^ 1, size1 + 2, 0,         min_size, size,      size2, owidth);
    }
#endif	/* !VERTICAL_SPLIT */
}


static void
view_redo (WDiff *view)
{
    int rv;
    if (quick_dialog(&diffopt) != B_CANCEL) {
	view_fini(view, 0);
	mc_setctl (view->file[0], VFS_SETCTL_STALE_DATA, NULL);
	mc_setctl (view->file[1], VFS_SETCTL_STALE_DATA, NULL);
	rv = redo_diff(view, REINIT_OPEN | REINIT_READ_LEFT | REINIT_READ_RIGHT);
	if (rv < 0) {
	    view->view_quit = 1;
	}
    }
}


static void
view_search (WDiff *view, int again)
{
    if (again < 0) {
	return;
    }

    /* XXX */
    error_dialog(_("Search"), _(" Search not yet implemented "));
}


static void
view_search_cmd (WDiff *view)
{
    view_search(view, 0);
}


static void
view_edit (WDiff *view, int ord)
{
    /* XXX */
    error_dialog(_("Edit"), _(" Edit not yet implemented "));
}


static void
view_edit_cmd (WDiff *view)
{
    view_edit(view, view->ord);
}


static void
view_goto_cmd (WDiff *view)
{
    static char prev[256];
    /* XXX some statics here, to be remembered between runs */

    off_t address;
    char *input;

    input = input_dialog(_(" Goto Address "), _(" Enter Address: "), prev);
    if (input != NULL) {
	const char *s = input;
	if (scan_unsigned(&s, &address) == 0 && *s == '\0') {
	    struct display_file *lf = &view->df[view->ord];
	    struct display_file *rf = &view->df[view->ord ^ 1];
	    recalc_offset_pos(view, lf, rf, address);
	    view->last_found = -1;
	    view_update(view);
	}
	g_free(input);
    }
}


static void
view_help_cmd (void)
{
    interactive_display(NULL, "[Binary Diff Viewer]");
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

    buttonbar_set_label_data(h, 4, Q_("ButtonBar|Edit"), (buttonbarfn)view_edit_cmd, view);
    buttonbar_set_label_data(h, 5, Q_("ButtonBar|Goto"), (buttonbarfn)view_goto_cmd, view);
    buttonbar_set_label_data(h, 7, Q_("ButtonBar|Search"), (buttonbarfn)view_search_cmd, view);
    buttonbar_set_label_data(h, 10, Q_("ButtonBar|Quit"), (buttonbarfn)view_quit_cmd, view);
}


static int
view_event (Gpm_Event *event, void *x)
{
    WDiff *view = (WDiff *)x;
    int result = MOU_NORMAL;
    struct display_file *lf = &view->df[view->ord];
    struct display_file *rf = &view->df[view->ord ^ 1];

    /* We are not interested in the release events */
    if (!(event->type & (GPM_DOWN | GPM_DRAG))) {
	return result;
    }

    /* Wheel events */
    if ((event->buttons & GPM_B_UP) && (event->type & GPM_DOWN)) {
	recalc_offset_rel(view, lf, rf, -view->nbytes);
	view_update(view);
	return result;
    }
    if ((event->buttons & GPM_B_DOWN) && (event->type & GPM_DOWN)) {
	recalc_offset_rel(view, lf, rf, view->nbytes);
	view_update(view);
	return result;
    }

    return result;
}


static cb_ret_t
view_handle_key (WDiff *view, int c)
{
    struct display_file *lf = &view->df[view->ord];
    struct display_file *rf = &view->df[view->ord ^ 1];

    c = convert_from_input_c(c);

    switch (c) {
	case 'l':
	    view->display_numbers ^= 1;
	    view->new_frame = 1;
	    return MSG_HANDLED;

	case 'f':
	    view->full ^= 1;
	    view->new_frame = 1;
	    return MSG_HANDLED;

	case '=': /* XXX testing only */
	    if (!view->full) {
		view->bias = 0;
		view->new_frame = 1;
	    }
	    return MSG_HANDLED;

	case '>': /* XXX testing only */
	    if (!view->full) {
		view_compute_split(view, 1);
		view->new_frame = 1;
	    }
	    return MSG_HANDLED;

	case '<': /* XXX testing only */
	    if (!view->full) {
		view_compute_split(view, -1);
		view->new_frame = 1;
	    }
	    return MSG_HANDLED;

	case '+':
	    if (view->subtract) {
		view->subtract--;
		view->new_frame = 1;
	    }
	    return MSG_HANDLED;
	case '-':
	    view->subtract++;
	    view->new_frame = 1;
	    return MSG_HANDLED;

	case '1':
	    lf->move = 1;
	    rf->move ^= 1;
	    return MSG_HANDLED;
	case '2':
	    lf->move ^= 1;
	    rf->move = 1;
	    return MSG_HANDLED;

	case XCTRL('u'): {
	    view->ord ^= 1;
	    return MSG_HANDLED;
	}

	case XCTRL('r'):
	    view_redo(view);
	    return MSG_HANDLED;

	case 'n':
	    find_next_hunk(view);
	    return MSG_HANDLED;

	case 'p':
	    find_prev_hunk(view);
	    return MSG_HANDLED;

	case KEY_BACKSPACE:
	case KEY_DC:
	    view->last_found = -1;
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
	    recalc_offset_end(view, lf, rf, 0);
	    return MSG_HANDLED;

	case KEY_END:
	case ALT ('>'):
	case KEY_M_CTRL | KEY_NPAGE:
	    view->last_found = -1;
	    recalc_offset_end(view, lf, rf, 1);
	    return MSG_HANDLED;

	case KEY_UP:
	    recalc_offset_rel(view, lf, rf, -view->nbytes);
	    return MSG_HANDLED;

	case KEY_DOWN:
	    recalc_offset_rel(view, lf, rf, view->nbytes);
	    return MSG_HANDLED;

	case KEY_NPAGE:
	    recalc_offset_rel(view, lf, rf, view->pbytes);
	    return MSG_HANDLED;

	case KEY_PPAGE:
	    recalc_offset_rel(view, lf, rf, -view->pbytes);
	    return MSG_HANDLED;

	case KEY_LEFT:
	    recalc_offset_rel(view, lf, rf, -1);
	    return MSG_HANDLED;

	case KEY_RIGHT:
	    recalc_offset_rel(view, lf, rf, 1);
	    return MSG_HANDLED;

	case KEY_M_CTRL | KEY_LEFT:
	    recalc_offset_rel(view, lf, rf, -16);
	    return MSG_HANDLED;

	case KEY_M_CTRL | KEY_RIGHT:
	    recalc_offset_rel(view, lf, rf, 16);
	    return MSG_HANDLED;

	case XCTRL('o'):
	    view_other_cmd();
	    return MSG_HANDLED;

	case 't':
	    diff_view(view->file[0], view->file[1]);
	    return MSG_HANDLED;

	case 'q':
	case ESC_CHAR:
	    view->view_quit = 1;
	    return MSG_HANDLED;

	case '\n':
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
xdiff_view (const char *file1, const char *file2)
{
    int error;
    WDiff *view;
    WButtonBar *bar;
    Dlg_head *view_dlg;

    /* Create dialog and widgets, put them on the dialog */
    view_dlg =
	create_dlg(0, 0, LINES, COLS, NULL, view_dialog_callback,
		   "[Binary Diff Viewer]", NULL, DLG_WANT_TAB);

    view = g_new0(WDiff, 1);

    init_widget(&view->widget, 0, 0, LINES - 1, COLS,
		(callback_fn)view_callback,
		(mouse_h)view_event);

    widget_want_cursor(view->widget, 0);

    bar = buttonbar_new(1);

    add_widget(view_dlg, bar);
    add_widget(view_dlg, view);

    error = view_init(view, file1, file2);

    /* Please note that if you add another widget,
     * you have to modify view_adjust_size to
     * be aware of it
     */
    if (!error) {
	run_dlg(view_dlg);
	view_search(view, -1);
	view_fini(view, 1);
    }
    destroy_dlg(view_dlg);

    return error;
}

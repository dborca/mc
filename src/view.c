/*
   Internal file viewer for the Midnight Commander

   Copyright (C) 1994, 1995, 1996 The Free Software Foundation

   Written by: 1994, 1995, 1998 Miguel de Icaza
	       1994, 1995 Janne Kukonlehto
	       1995 Jakub Jelinek
	       1996 Joseph M. Hinkle
	       1997 Norbert Warmuth
	       1998 Pavel Machek
	       2004 Roland Illig <roland.illig@gmx.de>
	       2005 Roland Illig <roland.illig@gmx.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "global.h"
#include "tty.h"
#include "cmd.h"		/* For view_other_cmd */
#include "dialog.h"		/* Needed by widget.h */
#include "widget.h"		/* Needed for buttonbar_new */
#include "color.h"
#include "mouse.h"
#include "help.h"
#include "key.h"		/* For mi_getch() */
#include "layout.h"
#include "setup.h"
#include "wtools.h"		/* For query_set_sel() */
#include "dir.h"
#include "panel.h"		/* Needed for current_panel and other_panel */
#include "win.h"
#include "execute.h"
#include "main.h"		/* slow_terminal */
#include "view.h"

#include "charsets.h"
#include "selcodepage.h"

/* Block size for reading files in parts */
#define VIEW_PAGE_SIZE		((size_t) 8192)
#define VIEW_COORD_CACHE_GRANUL	1024

typedef unsigned char byte;

/* Offset in bytes into a file */
typedef unsigned long offset_type;
#define INVALID_OFFSET ((offset_type) -1)
#define OFFSETTYPE_MAX (~((offset_type) 0))
#define OFFSETTYPE_PRIX		"lX"
#define OFFSETTYPE_PRId		"lu"

/* A width or height on the screen */
typedef unsigned int screen_dimen;

/* A cache entry for mapping offsets into line/column pairs and vice versa.
 * cc_offset, cc_line, and cc_column are the 0-based values of the offset,
 * line and column of that cache entry. cc_nroff_column is the column
 * corresponding to cc_offset in nroff mode.
 */
struct coord_cache_entry {
    offset_type cc_offset;
    offset_type cc_line;
    offset_type cc_column;
    offset_type cc_nroff_column;
};

/* A node for building a change list on change_list */
struct hexedit_change_node {
   struct hexedit_change_node *next;
   offset_type                offset;
   byte                       value;
};

/* data sources of the view */
enum view_ds {
    DS_NONE,			/* No data available */
    DS_STDIO_PIPE,		/* Data comes from a pipe using popen/pclose */
    DS_VFS_PIPE,		/* Data comes from a piped-in VFS file */
    DS_FILE,			/* Data comes from a VFS file */
    DS_STRING			/* Data comes from a string in memory */
};

struct area {
    screen_dimen top, left;
    screen_dimen height, width;
};

struct WView {
    Widget widget;

    char *filename;		/* Name of the file */
    char *command;		/* Command used to pipe data in */

    enum view_ds datasource;	/* Where the displayed data comes from */

    /* stdio pipe data source */
    FILE  *ds_stdio_pipe;	/* Output of a shell command */

    /* vfs pipe data source */
    int    ds_vfs_pipe;		/* Non-seekable vfs file descriptor */

    /* vfs file data source */
    int    ds_file_fd;		/* File with random access */
    off_t  ds_file_filesize;	/* Size of the file */
    off_t  ds_file_offset;	/* Offset of the currently loaded data */
    byte  *ds_file_data;	/* Currently loaded data */
    size_t ds_file_datalen;	/* Number of valid bytes in file_data */
    size_t ds_file_datasize;	/* Number of allocated bytes in file_data */

    /* string data source */
    byte  *ds_string_data;	/* The characters of the string */
    size_t ds_string_len;	/* The length of the string */

    /* Growing buffers information */
    gboolean growbuf_in_use;	/* Use the growing buffers? */
    byte   **growbuf_blockptr;	/* Pointer to the block pointers */
    size_t   growbuf_blocks;	/* The number of blocks in *block_ptr */
    size_t   growbuf_lastindex;	/* Number of bytes in the last page of the
				   growing buffer */
    gboolean growbuf_finished;	/* TRUE when all data has been read. */

    /* Editor modes */
    gboolean hex_mode;		/* Hexview or Hexedit */
    gboolean hexedit_mode;	/* Hexedit */
    gboolean hexview_in_text;	/* Is the hexview cursor in the text area? */
    gboolean text_nroff_mode;	/* Nroff-style highlighting */
    gboolean text_wrap_mode;	/* Wrap text lines to fit them on the screen */
    gboolean magic_mode;	/* Preprocess the file using external programs */

    /* Additional editor state */
    gboolean hexedit_lownibble;	/* Are we editing the last significant nibble? */
    GArray *coord_cache;	/* Cache for mapping offsets to cursor positions */

    /* Display information */
    screen_dimen dpy_frame_size;/* Size of the frame surrounding the real viewer */
    offset_type dpy_start;	/* Offset of the displayed data */
    offset_type dpy_end;	/* Offset after the displayed data */
    offset_type dpy_text_column;/* Number of skipped columns in non-wrap
				 * text mode */
    offset_type hex_cursor;	/* Hexview cursor position in file */
    screen_dimen cursor_col;	/* Cursor column */
    screen_dimen cursor_row;	/* Cursor row */
    struct hexedit_change_node *change_list;   /* Linked list of changes */
    struct area status_area;	/* Where the status line is displayed */
    struct area ruler_area;	/* Where the ruler is displayed */
    struct area data_area;	/* Where the data is displayed */

    int dirty;			/* Number of skipped updates */
    gboolean dpy_bbar_dirty;	/* Does the button bar need to be updated? */

    /* Mode variables */
    int bytes_per_line;		/* Number of bytes per line in hex mode */

    /* Search variables */
    offset_type search_start;	/* First character to start searching from */
    offset_type search_length;	/* Length of found string or 0 if none was found */
    char *search_exp;		/* The search expression */
    int  direction;		/* 1= forward; -1 backward */
    void (*last_search)(WView *);
				/* Pointer to the last search command */
    gboolean want_to_quit;	/* Prepare for cleanup ... */

    /* Markers */
    int marker;			/* mark to use */
    offset_type marks [10];	/* 10 marks: 0..9 */

    int  move_dir;		/* return value from widget:
				 * 0 do nothing
				 * -1 view previous file
				 * 1 view next file
				 */

    offset_type update_steps;	/* The number of bytes between percent
				 * increments */
    offset_type update_activate;/* Last point where we updated the status */
};


/* {{{ Global Variables }}} */

/* Maxlimit for skipping updates */
int max_dirt_limit = 10;

/* If set, show a ruler */
static enum ruler_type {
    RULER_NONE,
    RULER_TOP,
    RULER_BOTTOM
} ruler = RULER_NONE;

/* Scrolling is done in pages or line increments */
int mouse_move_pages_viewer = 1;

/* wrap mode default */
int global_wrap_mode = 1;

int default_hex_mode = 0;
int default_magic_flag = 1;
int default_nroff_flag = 1;
int altered_hex_mode = 0;
int altered_magic_flag = 0;
int altered_nroff_flag = 0;

static const char hex_char[] = "0123456789ABCDEF";

int mcview_remember_file_position = FALSE;

/* {{{ Function Prototypes }}} */

/* Our widget callback */
static cb_ret_t view_callback (Widget *, widget_msg_t, int);

static int regexp_view_search (WView * view, char *pattern, char *string,
			       int match_type);
static void view_labels (WView * view);

static void view_init_growbuf (WView *);
static void view_place_cursor (WView *view);
static void display (WView *);
static void view_done (WView *);

/* {{{ Helper Functions }}} */

/* difference or zero */
static inline screen_dimen
dimen_doz (screen_dimen a, screen_dimen b)
{
	return (a >= b) ? a - b : 0;
}

static inline screen_dimen
dimen_min (screen_dimen a, screen_dimen b)
{
    return (a < b) ? a : b;
}

static inline offset_type
offset_doz (offset_type a, offset_type b)
{
	return (a >= b) ? a - b : 0;
}

static inline offset_type
offset_rounddown (offset_type a, offset_type b)
{
	assert (b != 0);
	return a - a % b;
}

/* {{{ Simple Primitive Functions for WView }}} */

static inline gboolean
view_is_in_panel (WView *view)
{
    return (view->dpy_frame_size != 0);
}

static void
view_compute_areas (WView *view)
{
    struct area view_area;
    screen_dimen height, rest, y;

    /* The viewer is surrounded by a frame of size view->dpy_frame_size.
     * Inside that frame, there are: The status line (at the top),
     * the data area and an optional ruler, which is shown above or
     * below the data area. */

    view_area.top = view->dpy_frame_size;
    view_area.left = view->dpy_frame_size;
    view_area.height = dimen_doz(view->widget.lines, 2 * view->dpy_frame_size);
    view_area.width = dimen_doz(view->widget.cols, 2 * view->dpy_frame_size);

    /* Most coordinates of the areas equal those of the whole viewer */
    view->status_area = view_area;
    view->ruler_area = view_area;
    view->data_area = view_area;

    /* Compute the heights of the areas */
    rest = view_area.height;

    height = dimen_min(rest, 1);
    view->status_area.height = height;
    rest -= height;

    height = dimen_min(rest, (ruler == RULER_NONE || view->hex_mode) ? 0 : 2);
    view->ruler_area.height = height;
    rest -= height;

    view->data_area.height = rest;

    /* Compute the position of the areas */
    y = view_area.top;

    view->status_area.top = y;
    y += view->status_area.height;

    if (ruler == RULER_TOP) {
	view->ruler_area.top = y;
	y += view->ruler_area.height;
    }

    view->data_area.top = y;
    y += view->data_area.height;

    if (ruler == RULER_BOTTOM) {
	view->ruler_area.top = y;
	y += view->ruler_area.height;
    }
}

static void
view_hexedit_free_change_list (WView *view)
{
    struct hexedit_change_node *curr, *next;

    for (curr = view->change_list; curr != NULL; curr = next) {
	next = curr->next;
	g_free (curr);
    }
    view->change_list = NULL;
    view->dirty++;
}

/* {{{ Growing buffer }}} */

static void
view_init_growbuf (WView *view)
{
    view->growbuf_in_use    = TRUE;
    view->growbuf_blockptr  = NULL;
    view->growbuf_blocks    = 0;
    view->growbuf_lastindex = VIEW_PAGE_SIZE;
    view->growbuf_finished  = FALSE;
}

static void
view_growbuf_free (WView *view)
{
    size_t i;

    assert (view->growbuf_in_use);

    for (i = 0; i < view->growbuf_blocks; i++)
	g_free (view->growbuf_blockptr[i]);
    g_free (view->growbuf_blockptr);
    view->growbuf_blockptr = NULL;
    view->growbuf_in_use = FALSE;
}

static offset_type
view_growbuf_filesize (WView *view)
{
    assert(view->growbuf_in_use);

    if (view->growbuf_blocks == 0)
	return 0;
    else
	return ((offset_type) view->growbuf_blocks - 1) * VIEW_PAGE_SIZE
	       + view->growbuf_lastindex;
}

/* Copies the output from the pipe to the growing buffer, until either
 * the end-of-pipe is reached or the interval [0..ofs) of the growing
 * buffer is completely filled. */
static void
view_growbuf_read_until (WView *view, offset_type ofs)
{
    ssize_t nread;
    byte *p;
    size_t bytesfree;
    gboolean short_read;

    assert (view->growbuf_in_use);

    if (view->growbuf_finished)
	return;

    short_read = FALSE;
    while (view_growbuf_filesize (view) < ofs || short_read) {
	if (view->growbuf_lastindex == VIEW_PAGE_SIZE) {
	    /* Append a new block to the growing buffer */
	    byte *newblock = g_try_malloc (VIEW_PAGE_SIZE);
	    byte **newblocks = g_try_malloc (sizeof (*newblocks) * (view->growbuf_blocks + 1));
	    if (!newblock || !newblocks) {
		g_free (newblock);
		g_free (newblocks);
		return;
	    }
	    memcpy (newblocks, view->growbuf_blockptr, sizeof (*newblocks) * view->growbuf_blocks);
	    g_free (view->growbuf_blockptr);
	    view->growbuf_blockptr = newblocks;
	    view->growbuf_blockptr[view->growbuf_blocks++] = newblock;
	    view->growbuf_lastindex = 0;
	}
	p = view->growbuf_blockptr[view->growbuf_blocks - 1] + view->growbuf_lastindex;
	bytesfree = VIEW_PAGE_SIZE - view->growbuf_lastindex;

	if (view->datasource == DS_STDIO_PIPE) {
	    nread = fread (p, 1, bytesfree, view->ds_stdio_pipe);
	    if (nread == 0) {
		view->growbuf_finished = TRUE;
		(void) pclose (view->ds_stdio_pipe);
		display (view);
		close_error_pipe (0, NULL);
		view->ds_stdio_pipe = NULL;
		return;
	    }
	} else {
	    assert (view->datasource == DS_VFS_PIPE);
	    do {
		nread = mc_read (view->ds_vfs_pipe, p, bytesfree);
	    } while (nread == -1 && errno == EINTR);
	    if (nread == -1 || nread == 0) {
		view->growbuf_finished = TRUE;
		(void) mc_close (view->ds_vfs_pipe);
		view->ds_vfs_pipe = -1;
		return;
	    }
	}
	short_read = ((size_t)nread < bytesfree);
	view->growbuf_lastindex += nread;
    }
}

static int
get_byte_growing_buffer (WView *view, offset_type byte_index)
{
    offset_type pageno    = byte_index / VIEW_PAGE_SIZE;
    offset_type pageindex = byte_index % VIEW_PAGE_SIZE;

    assert (view->growbuf_in_use);

    if ((size_t) pageno != pageno)
	return -1;

    view_growbuf_read_until (view, byte_index + 1);
    if (view->growbuf_blocks == 0)
	return -1;
    if (pageno < view->growbuf_blocks - 1)
	return view->growbuf_blockptr[pageno][pageindex];
    if (pageno == view->growbuf_blocks - 1 && pageindex < view->growbuf_lastindex)
	return view->growbuf_blockptr[pageno][pageindex];
    return -1;
}

/* {{{ Data sources }}} */

/*
    The data source provides the viewer with data from either a file, a
    string or the output of a command. The get_byte() function can be
    used to get the value of a byte at a specific offset. If the offset
    is out of range, -1 is returned. The function get_byte_indexed(a,b)
    returns the byte at the offset a+b, or -1 if a+b is out of range.

    The view_set_byte() function has the effect that later calls to
    get_byte() will return the specified byte for this offset. This
    function is designed only for use by the hexedit component after
    saving its changes. Inspect the source before you want to use it for
    other purposes.

    The view_get_filesize() function returns the current size of the
    data source. If the growing buffer is used, this size may increase
    later on. Use the view_may_still_grow() function when you want to
    know if the size can change later.
 */

static offset_type
view_get_filesize (WView *view)
{
    switch (view->datasource) {
	case DS_NONE:
	    return 0;
	case DS_STDIO_PIPE:
	case DS_VFS_PIPE:
	    return view_growbuf_filesize (view);
	case DS_FILE:
	    return view->ds_file_filesize;
	case DS_STRING:
	    return view->ds_string_len;
	default:
	    assert(!"Unknown datasource type");
	    return 0;
    }
}

static inline gboolean
view_may_still_grow (WView *view)
{
    return (view->growbuf_in_use && !view->growbuf_finished);
}

/* returns TRUE if the idx lies in the half-open interval
 * [offset; offset + size), FALSE otherwise.
 */
static inline gboolean
already_loaded (offset_type offset, offset_type idx, size_t size)
{
    return (offset <= idx && idx - offset < size);
}

static inline void
view_file_load_data (WView *view, offset_type byte_index)
{
    offset_type blockoffset;
    ssize_t res;
    size_t bytes_read;

    assert (view->datasource == DS_FILE);

    if (already_loaded (view->ds_file_offset, byte_index, view->ds_file_datalen))
	return;

    if (byte_index >= view->ds_file_filesize)
	return;

    blockoffset = offset_rounddown (byte_index, view->ds_file_datasize);
    if (mc_lseek (view->ds_file_fd, blockoffset, SEEK_SET) == -1)
	goto error;

    bytes_read = 0;
    while (bytes_read < view->ds_file_datasize) {
	res = mc_read (view->ds_file_fd, view->ds_file_data + bytes_read, view->ds_file_datasize - bytes_read);
	if (res == -1)
	    goto error;
	if (res == 0)
	    break;
	bytes_read += (size_t) res;
    }
    view->ds_file_offset  = blockoffset;
    if (bytes_read > view->ds_file_filesize - view->ds_file_offset) {
	/* the file has grown in the meantime -- stick to the old size */
	view->ds_file_datalen = view->ds_file_filesize - view->ds_file_offset;
    } else {
	view->ds_file_datalen = bytes_read;
    }
    return;

error:
    view->ds_file_datalen = 0;
}

static int
get_byte_none (WView *view, offset_type byte_index)
{
    assert (view->datasource == DS_NONE);
    (void) &view;
    (void) byte_index;
    return -1;
}

static inline int
get_byte_file (WView *view, offset_type byte_index)
{
    assert (view->datasource == DS_FILE);

    view_file_load_data (view, byte_index);
    if (already_loaded(view->ds_file_offset, byte_index, view->ds_file_datalen))
	return view->ds_file_data[byte_index - view->ds_file_offset];
    return -1;
}

static int
get_byte_string (WView *view, offset_type byte_index)
{
    assert (view->datasource == DS_STRING);
    if (byte_index < view->ds_string_len)
	return view->ds_string_data[byte_index];
    return -1;
}

static inline int
get_byte (WView *view, offset_type offset)
{
    switch (view->datasource) {
	case DS_STDIO_PIPE:
	case DS_VFS_PIPE:
	    return get_byte_growing_buffer (view, offset);
	case DS_FILE:
	    return get_byte_file (view, offset);
	case DS_STRING:
	    return get_byte_string (view, offset);
	case DS_NONE:
	    return get_byte_none (view, offset);
    }
    assert(!"Unknown datasource type");
    return -1;
}

static inline int
get_byte_indexed (WView *view, offset_type base, offset_type ofs)
{
    if (base <= OFFSETTYPE_MAX - ofs)
	return get_byte (view, base + ofs);
    return -1;
}

static void
view_set_byte (WView *view, offset_type offset, byte b)
{
    (void) &b;
    assert (offset < view_get_filesize (view));
    assert (view->datasource == DS_FILE);
    view->ds_file_datalen = 0; /* just force reloading */
}

static void
view_set_datasource_none (WView *view)
{
    view->datasource = DS_NONE;
}

static void
view_set_datasource_vfs_pipe (WView *view, int fd)
{
    assert (fd != -1);
    view->datasource = DS_VFS_PIPE;
    view->ds_vfs_pipe = fd;

    view_init_growbuf (view);
}

static void
view_set_datasource_stdio_pipe (WView *view, FILE *fp)
{
    assert (fp != NULL);
    view->datasource = DS_STDIO_PIPE;
    view->ds_stdio_pipe = fp;

    view_init_growbuf (view);
}

static void
view_set_datasource_string (WView *view, const char *s)
{
    view->datasource = DS_STRING;
    view->ds_string_data = (byte *) g_strdup (s);
    view->ds_string_len  = strlen (s);
}

static void
view_set_datasource_file (WView *view, int fd, const struct stat *st)
{
    view->datasource = DS_FILE;
    view->ds_file_fd = fd;
    view->ds_file_filesize = st->st_size;
    view->ds_file_offset = 0;
    view->ds_file_data = g_malloc (4096);
    view->ds_file_datalen = 0;
    view->ds_file_datasize = 4096;
}

static void
view_close_datasource (WView *view)
{
    switch (view->datasource) {
	case DS_NONE:
	    break;
	case DS_STDIO_PIPE:
	    if (view->ds_stdio_pipe != NULL) {
		(void) pclose (view->ds_stdio_pipe);
		display (view);
		close_error_pipe (0, NULL);
		view->ds_stdio_pipe = NULL;
	    }
	    view_growbuf_free (view);
	    break;
	case DS_VFS_PIPE:
	    if (view->ds_vfs_pipe != -1) {
		(void) mc_close (view->ds_vfs_pipe);
		view->ds_vfs_pipe = -1;
	    }
	    view_growbuf_free (view);
	    break;
	case DS_FILE:
	    (void) mc_close (view->ds_file_fd);
	    view->ds_file_fd = -1;
	    g_free (view->ds_file_data);
	    view->ds_file_data = NULL;
	    break;
	case DS_STRING:
	    g_free (view->ds_string_data);
	    view->ds_string_data = NULL;
	    break;
	default:
	    assert (!"Unknown datasource type");
    }
    view->datasource = DS_NONE;
}

/* {{{ The Coordinate Cache }}} */

/*
   This cache provides you with a fast lookup to map file offsets into
   line/column pairs and vice versa. The interface to the mapping is
   provided by the functions view_coord_to_offset() and
   view_offset_to_coord().

   The cache is implemented as a simple sorted array holding entries
   that map some of the offsets to their line/column pair. Entries that
   are not cached themselves are interpolated (exactly) from their
   neighbor entries. The algorithm used for determining the line/column
   for a specific offset needs to be kept synchronized with the one used
   in display().
*/

enum ccache_type {
    CCACHE_OFFSET,
    CCACHE_LINECOL
};

static inline gboolean
coord_cache_entry_less (const struct coord_cache_entry *a,
	const struct coord_cache_entry *b, enum ccache_type crit,
	gboolean nroff_mode)
{
    if (crit == CCACHE_OFFSET)
	return (a->cc_offset < b->cc_offset);

    if (a->cc_line < b->cc_line)
	return TRUE;

    if (a->cc_line == b->cc_line) {
	if (nroff_mode) {
	    return (a->cc_nroff_column < b->cc_nroff_column);
	} else {
	    return (a->cc_column < b->cc_column);
	}
    }
    return FALSE;
}

#ifdef MC_ENABLE_DEBUGGING_CODE
static void view_coord_to_offset (WView *, offset_type *, offset_type, offset_type);
static void view_offset_to_coord (WView *, offset_type *, offset_type *, offset_type);

static void
view_ccache_dump (WView *view)
{
    FILE *f;
    offset_type offset, line, column, nextline_offset, filesize;
    guint i;
    const struct coord_cache_entry *cache;

    assert (view->coord_cache != NULL);

    filesize = view_get_filesize (view);
    cache = &(g_array_index (view->coord_cache, struct coord_cache_entry, 0));

    f = fopen("mcview-ccache.out", "w");
    if (f == NULL)
	return;
    (void)setvbuf(f, NULL, _IONBF, 0);

    /* cache entries */
    for (i = 0; i < view->coord_cache->len; i++) {
	(void) fprintf (f,
	    "entry %8u  "
	    "offset %8"OFFSETTYPE_PRId"  "
	    "line %8"OFFSETTYPE_PRId"  "
	    "column %8"OFFSETTYPE_PRId"  "
	    "nroff_column %8"OFFSETTYPE_PRId"\n",
	    (unsigned int) i, cache[i].cc_offset, cache[i].cc_line,
	    cache[i].cc_column, cache[i].cc_nroff_column);
    }
    (void)fprintf (f, "\n");

    /* offset -> line/column translation */
    for (offset = 0; offset < filesize; offset++) {
	view_offset_to_coord (view, &line, &column, offset);
	(void)fprintf (f,
	    "offset %8"OFFSETTYPE_PRId"  "
	    "line %8"OFFSETTYPE_PRId"  "
	    "column %8"OFFSETTYPE_PRId"\n",
	    offset, line, column);
    }

    /* line/column -> offset translation */
    for (line = 0; TRUE; line++) {
	view_coord_to_offset (view, &nextline_offset, line + 1, 0);
	(void)fprintf (f, "nextline_offset %8"OFFSETTYPE_PRId"\n",
	    nextline_offset);

	for (column = 0; TRUE; column++) {
	    view_coord_to_offset (view, &offset, line, column);
	    if (offset >= nextline_offset)
		break;

	    (void)fprintf (f, "line %8"OFFSETTYPE_PRId"  column %8"OFFSETTYPE_PRId"  offset %8"OFFSETTYPE_PRId"\n",
		line, column, offset);
	}

	if (nextline_offset >= filesize - 1)
	    break;
    }

    (void)fclose (f);
}
#endif

static inline gboolean
is_nroff_sequence (WView *view, offset_type offset)
{
    int c0, c1, c2;

    /* The following commands are ordered to speed up the calculation. */

    c1 = get_byte_indexed (view, offset, 1);
    if (c1 == -1 || c1 != '\b')
	return FALSE;

    c0 = get_byte_indexed (view, offset, 0);
    if (c0 == -1 || !is_printable(c0))
	return FALSE;

    c2 = get_byte_indexed (view, offset, 2);
    if (c2 == -1 || !is_printable(c2))
	return FALSE;

    return (c0 == c2 || c0 == '_' || (c0 == '+' && c2 == 'o'));
}

/* Find and return the index of the last cache entry that is
 * smaller than ''coord'', according to the criterion ''sort_by''. */
static inline guint
view_ccache_find (WView *view, const struct coord_cache_entry *cache,
	const struct coord_cache_entry *coord, enum ccache_type sort_by)
{
    guint base, i, limit;

    limit = view->coord_cache->len;
    assert (limit != 0);

    base = 0;
    while (limit > 1) {
	i = base + limit / 2;
	if (coord_cache_entry_less (coord, &cache[i], sort_by, view->text_nroff_mode)) {
	    /* continue the search in the lower half of the cache */
	} else {
	    /* continue the search in the upper half of the cache */
	    base = i;
	}
	limit = (limit + 1) / 2;
    }
    return base;
}

/* Look up the missing components of ''coord'', which are given by
 * ''lookup_what''. The function returns the smallest value that
 * matches the existing components of ''coord''.
 */
static void
view_ccache_lookup (WView *view, struct coord_cache_entry *coord,
	enum ccache_type lookup_what)
{
    guint i;
    struct coord_cache_entry *cache, current, next, entry;
    enum ccache_type sorter;
    offset_type limit;
    enum {
	NROFF_START,
	NROFF_BACKSPACE,
	NROFF_CONTINUATION
    } nroff_state;

    if (!view->coord_cache) {
	view->coord_cache = g_array_new (FALSE, FALSE, sizeof(struct coord_cache_entry));
	current.cc_offset = 0;
	current.cc_line = 0;
	current.cc_column = 0;
	current.cc_nroff_column = 0;
	g_array_append_val (view->coord_cache, current);
    }

    sorter = (lookup_what == CCACHE_OFFSET) ? CCACHE_LINECOL : CCACHE_OFFSET;

  retry:
    /* find the two neighbor entries in the cache */
    cache = &(g_array_index (view->coord_cache, struct coord_cache_entry, 0));
    i = view_ccache_find (view, cache, coord, sorter);
    /* now i points to the lower neighbor in the cache */

    current = cache[i];
    if (i + 1 < view->coord_cache->len)
	limit = cache[i + 1].cc_offset;
    else
	limit = current.cc_offset + VIEW_COORD_CACHE_GRANUL;

    entry = current;
    nroff_state = NROFF_START;
    for (; current.cc_offset < limit; current = next) {
	int c, nextc;

	if ((c = get_byte (view, current.cc_offset)) == -1)
	    break;

	if (!coord_cache_entry_less (&current, coord, sorter, view->text_nroff_mode)) {
	    if (lookup_what == CCACHE_OFFSET
		&& view->text_nroff_mode
		&& nroff_state != NROFF_START) {
		/* don't break here */
	    } else {
		break;
	    }
	}

	/* Provide useful default values for ''next'' */
	next.cc_offset = current.cc_offset + 1;
	next.cc_line = current.cc_line;
	next.cc_column = current.cc_column + 1;
	next.cc_nroff_column = current.cc_nroff_column + 1;

	/* and override some of them as necessary. */
	if (c == '\r') {
	    nextc = get_byte_indexed(view, current.cc_offset, 1);

	    /* Ignore '\r' if it is followed by '\r' or '\n'. If it is
	     * followed by anything else, it is a Mac line ending and
	     * produces a line break.
	     */
	    if (nextc == '\r' || nextc == '\n') {
		next.cc_column = current.cc_column;
		next.cc_nroff_column = current.cc_nroff_column;
	    } else {
		next.cc_line = current.cc_line + 1;
		next.cc_column = 0;
		next.cc_nroff_column = 0;
	    }

	} else if (nroff_state == NROFF_BACKSPACE) {
	    next.cc_nroff_column = current.cc_nroff_column - 1;

	} else if (c == '\t') {
	    next.cc_column = offset_rounddown (current.cc_column, 8) + 8;
	    next.cc_nroff_column =
		offset_rounddown (current.cc_nroff_column, 8) + 8;

	} else if (c == '\n') {
	    next.cc_line = current.cc_line + 1;
	    next.cc_column = 0;
	    next.cc_nroff_column = 0;

	} else {
	    /* Use all default values from above */
	}

	switch (nroff_state) {
	    case NROFF_START:
	    case NROFF_CONTINUATION:
		if (is_nroff_sequence (view, current.cc_offset))
		    nroff_state = NROFF_BACKSPACE;
		else
		    nroff_state = NROFF_START;
		break;
	    case NROFF_BACKSPACE:
		nroff_state = NROFF_CONTINUATION;
		break;
	}

	/* Cache entries must guarantee that for each i < j,
	 * line[i] <= line[j] and column[i] < column[j]. In the case of
	 * nroff sequences and '\r' characters, this is not guaranteed,
	 * so we cannot save them. */
	if (nroff_state == NROFF_START && c != '\r')
	    entry = next;
    }

    if (i + 1 == view->coord_cache->len && entry.cc_offset != cache[i].cc_offset) {
	g_array_append_val (view->coord_cache, entry);
	goto retry;
    }

    if (lookup_what == CCACHE_OFFSET) {
	coord->cc_offset = current.cc_offset;
    } else {
	coord->cc_line = current.cc_line;
	coord->cc_column = current.cc_column;
	coord->cc_nroff_column = current.cc_nroff_column;
    }
}

static void
view_coord_to_offset (WView *view, offset_type *ret_offset,
	offset_type line, offset_type column)
{
    struct coord_cache_entry coord;

    coord.cc_line = line;
    coord.cc_column = column;
    coord.cc_nroff_column = column;
    view_ccache_lookup (view, &coord, CCACHE_OFFSET);
    *ret_offset = coord.cc_offset;
}

static void
view_offset_to_coord (WView *view, offset_type *ret_line,
	offset_type *ret_column, offset_type offset)
{
    struct coord_cache_entry coord;

    coord.cc_offset = offset;
    view_ccache_lookup (view, &coord, CCACHE_LINECOL);
    *ret_line = coord.cc_line;
    *ret_column = (view->text_nroff_mode)
	? coord.cc_nroff_column
	: coord.cc_column;
}

/* {{{ Cursor Movement }}} */

/*
   The following variables have to do with the current position and are
   updated by the cursor movement functions.

   In hex view and wrapped text view mode, dpy_start marks the offset of
   the top-left corner on the screen, in non-wrapping text mode it is
   the beginning of the current line.  In hex mode, hex_cursor is the
   offset of the cursor.  In non-wrapping text mode, dpy_text_column is
   the number of columns that are hidden on the left side on the screen.

   In hex mode, dpy_start is updated by the view_fix_cursor_position()
   function in order to keep the other functions simple.  In
   non-wrapping text mode dpy_start and dpy_text_column are normalized
   such that dpy_text_column < view_get_datacolumns().
 */

/* prototypes for functions used by view_moveto_bottom() */
static void view_move_up (WView *, offset_type);
static void view_moveto_bol (WView *);

static void
view_scroll_to_cursor (WView *view)
{
    if (view->hex_mode) {
	const offset_type bytes = view->bytes_per_line;
	const offset_type displaysize = view->data_area.height * bytes;
	const offset_type cursor = view->hex_cursor;
	offset_type topleft = view->dpy_start;

	if (topleft + displaysize <= cursor)
	    topleft = offset_rounddown (cursor, bytes)
		    - (displaysize - bytes);
	if (cursor < topleft)
	    topleft = offset_rounddown (cursor, bytes);
	view->dpy_start = topleft;
    } else if (view->text_wrap_mode) {
	offset_type line, col, columns;

	columns = view->data_area.width;
	view_offset_to_coord (view, &line, &col, view->dpy_start + view->dpy_text_column);
	if (columns != 0)
	    col = offset_rounddown (col, columns);
	view_coord_to_offset (view, &(view->dpy_start), line, col);
	view->dpy_text_column = 0;
    } else {
	/* nothing to do */
    }
}

static void
view_movement_fixups (WView *view, gboolean reset_search)
{
    view_scroll_to_cursor (view);
    if (reset_search) {
	view->search_start = view->dpy_start;
	view->search_length = 0;
    }
    view->dirty++;
}

static void
view_moveto_top (WView *view)
{
    view->dpy_start = 0;
    view->hex_cursor = 0;
    view->dpy_text_column = 0;
    view_movement_fixups (view, TRUE);
}

static void
view_moveto_bottom (WView *view)
{
    offset_type datalines, lines_up, filesize, last_offset;

    if (view->growbuf_in_use)
	view_growbuf_read_until (view, OFFSETTYPE_MAX);

    filesize = view_get_filesize (view);
    last_offset = offset_doz(filesize, 1);
    datalines = view->data_area.height;
    lines_up = offset_doz(datalines, 1);

    if (view->hex_mode) {
	view->hex_cursor = filesize;
	view_move_up (view, lines_up);
	view->hex_cursor = last_offset;
    } else {
	view->dpy_start = last_offset;
	view_moveto_bol (view);
	view_move_up (view, lines_up);
    }
    view_movement_fixups (view, TRUE);
}

static void
view_moveto_bol (WView *view)
{
    if (view->hex_mode) {
	view->hex_cursor -= view->hex_cursor % view->bytes_per_line;
    } else if (view->text_wrap_mode) {
	/* do nothing */
    } else {
	offset_type line, column;
	view_offset_to_coord (view, &line, &column, view->dpy_start);
	view_coord_to_offset (view, &(view->dpy_start), line, 0);
	view->dpy_text_column = 0;
    }
    view_movement_fixups (view, TRUE);
}

static void
view_moveto_eol (WView *view)
{
    if (view->hex_mode) {
	offset_type filesize, bol;

	bol = offset_rounddown (view->hex_cursor, view->bytes_per_line);
	if (get_byte_indexed (view, bol, view->bytes_per_line - 1) != -1) {
	    view->hex_cursor = bol + view->bytes_per_line - 1;
	} else {
	    filesize = view_get_filesize (view);
	    view->hex_cursor = offset_doz(filesize, 1);
	}
    } else if (view->text_wrap_mode) {
	/* nothing to do */
    } else {
	offset_type line, col;

	view_offset_to_coord (view, &line, &col, view->dpy_start);
	view_coord_to_offset (view, &(view->dpy_start), line, OFFSETTYPE_MAX);
    }
    view_movement_fixups (view, FALSE);
}

static void
view_moveto_offset (WView *view, offset_type offset)
{
    if (view->hex_mode) {
	view->hex_cursor = offset;
	view->dpy_start = offset - offset % view->bytes_per_line;
    } else {
	view->dpy_start = offset;
    }
    view_movement_fixups (view, TRUE);
}

static void
view_moveto (WView *view, offset_type line, offset_type col)
{
    offset_type offset;

    view_coord_to_offset (view, &offset, line, col);
    view_moveto_offset (view, offset);
}

static void
view_move_up (WView *view, offset_type lines)
{
    if (view->hex_mode) {
	offset_type bytes = lines * view->bytes_per_line;
	if (view->hex_cursor >= bytes) {
	    view->hex_cursor -= bytes;
	    if (view->hex_cursor < view->dpy_start)
		view->dpy_start = offset_doz (view->dpy_start, bytes);
	} else {
	    view->hex_cursor %= view->bytes_per_line;
	}
    } else if (view->text_wrap_mode) {
	const screen_dimen width = view->data_area.width;
	offset_type i, col, line, linestart;

	for (i = 0; i < lines; i++) {
	    view_offset_to_coord (view, &line, &col, view->dpy_start);
	    if (col >= width) {
		col -= width;
	    } else if (line >= 1) {
		view_coord_to_offset (view, &linestart, line, 0);
		view_offset_to_coord (view, &line, &col, linestart - 1);

		/* if the only thing that would be displayed were a
		 * single newline character, advance to the previous
		 * part of the line. */
		if (col > 0 && col % width == 0)
		    col -= width;
		else
		    col -= col % width;
	    } else {
		/* nothing to do */
	    }
	    view_coord_to_offset (view, &(view->dpy_start), line, col);
	}
    } else {
	offset_type line, column;

	view_offset_to_coord (view, &line, &column, view->dpy_start);
	line = offset_doz(line, lines);
	view_coord_to_offset (view, &(view->dpy_start), line, column);
    }
    view_movement_fixups (view, (lines != 1));
}

static void
view_move_down (WView *view, offset_type lines)
{
    if (view->hex_mode) {
	offset_type i, limit, last_byte;

	last_byte = view_get_filesize (view);
	if (last_byte >= (offset_type) view->bytes_per_line)
	    limit = last_byte - view->bytes_per_line;
	else
	    limit = 0;
	for (i = 0; i < lines && view->hex_cursor < limit; i++) {
	    view->hex_cursor += view->bytes_per_line;
	    if (lines != 1)
		view->dpy_start += view->bytes_per_line;
	}

    } else if (view->dpy_end == view_get_filesize (view)) {
	/* don't move further down. There's nothing more to see. */

    } else if (view->text_wrap_mode) {
	offset_type line, col, i;

	for (i = 0; i < lines; i++) {
	    offset_type new_offset, chk_line, chk_col;

	    view_offset_to_coord (view, &line, &col, view->dpy_start);
	    col += view->data_area.width;
	    view_coord_to_offset (view, &new_offset, line, col);

	    /* skip to the next line if the only thing that would be
	     * displayed is the newline character. */
	    view_offset_to_coord (view, &chk_line, &chk_col, new_offset);
	    if (chk_line == line && chk_col == col
		&& get_byte (view, new_offset) == '\n')
		new_offset++;

	    view->dpy_start = new_offset;
	}

    } else {
	offset_type line, col;

	view_offset_to_coord (view, &line, &col, view->dpy_start);
	line += lines;
	view_coord_to_offset (view, &(view->dpy_start), line, col);
    }
    view_movement_fixups (view, (lines != 1));
}

static void
view_move_left (WView *view, offset_type columns)
{
    if (view->hex_mode) {
	assert (columns == 1);
	if (view->hexview_in_text || !view->hexedit_lownibble) {
	    if (view->hex_cursor > 0)
		view->hex_cursor--;
	}
	if (!view->hexview_in_text)
	    view->hexedit_lownibble = !view->hexedit_lownibble;
    } else if (view->text_wrap_mode) {
	/* nothing to do */
    } else {
	if (view->dpy_text_column >= columns)
	    view->dpy_text_column -= columns;
	else
	    view->dpy_text_column = 0;
    }
    view_movement_fixups (view, FALSE);
}

static void
view_move_right (WView *view, offset_type columns)
{
    if (view->hex_mode) {
	assert (columns == 1);
	if (view->hexview_in_text || view->hexedit_lownibble) {
	    if (get_byte_indexed (view, view->hex_cursor, 1) != -1)
		view->hex_cursor++;
	}
	if (!view->hexview_in_text)
	    view->hexedit_lownibble = !view->hexedit_lownibble;
    } else if (view->text_wrap_mode) {
	/* nothing to do */
    } else {
	view->dpy_text_column += columns;
    }
    view_movement_fixups (view, FALSE);
}

/* {{{ Toggling of viewer modes }}} */

static void
view_toggle_hex_mode (WView *view)
{
    view->hex_mode = !view->hex_mode;

    if (view->hex_mode) {
	view->hex_cursor = view->dpy_start;
	view->dpy_start =
	    offset_rounddown (view->dpy_start, view->bytes_per_line);
	view->widget.options |= W_WANT_CURSOR;
    } else {
	view->dpy_start = view->hex_cursor;
	view_moveto_bol (view);
	view->widget.options &= ~W_WANT_CURSOR;
    }
    altered_hex_mode = 1;
    view->dpy_bbar_dirty = TRUE;
    view->dirty++;
}

static void
view_toggle_hexedit_mode (WView *view)
{
    view->hexedit_mode = !view->hexedit_mode;
    view->dpy_bbar_dirty = TRUE;
    view->dirty++;
}

static void
view_toggle_wrap_mode (WView *view)
{
    view->text_wrap_mode = !view->text_wrap_mode;
    if (view->text_wrap_mode) {
	view_scroll_to_cursor (view);
    } else {
	offset_type line;

	view_offset_to_coord (view, &line, &(view->dpy_text_column), view->dpy_start);
	view_coord_to_offset (view, &(view->dpy_start), line, 0);
    }
    view->dpy_bbar_dirty = TRUE;
    view->dirty++;
}

static void
view_toggle_nroff_mode (WView *view)
{
    view->text_nroff_mode = !view->text_nroff_mode;
    altered_nroff_flag = 1;
    view->dpy_bbar_dirty = TRUE;
    view->dirty++;
}

static void
view_toggle_magic_mode (WView *view)
{
    char *filename, *command;

    altered_magic_flag = 1;
    view->magic_mode = !view->magic_mode;
    filename = g_strdup (view->filename);
    command = g_strdup (view->command);

    view_done (view);
    view_load (view, command, filename, 0);
    g_free (filename);
    g_free (command);
    view->dpy_bbar_dirty = TRUE;
    view->dirty++;
}

/* {{{ Miscellaneous functions }}} */

static void
view_done (WView *view)
{
    /* Save current file position */
    if (mcview_remember_file_position && view->filename != NULL) {
	char *canon_fname;
	offset_type line, col;

	canon_fname = vfs_canon (view->filename);
	view_offset_to_coord (view, &line, &col, view->dpy_start);
	save_file_position (canon_fname, line + 1, col);
	g_free (canon_fname);
    }

    /* Write back the global viewer mode */
    default_hex_mode = view->hex_mode;
    default_nroff_flag = view->text_nroff_mode;
    default_magic_flag = view->magic_mode;
    global_wrap_mode = view->text_wrap_mode;

    /* Free memory used by the viewer */

    /* view->widget needs no destructor */

    g_free (view->filename), view->filename = NULL;
    g_free (view->command), view->command = NULL;

    view_close_datasource (view);
    /* the growing buffer is freed with the datasource */

    if (view->coord_cache) {
	g_array_free (view->coord_cache, TRUE), view->coord_cache = NULL;
    }

    view_hexedit_free_change_list (view);
    /* FIXME: what about view->search_exp? */
}

static void
view_show_error (WView *view, const char *msg)
{
    view_close_datasource (view);
    if (view_is_in_panel (view)) {
	view_set_datasource_string (view, msg);
    } else {
	message (1, MSG_ERROR, "%s", msg);
    }
}

static gboolean
view_load_command_output (WView *view, const char *command)
{
    FILE *fp;

    view_close_datasource (view);

    open_error_pipe ();
    if ((fp = popen (command, "r")) == NULL) {
	/* Avoid two messages.  Message from stderr has priority.  */
	display (view);
	if (!close_error_pipe (view_is_in_panel (view) ? -1 : 1, NULL))
	    view_show_error (view, _(" Cannot spawn child process "));
	return FALSE;
    }

    /* First, check if filter produced any output */
    view_set_datasource_stdio_pipe (view, fp);
    if (get_byte (view, 0) == -1) {
	view_close_datasource (view);

	/* Avoid two messages.  Message from stderr has priority.  */
	display (view);
	if (!close_error_pipe (view_is_in_panel (view) ? -1 : 1, NULL))
	    view_show_error (view, _("Empty output from child filter"));
	return FALSE;
    }
    return TRUE;
}

gboolean
view_load (WView *view, const char *command, const char *file,
	   int start_line)
{
    int i, type;
    int fd = -1;
    char tmp[BUF_MEDIUM];
    struct stat st;
    gboolean retval = FALSE;

    assert (view->bytes_per_line != 0);
    view_done (view);

    /* Set up the state */
    view_set_datasource_none (view);
    view->filename = g_strdup (file);
    view->command = 0;

    /* Clear the markers */
    view->marker = 0;
    for (i = 0; i < 10; i++)
	view->marks[i] = 0;

    if (!view_is_in_panel (view)) {
	view->dpy_text_column = 0;
    }

    if (command && (view->magic_mode || file == NULL || file[0] == '\0')) {
	retval = view_load_command_output (view, command);
    } else if (file != NULL && file[0] != '\0') {
	/* Open the file */
	if ((fd = mc_open (file, O_RDONLY | O_NONBLOCK)) == -1) {
	    g_snprintf (tmp, sizeof (tmp), _(" Cannot open \"%s\"\n %s "),
			file, unix_error_string (errno));
	    view_show_error (view, tmp);
	    goto finish;
	}

	/* Make sure we are working with a regular file */
	if (mc_fstat (fd, &st) == -1) {
	    mc_close (fd);
	    g_snprintf (tmp, sizeof (tmp), _(" Cannot stat \"%s\"\n %s "),
			file, unix_error_string (errno));
	    view_show_error (view, tmp);
	    goto finish;
	}

	if (!S_ISREG (st.st_mode)) {
	    mc_close (fd);
	    view_show_error (view, _(" Cannot view: not a regular file "));
	    goto finish;
	}

	if (st.st_size == 0 || mc_lseek (fd, 0, SEEK_SET) == -1) {
	    /* Must be one of those nice files that grow (/proc) */
	    view_set_datasource_vfs_pipe (view, fd);
	} else {
	    type = get_compression_type (fd);

	    if (view->magic_mode && (type != COMPRESSION_NONE)) {
		g_free (view->filename);
		view->filename = g_strconcat (file, decompress_extension (type), (char *) NULL);
	    }
	    view_set_datasource_file (view, fd, &st);
	}
	retval = TRUE;
    }

  finish:
    view->command = g_strdup (command);
    view->dpy_start = 0;
    view->search_start = 0;
    view->search_length = 0;
    view->dpy_text_column = 0;
    view->last_search = 0;	/* Start a new search */

    assert (view->bytes_per_line != 0);
    if (mcview_remember_file_position && file != NULL && start_line == 0) {
	long line, col;
	char *canon_fname;

	canon_fname = vfs_canon (file);
	load_file_position (file, &line, &col);
	g_free (canon_fname);
	view_moveto (view, offset_doz(line, 1), col);
    } else if (start_line > 0) {
	view_moveto (view, start_line - 1, 0);
    }

    view->hexedit_lownibble = FALSE;
    view->hexview_in_text = FALSE;
    view->change_list = NULL;

    return retval;
}

/* {{{ Display management }}} */

static void
view_update_bytes_per_line (WView *view)
{
    const screen_dimen cols = view->data_area.width;
    int bytes;

    if (cols < 8 + 17)
	bytes = 4;
    else
	bytes = 4 * ((cols - 8) / ((cols < 80) ? 17 : 18));
    assert(bytes != 0);

    view->bytes_per_line = bytes;
    view->dirty = max_dirt_limit + 1;	/* To force refresh */
}

static void
view_percent (WView *view, offset_type p)
{
    const screen_dimen top = view->status_area.top;
    const screen_dimen right = view->status_area.left + view->status_area.width;
    const screen_dimen height = view->status_area.height;
    int percent;
    offset_type filesize;

    if (height < 1 || right < 4)
	return;
    if (view_may_still_grow (view))
	return;
    filesize = view_get_filesize (view);

    if (filesize == 0 || view->dpy_end == filesize)
	percent = 100;
    else if (p > (INT_MAX / 100))
	percent = p / (filesize / 100);
    else
	percent = p * 100 / filesize;

    widget_move (view, top, right - 4);
    tty_printf ("%3d%%", percent);
}

static void
view_display_status (WView *view)
{
    const screen_dimen top = view->status_area.top;
    const screen_dimen left = view->status_area.left;
    const screen_dimen width = view->status_area.width;
    const screen_dimen height = view->status_area.height;
    const char *file_label, *file_name;
    screen_dimen file_label_width;
    int i;

    if (height < 1)
	return;

    tty_setcolor (SELECTED_COLOR);
    widget_move (view, top, left);
    hline (' ', width);

    file_label = _("File: %s");
    file_label_width = strlen (file_label) - 2;
    file_name = view->filename ? view->filename
	: view->command ? view->command
	: "";

    if (width < file_label_width + 6)
	addstr ((char *) name_trunc (file_name, width));
    else {
	i = (width > 22 ? 22 : width) - file_label_width;
	tty_printf (file_label, name_trunc (file_name, i));
	if (width > 46) {
	    widget_move (view, top, left + 24);
	    /* FIXME: the format strings need to be changed when offset_type changes */
	    if (view->hex_mode)
		tty_printf (_("Offset 0x%08lx"), (unsigned long) view->hex_cursor);
	    else {
		offset_type line, col;
		view_offset_to_coord (view, &line, &col, view->dpy_start);
		tty_printf (_("Line %lu Col %lu"),
		    (unsigned long) line + 1,
		    (unsigned long) (view->text_wrap_mode ? col : view->dpy_text_column));
	    }
	}
	if (width > 62) {
	    offset_type filesize;
	    filesize = view_get_filesize (view);
	    widget_move (view, top, left + 43);
	    if (!view_may_still_grow (view)) {
		tty_printf (_("%s bytes"), size_trunc (filesize));
	    } else {
		tty_printf (_(">= %s bytes"), size_trunc (filesize));
	    }
	}
	if (width > 26) {
	    view_percent (view, view->hex_mode
		? view->hex_cursor
		: view->dpy_end);
	}
    }
    tty_setcolor (SELECTED_COLOR);
}

static inline void
view_display_clean (WView *view)
{
    tty_setcolor (NORMAL_COLOR);
    widget_erase ((Widget *) view);
    if (view->dpy_frame_size != 0) {
	draw_double_box (view->widget.parent, view->widget.y,
			 view->widget.x, view->widget.lines,
			 view->widget.cols);
    }
}

typedef enum {
    MARK_NORMAL,
    MARK_SELECTED,
    MARK_CURSOR,
    MARK_CHANGED
} mark_t;

static inline int
view_count_backspaces (WView *view, off_t offset)
{
    int backspaces = 0;
    while (offset >= 2 * backspaces
	   && get_byte (view, offset - 2 * backspaces) == '\b')
	backspaces++;
    return backspaces;
}

static void
view_display_ruler (WView *view)
{
    static const char ruler_chars[] = "|----*----";
    const screen_dimen top = view->ruler_area.top;
    const screen_dimen left = view->ruler_area.left;
    const screen_dimen width = view->ruler_area.width;
    const screen_dimen height = view->ruler_area.height;
    const screen_dimen line_row = (ruler == RULER_TOP) ? 0 : 1;
    const screen_dimen nums_row = (ruler == RULER_TOP) ? 1 : 0;

    char r_buff[10];
    offset_type cl;
    screen_dimen c;

    if (ruler == RULER_NONE || height < 1)
	return;

    tty_setcolor (MARKED_COLOR);
    for (c = 0; c < width; c++) {
	cl = view->dpy_text_column + c;
	if (line_row < height) {
	    widget_move (view, top + line_row, left + c);
	    tty_print_char (ruler_chars[cl % 10]);
	}

	if ((cl != 0) && (cl % 10) == 0) {
	    g_snprintf (r_buff, sizeof (r_buff), "%"OFFSETTYPE_PRId, cl);
	    if (nums_row < height) {
		widget_move (view, top + nums_row, left + c - 1);
		tty_print_string (r_buff);
	    }
	}
    }
    attrset (NORMAL_COLOR);
}

static void
view_display_hex (WView *view)
{
    const screen_dimen top = view->data_area.top;
    const screen_dimen left = view->data_area.left;
    const screen_dimen height = view->data_area.height;
    const screen_dimen width = view->data_area.width;
    const int ngroups = view->bytes_per_line / 4;
    const screen_dimen text_start =
	8 + 13 * ngroups + ((width < 80) ? 0 : (ngroups - 1 + 1));
    /* 8 characters are used for the file offset, and every hex group
     * takes 13 characters. On ``big'' screens, the groups are separated
     * by an extra vertical line, and there is an extra space before the
     * text column.
     */

    screen_dimen row, col;
    offset_type from;
    int c;
    mark_t boldflag = MARK_NORMAL;
    struct hexedit_change_node *curr = view->change_list;
    size_t i;

    char hex_buff[10];	/* A temporary buffer for sprintf and mvwaddstr */
    int bytes;		/* Number of bytes already printed on the line */

    view_display_clean (view);

    /* Find the first displayable changed byte */
    from = view->dpy_start;
    while (curr && (curr->offset < from)) {
	curr = curr->next;
    }

    for (row = 0; get_byte (view, from) != -1 && row < height; row++) {
	col = 0;

	/* Print the hex offset */
	g_snprintf (hex_buff, sizeof (hex_buff), "%08"OFFSETTYPE_PRIX" ", from);
	widget_move (view, top + row, left);
	tty_setcolor (MARKED_COLOR);
	for (i = 0; col < width && hex_buff[i] != '\0'; i++) {
		tty_print_char(hex_buff[i]);
		col += 1;
	}
	tty_setcolor (NORMAL_COLOR);

	for (bytes = 0; bytes < view->bytes_per_line; bytes++, from++) {

	    if ((c = get_byte (view, from)) == -1)
		break;

	    /* Save the cursor position for view_place_cursor() */
	    if (from == view->hex_cursor && !view->hexview_in_text) {
		view->cursor_row = row;
		view->cursor_col = col;
	    }

	    /* Determine the state of the current byte */
	    boldflag =
		  (from == view->hex_cursor) ? MARK_CURSOR
		: (curr != NULL && from == curr->offset) ? MARK_CHANGED
		: (view->search_start <= from &&
		   from < view->search_start + view->search_length
		  ) ? MARK_SELECTED
		: MARK_NORMAL;

	    /* Determine the value of the current byte */
	    if (curr != NULL && from == curr->offset) {
		c = curr->value;
		curr = curr->next;
	    }

	    /* Select the color for the hex number */
	    tty_setcolor (
		boldflag == MARK_NORMAL ? NORMAL_COLOR :
		boldflag == MARK_SELECTED ? MARKED_COLOR :
		boldflag == MARK_CHANGED ? VIEW_UNDERLINED_COLOR :
		/* boldflag == MARK_CURSOR */
		view->hexview_in_text ? MARKED_SELECTED_COLOR :
		VIEW_UNDERLINED_COLOR);

	    /* Print the hex number */
	    widget_move (view, top + row, left + col);
	    if (col < width) {
		tty_print_char (hex_char[c / 16]);
		col += 1;
	    }
	    if (col < width) {
		tty_print_char (hex_char[c % 16]);
		col += 1;
	    }

	    /* Print the separator */
	    tty_setcolor (NORMAL_COLOR);
	    if (bytes != view->bytes_per_line - 1) {
	    	if (col < width) {
		    tty_print_char (' ');
		    col += 1;
		}

		/* After every four bytes, print a group separator */
		if (bytes % 4 == 3) {
		    if (view->data_area.width >= 80 && col < width) {
			tty_print_one_vline ();
			col += 1;
		    }
		    if (col < width) {
			tty_print_char (' ');
			col += 1;
		    }
		}
	    }

	    /* Select the color for the character; this differs from the
	     * hex color when boldflag == MARK_CURSOR */
	    tty_setcolor (
		boldflag == MARK_NORMAL ? NORMAL_COLOR :
		boldflag == MARK_SELECTED ? MARKED_COLOR :
		boldflag == MARK_CHANGED ? VIEW_UNDERLINED_COLOR :
		/* boldflag == MARK_CURSOR */
		view->hexview_in_text ? VIEW_UNDERLINED_COLOR :
		MARKED_SELECTED_COLOR);

	    c = convert_to_display_c (c);
	    if (!is_printable (c))
		c = '.';

	    /* Print corresponding character on the text side */
	    if (text_start + bytes < width) {
		widget_move (view, top + row, left + text_start + bytes);
		tty_print_char (c);
	    }

	    /* Save the cursor position for view_place_cursor() */
	    if (from == view->hex_cursor && view->hexview_in_text) {
		view->cursor_row = row;
		view->cursor_col = text_start + bytes;
	    }
	}
    }

    /* Be polite to the other functions */
    tty_setcolor (NORMAL_COLOR);

    view_place_cursor (view);
    view->dpy_end = from;
}

static void
view_display_text (WView * view)
{
    const screen_dimen left = view->data_area.left;
    const screen_dimen top = view->data_area.top;
    const screen_dimen width = view->data_area.width;
    const screen_dimen height = view->data_area.height;
    screen_dimen row, col;
    offset_type from;
    int c;
    struct hexedit_change_node *curr = view->change_list;

    view_display_clean (view);
    view_display_ruler (view);

    /* Find the first displayable changed byte */
    from = view->dpy_start;
    while (curr && (curr->offset < from)) {
	curr = curr->next;
    }

    tty_setcolor (NORMAL_COLOR);
    for (row = 0, col = 0; row < height && (c = get_byte (view, from)) != -1; from++) {

	if (view->text_nroff_mode && c == '\b') {
	    int c_prev;
	    int c_next;

	    if ((c_next = get_byte_indexed (view, from, 1)) != -1
		&& is_printable (c_next)
		&& from >= 1
		&& (c_prev = get_byte (view, from - 1)) != -1
		&& is_printable (c_prev)
		&& (c_prev == c_next || c_prev == '_'
		    || (c_prev == '+' && c_next == 'o'))) {
		if (col == 0) {
		    if (row == 0) {
			/* We're inside an nroff character sequence at the
			 * beginning of the screen -- just skip the
			 * backspace and continue with the next character. */
			continue;
		    }
		    row--;
		    col = width;
		}
		col--;
		if (c_prev == '_' && (c_next != '_' || view_count_backspaces (view, from) == 1))
		    tty_setcolor (VIEW_UNDERLINED_COLOR);
		else
		    tty_setcolor (MARKED_COLOR);
		continue;
	    }
	}

	if ((c == '\n') || (col >= width && view->text_wrap_mode)) {
	    col = 0;
	    row++;
	    if (c == '\n' || row >= height)
		continue;
	}

	if (c == '\r') {
	    c = get_byte_indexed(view, from, 1);
	    if (c == '\r' || c == '\n')
		continue;
	    col = 0;
	    row++;
	    continue;
	}

	if (c == '\t') {
	    offset_type line, column;
	    view_offset_to_coord (view, &line, &column, from);
	    col += (8 - column % 8);
	    if (view->text_wrap_mode && col >= width && width != 0) {
		row += col / width;
		col %= width;
	    }
	    continue;
	}

	if (view->search_start <= from
	 && from < view->search_start + view->search_length) {
	    tty_setcolor (SELECTED_COLOR);
	}

	if (col >= view->dpy_text_column
	    && col - view->dpy_text_column < width) {
	    widget_move (view, top + row, left + (col - view->dpy_text_column));
	    c = convert_to_display_c (c);
	    if (!is_printable (c))
		c = '.';
	    tty_print_char (c);
	}
	col++;
	tty_setcolor (NORMAL_COLOR);
    }
    view->dpy_end = from;
}

/* Displays as much data from view->dpy_start as fits on the screen */
static void
display (WView *view)
{
    view_compute_areas (view);
    if (view->hex_mode) {
	view_display_hex (view);
    } else {
	view_display_text (view);
    }
    view_display_status (view);
}

static void
view_place_cursor (WView *view)
{
    const screen_dimen top = view->data_area.top;
    const screen_dimen left = view->data_area.left;
    screen_dimen col;

    col = view->cursor_col;
    if (!view->hexview_in_text && view->hexedit_lownibble)
	col++;
    widget_move (&view->widget, top + view->cursor_row, left + col);
}

static void
view_update (WView *view)
{
    static int dirt_limit = 1;

    if (view->dpy_bbar_dirty) {
	view->dpy_bbar_dirty = FALSE;
	view_labels (view);
	buttonbar_redraw (view->widget.parent);
    }

    if (view->dirty > dirt_limit) {
	/* Too many updates skipped -> force a update */
	display (view);
	view->dirty = 0;
	/* Raise the update skipping limit */
	dirt_limit++;
	if (dirt_limit > max_dirt_limit)
	    dirt_limit = max_dirt_limit;
    }
    if (view->dirty) {
	if (is_idle ()) {
	    /* We have time to update the screen properly */
	    display (view);
	    view->dirty = 0;
	    if (dirt_limit > 1)
		dirt_limit--;
	} else {
	    /* We are busy -> skipping full update,
	       only the status line is updated */
	    view_display_status (view);
	}
	/* Here we had a refresh, if fast scrolling does not work
	   restore the refresh, although this should not happen */
    }
}

/* {{{ Hex editor }}} */

static void
enqueue_change (struct hexedit_change_node **head,
		struct hexedit_change_node *node)
{
    /* chnode always either points to the head of the list or
     * to one of the ->next fields in the list. The value at
     * this location will be overwritten with the new node.   */
    struct hexedit_change_node **chnode = head;

    while (*chnode != NULL && (*chnode)->offset < node->offset)
	chnode = &((*chnode)->next);

    node->next = *chnode;
    *chnode = node;
}

static cb_ret_t
view_handle_editkey (WView *view, int key)
{
    struct hexedit_change_node *node;
    byte byte_val;

    /* Has there been a change at this position? */
    node = view->change_list;
    while (node && (node->offset != view->hex_cursor))
	node = node->next;

    if (!view->hexview_in_text) {
	/* Hex editing */
	unsigned int hexvalue = 0;

	if (key >= '0' && key <= '9')
	    hexvalue =  0 + (key - '0');
	else if (key >= 'A' && key <= 'F')
	    hexvalue = 10 + (key - 'A');
	else if (key >= 'a' && key <= 'f')
	    hexvalue = 10 + (key - 'a');
	else
	    return MSG_NOT_HANDLED;

	if (node)
	    byte_val = node->value;
	else
	    byte_val = get_byte (view, view->hex_cursor);

	if (view->hexedit_lownibble) {
	    byte_val = (byte_val & 0xf0) | (hexvalue);
	} else {
	    byte_val = (byte_val & 0x0f) | (hexvalue << 4);
	}
    } else {
	/* Text editing */
	if (key < 256 && (is_printable (key) || (key == '\n')))
	    byte_val = key;
	else
	    return MSG_NOT_HANDLED;
    }
    if (!node) {
	node = g_new (struct hexedit_change_node, 1);
	node->offset = view->hex_cursor;
	node->value = byte_val;
	enqueue_change (&view->change_list, node);
    } else {
	node->value = byte_val;
    }
    view->dirty++;
    view_update (view);
    view_move_right (view, 1);
    return MSG_HANDLED;
}

static gboolean
view_hexedit_save_changes (WView *view)
{
    struct hexedit_change_node *curr, *next;
    int fp, answer;
    char *text, *error;

    if (view->change_list == NULL)
	return TRUE;

  retry_save:
    assert (view->filename != NULL);
    fp = mc_open (view->filename, O_WRONLY);
    if (fp == -1)
	goto save_error;

    for (curr = view->change_list; curr != NULL; curr = next) {
	next = curr->next;

	if (mc_lseek (fp, curr->offset, SEEK_SET) == -1
	    || mc_write (fp, &(curr->value), 1) != 1)
	    goto save_error;

	/* delete the saved item from the change list */
	view->change_list = next;
	view->dirty++;
	view_set_byte (view, curr->offset, curr->value);
	g_free (curr);
    }

    if (mc_close (fp) == -1) {
	error = g_strdup (strerror (errno));
	message (D_ERROR, _(" Save file "),
	    _(" Error while closing the file: \n %s \n"
	      " Data may have been written or not. "), error);
	g_free (error);
    }
    view_update (view);
    return TRUE;

  save_error:
    error = g_strdup (strerror (errno));
    text = g_strdup_printf (_(" Cannot save file: \n %s "), error);
    g_free (error);
    (void) mc_close (fp);

    answer = query_dialog (_(" Save file "), text, D_ERROR,
	2, _("&Retry"), _("&Cancel"));
    g_free (text);

    if (answer == 0)
	goto retry_save;
    return FALSE;
}

/* {{{ Miscellaneous functions }}} */

static gboolean
view_ok_to_quit (WView *view)
{
    int r;

    if (view->change_list == NULL)
	return TRUE;

    r = query_dialog (_("Quit"),
		      _(" File was modified, Save with exit? "), D_NORMAL, 3,
		      _("&Cancel quit"), _("&Yes"), _("&No"));

    switch (r) {
    case 1:
	return view_hexedit_save_changes (view);
    case 2:
	view_hexedit_free_change_list (view);
	return TRUE;
    default:
	return FALSE;
    }
}

static inline void
my_define (Dlg_head *h, int idx, const char *text, void (*fn) (WView *),
	   WView *view)
{
    buttonbar_set_label_data (h, idx, text, (buttonbarfn) fn, view);
}

/* {{{ Searching }}} */

/* Case insensitive search of text in data */
static int
icase_search_p (WView *view, char *text, char *data, int nothing)
{
    const char *q;
    int lng;
    const int direction = view->direction;

    (void) nothing;

    /* If we are searching backwards, reverse the string */
    if (direction == -1) {
	g_strreverse (text);
	g_strreverse (data);
    }

    q = _icase_search (text, data, &lng);

    if (direction == -1) {
	g_strreverse (text);
	g_strreverse (data);
    }

    if (q != 0) {
	if (direction > 0)
	    view->search_start = q - data - lng;
	else
	    view->search_start = strlen (data) - (q - data);
	view->search_length = lng;
	return 1;
    }
    return 0;
}

static char *
grow_string_buffer (char *text, gulong *size)
{
    char *new;

    /* The grow steps */
    *size += 160;
    new = g_realloc (text, *size);
    if (text == NULL) {
	*new = '\0';
    }
    return new;
}

static char *
get_line_at (WView *view, offset_type *p, offset_type *skipped)
{
    char *buffer = NULL;
    gulong buffer_size = 0;
    offset_type usable_size = 0;
    int ch;
    const int direction = view->direction;
    offset_type pos = *p;
    offset_type i = 0;
    int prev = '\0';

    *skipped = 0;

    if (pos == 0 && direction == -1)
	return 0;

    /* skip over all the possible zeros in the file */
    while ((ch = get_byte (view, pos)) == 0) {
	if (pos == 0 && direction == -1)
	    return 0;
	pos += direction;
	i++;
    }
    *skipped = i;

    if (i == 0 && (pos != 0 || direction == -1)) {
	prev = get_byte (view, pos - direction);
	if ((prev == -1) || (prev == '\n'))
	    prev = '\0';
    }

    for (i = 1; ch != -1; ch = get_byte (view, pos)) {
	if (i >= usable_size) {
	    buffer = grow_string_buffer (buffer, &buffer_size);
	    usable_size = buffer_size - 2;	/* prev & null terminator */
	}

	buffer[i++] = ch;

	if (pos == 0 && direction == -1)
	    break;

	pos += direction;

	if (ch == '\n' || ch == '\0') {
	    i--;			/* Strip newline/zero */
	    break;
	}
    }

    if (buffer) {
	buffer[0] = prev;
	buffer[i] = '\0';

	/* If we are searching backwards, reverse the string */
	if (direction == -1) {
	    g_strreverse (buffer + 1);
	}
    }

    *p = pos;
    return buffer;
}

static void
search_update_steps (WView *view)
{
    offset_type filesize = view_get_filesize (view);
    if (filesize != 0)
	view->update_steps = 40000;
    else /* viewing a data stream, not a file */
	view->update_steps = filesize / 100;

    /* Do not update the percent display but every 20 ks */
    if (view->update_steps < 20000)
	view->update_steps = 20000;
}

static void
search (WView *view, char *text,
	int (*search) (WView *, char *, char *, int))
{
    char *s = NULL;	/*  The line we read from the view buffer */
    offset_type p, beginning, search_start;
    int found_len;
    int search_status;
    Dlg_head *d = 0;

    /* Used to keep track of where the line starts, when looking forward
     * is the index before transfering the line; the reverse case uses
     * the position returned after the line has been read */
    offset_type forward_line_start;
    offset_type reverse_line_start;
    offset_type t;

    if (verbose) {
	d = create_message (D_NORMAL, _("Search"), _("Searching %s"), text);
	mc_refresh ();
    }

    found_len = view->search_length;
    search_start = view->search_start;

    if (view->direction == 1) {
	p = search_start + ((found_len) ? 1 : 0);
    } else {
	p = search_start - ((found_len && search_start >= 1) ? 1 : 0);
    }
    beginning = p;

    /* Compute the percent steps */
    search_update_steps (view);
    view->update_activate = 0;

    enable_interrupt_key ();
    for (;; g_free (s)) {
	if (p >= view->update_activate) {
	    view->update_activate += view->update_steps;
	    if (verbose) {
		view_percent (view, p);
		mc_refresh ();
	    }
	    if (got_interrupt ())
		break;
	}
	forward_line_start = p;
	s = get_line_at (view, &p, &t);
	reverse_line_start = p;

	if (!s)
	    break;

	search_status = (*search) (view, text, s + 1, match_normal);
	if (search_status < 0) {
	    g_free (s);
	    break;
	}

	if (search_status == 0)
	    continue;

	/* We found the string */

	/* Handle ^ and $ when regexp search starts at the middle of the line */
	if (*s && !view->search_start && (search == regexp_view_search)) {
	    if ((*text == '^' && view->direction == 1)
		|| (view->direction == -1 && text[strlen (text) - 1] == '$')
	       ) {
		continue;
	    }
	}
	/* Record the position used to continue the search */
	if (view->direction == 1)
	    t += forward_line_start;
	else
	    t = reverse_line_start ? reverse_line_start + 2 : 0;
	view->search_start += t;

	if (t != beginning) {
	    view->dpy_start = t;
	}

	g_free (s);
	break;
    }
    disable_interrupt_key ();
    if (verbose) {
	dlg_run_done (d);
	destroy_dlg (d);
    }
    if (!s) {
	message (0, _("Search"), _(" Search string not found "));
	view->search_length = 0;
    }
}

/* Search buffer (its size is len) in the complete buffer
 * returns the position where the block was found or INVALID_OFFSET
 * if not found */
static offset_type
block_search (WView *view, const char *buffer, int len)
{
    int direction = view->direction;
    const char *d = buffer;
    char b;
    offset_type e;

    enable_interrupt_key ();
    if (direction == 1)
	e = view->search_start + ((view->search_length) ? 1 : 0);
    else
	e = view->search_start
	  - ((view->search_length && view->search_start >= 1) ? 1 : 0);

    search_update_steps (view);
    view->update_activate = 0;

    if (direction == -1) {
	for (d += len - 1;; e--) {
	    if (e <= view->update_activate) {
		view->update_activate -= view->update_steps;
		if (verbose) {
		    view_percent (view, e);
		    mc_refresh ();
		}
		if (got_interrupt ())
		    break;
	    }
	    b = get_byte (view, e);

	    if (*d == b) {
		if (d == buffer) {
		    disable_interrupt_key ();
		    return e;
		}
		d--;
	    } else {
		e += buffer + len - 1 - d;
		d = buffer + len - 1;
	    }
	    if (e == 0)
		break;
	}
    } else {
	while (get_byte (view, e) != -1) {
	    if (e >= view->update_activate) {
		view->update_activate += view->update_steps;
		if (verbose) {
		    view_percent (view, e);
		    mc_refresh ();
		}
		if (got_interrupt ())
		    break;
	    }
	    b = get_byte (view, e++);

	    if (*d == b) {
		d++;
		if (d - buffer == len) {
		    disable_interrupt_key ();
		    return e - len;
		}
	    } else {
		e -= d - buffer;
		d = buffer;
	    }
	}
    }
    disable_interrupt_key ();
    return INVALID_OFFSET;
}

/*
 * Search in the hex mode.  Supported input:
 * - numbers (oct, dec, hex).  Each of them matches one byte.
 * - strings in double quotes.  Matches exactly without quotes.
 */
static void
hex_search (WView *view, const char *text)
{
    char *buffer;		/* Parsed search string */
    char *cur;			/* Current position in it */
    int block_len;		/* Length of the search string */
    offset_type pos;		/* Position of the string in the file */
    int parse_error = 0;

    if (!*text) {
	view->search_length = 0;
	return;
    }

    /* buffer will never be longer that text */
    buffer = g_new (char, strlen (text));
    cur = buffer;

    /* First convert the string to a stream of bytes */
    while (*text) {
	int val;
	int ptr;

	/* Skip leading spaces */
	if (*text == ' ' || *text == '\t') {
	    text++;
	    continue;
	}

	/* %i matches octal, decimal, and hexadecimal numbers */
	if (sscanf (text, "%i%n", &val, &ptr) > 0) {
	    /* Allow signed and unsigned char in the user input */
	    if (val < -128 || val > 255) {
		parse_error = 1;
		break;
	    }

	    *cur++ = (char) val;
	    text += ptr;
	    continue;
	}

	/* Try quoted string, strip quotes */
	if (*text == '"') {
	    const char *next_quote;

	    text++;
	    next_quote = strchr (text, '"');
	    if (next_quote) {
		memcpy (cur, text, next_quote - text);
		cur += next_quote - text;
		text = next_quote + 1;
		continue;
	    }
	    /* fall through */
	}

	parse_error = 1;
	break;
    }

    block_len = cur - buffer;

    /* No valid bytes in the user input */
    if (block_len <= 0 || parse_error) {
	message (0, _("Search"), _("Invalid hex search expression"));
	g_free (buffer);
	view->search_length = 0;
	return;
    }

    /* Then start the search */
    pos = block_search (view, buffer, block_len);

    g_free (buffer);

    if (pos == INVALID_OFFSET) {
	message (0, _("Search"), _(" Search string not found "));
	view->search_length = 0;
	return;
    }

    view->search_start = pos;
    view->search_length = block_len;
    /* Set the edit cursor to the search position, left nibble */
    view->hex_cursor = view->search_start;
    view->hexedit_lownibble = FALSE;

    /* Adjust the file offset */
    view->dpy_start = pos - pos % view->bytes_per_line;
}

static int
regexp_view_search (WView *view, char *pattern, char *string,
		    int match_type)
{
    static regex_t r;
    static char *old_pattern = NULL;
    static int old_type;
    regmatch_t pmatch[1];
    int i, flags = REG_ICASE;

    if (old_pattern == NULL || strcmp (old_pattern, pattern) != 0
	|| old_type != match_type) {
	if (old_pattern != NULL) {
	    regfree (&r);
	    g_free (old_pattern);
	    old_pattern = 0;
	}
	for (i = 0; pattern[i] != '\0'; i++) {
	    if (isupper ((unsigned char) pattern[i])) {
		flags = 0;
		break;
	    }
	}
	flags |= REG_EXTENDED;
	if (regcomp (&r, pattern, flags)) {
	    message (1, MSG_ERROR, _(" Invalid regular expression "));
	    return -1;
	}
	old_pattern = g_strdup (pattern);
	old_type = match_type;
    }
    if (regexec (&r, string, 1, pmatch, 0) != 0)
	return 0;
    view->search_length = pmatch[0].rm_eo - pmatch[0].rm_so;
    view->search_start = pmatch[0].rm_so;
    return 1;
}

static void
do_regexp_search (WView *view)
{
    search (view, view->search_exp, regexp_view_search);
    /* Had a refresh here */
    view->dirty++;
    view_update (view);
}

static void
do_normal_search (WView *view)
{
    if (view->hex_mode)
	hex_search (view, view->search_exp);
    else
	search (view, view->search_exp, icase_search_p);
    /* Had a refresh here */
    view->dirty++;
    view_update (view);
}

/* {{{ User-definable commands }}} */

/*
    The functions in this section can be bound to hotkeys. They are all
    of the same type (taking a pointer to WView as parameter and
    returning void). TODO: In the not-too-distant future, these commands
    will become fully configurable, like they already are in the
    internal editor. By convention, all the function names end in
    "_cmd".
 */

static void
view_help_cmd (void)
{
    interactive_display (NULL, "[Internal File Viewer]");
}

/* Toggle between hexview and hexedit mode */
static void
view_toggle_hexedit_mode_cmd (WView *view)
{
    view_toggle_hexedit_mode (view);
    view_update (view);
}

/* Toggle between wrapped and unwrapped view */
static void
view_toggle_wrap_mode_cmd (WView *view)
{
    view_toggle_wrap_mode (view);
    view_update (view);
}

/* Toggle between hex view and text view */
static void
view_toggle_hex_mode_cmd (WView *view)
{
    view_toggle_hex_mode (view);
    view_update (view);
}

static void
view_moveto_line_cmd (WView *view)
{
    char *answer, *answer_end, prompt[BUF_SMALL];
    offset_type line, col;

    view_offset_to_coord (view, &line, &col, view->dpy_start);

    g_snprintf (prompt, sizeof (prompt),
		_(" The current line number is %d.\n"
		  " Enter the new line number:"), (int) (line + 1));
    answer = input_dialog (_(" Goto line "), prompt, "");
    if (answer != NULL && answer[0] != '\0') {
	errno = 0;
	line = strtoul (answer, &answer_end, 10);
	if (*answer_end == '\0' && errno == 0 && line >= 1)
	    view_moveto (view, line - 1, 0);
    }
    g_free (answer);
    view->dirty++;
    view_update (view);
}

static void
view_moveto_addr_cmd (WView *view)
{
    char *line, *error, prompt[BUF_SMALL];
    offset_type addr;

    g_snprintf (prompt, sizeof (prompt),
		_(" The current address is 0x%lx.\n"
		  " Enter the new address:"), view->hex_cursor);
    line = input_dialog (_(" Goto Address "), prompt, "");
    if (line != NULL) {
	if (*line != '\0') {
	    addr = strtoul (line, &error, 0);
	    if ((*error == '\0') && get_byte (view, addr) != -1) {
		view_moveto_offset (view, addr);
	    } else {
		message (D_ERROR, _("Warning"), _(" Invalid address "));
	    }
	}
	g_free (line);
    }
    view->dirty++;
    view_update (view);
}

static void
view_hexedit_save_changes_cmd (WView *view)
{
    (void) view_hexedit_save_changes (view);
}

/* {{{ Searching }}} */

static void
regexp_search (WView *view, int direction)
{
    const char *defval;
    char *regexp;
    static char *last_regexp;

    defval = (last_regexp != NULL ? last_regexp : "");

    regexp = input_dialog (_("Search"), _(" Enter regexp:"), defval);
    if (regexp == NULL || regexp[0] == '\0') {
	g_free (regexp);
	return;
    }

    g_free (last_regexp);
    view->search_exp = last_regexp = regexp;

    view->direction = direction;
    do_regexp_search (view);
    view->last_search = do_regexp_search;
}

/* {{{ User-definable commands }}} */

static void
view_regexp_search_cmd (WView *view)
{
    regexp_search (view, 1);
}

/* Both views */
static void
view_normal_search_cmd (WView *view)
{
    char *defval, *exp = NULL;
    static char *last_search_string;

    enum {
	SEARCH_DLG_HEIGHT = 8,
	SEARCH_DLG_WIDTH = 58
    };

    static int replace_backwards;
    int treplace_backwards = replace_backwards;

    static QuickWidget quick_widgets[] = {
	{quick_button, 6, 10, 5, SEARCH_DLG_HEIGHT, N_("&Cancel"), 0,
	 B_CANCEL,
	 0, 0, NULL},
	{quick_button, 2, 10, 5, SEARCH_DLG_HEIGHT, N_("&OK"), 0, B_ENTER,
	 0, 0, NULL},
	{quick_checkbox, 3, SEARCH_DLG_WIDTH, 4, SEARCH_DLG_HEIGHT,
	 N_("&Backwards"), 0, 0,
	 0, 0, NULL},
	{quick_input, 3, SEARCH_DLG_WIDTH, 3, SEARCH_DLG_HEIGHT, "", 52, 0,
	 0, 0, N_("Search")},
	{quick_label, 2, SEARCH_DLG_WIDTH, 2, SEARCH_DLG_HEIGHT,
	 N_(" Enter search string:"), 0, 0,
	 0, 0, 0},
	NULL_QuickWidget
    };
    static QuickDialog Quick_input = {
	SEARCH_DLG_WIDTH, SEARCH_DLG_HEIGHT, -1, 0, N_("Search"),
	"[Input Line Keys]", quick_widgets, 0
    };

    defval = g_strdup (last_search_string != NULL ? last_search_string : "");
    convert_to_display (defval);

    quick_widgets[2].result = &treplace_backwards;
    quick_widgets[3].str_result = &exp;
    quick_widgets[3].text = defval;

    if (quick_dialog (&Quick_input) == B_CANCEL)
	goto cleanup;

    replace_backwards = treplace_backwards;

    if (exp == NULL || exp[0] == '\0')
	goto cleanup;

    convert_from_input (exp);

    g_free (last_search_string);
    view->search_exp = last_search_string = exp;
    exp = NULL;

    view->direction = replace_backwards ? -1 : 1;
    do_normal_search (view);
    view->last_search = do_normal_search;

cleanup:
    g_free (exp);
    g_free (defval);
}

static void
view_toggle_magic_mode_cmd (WView *view)
{
    view_toggle_magic_mode (view);
    view_update (view);
}

static void
view_toggle_nroff_mode_cmd (WView *view)
{
    view_toggle_nroff_mode (view);
    view_update (view);
}

static void
view_quit_cmd (WView *view)
{
    if (view_ok_to_quit (view))
	dlg_stop (view->widget.parent);
}

/* {{{ Miscellaneous functions }}} */

/* Define labels and handlers for functional keys */
static void
view_labels (WView *view)
{
    Dlg_head *h = view->widget.parent;

    buttonbar_set_label (h, 1, Q_("ButtonBar|Help"), view_help_cmd);

    my_define (h, 10, Q_("ButtonBar|Quit"), view_quit_cmd, view);
    my_define (h, 4, view->hex_mode
	? Q_("ButtonBar|Ascii")
	: Q_("ButtonBar|Hex"),
	view_toggle_hex_mode_cmd, view);
    my_define (h, 5, view->hex_mode
	? Q_("ButtonBar|Goto")
	: Q_("ButtonBar|Line"),
	view->hex_mode ? view_moveto_addr_cmd : view_moveto_line_cmd, view);

    if (view->hex_mode) {
	if (view->hexedit_mode) {
	    my_define (h, 2, Q_("ButtonBar|View"),
		view_toggle_hexedit_mode_cmd, view);
	} else if (view->datasource == DS_FILE) {
	    my_define (h, 2, Q_("ButtonBar|Edit"),
		view_toggle_hexedit_mode_cmd, view);
	} else {
	    buttonbar_clear_label (h, 2);
	}
	my_define (h, 6, Q_("ButtonBar|Save"),
	    view_hexedit_save_changes_cmd, view);
    } else {
	my_define (h, 2, view->text_wrap_mode
	    ? Q_("ButtonBar|UnWrap")
	    : Q_("ButtonBar|Wrap"),
	    view_toggle_wrap_mode_cmd, view);
	my_define (h, 6, Q_("ButtonBar|RxSrch"),
	    view_regexp_search_cmd, view);
    }

    my_define (h, 7, view->hex_mode
	? Q_("ButtonBar|HxSrch")
	: Q_("ButtonBar|Search"),
	view_normal_search_cmd, view);
    my_define (h, 8, view->magic_mode
	? Q_("ButtonBar|Raw")
	: Q_("ButtonBar|Parse"),
	view_toggle_magic_mode_cmd, view);

    /* don't override the key to access the main menu */
    if (!view_is_in_panel (view)) {
	my_define (h, 9, view->text_nroff_mode
	    ? Q_("ButtonBar|Unform")
	    : Q_("ButtonBar|Format"),
	    view_toggle_nroff_mode_cmd, view);
	my_define (h, 3, Q_("ButtonBar|Quit"), view_quit_cmd, view);
    }
}

/* {{{ Event handling }}} */

/* Check for left and right arrows, possibly with modifiers */
static cb_ret_t
check_left_right_keys (WView *view, int c)
{
    if (c == KEY_LEFT) {
	view_move_left (view, 1);
	return MSG_HANDLED;
    }

    if (c == KEY_RIGHT) {
	view_move_right (view, 1);
	return MSG_HANDLED;
    }

    /* Ctrl with arrows moves by 10 postions in the unwrap mode */
    if (view->hex_mode || view->text_wrap_mode)
	return MSG_NOT_HANDLED;

    if (c == (KEY_M_CTRL | KEY_LEFT)) {
	if (view->dpy_text_column >= 10)
	    view->dpy_text_column -= 10;
	else
	    view->dpy_text_column = 0;
	view->dirty++;
	return MSG_HANDLED;
    }

    if (c == (KEY_M_CTRL | KEY_RIGHT)) {
	if (view->dpy_text_column <= OFFSETTYPE_MAX - 10)
	    view->dpy_text_column += 10;
	else
	    view->dpy_text_column = OFFSETTYPE_MAX;
	view->dirty++;
	return MSG_HANDLED;
    }

    return MSG_NOT_HANDLED;
}

/* {{{ User-definable commands }}} */

static void
view_continue_search_cmd (WView *view)
{
    if (view->last_search) {
	view->last_search (view);
    } else {
	/* if not... then ask for an expression */
	view_normal_search_cmd (view);
    }
}

static void
view_toggle_ruler_cmd (WView *view)
{
    static const enum ruler_type next[3] = {
	RULER_TOP,
	RULER_BOTTOM,
	RULER_NONE
    };

    assert ((size_t) ruler < 3);
    ruler = next[(size_t) ruler];
    view->dirty++;
}

/* {{{ Event handling }}} */

static void view_cmk_move_up (void *w, int n) {
    view_move_up ((WView *) w, n);
}
static void view_cmk_move_down (void *w, int n) {
    view_move_down ((WView *) w, n);
}
static void view_cmk_moveto_top (void *w, int n) {
    (void) &n;
    view_moveto_top ((WView *) w);
}
static void view_cmk_moveto_bottom (void *w, int n) {
    (void) &n;
    view_moveto_bottom ((WView *) w);
}

/* Both views */
static cb_ret_t
view_handle_key (WView *view, int c)
{
    c = convert_from_input_c (c);

    if (view->hex_mode) {
	switch (c) {
	case '\t':
	    view->hexview_in_text = !view->hexview_in_text;
	    view->dirty++;
	    return MSG_HANDLED;

	case XCTRL ('a'):
	    view_moveto_bol (view);
	    view->dirty++;
	    return MSG_HANDLED;

	case XCTRL ('b'):
	    view_move_left (view, 1);
	    return MSG_HANDLED;

	case XCTRL ('e'):
	    view_moveto_eol (view);
	    return MSG_HANDLED;

	case XCTRL ('f'):
	    view_move_right (view, 1);
	    return MSG_HANDLED;
	}

	if (view->hexedit_mode
	    && view_handle_editkey (view, c) == MSG_HANDLED)
	    return MSG_HANDLED;
    }

    if (check_left_right_keys (view, c))
	return MSG_HANDLED;

    if (check_movement_keys (c, view->data_area.height + 1, view,
	view_cmk_move_up, view_cmk_move_down,
	view_cmk_moveto_top, view_cmk_moveto_bottom))
	return MSG_HANDLED;

    switch (c) {

    case '?':
	regexp_search (view, -1);
	return MSG_HANDLED;

    case '/':
	regexp_search (view, 1);
	return MSG_HANDLED;

	/* Continue search */
    case XCTRL ('r'):
    case XCTRL ('s'):
    case 'n':
    case KEY_F (17):
	view_continue_search_cmd (view);
	return MSG_HANDLED;

	/* toggle ruler */
    case ALT ('r'):
	view_toggle_ruler_cmd (view);
	return MSG_HANDLED;

    case 'h':
	view_move_left (view, 1);
	return MSG_HANDLED;

    case 'j':
    case '\n':
    case 'e':
	view_move_down (view, 1);
	return MSG_HANDLED;

    case 'd':
	view_move_down (view, (view->data_area.height + 1) / 2);
	return MSG_HANDLED;

    case 'u':
	view_move_up (view, (view->data_area.height + 1) / 2);
	return MSG_HANDLED;

    case 'k':
    case 'y':
	view_move_up (view, 1);
	return MSG_HANDLED;

    case 'l':
	view_move_right (view, 1);
	return MSG_HANDLED;

    case ' ':
    case 'f':
	view_move_down (view, view->data_area.height);
	return MSG_HANDLED;

    case XCTRL ('o'):
	view_other_cmd ();
	return MSG_HANDLED;

	/* Unlike Ctrl-O, run a new shell if the subshell is not running.  */
    case '!':
	exec_shell ();
	return MSG_HANDLED;

    case 'b':
	view_move_up (view, view->data_area.height);
	return MSG_HANDLED;

    case KEY_IC:
	view_move_up (view, 2);
	return MSG_HANDLED;

    case KEY_DC:
	view_move_down (view, 2);
	return MSG_HANDLED;

    case 'm':
	view->marks[view->marker] = view->dpy_start;
	return MSG_HANDLED;

    case 'r':
	view->dpy_start = view->marks[view->marker];
	view->dirty++;
	return MSG_HANDLED;

	/*  Use to indicate parent that we want to see the next/previous file */
	/* Does not work in panel mode */
    case XCTRL ('f'):
    case XCTRL ('b'):
	if (!view_is_in_panel (view))
	    view->move_dir = c == XCTRL ('f') ? 1 : -1;
	/* FALLTHROUGH */
    case 'q':
    case XCTRL ('g'):
    case ESC_CHAR:
	if (view_ok_to_quit (view))
	    view->want_to_quit = TRUE;
	return MSG_HANDLED;

#ifdef HAVE_CHARSET
    case XCTRL ('t'):
	do_select_codepage ();
	view->dirty++;
	view_update (view);
	return MSG_HANDLED;
#endif				/* HAVE_CHARSET */

#ifdef MC_ENABLE_DEBUGGING_CODE
    case 't': /* mnemonic: "test" */
	view_ccache_dump (view);
	return MSG_HANDLED;
#endif
    }
    if (c >= '0' && c <= '9')
	view->marker = c - '0';

    /* Key not used */
    return MSG_NOT_HANDLED;
}

/* Both views */
static int
view_event (WView *view, Gpm_Event *event, int *result)
{
    screen_dimen y, x;

    *result = MOU_NORMAL;

    /* We are not interested in the release events */
    if (!(event->type & (GPM_DOWN | GPM_DRAG)))
	return 0;

    /* Wheel events */
    if ((event->buttons & GPM_B_UP) && (event->type & GPM_DOWN)) {
	view_move_up (view, 2);
	return 1;
    }
    if ((event->buttons & GPM_B_DOWN) && (event->type & GPM_DOWN)) {
	view_move_down (view, 2);
	return 1;
    }

    x = event->x;
    y = event->y;

    /* Scrolling left and right */
    if (!view->text_wrap_mode) {
	if (x < view->data_area.width * 1/4) {
	    view_move_left (view, 1);
	    goto processed;
	} else if (x < view->data_area.width * 3/4) {
	    /* ignore the click */
	} else {
	    view_move_right (view, 1);
	    goto processed;
	}
    }

    /* Scrolling up and down */
    if (y < view->data_area.top + view->data_area.height * 1/3) {
	if (mouse_move_pages_viewer)
	    view_move_up (view, view->data_area.height / 2);
	else
	    view_move_up (view, 1);
	goto processed;
    } else if (y < view->data_area.top + view->data_area.height * 2/3) {
	/* ignore the click */
    } else {
	if (mouse_move_pages_viewer)
	    view_move_down (view, view->data_area.height / 2);
	else
	    view_move_down (view, 1);
	goto processed;
    }

    return 0;

  processed:
    *result = MOU_REPEAT;
    return 1;
}

/* Real view only */
static int
real_view_event (Gpm_Event *event, void *x)
{
    WView *view = (WView *) x;
    int result;

    if (view_event (view, event, &result))
	view_update (view);
    return result;
}

static void
view_adjust_size (Dlg_head *h)
{
    WView *view;
    WButtonBar *bar;

    /* Look up the viewer and the buttonbar, we assume only two widgets here */
    view = (WView *) find_widget_type (h, view_callback);
    bar = find_buttonbar (h);
    widget_set_size (&view->widget, 0, 0, LINES - 1, COLS);
    widget_set_size ((Widget *) bar, LINES - 1, 0, 1, COLS);

    view_compute_areas (view);
    view_update_bytes_per_line (view);
}

/* Callback for the view dialog */
static cb_ret_t
view_dialog_callback (Dlg_head *h, dlg_msg_t msg, int parm)
{
    switch (msg) {
    case DLG_RESIZE:
	view_adjust_size (h);
	return MSG_HANDLED;

    default:
	return default_dlg_callback (h, msg, parm);
    }
}

/* {{{ External interface }}} */

/* Real view only */
int
mc_internal_viewer (const char *command, const char *file,
	int *move_dir_p, int start_line)
{
    gboolean succeeded;
    WView *wview;
    WButtonBar *bar;
    Dlg_head *view_dlg;

    /* Create dialog and widgets, put them on the dialog */
    view_dlg =
	create_dlg (0, 0, LINES, COLS, NULL, view_dialog_callback,
		    "[Internal File Viewer]", NULL, DLG_WANT_TAB);

    wview = view_new (0, 0, COLS, LINES - 1, 0);

    bar = buttonbar_new (1);

    add_widget (view_dlg, bar);
    add_widget (view_dlg, wview);

    succeeded = view_load (wview, command, file, start_line);
    if (succeeded) {
	run_dlg (view_dlg);
	if (move_dir_p)
	    *move_dir_p = wview->move_dir;
    } else {
	if (move_dir_p)
	    *move_dir_p = 0;
    }
    destroy_dlg (view_dlg);

    return succeeded;
}

/* {{{ Miscellaneous functions }}} */

static void
view_hook (void *v)
{
    WView *view = (WView *) v;
    WPanel *panel;

    /* If the user is busy typing, wait until he finishes to update the
       screen */
    if (!is_idle ()) {
	if (!hook_present (idle_hook, view_hook))
	    add_hook (&idle_hook, view_hook, v);
	return;
    }

    delete_hook (&idle_hook, view_hook);

    if (get_current_type () == view_listing)
	panel = current_panel;
    else if (get_other_type () == view_listing)
	panel = other_panel;
    else
	return;

    view_load (view, 0, panel->dir.list[panel->selected].fname, 0);
    display (view);
}

/* {{{ Event handling }}} */

static cb_ret_t
view_callback (Widget *w, widget_msg_t msg, int parm)
{
    WView *view = (WView *) w;
    cb_ret_t i;
    Dlg_head *h = view->widget.parent;

    view_compute_areas (view);
    view_update_bytes_per_line (view);

    switch (msg) {
    case WIDGET_INIT:
	if (view_is_in_panel (view))
	    add_hook (&select_file_hook, view_hook, view);
	else
	    view->dpy_bbar_dirty = TRUE;
	return MSG_HANDLED;

    case WIDGET_DRAW:
	display (view);
	return MSG_HANDLED;

    case WIDGET_CURSOR:
	if (view->hex_mode)
	    view_place_cursor (view);
	return MSG_HANDLED;

    case WIDGET_KEY:
	i = view_handle_key ((WView *) view, parm);
	if (view->want_to_quit && !view_is_in_panel (view))
	    dlg_stop (h);
	else {
	    view_update (view);
	}
	return i;

    case WIDGET_FOCUS:
	view->dpy_bbar_dirty = TRUE;
	view_update (view);
	return MSG_HANDLED;

    case WIDGET_DESTROY:
	view_done (view);
	if (view_is_in_panel (view))
	    delete_hook (&select_file_hook, view_hook);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

/* {{{ External interface }}} */

WView *
view_new (int y, int x, int cols, int lines, int is_panel)
{
    WView *view = g_new0 (WView, 1);
    size_t i;

    init_widget (&view->widget, y, x, lines, cols,
		 view_callback,
		 real_view_event);

    view->filename          = NULL;
    view->command           = NULL;

    view_set_datasource_none (view);

    view->growbuf_in_use    = FALSE;
    /* leave the other growbuf fields uninitialized */

    view->hex_mode = FALSE;
    view->hexedit_mode = FALSE;
    view->hexview_in_text = FALSE;
    view->text_nroff_mode = FALSE;
    view->text_wrap_mode = FALSE;
    view->magic_mode = FALSE;

    view->hexedit_lownibble = FALSE;
    view->coord_cache       = NULL;

    view->dpy_frame_size    = is_panel ? 1 : 0;
    view->dpy_start = 0;
    view->dpy_text_column   = 0;
    view->dpy_end= 0;
    view->hex_cursor        = 0;
    view->cursor_col        = 0;
    view->cursor_row        = 0;
    view->change_list       = NULL;

    /* {status,ruler,data}_area are left uninitialized */

    view->dirty             = 0;
    view->dpy_bbar_dirty = TRUE;
    view->bytes_per_line    = 1;

    view->search_start      = 0;
    view->search_length     = 0;
    view->search_exp        = NULL;
    view->direction         = 1; /* forward */
    view->last_search       = 0; /* it's a function */

    view->want_to_quit      = FALSE;
    view->marker            = 0;
    for (i = 0; i < sizeof(view->marks) / sizeof(view->marks[0]); i++)
	view->marks[i] = 0;

    view->move_dir          = 0;
    view->update_steps      = 0;
    view->update_activate   = 0;

    if (default_hex_mode)
	view_toggle_hex_mode (view);
    if (default_nroff_flag)
	view_toggle_nroff_mode (view);
    if (global_wrap_mode)
	view_toggle_wrap_mode (view);
    if (default_magic_flag)
	view_toggle_magic_mode (view);

    return view;
}

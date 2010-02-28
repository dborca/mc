/* Hypertext file browser.
   Copyright (C) 1994, 1995 Miguel de Icaza.
   Copyright (C) 1994, 1995 Janne Kukonlehto
   
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

/*
   Implements the hypertext file viewer.
   The hypertext file is a file that may have one or more nodes.  Each
   node ends with a ^D character and starts with a bracket, then the
   name of the node and then a closing bracket. Right after the closing
   bracket a newline is placed. This newline is not to be displayed by
   the help viewer and must be skipped - its sole purpose is to faciliate
   the work of the people managing the help file template (xnc.hlp) .

   Links in the hypertext file are specified like this: the text that
   will be highlighted should have a leading ^A, then it comes the
   text, then a ^B indicating that highlighting is done, then the name
   of the node you want to link to and then a ^C.

   The file must contain a ^D at the beginning and at the end of the
   file or the program will not be able to detect the end of file.

   Lazyness/widgeting attack: This file does use the dialog manager
   and uses mainly the dialog to achieve the help work.  there is only
   one specialized widget and it's only used to forward the mouse messages
   to the appropiate routine.
   
*/

#include <config.h>

#include <errno.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "global.h"
#include "tty.h"
#include "color.h"
#include "win.h"
#include "mouse.h"
#include "key.h"		/* For mi_getch() */
#include "help.h"
#include "dialog.h"		/* For Dlg_head */
#include "widget.h"		/* For Widget */
#include "wtools.h"		/* For common_dialog_repaint() */

#define MAXLINKNAME 80
#define HISTORY_SIZE 20
#define HELP_WINDOW_WIDTH (HELP_TEXT_WIDTH + 4)

#define STRING_LINK_START	"\01"
#define STRING_LINK_POINTER	"\02"
#define STRING_LINK_END		"\03"
#define STRING_NODE_END		"\04"

static char *data;		/* Pointer to the loaded data file */
static int help_lines;		/* Lines in help viewer */
static int  history_ptr;	/* For the history queue */
static const char *main_node;	/* The main node */
static const char *last_shown = NULL;	/* Last byte shown in a screen */
static int end_of_node = 0;	/* Flag: the last character of the node shown? */
static const char *currentpoint;
static const char *selected_item;

/* The widget variables */
static Dlg_head *whelp;

static struct {
    const char *page;		/* Pointer to the selected page */
    const char *link;		/* Pointer to the selected link */
} history [HISTORY_SIZE];

/* Link areas for the mouse */
typedef struct Link_Area {
    int x1, y1, x2, y2;
    const char *link_name;
    struct Link_Area *next;
} Link_Area;

static Link_Area *link_area = NULL;
static int inside_link_area = 0;

static cb_ret_t help_callback (struct Dlg_head *h, dlg_msg_t, int parm);

#ifdef HAS_ACS_AS_PCCHARS
static const struct {
    int acscode;
    int pccode;
} acs2pc_table [] = {
    { 'q',  0xC4 },
    { 'x',  0xB3 },
    { 'l',  0xDA },
    { 'k',  0xBF },
    { 'm',  0xC0 },
    { 'j',  0xD9 },
    { 'a',  0xB0 },
    { 'u',  0xB4 },
    { 't',  0xC3 },
    { 'w',  0xC2 },
    { 'v',  0xC1 },
    { 'n',  0xC5 },
    { 0, 0 } };

static int acs2pc (int acscode)
{
    int i;

    for (i = 0; acs2pc_table[i].acscode != 0; i++)
	if (acscode == acs2pc_table[i].acscode) {
	    return acs2pc_table[i].pccode;
	}
    return 0;
}
#endif

/* returns the position where text was found in the start buffer */
/* or 0 if not found */
static const char *
search_string (const char *start, const char *text)
{
    const char *result = NULL;
    char *local_text = g_strdup (text);
    char *d = local_text;
    const char *e = start;

    /* fmt sometimes replaces a space with a newline in the help file */
    /* Replace the newlines in the link name with spaces to correct the situation */
    while (*d){
	if (*d == '\n')
	    *d = ' ';
	d++;
    }
    /* Do search */
    for (d = local_text; *e; e++){
	if (*d == *e)
	    d++;
	else
	    d = local_text;
	if (!*d) {
	    result = e + 1;
	    goto cleanup;
	}
    }
cleanup:
    g_free (local_text);
    return result;
}

/* Searches text in the buffer pointed by start.  Search ends */
/* if the CHAR_NODE_END is found in the text.  Returns 0 on failure */
static const char *search_string_node (const char *start, const char *text)
{
    const char *d = text;
    const char *e = start;

    if (!start)
	return 0;
    
    for (; *e && *e != CHAR_NODE_END; e++){
	if (*d == *e)
	    d++;
	else
	    d = text;
	if (!*d)
	    return e+1;
    }
    return 0;
}

/* Searches the_char in the buffer pointer by start and searches */
/* it can search forward (direction = 1) or backward (direction = -1) */
static const char *search_char_node (const char *start, char the_char, int direction)
{
    const char *e;

    e = start;
    
    for (; *e && (*e != CHAR_NODE_END); e += direction){
	if (*e == the_char)
	    return e;
    }
    return 0;
}

/* Returns the new current pointer when moved lines lines */
static const char *move_forward2 (const char *c, int lines)
{
    const char *p;
    int  line;

    currentpoint = c;
    for (line = 0, p = currentpoint; *p && *p != CHAR_NODE_END; p++){
	if (line == lines)
	    return currentpoint = p;
	if (*p == '\n')
	    line++;
    }
    return currentpoint = c;
}

static const char *move_backward2 (const char *c, int lines)
{
    const char *p;
    int line;

    currentpoint = c;
    for (line = 0, p = currentpoint; *p && p >= data; p--){
	if (*p == CHAR_NODE_END)
	{
	    /* We reached the beginning of the node */
	    /* Skip the node headers */
	    while (*p != ']') p++;
	    return currentpoint = p + 2; /* Skip the newline following the start of the node */
	}
	if (*(p - 1) == '\n')
	    line++;
	if (line == lines)
	    return currentpoint = p;
    }
    return currentpoint = c;
}

static void move_forward (int i)
{
    if (end_of_node)
	return;
    currentpoint = move_forward2 (currentpoint, i);
}

static void move_backward (int i)
{
    currentpoint = move_backward2 (currentpoint, ++i);
}

static void move_to_top (void)
{
    while (currentpoint > data && *currentpoint != CHAR_NODE_END)
	currentpoint--;
    while (*currentpoint != ']')
	currentpoint++;
    currentpoint = currentpoint + 2; /* Skip the newline following the start of the node */
    selected_item = NULL;
}

static void move_to_bottom (void)
{
    while (*currentpoint && *currentpoint != CHAR_NODE_END)
	currentpoint++;
    currentpoint--;
    move_backward (help_lines - 1);
}

static const char *help_follow_link (const char *start, const char *selected_item)
{
    char link_name [MAXLINKNAME];
    const char *p;
    int  i = 0;

    if (!selected_item)
	return start;
    
    for (p = selected_item; *p && *p != CHAR_NODE_END && *p != CHAR_LINK_POINTER; p++)
	;
    if (*p == CHAR_LINK_POINTER){
	link_name [0] = '[';
	for (i = 1; *p != CHAR_LINK_END && *p && *p != CHAR_NODE_END && i < MAXLINKNAME-3; )
	    link_name [i++] = *++p;
	link_name [i-1] = ']';
	link_name [i] = 0;
	p = search_string (data, link_name);
	if (p) {
	    p += 1; /* Skip the newline following the start of the node */
	    return p;
	}
    }

    /* Create a replacement page with the error message */
    return _(" Help file format error\n");
}

static const char *select_next_link (const char *current_link)
{
    const char *p;

    if (!current_link)
	return 0;

    p = search_string_node (current_link, STRING_LINK_END);
    if (!p)
	return NULL;
    p = search_string_node (p, STRING_LINK_START);
    if (!p)
	return NULL;
    return p - 1;
}

static const char *select_prev_link (const char *current_link)
{
    if (!current_link)
	return 0;

    return search_char_node (current_link - 1, CHAR_LINK_START, -1);
}

static void start_link_area (int x, int y, const char *link_name)
{
    Link_Area *new;

    if (inside_link_area)
	message (0, _("Warning"), _(" Internal bug: Double start of link area "));

    /* Allocate memory for a new link area */
    new = g_new (Link_Area, 1);
    new->next = link_area;
    link_area = new;

    /* Save the beginning coordinates of the link area */
    link_area->x1 = x;
    link_area->y1 = y;

    /* Save the name of the destination anchor */
    link_area->link_name = link_name;

    inside_link_area = 1;
}

static void end_link_area (int x, int y)
{
    if (inside_link_area){
	/* Save the end coordinates of the link area */
	link_area->x2 = x;
	link_area->y2 = y;

	inside_link_area = 0;
    }
}

static void clear_link_areas (void)
{
    Link_Area *current;

    while (link_area){
	current = link_area;
	link_area = current -> next;
	g_free (current);
    }
    inside_link_area = 0;
}

static void help_show (Dlg_head *h, const char *paint_start)
{
    const char *p;
    int  col, line, c;
    int  painting = 1;
    int acs;			/* Flag: Alternate character set active? */
    int repeat_paint;
    int active_col, active_line;/* Active link position */

    attrset (HELP_NORMAL_COLOR);
    do {
	
	line = col = acs = active_col = active_line = repeat_paint = 0;
    
	clear_link_areas ();
	if (selected_item < paint_start)
	    selected_item = NULL;
	
	for (p = paint_start; *p && *p != CHAR_NODE_END && line < help_lines; p++) {
	    c = (unsigned char)*p;
	    switch (c){
	    case CHAR_LINK_START:
		if (selected_item == NULL)
		    selected_item = p;
		if (p == selected_item){
		    attrset (HELP_SLINK_COLOR);

		    /* Store the coordinates of the link */
		    active_col = col + 2;
		    active_line = line + 2;
		}
		else
		    attrset (HELP_LINK_COLOR);
		start_link_area (col, line, p);
		break;
	    case CHAR_LINK_POINTER:
		painting = 0;
		end_link_area (col - 1, line);
		break;
	    case CHAR_LINK_END:
		painting = 1;
		attrset (HELP_NORMAL_COLOR);
		break;
	    case CHAR_ALTERNATE:
		acs = 1;
		break;
	    case CHAR_NORMAL:
		acs = 0;
		break;
	    case CHAR_VERSION:
		dlg_move (h, line+2, col+2);
		addstr (VERSION);
		col += strlen (VERSION);
		break;
	    case CHAR_FONT_BOLD:
		attrset (HELP_BOLD_COLOR);
		break;
	    case CHAR_FONT_ITALIC:
		attrset (HELP_ITALIC_COLOR);
		break;
	    case CHAR_FONT_NORMAL:
		attrset (HELP_NORMAL_COLOR);
		break;
	    case '\n':
		line++;
		col = 0;
		break;
	    case '\t':
		col = (col/8 + 1) * 8;
		break;
	    default:
		if (!painting)
		    continue;
		if (col > HELP_WINDOW_WIDTH-1)
		    continue;
		
		dlg_move (h, line+2, col+2);
		if (acs){
		    if (c == ' ' || c == '.')
			addch (c);
		    else
#ifndef HAVE_SLANG
			addch (acs_map [c]);
#else
			SLsmg_draw_object (h->y + line + 2, h->x + col + 2, c);
#endif
		} else
		    addch (c);
		col++;
		break;
	    }
	}
	last_shown = p;
	end_of_node = line < help_lines;
	attrset (HELP_NORMAL_COLOR);
	if (selected_item >= last_shown){
	    if (link_area != NULL){
		selected_item = link_area->link_name;
		repeat_paint = 1;
	    }
	    else
		selected_item = NULL;
	}
    } while (repeat_paint);

    /* Position the cursor over a nice link */
    if (active_col)
	dlg_move (h, active_line, active_col);
}

static int
help_event (Gpm_Event *event, void *vp)
{
    Widget *w = vp;
    Link_Area *current_area;

    if (! (event->type & GPM_UP))
	return 0;

    /* The event is relative to the dialog window, adjust it: */
    event->x -= 2;
    event->y -= 2;

    if (event->buttons & GPM_B_RIGHT){
	currentpoint = history [history_ptr].page;
	selected_item = history [history_ptr].link;
	history_ptr--;
	if (history_ptr < 0)
	    history_ptr = HISTORY_SIZE-1;
	
	help_callback (w->parent, DLG_DRAW, 0);
	return 0;
    }

    /* Test whether the mouse click is inside one of the link areas */
    current_area = link_area;
    while (current_area)
    {
	/* Test one line link area */
	if (event->y == current_area->y1 && event->x >= current_area->x1 &&
	    event->y == current_area->y2 && event->x <= current_area->x2)
	    break;
	/* Test two line link area */
	if (current_area->y1 + 1 == current_area->y2){
	    /* The first line */
	    if (event->y == current_area->y1 && event->x >= current_area->x1)
		break;
	    /* The second line */
	    if (event->y == current_area->y2 && event->x <= current_area->x2)
		break;
	}
	/* Mouse will not work with link areas of more than two lines */

	current_area = current_area -> next;
    }

    /* Test whether a link area was found */
    if (current_area){
	/* The click was inside a link area -> follow the link */
	history_ptr = (history_ptr+1) % HISTORY_SIZE;
	history [history_ptr].page = currentpoint;
	history [history_ptr].link = current_area->link_name;
	currentpoint = help_follow_link (currentpoint, current_area->link_name);
	selected_item = NULL;
    } else{
	if (event->y < 0)
	    move_backward (help_lines - 1);
	else if (event->y >= help_lines)
	    move_forward (help_lines - 1);
	else if (event->y < help_lines/2)
	    move_backward (1);
	else
	    move_forward (1);
    }

    /* Show the new node */
    help_callback (w->parent, DLG_DRAW, 0);

    return 0;
}

/* show help */
static void
help_help_cmd (void *vp)
{
    Dlg_head *h = vp;
    const char *p;

    history_ptr = (history_ptr+1) % HISTORY_SIZE;
    history [history_ptr].page = currentpoint;
    history [history_ptr].link = selected_item;

    p = search_string(data, "[How to use help]");
    if (p == NULL)
	return;

    currentpoint = p + 1; /* Skip the newline following the start of the node */
    selected_item = NULL;
    help_callback (h, DLG_DRAW, 0);
}

static void
help_index_cmd (void *vp)
{
    Dlg_head *h = vp;
    const char *new_item;

    if (!(new_item = search_string (data, "[Contents]"))) {
	message (1, MSG_ERROR, _(" Cannot find node %s in help file "),
		 "[Contents]");
	return;
    }

    history_ptr = (history_ptr + 1) % HISTORY_SIZE;
    history[history_ptr].page = currentpoint;
    history[history_ptr].link = selected_item;

    currentpoint = new_item + 1; /* Skip the newline following the start of the node */
    selected_item = NULL;
    help_callback (h, DLG_DRAW, 0);
}

static void help_quit_cmd (void *vp)
{
    dlg_stop ((Dlg_head *) vp);
}

static void prev_node_cmd (void *vp)
{
    Dlg_head *h = vp;
    currentpoint = history [history_ptr].page;
    selected_item = history [history_ptr].link;
    history_ptr--;
    if (history_ptr < 0)
	history_ptr = HISTORY_SIZE-1;
    
    help_callback (h, DLG_DRAW, 0);
}

static cb_ret_t
md_callback (Widget *w, widget_msg_t msg, int parm)
{
    (void) w;
    return default_proc (msg, parm);
}

static Widget *
mousedispatch_new (int y, int x, int yl, int xl)
{
    Widget *w = g_new (Widget, 1);

    init_widget (w, y, x, yl, xl, md_callback, help_event);

    return w;
}

static void help_cmk_move_backward(void *vp, int lines) {
    (void) &vp;
    move_backward(lines);
}
static void help_cmk_move_forward(void *vp, int lines) {
    (void) &vp;
    move_forward(lines);
}
static void help_cmk_moveto_top(void *vp, int lines) {
    (void) &vp;
    (void) &lines;
    move_to_top();
}
static void help_cmk_moveto_bottom(void *vp, int lines) {
    (void) &vp;
    (void) &lines;
    move_to_bottom();
}

static cb_ret_t
help_handle_key (struct Dlg_head *h, int c)
{
    const char *new_item;

    if (c != KEY_UP && c != KEY_DOWN &&
	check_movement_keys (c, help_lines, NULL,
			     help_cmk_move_backward,
			     help_cmk_move_forward,
			     help_cmk_moveto_top,
			     help_cmk_moveto_bottom)) {
	/* Nothing */;
    } else switch (c){
    case 'l':
    case KEY_LEFT:
	prev_node_cmd (h);
	break;
	
    case '\n':
    case KEY_RIGHT:
	/* follow link */
	if (!selected_item){
#ifdef WE_WANT_TO_GO_BACKWARD_ON_KEY_RIGHT
	    /* Is there any reason why the right key would take us
	     * backward if there are no links selected?, I agree
	     * with Torben than doing nothing in this case is better
	     */
	    /* If there are no links, go backward in history */
	    history_ptr--;
	    if (history_ptr < 0)
		history_ptr = HISTORY_SIZE-1;
	    
	    currentpoint = history [history_ptr].page;
	    selected_item   = history [history_ptr].link;
#endif
	} else {
	    history_ptr = (history_ptr+1) % HISTORY_SIZE;
	    history [history_ptr].page = currentpoint;
	    history [history_ptr].link = selected_item;
	    currentpoint = help_follow_link (currentpoint, selected_item);
	}
	selected_item = NULL;
	break;
	
    case KEY_DOWN:
    case '\t':
	new_item = select_next_link (selected_item);
	if (new_item){
	    selected_item = new_item;
	    if (selected_item >= last_shown){
		if (c == KEY_DOWN)
		    move_forward (1);
		else
		    selected_item = NULL;
	    }
	} else if (c == KEY_DOWN)
	    move_forward (1);
	else
	    selected_item = NULL;
	break;
	
    case KEY_UP:
    case ALT ('\t'):
	/* select previous link */
	new_item = select_prev_link (selected_item);
	selected_item = new_item;
	if (selected_item == NULL || selected_item < currentpoint) {
	    if (c == KEY_UP)
		move_backward (1);
	    else{
		if (link_area != NULL)
		    selected_item = link_area->link_name;
		else
		    selected_item = NULL;
	    }
	}
	break;
	
    case 'n':
	/* Next node */
	new_item = currentpoint;
	while (*new_item && *new_item != CHAR_NODE_END)
	    new_item++;
	if (*++new_item == '['){
	    while (*++new_item) {
		if (*new_item == ']' && *++new_item && *++new_item) {
		    currentpoint = new_item;
		    selected_item = NULL;
		    break;
		}
	    }
	}
	break;
	
    case 'p':
	/* Previous node */
	new_item = currentpoint;
	while (new_item > data + 1 && *new_item != CHAR_NODE_END)
	    new_item--;
	new_item--;
	while (new_item > data && *new_item != CHAR_NODE_END)
	    new_item--;
	while (*new_item != ']')
	    new_item++;
	currentpoint = new_item + 2;
	selected_item = NULL;
	break;
	
    case 'c':
	help_index_cmd (h);
	break;
	
    case ESC_CHAR:
    case XCTRL('g'):
	dlg_stop (h);
	break;

    default:
	return MSG_NOT_HANDLED;
	    
    }
    help_callback (h, DLG_DRAW, 0);
    return MSG_HANDLED;
}

static cb_ret_t
help_callback (struct Dlg_head *h, dlg_msg_t msg, int parm)
{
    switch (msg) {
    case DLG_DRAW:
	common_dialog_repaint (h);
	help_show (h, currentpoint);
	return MSG_HANDLED;

    case DLG_KEY:
	return help_handle_key (h, parm);

    default:
	return default_dlg_callback (h, msg, parm);
    }
}

static void
interactive_display_finish (void)
{
    clear_link_areas ();
    g_free (data);
}

void
interactive_display (const char *filename, const char *node)
{
    WButtonBar *help_bar;
    Widget *md;
    char *hlpfile = NULL;

    if (filename)
	data = load_file (filename);
    else
	data = load_mc_home_file ("mc.hlp", &hlpfile);

    if (data == NULL) {
	message (1, MSG_ERROR, _(" Cannot open file %s \n %s "), filename ? filename : hlpfile,
		 unix_error_string (errno));
    }

    if (!filename)
	g_free (hlpfile);

    if (!data)
	return;

    if (!node || !*node)
	node = "[main]";

    if (!(main_node = search_string (data, node))) {
	message (1, MSG_ERROR, _(" Cannot find node %s in help file "),
		 node);

	/* Fallback to [main], return if it also cannot be found */
	main_node = search_string (data, "[main]");
	if (!main_node) {
	    interactive_display_finish ();
	    return;
	}
    }

    help_lines = min (LINES - 4, max (2 * LINES / 3, 18));

    whelp =
	create_dlg (0, 0, help_lines + 4, HELP_WINDOW_WIDTH + 4,
		    dialog_colors, help_callback, "[Help]", _("Help"),
		    DLG_TRYUP | DLG_CENTER | DLG_WANT_TAB);

    selected_item = search_string_node (main_node, STRING_LINK_START) - 1;
    currentpoint = main_node + 1; /* Skip the newline following the start of the node */

    for (history_ptr = HISTORY_SIZE; history_ptr;) {
	history_ptr--;
	history[history_ptr].page = currentpoint;
	history[history_ptr].link = selected_item;
    }

    help_bar = buttonbar_new (1);
    ((Widget *) help_bar)->y -= whelp->y;
    ((Widget *) help_bar)->x -= whelp->x;

    md = mousedispatch_new (1, 1, help_lines, HELP_WINDOW_WIDTH - 2);

    add_widget (whelp, md);
    add_widget (whelp, help_bar);

    buttonbar_set_label_data (whelp, 1, _("Help"), help_help_cmd, whelp);
    buttonbar_set_label_data (whelp, 2, _("Index"), help_index_cmd, whelp);
    buttonbar_set_label_data (whelp, 3, _("Prev"), prev_node_cmd, whelp);
    buttonbar_clear_label (whelp, 4);
    buttonbar_clear_label (whelp, 5);
    buttonbar_clear_label (whelp, 6);
    buttonbar_clear_label (whelp, 7);
    buttonbar_clear_label (whelp, 8);
    buttonbar_clear_label (whelp, 9);
    buttonbar_set_label_data (whelp, 10, _("Quit"), help_quit_cmd, whelp);

    run_dlg (whelp);
    interactive_display_finish ();
    destroy_dlg (whelp);
}

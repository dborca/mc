/* editor initialisation and callback handler.

   Copyright (C) 1996, 1997 the Free Software Foundation

   Authors: 1996, 1997 Paul Sheer

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
 */

#include <config.h>

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#endif
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

#include <stdlib.h>

#include "../src/global.h"

#include "edit.h"
#include "edit-widget.h"

#include "../src/tty.h"		/* LINES */
#include "../src/widget.h"	/* buttonbar_redraw() */
#include "../src/menu.h"	/* menubar_new() */
#include "../src/key.h"		/* is_idle() */

#ifdef USE_DLGSWITCH
#include "../src/dlgswitch.h"
#endif

WEdit *wedit;

int column_highlighting = 0;

static cb_ret_t edit_callback (Widget *, widget_msg_t msg, int parm);

static int
edit_event (WEdit * edit, Gpm_Event * event, int *result)
{
    *result = MOU_NORMAL;
    if (edit->in_panel && !(edit->widget.options & W_WANT_CURSOR)) {
	dlg_select_widget (edit);
	if (event->buttons & (GPM_B_UP|GPM_B_DOWN)) {
	    return 0; /* Ignore first wheel after gaining focus */
	}
    }
    edit_update_curs_row (edit);
    edit_update_curs_col (edit);

    /* Unknown event type */
    if (!(event->type & (GPM_DOWN | GPM_DRAG | GPM_UP)))
	return 0;

    /* Wheel events */
    if ((event->buttons & GPM_B_UP) && (event->type & GPM_DOWN)) {
	edit_move_up (edit, 2, 1);
	goto update;
    }
    if ((event->buttons & GPM_B_DOWN) && (event->type & GPM_DOWN)) {
	edit_move_down (edit, 2, 1);
	goto update;
    }

    /* Outside editor window */
    if (event->y <= 1 || event->x <= 0
	|| event->x > edit->num_widget_columns
	|| event->y > edit->num_widget_lines + 1)
	return 0;

    /* A lone up mustn't do anything */
    if (edit->mark2 != -1 && event->type & (GPM_UP | GPM_DRAG))
	return 1;

    if (event->type & (GPM_DOWN | GPM_UP))
	edit_push_key_press (edit);

    edit->prev_col = event->x - edit->start_col - 1;

    if (--event->y > (edit->curs_row + 1))
	edit_move_down (edit, event->y - (edit->curs_row + 1), 0);
    else if (event->y < (edit->curs_row + 1))
	edit_move_up (edit, (edit->curs_row + 1) - event->y, 0);
    else
	edit_move_to_prev_col (edit, edit_bol (edit, edit->curs1));

    if (event->type & GPM_DOWN) {
	edit_mark_cmd (edit, 1);	/* reset */
	edit->highlight = 0;
    }

    if (!(event->type & GPM_DRAG))
	edit_mark_cmd (edit, 0);

  update:
    edit_find_bracket (edit);
    edit->force |= REDRAW_COMPLETELY;
    edit_update_curs_row (edit);
    edit_update_curs_col (edit);
    edit_update_screen (edit);

    return 1;
}


static int
edit_mouse_event (Gpm_Event *event, void *x)
{
    int result;
    struct WMenu *edit_menubar = ((WEdit *)x)->menubar;
    if (edit_event ((WEdit *) x, event, &result))
	return result;
    else if (!edit_menubar)
	return MOU_NORMAL;
    else
	return (*edit_menubar->widget.mouse) (event, edit_menubar);
}

static void
edit_adjust_size (Dlg_head *h)
{
    WEdit *edit;
    WButtonBar *edit_bar;
    struct WMenu *edit_menubar;

    edit = (WEdit *) find_widget_type (h, edit_callback);
    edit_bar = find_buttonbar (h);
    edit_menubar = edit->menubar;

    widget_set_size (&edit->widget, 0, 0, LINES - 1, COLS);
    widget_set_size ((Widget *) edit_bar, LINES - 1, 0, 1, COLS);
    widget_set_size (&edit_menubar->widget, 0, 0, 1, COLS);

#ifdef RESIZABLE_MENUBAR
    menubar_arrange (edit_menubar);
#endif
}

/* Callback for the edit dialog */
static cb_ret_t
edit_dialog_callback (Dlg_head *h, dlg_msg_t msg, int parm)
{
    WEdit *edit;

    switch (msg) {
    case DLG_RESIZE:
	edit_adjust_size (h);
	return MSG_HANDLED;

    case DLG_VALIDATE:
	edit = (WEdit *) find_widget_type (h, edit_callback);
	if (!edit_ok_to_exit (edit)) {
	    h->running = 1;
	}
	return MSG_HANDLED;

    default:
	return default_dlg_callback (h, msg, parm);
    }
}

#ifdef USE_DLGSWITCH
int
edit_file_modified (WEdit *wedit)
{
    return wedit->modified;
}

void
edit_finish_editor(void *edit_dlg, WEdit *wedit)
{
    edit_done_menu (wedit->menubar);	/* editmenu.c */
    destroy_dlg (edit_dlg);
    dlgswitch_remove_current();
}

void
edit_run_editor(void *dlg, WEdit *widget)
{
    Dlg_head *edit_dlg = dlg;
    WEdit *wold = wedit;
    wedit = widget;
    /* XXX There are a lot of globals around here. If we came here from the dialog switcher -- not from edit_file(),
     * take note that some of those globals may have changed while we were backgrounded. Get rid of all those globals
     * and put them inside WEdit.
     */

    run_dlg (edit_dlg);
    if (!edit_dlg->soft_exit) {
	edit_finish_editor(edit_dlg, wedit);
    }
    wedit = wold;
}
#endif

WEdit *
edit_new(int y, int x, int lines, int cols, const char *_file, int line, int in_panel)
{
    WEdit *edit = edit_init (NULL, lines - 2, cols, _file, line);
    if (edit) {
        init_widget (&(edit->widget), y, x, lines - 1, cols, edit_callback, edit_mouse_event);
        if (in_panel) {
            edit->in_panel = 1;
            widget_want_cursor (edit->widget, 0);
        }
        wedit = edit;
    }
    return edit;
}

int
edit_file (const char *_file, int line)
{
    static int made_directory = 0;
    Dlg_head *edit_dlg;
    WButtonBar *edit_bar;
    WEdit *wold = wedit;

    if (!made_directory) {
	char *dir = concat_dir_and_file (home_dir, EDIT_DIR);
	made_directory = (mkdir (dir, 0700) != -1 || errno == EEXIST);
	g_free (dir);
    }

    if (!edit_new(0, 0, LINES, COLS, _file, line, 0)) {
	return 0;
    }

    /* Create a new dialog and add it widgets to it */
    edit_dlg =
	create_dlg (0, 0, LINES, COLS, NULL, edit_dialog_callback,
		    "[Internal File Editor]", NULL, DLG_WANT_TAB | DLG_SWITCHABLE);

    widget_want_cursor (wedit->widget, 1);

    edit_bar = buttonbar_new (1);

    wedit->menubar = edit_init_menu (wedit);

    add_widget (edit_dlg, edit_bar);
    add_widget (edit_dlg, wedit);
    add_widget (edit_dlg, wedit->menubar);

#ifdef USE_DLGSWITCH
    dlgswitch_add(edit_dlg, DLG_TYPE_EDIT, _file, wedit);
    edit_run_editor(edit_dlg, wedit);
#else
    run_dlg (edit_dlg);

    edit_done_menu (wedit->menubar);		/* editmenu.c */

    destroy_dlg (edit_dlg);
#endif

    wedit = wold;
    return 1;
}

static void edit_my_define (Dlg_head * h, int idx, const char *text,
			    void (*fn) (WEdit *), WEdit * edit)
{
    text = edit->labels[idx - 1]? edit->labels[idx - 1] : text;
    /* function-cast ok */
    buttonbar_set_label_data (h, idx, text, (buttonbarfn) fn, edit);
}


static void cmd_F1 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (1));
}

static void cmd_F2 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (2));
}

static void cmd_F3 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (3));
}

static void cmd_F4 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (4));
}

static void cmd_F5 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (5));
}

static void cmd_F6 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (6));
}

static void cmd_F7 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (7));
}

static void cmd_F8 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (8));
}

#if 0
static void cmd_F9 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (9));
}
#endif

static void cmd_F10 (WEdit * edit)
{
    send_message ((Widget *) edit, WIDGET_KEY, KEY_F (10));
}

static void
edit_labels (WEdit *edit)
{
    Dlg_head *h = edit->widget.parent;

    edit_my_define (h, 1, _("Help"), cmd_F1, edit);
    edit_my_define (h, 2, _("Save"), cmd_F2, edit);
    edit_my_define (h, 3, _("Mark"), cmd_F3, edit);
    edit_my_define (h, 4, _("Replac"), cmd_F4, edit);
    edit_my_define (h, 5, _("Copy"), cmd_F5, edit);
    edit_my_define (h, 6, _("Move"), cmd_F6, edit);
    edit_my_define (h, 7, _("Search"), cmd_F7, edit);
    edit_my_define (h, 8, _("Delete"), cmd_F8, edit);
    if (!edit->in_panel) { /* don't override the key to access the main menu */
	edit_my_define (h, 9, _("PullDn"), edit_menu_cmd, edit);
	edit_my_define (h, 10, _("Quit"), cmd_F10, edit);
    }

    buttonbar_redraw (h);
}

void edit_update_screen (WEdit * e)
{
    edit_scroll_screen_over_cursor (e);

    edit_update_curs_col (e);
    edit_status (e);

/* pop all events for this window for internal handling */

    if (!is_idle ()) {
	e->force |= REDRAW_PAGE;
	return;
    }
    if (e->force & REDRAW_COMPLETELY)
	e->force |= REDRAW_PAGE;
    edit_render_keypress (e);
}

static cb_ret_t
edit_callback (Widget *w, widget_msg_t msg, int parm)
{
    WEdit *e = (WEdit *) w;

    switch (msg) {
    case WIDGET_INIT:
	e->force |= REDRAW_COMPLETELY;
	if (e->in_panel) {
	    return MSG_HANDLED;
	}
	edit_labels (e);
	return MSG_HANDLED;

    case WIDGET_DRAW:
	e->force |= REDRAW_COMPLETELY;
	e->num_widget_lines = w->lines - 1;
	e->num_widget_columns = w->cols;
	/* fallthrough */

    case WIDGET_FOCUS:
	if (e->in_panel && msg == WIDGET_FOCUS) {
	    widget_want_cursor (e->widget, 1);
	    edit_labels(e);
	}
	edit_update_screen (e);
	return MSG_HANDLED;

    case WIDGET_KEY:
	{
	    int cmd, ch;

	    /* The user may override the access-keys for the menu bar. */
	    if (edit_translate_key (e, parm, &cmd, &ch)) {
		edit_execute_key_command (e, cmd, ch);
		edit_update_screen (e);
		return MSG_HANDLED;
	    } else  if (edit_drop_hotkey_menu (e, parm)) {
		return MSG_HANDLED;
	    } else {
		return MSG_NOT_HANDLED;
	    }
	}

    case WIDGET_CURSOR:
	widget_move (&e->widget, e->curs_row + EDIT_TEXT_VERTICAL_OFFSET,
		     e->curs_col + e->start_col);
	return MSG_HANDLED;

    case WIDGET_DESTROY:
	edit_clean (e);
	return MSG_HANDLED;

    case WIDGET_UNFOCUS:
	if (e->in_panel) {
	    widget_want_cursor (e->widget, 0);
	    edit_status (e);
	    return MSG_HANDLED;
	}
	/* fallthrough */

    default:
	return default_proc (msg, parm);
    }
}

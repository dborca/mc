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
 *
 * Original idea and code: Oleg "Olegarch" Konovalov <olegarch@linuxinside.com>
 */


#include <config.h>
#include <stdlib.h>
#include "global.h"
#include "dialog.h"
#include "panel.h"
#include "wtools.h"
#include "main.h"
#include "view.h"
#include "../edit/edit.h"
#include "dlgswitch.h"


#define LOG(format, ...) /*fprintf (stderr, format, ## __VA_ARGS__)*/


struct DLG_NODE {
    struct DLG_NODE *next;
    char *name;
    DLG_TYPE type;
    Dlg_head *dlg;
    union {
	struct {
	    WView *wview;
	} view_data;
	struct {
	    WEdit *wedit;
	} edit_data;
    } u;
};


static struct DLG_NODE *mc_dialogs = NULL;	/* List of (background) dialogs: filemanagers, editors, viewers */
static struct DLG_NODE *mc_cur_dlg = NULL;	/* Currently active dialog */
static struct DLG_NODE *mc_manager = NULL;	/* File manager dialog - there can be only one */
static int dlgswitch_pending = 0;		/* Is there any dialogs that we have to run after returning to the manager from another dialog */
static int dlgswitch_listbox = 0;		/* We are in dlgswitch selector, no point in allowing recursion */


int
dlgswitch_remove_current (void)
{
    struct DLG_NODE *e, *p;

    if (mc_cur_dlg == NULL) {
	mc_cur_dlg = mc_manager;
	return 1;
    }

    for (p = NULL, e = mc_dialogs; e != NULL; p = e, e = e->next) {
	if (e == mc_cur_dlg) {
	    if (p != NULL) {
		p->next = e->next;
	    } else {
		mc_dialogs = e->next;
	    }
	    g_free(e->name);
	    free(e);
	    if (e == mc_manager) {
		mc_manager = NULL;
	    }
	    mc_cur_dlg = mc_manager;
	    return 0;
	}
    }

    return -1;
}


int
dlgswitch_add (Dlg_head *h, DLG_TYPE type, const char *name, ...)
{
    struct DLG_NODE *e;

    if (mc_manager != NULL && type == DLG_TYPE_MC) {
	LOG("BUG: %s: mc_manager=%p\n", __FUNCTION__, mc_manager);
	return -1; /* XXX bug: cannot have two managers */
    }
    if (mc_manager == NULL && type != DLG_TYPE_MC) {
	goto err;
    }

    e = malloc(sizeof(struct DLG_NODE));
    if (e) {
	switch (type) {
	    case DLG_TYPE_VIEW:
		e->name = concat_dir_and_file(current_panel->cwd, name);
		if (e->name != NULL) {
		    va_list ap;
		    va_start(ap, name);
		    e->u.view_data.wview = va_arg(ap, WView *);
		    va_end(ap);
		}
		break;
#ifdef USE_INTERNAL_EDIT
	    case DLG_TYPE_EDIT:
		e->name = name ? concat_dir_and_file(current_panel->cwd, name) : g_strdup("<new>");
		if (e->name != NULL) {
		    va_list ap;
		    va_start(ap, name);
		    e->u.edit_data.wedit = va_arg(ap, WEdit *);
		    va_end(ap);
		}
		break;
#endif
	    case DLG_TYPE_MC:
		e->name = g_strdup(name);
		if (e->name != NULL) {
		    mc_manager = e;
		}
		break;
	    default:
		e->name = NULL;
	}
	if (e->name == NULL) {
	    free(e);
	    goto err;
	}
	e->dlg = h;
	e->type = type;
	e->next = mc_dialogs;
	mc_dialogs = e;
	mc_cur_dlg = e;
	return 0;
    }

  err:
    mc_cur_dlg = NULL;
    return -1;
}


int
dlgswitch_update_path (const char *dir, const char *file)
{
    char *p;

    if (mc_cur_dlg == NULL) {
	return -1;
    }

    if (file == NULL) {
	return -1;
    }

    if (dir == NULL) {
	dir = current_panel->cwd;
    }

    p = concat_dir_and_file(dir, file);
    if (p == NULL) {
	return -1;
    }

    g_free(mc_cur_dlg->name);
    mc_cur_dlg->name = p;

    return 0;
}


static int
dlgswitch_get_title_len(struct DLG_NODE *e)
{
    switch (e->type) {
	case DLG_TYPE_VIEW:
	    return sizeof(" View: ") + strlen(e->name);
#ifdef USE_INTERNAL_EDIT
	case DLG_TYPE_EDIT:
	    return sizeof(" Edit: ") + strlen(e->name);
#endif
	case DLG_TYPE_MC:
	    return sizeof(" ") + strlen(e->name);
    }
    return 0;
}


static char *
dlgswitch_get_title(struct DLG_NODE *e)
{
    char *name;
    switch (e->type) {
	case DLG_TYPE_VIEW:
	    name = malloc(sizeof(" View: ") + strlen(e->name) + 1);
	    if (name != NULL) {
		strcpy(name, " View: ");
		if (view_file_modified(e->u.view_data.wview)) {
		    *name = '*';
		}
	    }
	    break;
#ifdef USE_INTERNAL_EDIT
	case DLG_TYPE_EDIT:
	    name = malloc(sizeof(" Edit: ") + strlen(e->name) + 1);
	    if (name != NULL) {
		strcpy(name, " Edit: ");
		if (edit_file_modified(e->u.edit_data.wedit)) {
		    *name = '*';
		}
	    }
	    break;
#endif
	case DLG_TYPE_MC:
	    name = malloc(sizeof(" ") + strlen(e->name) + 1);
	    if (name != NULL) {
		strcpy(name, " ");
	    }
	    break;
	default:
	    name = NULL;
    }
    if (name) {
	strcat(name, e->name);
	strcat(name, " ");
    }
    return name;
}


void
dlgswitch_process_pending(void)
{
    while (dlgswitch_pending) {
	dlgswitch_pending = 0;

	mc_cur_dlg->dlg->soft_exit = 0;

	switch (mc_cur_dlg->type) {
	    case DLG_TYPE_VIEW:
		view_run_viewer(mc_cur_dlg->dlg, mc_cur_dlg->u.view_data.wview, NULL);
		/* XXX might want to update panels here, because of hexviewer save block */
		break;
#ifdef USE_INTERNAL_EDIT
	    case DLG_TYPE_EDIT:
		edit_run_editor(mc_cur_dlg->dlg, mc_cur_dlg->u.edit_data.wedit);
		update_panels (UP_OPTIMIZE, UP_KEEPSEL); /* XXX a bit heavy-handed */
		break;
#endif
	    case DLG_TYPE_MC:
		/* XXX DLG_TYPE_MC can't be pending */
	    default:
		break;
	}
    }

    do_refresh();
}


static void
dlgswitch_goto(struct DLG_NODE *e)
{
    if (mc_cur_dlg != e) {
	struct DLG_NODE *old_dlg = mc_cur_dlg;
	mc_cur_dlg = e;
	if (old_dlg->type != DLG_TYPE_MC) {
	    old_dlg->dlg->running = 0;
	    old_dlg->dlg->soft_exit = 1;
	    if (e->type != DLG_TYPE_MC) {
		dlgswitch_pending = 1;
	    } else {
		do_refresh();
	    }
	} else {
	    dlgswitch_pending = 1;
	    dlgswitch_process_pending();
	}
    }
}


void
dlgswitch_select (void)
{
    struct DLG_NODE *e;
    int rows, cols;
    int i;

    int rv;
    Listbox *listbox;

    if (midnight_shutdown || mc_cur_dlg == NULL) {
	return;
    }

    if (mc_dialogs == NULL) {
	return;
    }

    if (dlgswitch_listbox) {
	return;
    }
    dlgswitch_listbox = 1;

    rows = 0;
    cols = 0;
    for (e = mc_dialogs; e != NULL; e = e->next) {
	int len = dlgswitch_get_title_len(e);
	if (cols < len) {
	    cols = len;
	}
	rows++;
    }

    listbox = create_listbox_compact(NULL, cols + 2, rows, _(" Dialogs "), "[Dialog selector]");
    if (listbox == NULL) {
	dlgswitch_listbox = 0;
	return;
    }
    for (i = 0, e = mc_dialogs; e != NULL; e = e->next, i++) {
	char *text = dlgswitch_get_title(e);
	LISTBOX_APPEND_TEXT(listbox, (i < 9) ? '1' + i : 'a' + i - 9, text, NULL);
	free(text);
	if (e == mc_cur_dlg) {
	    listbox_select_by_number(listbox->list, i);
	}
    }
    rv = run_listbox(listbox);
    dlgswitch_listbox = 0;
    if (rv != -1) {
	for (i = 0, e = mc_dialogs; e != NULL; e = e->next, i++) {
	    if (i == rv) {
		dlgswitch_goto(e);
		break;
	    }
	}
    }
}


void
dlgswitch_goto_next (void)
{
    struct DLG_NODE *e;

    if (midnight_shutdown || mc_cur_dlg == NULL) {
	return;
    }
    if (dlgswitch_listbox) {
	return;
    }

    for (e = mc_dialogs; e != NULL; e = e->next) {
	if (e == mc_cur_dlg) {
	    e = e->next;
	    if (!e) {
		e = mc_dialogs;
	    }
	    dlgswitch_goto(e);
	    break;
	}
    }
}


void
dlgswitch_goto_prev (void)
{
    struct DLG_NODE *e, *p;

    if (midnight_shutdown || mc_cur_dlg == NULL) {
	return;
    }
    if (dlgswitch_listbox) {
	return;
    }

    for (p = NULL, e = mc_dialogs; e != NULL; p = e, e = e->next) {
	if (e == mc_cur_dlg) {
	    if (!p) {
		for (p = mc_dialogs; p->next != NULL; p = p->next) {
		}
	    }
	    dlgswitch_goto(p);
	    break;
	}
    }
}


int
dlgswitch_reuse (DLG_TYPE type, const char *dir, const char *file)
{
    struct DLG_NODE *e;

    if (midnight_shutdown || mc_cur_dlg != mc_manager) {
	LOG("BUG: %s: midnight_shutdown=%d, mc_cur_dlg=%p, mc_manager=%p\n", __FUNCTION__, midnight_shutdown, mc_cur_dlg, mc_manager);
	return -1; /* XXX bug? */
    }

    if ((type == DLG_TYPE_VIEW || type == DLG_TYPE_EDIT) && file != NULL) {
	char *filename;
	if (dir == NULL) {
	    dir = current_panel->cwd;
	}
	filename = concat_dir_and_file(dir, file);
	if (filename == NULL) {
	    return -1;
	}
	for (e = mc_dialogs; e != NULL; e = e->next) {
	    if (type == e->type && !strcmp(filename, e->name)) {
		g_free(filename);
		dlgswitch_goto(e);
		return 0;
	    }
	}
	g_free(filename);
    }

    return -1;
}


void
dlgswitch_before_exit (void)
{
    struct DLG_NODE *e = mc_dialogs;

    while (e != NULL) {
	mc_cur_dlg = e;
	e = mc_cur_dlg->next;
	switch (mc_cur_dlg->type) {
	    case DLG_TYPE_VIEW:
		if (view_file_modified(mc_cur_dlg->u.view_data.wview)) {
		    dlgswitch_pending = 1;
		    dlgswitch_process_pending();
		    break;
		}
		view_finish_viewer(mc_cur_dlg->dlg, mc_cur_dlg->u.view_data.wview, NULL);
		break;
#ifdef USE_INTERNAL_EDIT
	    case DLG_TYPE_EDIT:
		if (edit_file_modified(mc_cur_dlg->u.edit_data.wedit)) {
		    dlgswitch_pending = 1;
		    dlgswitch_process_pending();
		    break;
		}
		edit_finish_editor(mc_cur_dlg->dlg, mc_cur_dlg->u.edit_data.wedit);
		break;
#endif
	    case DLG_TYPE_MC:
	    default:
		break;
	}
    }

    dlgswitch_remove_current();
}


void
dlgswitch_got_winch (void)
{
    struct DLG_NODE *e;
    for (e = mc_dialogs; e != NULL; e = e->next) {
	if (e != mc_cur_dlg && e != mc_manager) {
	    e->dlg->winch_pending = 1;
	}
    }
}

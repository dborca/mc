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


#ifdef USE_DLGSWITCH

#define LOG(format, ...) /*fprintf (stderr, format, ## __VA_ARGS__)*/


struct DLG_NODE {
    struct DLG_NODE *next;
    char *name;
    DLG_TYPE type;
    Dlg_head *dlg;
    union {
	struct {
	    WView *wview;
	    int *move_dir_p;
	} view_data;
	struct {
	    WEdit *wedit;
	    void *edit_menubar;
	} edit_data;
    } u;
};


static struct DLG_NODE *mc_dialogs = NULL;	/* List of (background) dialogs: filemanagers, editors, viewers */
static struct DLG_NODE *mc_cur_dlg = NULL;	/* Currently active dialog */
static struct DLG_NODE *mc_manager = NULL;	/* File manager dialog - there can be only one */
static int dlgswitch_pending = 0;		/* Is there any dialogs that we have to run after returning to the manager from another dialog */


static unsigned char
get_hotkey (int n)
{
    return (n <= 9) ? '0' + n : 'a' + n - 10;
}


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
	    free(e->name);
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
		    e->u.view_data.move_dir_p = va_arg(ap, int *);
		    va_end(ap);
		}
		break;
	    case DLG_TYPE_EDIT:
		e->name = name ? concat_dir_and_file(current_panel->cwd, name) : strdup("<new>");
		if (e->name != NULL) {
		    va_list ap;
		    va_start(ap, name);
		    e->u.edit_data.wedit = va_arg(ap, WEdit *);
		    e->u.edit_data.edit_menubar = va_arg(ap, void *);
		    va_end(ap);
		}
		break;
	    case DLG_TYPE_MC:
		e->name = strdup(name);
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

    free(mc_cur_dlg->name);
    mc_cur_dlg->name = p;

    return 0;
}


static int
dlgswitch_get_title_len(struct DLG_NODE *e)
{
    int len;
    switch (e->type) {
	case DLG_TYPE_VIEW:
	    len = strlen(" View: ") + strlen(e->name);
	    break;
	case DLG_TYPE_EDIT:
	    len = strlen(" Edit: ") + strlen(e->name);
	    if (edit_file_modified(e->u.edit_data.wedit)) {
		len += strlen(" (*)");
	    }
	    break;
	case DLG_TYPE_MC:
	    len = strlen(" ") + strlen(e->name);
	    break;
	default:
	    len = 0;
    }
    return len;
}


static char *
dlgswitch_get_title(struct DLG_NODE *e)
{
    char *name;
    switch (e->type) {
	case DLG_TYPE_VIEW:
	    name = malloc(strlen(" View: ") + strlen(e->name) + 1);
	    if (name != NULL) {
		strcpy(name, " View: ");
		strcat(name, e->name);
	    }
	    break;
	case DLG_TYPE_EDIT:
	    name = malloc(strlen(" Edit: ") + strlen(e->name) + 1 + strlen(" (*)"));
	    if (name != NULL) {
		strcpy(name, " Edit: ");
		strcat(name, e->name);
		if (edit_file_modified(e->u.edit_data.wedit)) {
		    strcat(name, " (*)");
		}
	    }
	    break;
	case DLG_TYPE_MC:
	    name = malloc(strlen(" ") + strlen(e->name) + 1);
	    if (name != NULL) {
		strcpy(name, " ");
		strcat(name, e->name);
	    }
	    break;
	default:
	    name = NULL;
    }
    if (name != NULL && e == mc_cur_dlg) {
	name[0] = '>';
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
		view_run_viewer(mc_cur_dlg->dlg, mc_cur_dlg->u.view_data.wview, NULL); /* XXX move_dir_p may not be valid anymore */
		break;
	    case DLG_TYPE_EDIT:
		edit_run_editor(mc_cur_dlg->dlg, mc_cur_dlg->u.edit_data.wedit, mc_cur_dlg->u.edit_data.edit_menubar);
		update_panels (UP_OPTIMIZE, UP_KEEPSEL); /* XXX a bit heavy-handed */
		break;
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

    rows = 0;
    cols = 0;
    for (e = mc_dialogs; e != NULL; e = e->next) {
	int len = dlgswitch_get_title_len(e);
	if (cols < len) {
	    cols = len;
	}
	rows++;
    }

    listbox = create_listbox_window(cols + 2, rows, _(" Dialogs "), "[Dialog selector]");
    if (listbox == NULL) {
	return;
    }
    for (i = 0, e = mc_dialogs; e != NULL; e = e->next, i++) {
	char *text = dlgswitch_get_title(e);
	LISTBOX_APPEND_TEXT(listbox, get_hotkey(i), text, NULL);
	free(text);
    }
    rv = run_listbox(listbox);
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
		free(filename);
		dlgswitch_goto(e);
		return 0;
	    }
	}
	free(filename);
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
		view_finish_viewer(mc_cur_dlg->dlg, mc_cur_dlg->u.view_data.wview, NULL); /* XXX move_dir_p may not be valid anymore */
		break;
	    case DLG_TYPE_EDIT:
		if (edit_file_modified(mc_cur_dlg->u.edit_data.wedit)) {
		    dlgswitch_pending = 1;
		    dlgswitch_process_pending();
		    break;
		}
		edit_finish_editor(mc_cur_dlg->dlg, mc_cur_dlg->u.edit_data.wedit, mc_cur_dlg->u.edit_data.edit_menubar);
		break;
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
#endif

/* External panelize
   Copyright (C) 1995 The Free Software Foundation
   
   Written by: 1995 Janne Kukonlehto
               1995 Jakub Jelinek

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

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "global.h"
#include "tty.h"		/* attrset() */
#include "win.h"
#include "color.h"
#include "dialog.h"
#include "widget.h"
#include "wtools.h"		/* For common_dialog_repaint() */
#include "setup.h"		/* For profile_bname */
#include "profile.h"		/* Load/save directories panelize */
#include "dir.h"
#include "panel.h"		/* current_panel */
#include "main.h"		/* repaint_screen */
#include "panelize.h"

#define UX		5
#define UY		2

#define BX		5
#define BY		18

#define BUTTONS		4
#define LABELS          3
#define B_ADD		B_USER
#define B_REMOVE        (B_USER + 1)

static WListbox *l_panelize;
static Dlg_head *panelize_dlg;
static int last_listitem;
static WInput *pname;

static struct {
    int ret_cmd, flags, y, x;
    const char *text;
} panelize_but [BUTTONS] = {
    { B_CANCEL, NORMAL_BUTTON, 0, 53, N_("&Cancel")   },
    { B_ADD, NORMAL_BUTTON,    0, 28, N_("&Add new")  },
    { B_REMOVE, NORMAL_BUTTON, 0, 16, N_("&Remove")   },
    { B_ENTER, DEFPUSH_BUTTON, 0,  0, N_("Pane&lize") },
};

static const char *panelize_section = "Panelize";
static void do_external_panelize (char *command);

/* Directory panelize */
static struct panelize {
    char *command;
    char *label;
    struct panelize *next;
} *panelize = NULL;

static void
update_command (void)
{
    if (l_panelize->pos != last_listitem) {
    	last_listitem = l_panelize->pos;
        assign_text (pname, 
            ((struct panelize *) l_panelize->current->data)->command);
	pname->point = 0;
        update_input (pname, 1);
    }
}

static cb_ret_t
panelize_callback (Dlg_head *h, dlg_msg_t msg, int parm)
{
    switch (msg) {
    case DLG_DRAW:
	common_dialog_repaint (h);
	attrset (COLOR_NORMAL);
	draw_box (h, UY, UX, h->lines - 10, h->cols - 10);
	return MSG_HANDLED;

    case DLG_POST_KEY:
	/* fall */
    case DLG_INIT:
	attrset (MENU_ENTRY_COLOR);
	update_command ();
	return MSG_HANDLED;

    default:
	return default_dlg_callback (h, msg, parm);
    }
}

static void
init_panelize (void)
{
    int i, panelize_cols = COLS - 6;
    struct panelize *current = panelize;

#ifdef ENABLE_NLS
    static int i18n_flag = 0;
    static int maxlen = 0;

    if (!i18n_flag) {
	i = sizeof (panelize_but) / sizeof (panelize_but[0]);
	while (i--) {
	    panelize_but[i].text = _(panelize_but[i].text);
	    maxlen += strlen (panelize_but[i].text) + 5;
	}
	maxlen += 10;

	i18n_flag = 1;
    }
    panelize_cols = max (panelize_cols, maxlen);

    panelize_but[2].x =
	panelize_but[3].x + strlen (panelize_but[3].text) + 7;
    panelize_but[1].x =
	panelize_but[2].x + strlen (panelize_but[2].text) + 5;
    panelize_but[0].x =
	panelize_cols - strlen (panelize_but[0].text) - 8 - BX;

#endif				/* ENABLE_NLS */

    last_listitem = 0;

    do_refresh ();

    panelize_dlg =
	create_dlg (0, 0, 22, panelize_cols, dialog_colors,
		    panelize_callback, "[External panelize]",
		    _("External panelize"), DLG_CENTER | DLG_REVERSE);

    for (i = 0; i < BUTTONS; i++)
	add_widget (panelize_dlg,
		    button_new (BY + panelize_but[i].y,
				BX + panelize_but[i].x,
				panelize_but[i].ret_cmd,
				panelize_but[i].flags,
				panelize_but[i].text, 0));

    pname =
	input_new (UY + 14, UX, INPUT_COLOR, panelize_dlg->cols - 10, "",
		   "in");
    add_widget (panelize_dlg, pname);

    add_widget (panelize_dlg, label_new (UY + 13, UX, _("Command")));

    /* get new listbox */
    l_panelize =
	listbox_new (UY + 1, UX + 1, panelize_dlg->cols - 12, 10, NULL);

    while (current) {
	listbox_add_item (l_panelize, 0, 0, current->label, current);
	current = current->next;
    }

    /* add listbox to the dialogs */
    add_widget (panelize_dlg, l_panelize);

    listbox_select_entry (l_panelize,
			  listbox_search_text (l_panelize,
					       _("Other command")));
}

static void panelize_done (void)
{
    destroy_dlg (panelize_dlg);
    repaint_screen ();
}

static void add2panelize (char *label, char *command)
{
    struct panelize *current, *old;

    old = NULL;
    current = panelize;
    while (current && strcmp (current->label, label) <= 0){
	old = current;
	current = current->next;
    }

    if (old == NULL){
	panelize = g_new (struct panelize, 1);
	panelize->label = label;
	panelize->command = command;
	panelize->next = current;
    } else {
	struct panelize *new;
	new = g_new (struct panelize, 1);
	new->label = label;
	new->command = command;
	old->next = new;
	new->next = current;
    }
}

static void
add2panelize_cmd (void)
{
    char *label;

    if (pname->buffer && (*pname->buffer)) {
	label = input_dialog (_(" Add to external panelize "), 
		_(" Enter command label: "), 
			      "");
	if (!label)
	    return;
	if (!*label) {
	    g_free (label);
	    return;
	}
	
	add2panelize (label, g_strdup (pname->buffer));
    }
}

static void remove_from_panelize (struct panelize *entry)
{
    if (strcmp (entry->label, _("Other command")) != 0) {
	if (entry == panelize) {
	    panelize = panelize->next;
	} else {
	    struct panelize *current = panelize;
	    while (current && current->next != entry)
		current = current->next;
	    if (current) {
		current->next = entry->next;
	    }
	}

	g_free (entry->label);
	g_free (entry->command);
	g_free (entry);
    }
}

void
external_panelize (void)
{
    char *target = NULL;

    if (!vfs_current_is_local ()){
	message (1, MSG_ERROR,
		 _(" Cannot run external panelize in a non-local directory "));
	return;
    }

    init_panelize ();
    
    /* display file info */
    attrset (SELECTED_COLOR);

    run_dlg (panelize_dlg);

    switch (panelize_dlg->ret_value) {
    case B_CANCEL:
	break;

    case B_ADD:
	add2panelize_cmd ();
	break;

    case B_REMOVE:
	remove_from_panelize (l_panelize->current->data);
	break;

    case B_ENTER:
	target = pname->buffer;
	if (target != NULL && *target) {
	    char *cmd = g_strdup (target);
	    destroy_dlg (panelize_dlg);
	    do_external_panelize (cmd);
	    g_free (cmd);
	    repaint_screen ();
	    return;
	}
	break;
    }

    panelize_done ();
}

void load_panelize (void)
{
    void *profile_keys;
    char *key, *value;
    
    profile_keys = profile_init_iterator (panelize_section, profile_name);
    
    add2panelize (g_strdup (_("Other command")), g_strdup (""));

    if (!profile_keys){
	add2panelize (g_strdup (_("Find rejects after patching")), g_strdup ("find . -name \\*.rej -print"));
	add2panelize (g_strdup (_("Find *.orig after patching")), g_strdup ("find . -name \\*.orig -print"));
	add2panelize (g_strdup (_("Find SUID and SGID programs")), g_strdup ("find . \\( \\( -perm -04000 -a -perm +011 \\) -o \\( -perm -02000 -a -perm +01 \\) \\) -print"));
	return;
    }
    
    while (profile_keys){
	profile_keys = profile_iterator_next (profile_keys, &key, &value);
	add2panelize (g_strdup (key), g_strdup (value));
    }
}

void save_panelize (void)
{
    struct panelize *current = panelize;
    
    profile_clean_section (panelize_section, profile_name);
    for (;current; current = current->next){
    	if (strcmp (current->label, _("Other command")))
	    WritePrivateProfileString (panelize_section,
				       current->label,
				       current->command,
				       profile_name);
    }
    sync_profiles ();
}

void done_panelize (void)
{
    struct panelize *current = panelize;
    struct panelize *next;

    for (; current; current = next){
	next = current->next;
	g_free (current->label);
	g_free (current->command);
	g_free (current);
    }
}

static void do_external_panelize (char *command)
{
    int status, link_to_dir, stale_link;
    int next_free = 0;
    struct stat st;
    dir_list *list = &current_panel->dir;
    char line [MC_MAXPATHLEN];
    char *name;
    FILE *external;

    open_error_pipe ();
    external = popen (command, "r");
    if (!external){
	close_error_pipe (1, _("Cannot invoke command."));
	return;
    }
    /* Clear the counters and the directory list */
    panel_clean_dir (current_panel);

    while (1) {
	clearerr(external);
	if (fgets (line, MC_MAXPATHLEN, external) == NULL) {
	    if (ferror(external) && errno == EINTR)
		continue;
	    else
		break;
	}
	if (line[strlen(line)-1] == '\n')
	    line[strlen(line)-1] = 0;
	if (strlen(line) < 1)
	    continue;
	if (line [0] == '.' && line[1] == PATH_SEP)
	    name = line + 2;
	else
	    name = line;
        status = handle_path (list, name, &st, next_free, &link_to_dir,
    	    &stale_link);
	if (status == 0)
	    continue;
	if (status == -1)
	    break;
	list->list [next_free].fnamelen = strlen (name);
	list->list [next_free].fname = g_strdup (name);
	file_mark (current_panel, next_free, 0);
	list->list [next_free].f.link_to_dir = link_to_dir;
	list->list [next_free].f.stale_link = stale_link;
	list->list [next_free].f.dir_size_computed = 0;
	list->list [next_free].st = st;
	next_free++;
	if (!(next_free & 32))
	    rotate_dash ();
    }

    current_panel->is_panelized = 1;
    if (next_free){
	current_panel->count = next_free;
	if (list->list [0].fname [0] == PATH_SEP){
	    strcpy (current_panel->cwd, PATH_SEP_STR);
	    chdir (PATH_SEP_STR);
	}
    } else {
	current_panel->count = set_zero_dir (list);
    }
    if (pclose (external) < 0)
	message (0, _("External panelize"), _("Pipe close failed"));
    close_error_pipe (0, 0);
    try_to_select (current_panel, NULL);
    panel_re_sort (current_panel);
}

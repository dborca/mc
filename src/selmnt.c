/*
 * Mountpoint selector
 *
 * Written by Daniel Borca <dborca@yahoo.com>
 * Original idea and code: Oleg "Olegarch" Konovalov <olegarch@linuxinside.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#include <config.h>
#include <stdlib.h>
#include "global.h"
#include "color.h"
#include "layout.h"
#include "main.h"
#include "panel.h"
#include "wtools.h"
#include "mountlist.h"
#include "selmnt.h"


/* XXX HAVE_MOUNT* conditions? add usage for dir? */


static unsigned char
get_hotkey (int n)
{
    return (n <= 9) ? '0' + n : 'a' + n - 10;
}


static void
destroy_mnt_list (struct mount_entry *list)
{
    while (list != NULL) {
	struct mount_entry *e = list;
	list = list->me_next;
	free(e->me_devname);
	free(e->me_mountdir);
	free(e->me_type);
	free(e);
    }
}


static void
select_mnt (WPanel *p)
{
    struct mount_entry *list, *e;
    int rows, cols;
    int i;

    list = read_filesystem_list(0, 0);
    if (list == NULL) {
	return;
    }

    rows = 0;
    cols = 0;
    for (e = list; e != NULL; e = e->me_next) {
	int len = strlen(e->me_mountdir);
	if (cols < len) {
	    cols = len;
	}
	rows++;
    }

#if 0
{
    int rv;
    Listbox *listbox;

    listbox = create_listbox_window(cols + 2, rows, _(" Mountpoints "), "[Mountpoint selector]");
    if (listbox == NULL) {
	destroy_mnt_list(list);
	return;
    }
    for (i = 0, e = list; e != NULL; e = e->me_next, i++) {
	LISTBOX_APPEND_TEXT(listbox, get_hotkey(i), e->me_mountdir, NULL);
    }
    rv = run_listbox(listbox);
    if (rv != -1) {
	for (i = 0, e = list; e != NULL; e = e->me_next, i++) {
	    if (i == rv) {
		do_panel_cd(p, e->me_mountdir, cd_exact);
		break;
	    }
	}
    }
}
#else
{
    char *q;
    Dlg_head *query_dlg;
    WListbox *query_list;
    int y, x;
    int h, w;

    h = rows + 2;
    w = cols + 4;
    if (w > p->widget.cols) {
	w = p->widget.cols;
    }
    if (h > p->widget.lines) {
	h = p->widget.lines;
    }
    y = p->widget.y + (p->widget.lines - h) / 2;
    x = p->widget.x + (p->widget.cols - w) / 2;
    query_dlg = create_dlg(y, x, h, w, dialog_colors, NULL, "[Mountpoint selector]", _(" Mountpoints "), DLG_COMPACT);
    if (query_dlg == NULL) {
	destroy_mnt_list(list);
	return;
    }
    query_list = listbox_new(1, 1, w - 2, h - 2, NULL);
    if (query_list == NULL) {
	destroy_dlg(query_dlg);
	destroy_mnt_list(list);
	return;
    }
    add_widget(query_dlg, query_list);
    for (i = 0, e = list; e != NULL; e = e->me_next, i++) {
	listbox_add_item(query_list, LISTBOX_APPEND_AT_END, get_hotkey(i), e->me_mountdir, NULL);
    }
    run_dlg(query_dlg);
    if (query_dlg->ret_value != B_CANCEL) {
	listbox_get_current(query_list, &q, NULL);
	do_panel_cd(p, q, cd_exact);
    }
    destroy_dlg(query_dlg);
}
#endif

    destroy_mnt_list(list);
}


void
select_mnt_left (void)
{
    if (get_display_type(0) == view_listing) {	/* XXX why? */
	select_mnt(left_panel);
    }
}


void
select_mnt_right (void)
{
    if (get_display_type(1) == view_listing) {	/* XXX why? */
	select_mnt(right_panel);
    }
}

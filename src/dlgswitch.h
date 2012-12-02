#ifndef DLGSWITCH_H_included
#define DLGSWITCH_H_included

typedef enum {
    DLG_TYPE_VIEW,
    DLG_TYPE_EDIT,
    DLG_TYPE_MC
} DLG_TYPE;

int dlgswitch_remove_current (void);
int dlgswitch_add (Dlg_head *h, DLG_TYPE type, const char *name, ...);
int dlgswitch_update_path (const char *dir, const char *file);
void dlgswitch_process_pending(void);
void dlgswitch_select (void);
void dlgswitch_goto_next (void);
void dlgswitch_goto_prev (void);
int dlgswitch_reuse (DLG_TYPE type, const char *dir, const char *file);
void dlgswitch_before_exit (void);
void dlgswitch_got_winch (void);

#endif

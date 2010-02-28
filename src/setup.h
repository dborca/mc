#ifndef MC_SETUP_H
#define MC_SETUP_H

#include "panel.h"

char *setup_init (void);
void save_layout (void);
void save_configure (void);
void load_setup (void);
void save_setup (void);
void done_setup (void);
void load_key_defs (void);
char *load_anon_passwd (void);

void panel_save_setup (struct WPanel *panel, const char *section);
void panel_load_setup (struct WPanel *panel, const char *section);

extern char *profile_name;
extern char *global_profile_name;

extern char setup_color_string[];
extern char term_color_string[];
extern char color_terminal_string[];

extern int startup_left_mode;
extern int startup_right_mode;
extern int verbose;

#define PROFILE_NAME     ".mc/ini"
#define HOTLIST_FILENAME ".mc/hotlist"

#endif

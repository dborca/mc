/* Input line filename/username/hostname/variable/command completion.
   (Let mc type for you...)
   
   Copyright (C) 1995 The Free Software Foundation
   
   Written by: 1995 Jakub Jelinek
   
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <config.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "global.h"
#include "tty.h"
#include "win.h"
#include "color.h"
#include "dialog.h"
#include "widget.h"
#include "wtools.h"
#include "complete.h"
#include "main.h"
#include "key.h"		/* XCTRL and ALT macros */

typedef char *CompletionFunction (char *, int);

/* This flag is used in filename_completion_function */
static int ignore_filenames = 0;

/* This flag is used by command_completion_function */
/* to hint the filename_completion_function */
static int look_for_executables = 0;

static char *
filename_completion_function (char *text, int state)
{
    static DIR *directory;
    static char *filename = NULL;
    static char *dirname = NULL;
    static char *users_dirname = NULL;
    static size_t filename_len;
    int isdir = 1, isexec = 0;

    struct dirent *entry = NULL;

    /* If we're starting the match process, initialize us a bit. */
    if (!state){
        const char *temp;

        g_free (dirname);
        g_free (filename);
        g_free (users_dirname);

	if ((*text) && (temp = strrchr (text, PATH_SEP))){
	    filename = g_strdup (++temp);
	    dirname = g_strndup (text, temp - text);
	} else {
	    dirname = g_strdup (".");
	    filename = g_strdup (text);
	}

        /* We aren't done yet.  We also support the "~user" syntax. */

        /* Save the version of the directory that the user typed. */
        users_dirname = dirname;
        {
	    dirname = tilde_expand (dirname);
	    canonicalize_pathname (dirname);
	    /* Here we should do something with variable expansion
	       and `command`.
	       Maybe a dream - UNIMPLEMENTED yet. */
        }
        directory = mc_opendir (dirname);
        filename_len = strlen (filename);
    }

    /* Now that we have some state, we can read the directory. */

    while (directory && (entry = mc_readdir (directory))){
        /* Special case for no filename.
	   All entries except "." and ".." match. */
        if (!filename_len){
	    if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
	        continue;
	} else {
	    /* Otherwise, if these match up to the length of filename, then
	       it may be a match. */
	    if ((entry->d_name[0] != filename[0]) ||
	        ((NLENGTH (entry)) < filename_len) ||
		strncmp (filename, entry->d_name, filename_len))
	        continue;
	}
	isdir = 1; isexec = 0;
	{
	    char *tmp = g_malloc (3 + strlen (dirname) + NLENGTH (entry));
	    struct stat tempstat;
	    
	    strcpy (tmp, dirname);
	    strcat (tmp, PATH_SEP_STR);
	    strcat (tmp, entry->d_name);
	    canonicalize_pathname (tmp);
	    /* Unix version */
	    if (!mc_stat (tmp, &tempstat)){
	    	uid_t my_uid = getuid ();
	    	gid_t my_gid = getgid ();
	    	
	        if (!S_ISDIR (tempstat.st_mode)){
	            isdir = 0;
	            if ((!my_uid && (tempstat.st_mode & 0111)) ||
	                (my_uid == tempstat.st_uid && (tempstat.st_mode & 0100)) ||
	                (my_gid == tempstat.st_gid && (tempstat.st_mode & 0010)) ||
	                (tempstat.st_mode & 0001))
	                isexec = 1;
	        }
	    }
	   g_free (tmp);
	}
	switch (look_for_executables)
	{
	    case 2: if (!isexec)
	    	        continue;
	    	    break;
	    case 1: if (!isexec && !isdir)
	    	        continue;
	    	    break;
	}
	if (ignore_filenames && !isdir)
	    continue;
	break;
    }

    if (!entry){
        if (directory){
	    mc_closedir (directory);
	    directory = NULL;
	}
	g_free (dirname);
	dirname = NULL;
	g_free (filename);
	filename = NULL;
	g_free (users_dirname);
	users_dirname = NULL;
        return NULL;
    } else {
        char *temp;

        if (users_dirname && (users_dirname[0] != '.' || users_dirname[1])){
	    int dirlen = strlen (users_dirname);
	    temp = g_malloc (3 + dirlen + NLENGTH (entry));
	    strcpy (temp, users_dirname);
	    /* We need a `/' at the end. */
	    if (users_dirname[dirlen - 1] != PATH_SEP){
	        temp[dirlen] = PATH_SEP;
	        temp[dirlen + 1] = 0;
	    }
	    strcat (temp, entry->d_name);
	} else {
	    temp = g_malloc (2 + NLENGTH (entry));
	    strcpy (temp, entry->d_name);
	}
	if (isdir)
	    strcat (temp, PATH_SEP_STR);
        return temp;
    }
}

/* We assume here that text[0] == '~' , if you want to call it in another way,
   you have to change the code */
static char *
username_completion_function (char *text, int state)
{
    static struct passwd *entry;
    static int userlen;

    if (!state){ /* Initialization stuff */
        setpwent ();
        userlen = strlen (text + 1);
    }
    while ((entry = getpwent ()) != NULL){
        /* Null usernames should result in all users as possible completions. */
        if (!userlen)
            break;
        else if (text[1] == entry->pw_name[0] &&
	         !strncmp (text + 1, entry->pw_name, userlen))
	    break;
    }

    if (!entry){
        endpwent ();
        return NULL;
    } else {
        char *temp = g_malloc (3 + strlen (entry->pw_name));
        
        *temp = '~';
        strcpy (temp + 1, entry->pw_name);
        strcat (temp, PATH_SEP_STR);
        return temp;
    }
}

/* Linux declares environ in <unistd.h>, so don't repeat it here. */
#if (!(defined(__linux__) && defined (__USE_GNU)) && !defined(__CYGWIN__))
extern char **environ;
#endif

/* We assume text [0] == '$' and want to have a look at text [1], if it is
   equal to '{', so that we should append '}' at the end */
static char *
variable_completion_function (char *text, int state)
{
    static char **env_p;
    static int varlen, isbrace;
    const char *p = NULL;

    if (!state){ /* Initialization stuff */
	isbrace = (text [1] == '{');
        varlen = strlen (text + 1 + isbrace);
        env_p = environ;
    }

    while (*env_p){
    	p = strchr (*env_p, '=');
    	if (p && p - *env_p >= varlen && !strncmp (text + 1 + isbrace, *env_p, varlen))
    	    break;
    	env_p++;
    }

    if (!*env_p)
        return NULL;
    else {
        char *temp = g_malloc (2 + 2 * isbrace + p - *env_p);

	*temp = '$';
	if (isbrace)
	    temp [1] = '{';
	memcpy (temp + 1 + isbrace, *env_p, p - *env_p);
        if (isbrace)
            strcpy (temp + 2 + (p - *env_p), "}");
        else
            temp [1 + p - *env_p] = 0;
        env_p++;
        return temp;
    }
}

#define whitespace(c) ((c) == ' ' || (c) == '\t')
#define cr_whitespace(c) (whitespace (c) || (c) == '\n' || (c) == '\r')

static char **hosts = NULL;
static char **hosts_p = NULL;
static int hosts_alloclen = 0;
static void fetch_hosts (const char *filename)
{
    FILE *file = fopen (filename, "r");
    char buffer[256], *name;
    register int i, start;

    if (!file)
        return;

    while (fgets (buffer, 255, file) != NULL){
        /* Skip to first character. */
        for (i = 0; buffer[i] && cr_whitespace (buffer[i]); i++);
        /* Ignore comments... */
        if (buffer[i] == '#')
            continue;
        /* Handle $include. */
        if (!strncmp (buffer + i, "$include ", 9)){
	    char *includefile = buffer + i + 9;
	    char *t;

	    /* Find start of filename. */
	    while (*includefile && whitespace (*includefile))
	        includefile++;
	    t = includefile;

	    /* Find end of filename. */
	    while (*t && !cr_whitespace (*t))
	        t++;
	    *t = '\0';

	    fetch_hosts (includefile);
	    continue;
	}

        /* Skip IP #s. */
	while (buffer[i] && !cr_whitespace (buffer[i]))
	    i++;

        /* Get the host names separated by white space. */
        while (buffer[i] && buffer[i] != '#'){
	    while (buffer[i] && cr_whitespace (buffer[i]))
		i++;
	    if (buffer[i] ==  '#')
		continue;
	    for (start = i; buffer[i] && !cr_whitespace (buffer[i]); i++);
	        if (i - start == 0)
	            continue;
	    name = g_strndup (buffer + start, i - start);
	    {
	    	char **host_p;
	    	
	    	if (hosts_p - hosts >= hosts_alloclen){
	    	    int j = hosts_p - hosts;
	    	
	    	    hosts = g_realloc ((void *)hosts, ((hosts_alloclen += 30) + 1) * sizeof (char *));
	    	    hosts_p = hosts + j;
	        }
	        for (host_p = hosts; host_p < hosts_p; host_p++)
	            if (!strcmp (name, *host_p))
	            	break; /* We do not want any duplicates */
	        if (host_p == hosts_p){
	            *(hosts_p++) = name;
	            *hosts_p = NULL;
	        } else
	            g_free (name);
	    }
	}
    }
    fclose (file);
}

static char *
hostname_completion_function (char *text, int state)
{
    static char **host_p;
    static int textstart, textlen;

    if (!state){ /* Initialization stuff */
        const char *p;
        
    	if (hosts != NULL){
    	    for (host_p = hosts; *host_p; host_p++)
    	    	g_free (*host_p);
    	   g_free (hosts);
    	}
    	hosts = g_new (char *, (hosts_alloclen = 30) + 1);
    	*hosts = NULL;
    	hosts_p = hosts;
    	fetch_hosts ((p = getenv ("HOSTFILE")) ? p : "/etc/hosts");
    	host_p = hosts;
    	textstart = (*text == '@') ? 1 : 0;
    	textlen = strlen (text + textstart);
    }
    
    while (*host_p){
    	if (!textlen)
    	    break; /* Match all of them */
    	else if (!strncmp (text + textstart, *host_p, textlen))
    	    break;
    	host_p++;
    }
    
    if (!*host_p){
    	for (host_p = hosts; *host_p; host_p++)
    	    g_free (*host_p);
    	g_free (hosts);
    	hosts = NULL;
    	return NULL;
    } else {
    	char *temp = g_malloc (2 + strlen (*host_p));

    	if (textstart)
    	    *temp = '@';
    	strcpy (temp + textstart, *host_p);
    	host_p++;
    	return temp;
    }
}

/*
 * This is the function to call when the word to complete is in a position
 * where a command word can be found. It looks around $PATH, looking for
 * commands that match. It also scans aliases, function names, and the
 * table of shell built-ins.
 */
static char *
command_completion_function (char *text, int state)
{
    static const char *path_end;
    static int isabsolute;
    static int phase;
    static int text_len;
    static const char *const *words;
    static char *path;
    static char *cur_path;
    static char *cur_word;
    static int init_state;
    static const char *const bash_reserved[] = {
	"if", "then", "else", "elif", "fi", "case", "esac", "for",
	    "select", "while", "until", "do", "done", "in", "function", 0
    };
    static const char *const bash_builtins[] = {
	"alias", "bg", "bind", "break", "builtin", "cd", "command",
	    "continue", "declare", "dirs", "echo", "enable", "eval",
	    "exec", "exit", "export", "fc", "fg", "getopts", "hash",
	    "help", "history", "jobs", "kill", "let", "local", "logout",
	    "popd", "pushd", "pwd", "read", "readonly", "return", "set",
	    "shift", "source", "suspend", "test", "times", "trap", "type",
	    "typeset", "ulimit", "umask", "unalias", "unset", "wait", 0
    };
    char *p, *found;

    if (!state) {		/* Initialize us a little bit */
	isabsolute = strchr (text, PATH_SEP) != 0;
	look_for_executables = isabsolute ? 1 : 2;
	if (!isabsolute) {
	    words = bash_reserved;
	    phase = 0;
	    text_len = strlen (text);
	    if (!path && (path = g_strdup (getenv ("PATH"))) != NULL) {
		p = path;
		path_end = strchr (p, 0);
		while ((p = strchr (p, PATH_ENV_SEP))) {
		    *p++ = 0;
		}
	    }
	}
    }

    if (isabsolute) {
	p = filename_completion_function (text, state);
	if (!p)
	    look_for_executables = 0;
	return p;
    }

    found = NULL;
    switch (phase) {
    case 0:			/* Reserved words */
	while (*words) {
	    if (!strncmp (*words, text, text_len))
		return g_strdup (*(words++));
	    words++;
	}
	phase++;
	words = bash_builtins;
    case 1:			/* Builtin commands */
	while (*words) {
	    if (!strncmp (*words, text, text_len))
		return g_strdup (*(words++));
	    words++;
	}
	phase++;
	if (!path)
	    break;
	cur_path = path;
	cur_word = NULL;
    case 2:			/* And looking through the $PATH */
	while (!found) {
	    if (!cur_word) {
		char *expanded;

		if (cur_path >= path_end)
		    break;
		expanded = tilde_expand (*cur_path ? cur_path : ".");
		cur_word = concat_dir_and_file (expanded, text);
		g_free (expanded);
		canonicalize_pathname (cur_word);
		cur_path = strchr (cur_path, 0) + 1;
		init_state = state;
	    }
	    found =
		filename_completion_function (cur_word,
					      state - init_state);
	    if (!found) {
		g_free (cur_word);
		cur_word = NULL;
	    }
	}
    }

    if (!found) {
	look_for_executables = 0;
	g_free (path);
	path = NULL;
	return NULL;
    }
    if ((p = strrchr (found, PATH_SEP)) != NULL) {
	p++;
	p = g_strdup (p);
	g_free (found);
	return p;
    }
    return found;

}

static int
match_compare (const void *a, const void *b)
{
    return strcmp (*(char **)a, *(char **)b);
}

/* Returns an array of char * matches with the longest common denominator
   in the 1st entry. Then a NULL terminated list of different possible
   completions follows.
   You have to supply your own CompletionFunction with the word you
   want to complete as the first argument and an count of previous matches
   as the second. 
   In case no matches were found we return NULL. */
static char **
completion_matches (char *text, CompletionFunction entry_function)
{
    /* Number of slots in match_list. */
    int match_list_size;

    /* The list of matches. */
    char **match_list = g_new (char *, (match_list_size = 30) + 1);

    /* Number of matches actually found. */
    int matches = 0;

    /* Temporary string binder. */
    char *string;

    match_list[1] = NULL;

    while ((string = (*entry_function) (text, matches)) != NULL){
        if (matches + 1 == match_list_size)
	    match_list = (char **) g_realloc (match_list, ((match_list_size += 30) + 1) * sizeof (char *));
        match_list[++matches] = string;
        match_list[matches + 1] = NULL;
    }

    /* If there were any matches, then look through them finding out the
       lowest common denominator.  That then becomes match_list[0]. */
    if (matches)
    {
        register int i = 1;
        int low = 4096;		/* Count of max-matched characters. */

        /* If only one match, just use that. */
        if (matches == 1){
	    match_list[0] = match_list[1];
	    match_list[1] = NULL;
        } else {
            int j;
            
	    qsort (match_list + 1, matches, sizeof (char *), match_compare);

	    /* And compare each member of the list with
	       the next, finding out where they stop matching. 
	       If we find two equal strings, we have to put one away... */

	    j = i + 1;
	    while (j < matches + 1)
	    {
		register int c1, c2, si;

		for (si = 0;(c1 = match_list [i][si]) && (c2 = match_list [j][si]); si++)
		    if (c1 != c2) break;
		
		if (!c1 && !match_list [j][si]){ /* Two equal strings */
		    g_free (match_list [j]);
		    j++;
		    if (j > matches)
		        break;
		    continue; /* Look for a run of equal strings */
		} else
	            if (low > si) low = si;
		if (i + 1 != j) /* So there's some gap */
		    match_list [i + 1] = match_list [j];
	        i++; j++;
	    }
	    matches = i;
            match_list [matches + 1] = NULL;
	    match_list[0] = g_strndup(match_list[1], low);
	}
    } else {				/* There were no matches. */
        g_free (match_list);
        match_list = NULL;
    }
    return match_list;
}

/* Check if directory completion is needed */
static int
check_is_cd (const char *text, int start, int flags)
{
    const char *p, *q;

    if (flags & INPUT_COMPLETE_CD)
	return 1;

    if (!(flags & INPUT_COMPLETE_COMMANDS))
	return 0;

    /* Skip initial spaces */
    p = text;
    q = text + start;
    while (p < q && *p && isspace ((unsigned char) *p))
	p++;

    /* Check if the command is "cd" and the cursor is after it */
    if (p[0] == 'c' && p[1] == 'd' && isspace ((unsigned char) p[2])
	&& (p + 2 < q))
	return 1;

    return 0;
}

/* Returns an array of matches, or NULL if none. */
static char **
try_complete (char *text, int *start, int *end, int flags)
{
    int in_command_position = 0, i;
    char *word, c;
    char **matches = NULL;
    const char *command_separator_chars = ";|&{(`";
    char *p = NULL, *q = NULL, *r = NULL;
    int is_cd = check_is_cd (text, *start, flags);

    ignore_filenames = 0;
    c = text [*end];
    text [*end] = 0;
    word = g_strdup (text + *start);
    text [*end] = c;

    /* Determine if this could be a command word. It is if it appears at
       the start of the line (ignoring preceding whitespace), or if it
       appears after a character that separates commands. And we have to
       be in a INPUT_COMPLETE_COMMANDS flagged Input line. */
    if (!is_cd && (flags & INPUT_COMPLETE_COMMANDS)){
        i = *start - 1;
        while (i > -1 && (text[i] == ' ' || text[i] == '\t'))
            i--;
        if (i < 0)
	    in_command_position++;
        else if (strchr (command_separator_chars, text[i])){
            register int this_char, prev_char;

            in_command_position++;
            
            if (i){
                /* Handle the two character tokens `>&', `<&', and `>|'.
                   We are not in a command position after one of these. */
                this_char = text[i];
                prev_char = text[i - 1];

                if ((this_char == '&' && (prev_char == '<' || prev_char == '>')) ||
	            (this_char == '|' && prev_char == '>'))
	            in_command_position = 0;
                else if (i > 0 && text [i-1] == '\\') /* Quoted */
	            in_command_position = 0;
	    }
	}
    }

    if (flags & INPUT_COMPLETE_COMMANDS)
    	p = strrchr (word, '`');
    if (flags & (INPUT_COMPLETE_COMMANDS | INPUT_COMPLETE_VARIABLES))
        q = strrchr (word, '$');
    if (flags & INPUT_COMPLETE_HOSTNAMES)    
        r = strrchr (word, '@');
    if (q && q [1] == '(' && INPUT_COMPLETE_COMMANDS){
    	if (q > p)
    	    p = q + 1;
    	q = NULL;
    }
    
    /* Command substitution? */
    if (p > q && p > r){
        matches = completion_matches (p + 1, command_completion_function);
        if (matches)
            *start += p + 1 - word;
    }

    /* Variable name? */
    else if  (q > p && q > r){
        matches = completion_matches (q, variable_completion_function);
        if (matches)
            *start += q - word;
    }

    /* Starts with '@', then look through the known hostnames for 
       completion first. */
    else if (r > p && r > q){
        matches = completion_matches (r, hostname_completion_function);
        if (matches)
            *start += r - word;
    }
        
    /* Starts with `~' and there is no slash in the word, then
       try completing this word as a username. */
    if (!matches && *word == '~' && (flags & INPUT_COMPLETE_USERNAMES) && !strchr (word, PATH_SEP))
        matches = completion_matches (word, username_completion_function);


    /* And finally if this word is in a command position, then
       complete over possible command names, including aliases, functions,
       and command names. */
    if (!matches && in_command_position)
        matches = completion_matches (word, command_completion_function);
        
    else if (!matches && (flags & INPUT_COMPLETE_FILENAMES)){
    	if (is_cd)
    	    ignore_filenames = 1;
    	matches = completion_matches (word, filename_completion_function);
    	ignore_filenames = 0;
    	if (!matches && is_cd && *word != PATH_SEP && *word != '~'){
    	    char *p, *q = text + *start;
    	    
    	    for (p = text; *p && p < q && (*p == ' ' || *p == '\t'); p++);
    	    if (!strncmp (p, "cd", 2))
    	        for (p += 2; *p && p < q && (*p == ' ' || *p == '\t'); p++);
    	    if (p == q){
		char * const cdpath_ref = g_strdup (getenv ("CDPATH"));
		char *cdpath = cdpath_ref;
		char c, *s, *r;

		if (cdpath == NULL)
		    c = 0;
		else
		    c = ':';
		while (!matches && c == ':'){
		    s = strchr (cdpath, ':');
		    if (s == NULL)
		        s = strchr (cdpath, 0);
		    c = *s; 
		    *s = 0;
		    if (*cdpath){
			r = concat_dir_and_file (cdpath, word);
		        ignore_filenames = 1;
    	    		matches = completion_matches (r, filename_completion_function);
    	    		ignore_filenames = 0;
    	    		g_free (r);
		    }
		    *s = c;
		    cdpath = s + 1;
		}
		g_free (cdpath_ref);
    	    }
    	}
    }

    g_free (word);

    return matches;
}

void free_completions (WInput *in)
{
    char **p;
    
    if (!in->completions)
    	return;
    for (p=in->completions; *p; p++)
    	g_free (*p);
    g_free (in->completions);
    in->completions = NULL;
}

static int query_height, query_width;
static WInput *input;
static int min_end;
static int start, end;

static int insert_text (WInput *in, char *text, ssize_t len)
{
    len = min (len, (ssize_t) strlen (text)) + start - end;
    if (strlen (in->buffer) + len >= (size_t) in->current_max_len){
    /* Expand the buffer */
    	char *narea = g_realloc (in->buffer, in->current_max_len + len + in->field_len);
	if (narea){
	    in->buffer = narea;
	    in->current_max_len += len + in->field_len;
	}
    }
    if (strlen (in->buffer)+1 < (size_t) in->current_max_len){
    	if (len > 0){
	    int i = strlen (&in->buffer [end]);
	    for (; i >= 0; i--)
	        in->buffer [end + len + i] = in->buffer [end + i];
	} else if (len < 0){
	    char *p = in->buffer + end + len, *q = in->buffer + end;
	    while (*q)
	    	*(p++) = *(q++);
	    *p = 0;
	}
	memcpy (in->buffer + start, text, len - start + end);
	in->point += len;
	update_input (in, 1);
	end += len;
    }
    return len != 0;
}

static cb_ret_t
query_callback (Dlg_head *h, dlg_msg_t msg, int parm)
{
    switch (msg) {
    case DLG_KEY:
	switch (parm) {
	case KEY_LEFT:
	case KEY_RIGHT:
	    h->ret_value = 0;
	    dlg_stop (h);
	    return MSG_HANDLED;

	case KEY_BACKSPACE:
	    if (end == min_end) {
		h->ret_value = 0;
		dlg_stop (h);
		return MSG_HANDLED;
	    } else {
		WLEntry *e, *e1;

		e1 = e = ((WListbox *) (h->current))->list;
		do {
		    if (!strncmp
			(input->buffer + start, e1->text,
			 end - start - 1)) {
			listbox_select_entry ((WListbox *) (h->current),
					      e1);
			handle_char (input, parm);
			end--;
			send_message (h->current, WIDGET_DRAW, 0);
			break;
		    }
		    e1 = e1->next;
		} while (e != e1);
	    }
	    return MSG_HANDLED;

	default:
	    if (parm > 0xff || !is_printable (parm)) {
		if (is_in_input_map (input, parm) == 2) {
		    if (end == min_end)
			return MSG_HANDLED;
		    h->ret_value = B_USER;	/* This means we want to refill the
						   list box and start again */
		    dlg_stop (h);
		    return MSG_HANDLED;
		} else
		    return MSG_NOT_HANDLED;
	    } else {
		WLEntry *e, *e1;
		int need_redraw = 0;
		int low = 4096;
		char *last_text = NULL;

		e1 = e = ((WListbox *) (h->current))->list;
		do {
		    if (!strncmp
			(input->buffer + start, e1->text, end - start)) {
			if (e1->text[end - start] == parm) {
			    if (need_redraw) {
				register int c1, c2, si;

				for (si = end - start + 1;
				     (c1 = last_text[si])
				     && (c2 = e1->text[si]); si++)
				    if (c1 != c2)
					break;
				if (low > si)
				    low = si;
				last_text = e1->text;
				need_redraw = 2;
			    } else {
				need_redraw = 1;
				listbox_select_entry ((WListbox *) (h->
								    current),
						      e1);
				last_text = e1->text;
			    }
			}
		    }
		    e1 = e1->next;
		} while (e != e1);
		if (need_redraw == 2) {
		    insert_text (input, last_text, low);
		    send_message (h->current, WIDGET_DRAW, 0);
		} else if (need_redraw == 1) {
		    h->ret_value = B_ENTER;
		    dlg_stop (h);
		}
	    }
	    return MSG_HANDLED;
	}
	break;

    default:
	return default_dlg_callback (h, msg, parm);
    }
}

#define DO_INSERTION 1
#define DO_QUERY     2
/* Returns 1 if the user would like to see us again */
static int
complete_engine (WInput *in, int what_to_do)
{
    if (in->completions && in->point != end)
    	free_completions (in);
    if (!in->completions){
    	end = in->point;
        for (start = end ? end - 1 : 0; start > -1; start--)
    	    if (strchr (" \t;|<>", in->buffer [start]))
    	        break;
    	if (start < end)
    	    start++;
    	in->completions = try_complete (in->buffer, &start, &end, in->completion_flags);
    }
    if (in->completions){
    	if (what_to_do & DO_INSERTION || ((what_to_do & DO_QUERY) && !in->completions[1])) {
    	    if (insert_text (in, in->completions [0], strlen (in->completions [0]))){
    	        if (in->completions [1])
    	    	    beep ();
		else
		    free_completions (in);
	    } else
	        beep ();
        }
    	if ((what_to_do & DO_QUERY) && in->completions && in->completions [1]) {
    	    int maxlen = 0, i, count = 0;
    	    int x, y, w, h;
    	    int start_x, start_y;
    	    char **p, *q;
    	    Dlg_head *query_dlg;
    	    WListbox *query_list;
    	    
    	    for (p=in->completions + 1; *p; count++, p++)
    	    	if ((i = strlen (*p)) > maxlen)
    	    	    maxlen = i;
    	    start_x = in->widget.x;
    	    start_y = in->widget.y;
    	    if (start_y - 2 >= count) {
    	    	y = start_y - 2 - count;
    	    	h = 2 + count;
    	    } else {
    	    	if (start_y >= LINES - start_y - 1) {
    	    	    y = 0;
    	    	    h = start_y;
    	    	} else {
    	    	    y = start_y + 1;
    	    	    h = LINES - start_y - 1;
    	    	}
    	    }
    	    x = start - in->first_shown - 2 + start_x;
    	    w = maxlen + 4;
    	    if (x + w > COLS)
    	    	x = COLS - w;
    	    if (x < 0)
    	    	x = 0;
    	    if (x + w > COLS)
    	    	w = COLS;
    	    input = in;
    	    min_end = end;
	    query_height = h;
	    query_width  = w;
    	    query_dlg = create_dlg (y, x, query_height, query_width,
				    dialog_colors, query_callback,
				    "[Completion]", NULL, DLG_COMPACT);
    	    query_list = listbox_new (1, 1, w - 2, h - 2, NULL);
    	    add_widget (query_dlg, query_list);
    	    for (p = in->completions + 1; *p; p++)
    	    	listbox_add_item (query_list, 0, 0, *p, NULL);
    	    run_dlg (query_dlg);
    	    q = NULL;
    	    if (query_dlg->ret_value == B_ENTER){
    	    	listbox_get_current (query_list, &q, NULL);
    	    	if (q)
    	    	    insert_text (in, q, strlen (q));
    	    }
    	    if (q || end != min_end)
    	    	free_completions (in);
    	    i = query_dlg->ret_value; /* B_USER if user wants to start over again */
    	    destroy_dlg (query_dlg);
    	    if (i == B_USER)
    	    	return 1;
    	}
    } else
    	beep ();
    return 0;
}

void complete (WInput *in)
{
    int engine_flags;

    if (in->completions)
	engine_flags = DO_QUERY;
    else
    {
	engine_flags = DO_INSERTION;

	if (show_all_if_ambiguous)
	    engine_flags |= DO_QUERY;
    }

    while (complete_engine (in, engine_flags));
}

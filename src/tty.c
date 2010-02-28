/*
   Interface to the terminal controlling library.

   Copyright (C) 2005 The Free Software Foundation, Inc.

   Written by:
   Roland Illig <roland.illig@gmx.de>, 2005.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.
 */

#include <config.h>

#include <signal.h>
#include <stdarg.h>

#include "global.h"
#include "color.h"
#include "main.h"		/* for slow_terminal */

#ifdef USE_NCURSES
#define WANT_TERM_H
#endif
#include "tty.h"

/*** file scope macro definitions **************************************/

#ifndef HAVE_SLANG
#   define acs()
#   define noacs()
#endif

/*** global variables **************************************************/

/*** file scope type declarations **************************************/

/*** file scope variables **********************************************/

static volatile sig_atomic_t got_interrupt = 0;

/*** file scope functions **********************************************/

static void
sigintr_handler(int signo)
{
    (void) &signo;
    got_interrupt = 1;
}

/*** public functions **************************************************/

extern void
tty_enable_interrupt_key(void)
{
    struct sigaction act;

    got_interrupt = 0;
    act.sa_handler = sigintr_handler;
    sigemptyset (&act.sa_mask);
    act.sa_flags = 0;
    sigaction (SIGINT, &act, NULL);
}

extern void
tty_disable_interrupt_key(void)
{
    struct sigaction act;

    act.sa_handler = SIG_IGN;
    sigemptyset (&act.sa_mask);
    act.sa_flags = 0;
    sigaction (SIGINT, &act, NULL);
}

extern gboolean
tty_got_interrupt(void)
{
    gboolean rv;

    rv = (got_interrupt != 0);
    got_interrupt = 0;
    return rv;
}

extern void
tty_gotoyx(int y, int x)
{
#ifdef HAVE_SLANG
    SLsmg_gotorc(y, x);
#else
    move(y, x);
#endif
}

extern void
tty_getyx(int *py, int *px)
{
#ifdef HAVE_SLANG
    *px = SLsmg_get_column();
    *py = SLsmg_get_row();
#else
    getyx(stdscr, *py, *px);
#endif
}

extern void
tty_setcolor(int c)
{
    attrset(c);
}

extern void
tty_print_char(int c)
{
#ifdef HAVE_SLANG
    /* We cannot use SLsmg_write_char here because the Debian and Redhat
     * people thought changing the API of an external project was fun,
     * especially when it depends on the preprocessor symbol UTF8 being
     * defined or not. Congratulations! At least, they left the API call
     * for SLsmg_write_nchars as it has always been.
     */
    char ch;

    ch = c;
    SLsmg_write_nchars(&ch, 1);
#else
    addch(c);
#endif
}

extern void
tty_print_alt_char(int c)
{
#ifdef HAVE_SLANG
    SLsmg_draw_object(SLsmg_get_row(), SLsmg_get_column(), c);
#else
    acs();
    addch(c);
    noacs();
#endif
}

extern void
tty_print_string(const char *s)
{
#ifdef HAVE_SLANG
    SLsmg_write_string(str_unconst(s));
#else
    addstr(s);
#endif
}

extern void
tty_print_one_hline(void)
{
    if (slow_terminal)
	tty_print_char(' ');
    else
	tty_print_alt_char(ACS_HLINE);
}

extern void
tty_print_one_vline(void)
{
    if (slow_terminal)
	tty_print_char(' ');
    else
	tty_print_alt_char(ACS_VLINE);
}

extern void
tty_print_hline(int top, int left, int length)
{
    int i;

    tty_gotoyx(top, left);
    for (i = 0; i < length; i++)
	tty_print_one_hline();
}

extern void
tty_print_vline(int top, int left, int length)
{
    int i;

    tty_gotoyx(top, left);
    for (i = 0; i < length; i++) {
	tty_gotoyx(top + i, left);
	tty_print_one_vline();
    }
}

extern void
tty_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
#ifdef HAVE_SLANG
    SLsmg_vprintf(str_unconst(fmt), args);
#else
    vw_printw(stdscr, fmt, args);
#endif
    va_end(args);
}

extern char *
tty_tgetstr (const char *cap)
{
#ifdef HAVE_SLANG
    return SLtt_tgetstr (str_unconst (cap));
#else
    {
	char *unused;
	return tgetstr (str_unconst (cap), &unused);
    }
#endif
}

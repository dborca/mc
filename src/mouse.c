/* Mouse managing
   Copyright (C) 1994 Miguel de Icaza.
   
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

/* Events received by clients of this library have their coordinates 0 */
/* based */

#include <config.h>

#include <stdio.h>

#include <sys/types.h>
#include <unistd.h>

#include "global.h"
#include "tty.h"
#include "mouse.h"
#include "key.h"		/* define sequence */

int mouse_enabled = 0;
const char *xmouse_seq;

#ifdef HAVE_LIBGPM
void show_mouse_pointer (int x, int y)
{
    if (use_mouse_p == MOUSE_GPM) {
	Gpm_DrawPointer (x, y, gpm_consolefd);
    }
}
#endif /* HAVE_LIBGPM */

void init_mouse (void)
{
    switch (use_mouse_p) {
#ifdef HAVE_LIBGPM
    case MOUSE_NONE:
	use_mouse_p = MOUSE_GPM;
	break;
#endif /* HAVE_LIBGPM */
    case MOUSE_XTERM_NORMAL_TRACKING:
    case MOUSE_XTERM_BUTTON_EVENT_TRACKING:
	define_sequence (MCKEY_MOUSE, xmouse_seq, MCKEY_NOACTION);
	break;
    default:
	break;
    }
    enable_mouse ();
}

void enable_mouse (void)
{
    if (mouse_enabled) {
	return;
    }

    switch (use_mouse_p) {
#ifdef HAVE_LIBGPM
    case MOUSE_GPM:
	{
	    int mouse_d;
	    Gpm_Connect conn;

	    conn.eventMask   = ~GPM_MOVE;
	    conn.defaultMask = GPM_MOVE;
	    conn.minMod      = 0;
	    conn.maxMod      = 0;

	    mouse_d = Gpm_Open (&conn, 0);
	    if (mouse_d == -1) {
		use_mouse_p = MOUSE_NONE;
	        return;
	    }
	    mouse_enabled = 1;
	}
	break;
#endif /* HAVE_LIBGPM */
    case MOUSE_XTERM_NORMAL_TRACKING:
	/* save old highlight mouse tracking */
	printf(ESC_STR "[?1001s");

	/* enable mouse tracking */
	printf(ESC_STR "[?1000h");

	fflush (stdout);
	mouse_enabled = 1; 
	break;
    case MOUSE_XTERM_BUTTON_EVENT_TRACKING:
	/* save old highlight mouse tracking */
	printf(ESC_STR "[?1001s");

	/* enable mouse tracking */
	printf(ESC_STR "[?1002h");

	fflush (stdout);
	mouse_enabled = 1; 
	break;
    default:
	break;
    }
}

void disable_mouse (void)
{
    if (!mouse_enabled) {
	return;
    }

    mouse_enabled = 0;

    switch (use_mouse_p) {
#ifdef HAVE_LIBGPM
    case MOUSE_GPM:
	Gpm_Close ();
	break;
#endif
    case MOUSE_XTERM_NORMAL_TRACKING:
	/* disable mouse tracking */
	printf(ESC_STR "[?1000l");

	/* restore old highlight mouse tracking */
	printf(ESC_STR "[?1001r");

	fflush (stdout);
	break;
    case MOUSE_XTERM_BUTTON_EVENT_TRACKING:
	/* disable mouse tracking */
	printf(ESC_STR "[?1002l");

	/* restore old highlight mouse tracking */
	printf(ESC_STR "[?1001r");

	fflush (stdout);
	break;
    default:
	break;
    }
}

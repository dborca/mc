/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   Character set handling
   Copyright (C) Andrew Tridgell 1992-1998
   
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

#ifndef CHARSET_C

extern char *dos_char_map;
extern char *upper_char_map;
extern char *lower_char_map;

#ifdef toupper
#undef toupper
#endif

#ifdef tolower
#undef tolower
#endif

#ifdef isupper
#undef isupper
#endif

#ifdef islower
#undef islower
#endif

#ifdef isdoschar
#undef isdoschar
#endif

#ifdef isspace
#undef isspace
#endif

#define toupper(c) (upper_char_map[(c&0xff)] & 0xff)
#define tolower(c) (lower_char_map[(c&0xff)] & 0xff)
#define isupper(c) ((c&0xff) != tolower(c&0xff))
#define islower(c) ((c&0xff) != toupper(c&0xff))
#define isdoschar(c) (dos_char_map[(c&0xff)] != 0)
#define isspace(c) ((c)==' ' || (c) == '\t')

/* this is used to determine if a character is safe to use in
   something that may be put on a command line */
#define issafe(c) (isalnum((c&0xff)) || strchr("-._",c))
#endif		/* !CHARSET_C */

/* Dynamic codepage files defines. */

/* Version id for dynamically loadable codepage files. */
#define CODEPAGE_FILE_VERSION_ID 0x1
/* Version 1 codepage file header size. */
#define CODEPAGE_HEADER_SIZE 8
/* Offsets for codepage file header entries. */
#define CODEPAGE_VERSION_OFFSET 0
#define CODEPAGE_CLIENT_CODEPAGE_OFFSET 2
#define CODEPAGE_LENGTH_OFFSET 4

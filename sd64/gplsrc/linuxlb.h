/* LINUXLB.H
 * Windows library substitutes for Linux.
 * Copyright (c) 2002 Ladybridge Systems, All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * 
 * START-HISTORY:
 * 31 Dec 23 SD launch - prior history suppressed
 * END-HISTORY
 *
 * START-DESCRIPTION:
 *
 * END-DESCRIPTION
 *
 * START-CODE
 */

#ifndef __LINUXLB
#define __LINUXLB

/* Inline macros */

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

/* Simple substitutes */

#define chsize(fd, bytes) ftruncate(fd, bytes)
#define GetCurrentProcessId() getpid()
#define stricmp(a, b) strcasecmp(a, b)

#define chsize64 chsize

/* Functions in linuxlb.c */

int64 filelength64(int fd);
#define filelength(f) (int)filelength64(f)
bool GetUserName(char* name, u_int32_t* bytes);
char* itoa(int value, char* string, int radix);
char* Ltoa(int32_t value, char* string, int radix);
char* sdrealpath(char* inpath, char* outpath);
void Sleep(int32_t n);
void strrep(char* s, char old, char new);

#endif

/* END-CODE */

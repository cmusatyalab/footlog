/* 
   Copyright (C) 2021 Carnegie Mellon University

   This code is distributed "AS IS" without warranty of any kind under
   the terms of the GNU General Public Licence Version 3, or any later
   version of that license.

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

extern int DebugFlag;

/* Minimum gap size in milliseconds between occurrences of the
   keystroke "1" that should be considered significant.  A series of
   closer-spaced keystrokes are considered to be part of the same foot
   pedal press
*/
extern int GapSize;  /* can be changed by "-g <value>" on command line */

/* Length of buffers used as globals for device information */
#define BUFLEN 1000   /* way too much, but playing it safe */

/* Global pathname of temp file for shell commands */
extern char tmpfilename[];

/* Global string values filled in by discovering foot pedal device */
extern char fpbus[], fpdevice[], fpid1[], fpid2[], fpdescription[];

/* Global string for synthesized pathname  of footpedal device */
extern char fppathname[];

/* Global defining number of milliseconds between event checks */
extern int evmilli;

/* Global log file details */
extern char LogFileName[];
extern FILE *LogFile;


extern void exec_shellcmd (char *);
extern int usbstuff_discover();
extern int usbstuff_disable();
extern void scan_devices();
extern void logevents();

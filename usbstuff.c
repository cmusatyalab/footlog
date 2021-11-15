/* 
   Copyright (C) 2021 Carnegie Mellon University

   This code is distributed "AS IS" without warranty of any kind under
   the terms of the GNU General Public Licence Version 2.

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <string.h>

#include "footlog.h"


int usbstuff_discover() 
/* Find footpedal USB device and fill its global values;
   returns 0 on success and globals are filled;
   returns -1 on failure and globals are undefined.
   System-level errors cause exit with error message
*/
{
  static char oneline[BUFLEN];
  int rc;
  FILE *shellout;
  char *s, *where;

  exec_shellcmd("lsusb"); /* successful if control returns */

  shellout = fopen(tmpfilename, "r");
  if (!shellout) {
    perror(tmpfilename);
    exit(-1);
  }

  where = 0; /* not found yet */
  while (!feof(shellout)) {
    s = fgets(oneline, sizeof(oneline)-1, shellout);
    if (!s) {
      if (feof(shellout)) break; /* hit EOF */
      else
	{/* something abnormal */
	  perror(tmpfilename);
	  exit(-1);
	}
    }
    where = strstr(oneline, "QinHeng Electronics");
    if (where) break; /* found it! */
  }
  if (!where) return (-1); /* device not found */

  /* Can't use %s for fpdescription because of possible whitespace */
  rc = sscanf(oneline, "Bus %3s Device %3s: ID %4s:%4s%[^\n]",
	      fpbus, fpdevice, fpid1, fpid2, fpdescription);
  if (rc != 5) {
    fprintf(stderr, "sscanf() returns %d rather than 5\n", rc);
    exit(-1);
  }

  return(0); /* success */
}



int usbstuff_disable() 
{
  static char oneline[BUFLEN], target[BUFLEN];
  int rc, xinput_id, devcount;
  FILE *shellout;
  char *where;

  /* Obtain list of X input devices */
  exec_shellcmd("xinput --list");

  /* Construct search target */
  snprintf(target, sizeof(target)-1, "HID %s:%s", fpid1, fpid2);
  if (DebugFlag) fprintf(stderr, "Looking for \"%s\"\n", target);

  /* Look for relevant lines in stdout */
  shellout = fopen(tmpfilename, "r");
  if (!shellout) {
    perror(tmpfilename);
    exit(-1);
  }
  devcount = 0; /* how many devices were disabled */
  while (!feof(shellout)) {
    if (!fgets(oneline, sizeof(oneline)-1, shellout)) {
      if (feof(shellout)) break; /* just hit EOF */
      perror(tmpfilename);
      exit(-1);
    }
    if (DebugFlag) fprintf(stderr, "Next: %s\n", oneline);

    where = strstr(oneline, target);
    if (!where) continue; /* not found in this line */

    /* Yes, found it; can't quit loop since device may be listed both
       as mouse and keyboard; need to disable all occurrences */
    where = strstr(oneline, "id=");
    if (!where) goto ErrorExit;
    rc = sscanf(where, "id=%d", &xinput_id);
    if (rc != 1) goto ErrorExit;
    if (DebugFlag) fprintf(stderr, "xinput id = %d\n", xinput_id);

    /* reuse oneline to construct shell command */
    snprintf(oneline, sizeof(oneline)-1, "xinput disable %d", xinput_id);
    exec_shellcmd(oneline); /* exit on error */
    devcount++; /* one more disabled */
  }

  if (DebugFlag) {
    fprintf(stderr, "usbstuff_disable() disabled %d devices\n", devcount);
  }

 SuccessExit:
  return(devcount); 

 ErrorExit:
  fprintf(stderr, "Can't find id of xinput device %s:%s\n", fpid1, fpid2);
  exit(-1);
}


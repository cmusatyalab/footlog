/* 
   Copyright (C) 2021 Carnegie Mellon University

   This code is distributed "AS IS" without warranty of any kind under
   the terms of the GNU General Public Licence Version 3, or any later
   version of that license.

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
#include <libusb-1.0/libusb.h>
#include <libudev.h>
#include <libgen.h>

#include "footlog.h"


int DebugFlag = 0; /* set by "-d" command line option */

int GapSize = 1000;  /* default value is 1000 milliseconds; change via "-g"  */


/* File used for stdout an stderr of shell commands via "system"
   Removed at the end of successful execution.
   Left behind for debugging in case of error exit */
char tmpfilename[BUFLEN];

/* Global values to hold information about discovered foot pedal device 
   All are zeroed out initially  by C semantics of globals */
char fpbus[BUFLEN], fpdevice[BUFLEN], fpid1[BUFLEN], 
  fpid2[BUFLEN], fpdescription[BUFLEN];

/* File where log entries are written */
char LogFileName[BUFLEN] = "/var/log/footlog/events.log"; /* can be changed by "-f" */
FILE *LogFile = 0; /* pointer to log file after it is opened */


void parseargs (int argc, char **argv) 
{
  int i; 

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-d")) {
      DebugFlag = 1;
      continue;
    }

    if (!strcmp(argv[i], "-t")) {
      if ((i+1) >= argc) goto ArgError;

      evmilli = atoi(argv[i+1]);
      fprintf(stderr, "evmilli = %d\n", evmilli);
      i++;
      continue;
    }

    if (!strcmp(argv[i], "-g")) {
      if ((i+1) >= argc) goto ArgError;

      GapSize  = atoi(argv[i+1]);
      fprintf(stderr, "GapSize = %d\n", GapSize);
      i++;
      continue;
    }

    if (!strcmp(argv[i], "-f")) {
      if ((i+1) >= argc) goto ArgError;
      if (strlen(argv[i+1]) >= sizeof(LogFile)) goto ArgError; /* make sure we have enough space */
      sscanf(argv[i+1], "%s", LogFileName);
      fprintf(stderr, "LogFile is \"%s\"\n", LogFileName);
      i++;
      continue;
    }


    /* error exit */
    ArgError:
    fprintf(stderr, "Usage: footlog [-d] [-t <milliseconds>]\n");
    exit(-1);
  }
}

void mktempfile()
{
  int shellfd;

  /* Create temp file for shell commands and save in global */
  sprintf(tmpfilename, "/tmp/footlog_XXXXXX");
  shellfd = mkstemp(tmpfilename); /* for stdout and stderr of shell commands */
  if (shellfd < 0) {
    perror("Can't create temporary file");
    exit(-1);
  }

  /* We should close shellfd here
     But I am not sure if the file will get garbage collected
     Leaving open, but note that shellfd is not defined as static
     Success exit from main() explicitly unlinks this file
  */
}

void exec_shellcmd (char *cmd)
/* Execute specified shell command and send stdout and stderr
   to file specified in global tmpfilename. Exit on errors */
{
  char shellcmd[BUFLEN];
  int rc;

  snprintf(shellcmd, sizeof(shellcmd)-1, "#!/bin/bash\n%s > %s 2>&1\n", 
	   cmd, tmpfilename);
  if (DebugFlag) fprintf(stderr, "Shellcmd is \"%s\"\n", shellcmd);
  rc = system(shellcmd);
  if (rc) { /* zero exit status assumed for all successful cmds */
    fprintf(stderr, "Shell command \"%s\" failed, see %s for reason\n", shellcmd, tmpfilename);
    exit(-1);
  }
}

void OpenWithSave()
/* 
  Check if LogFileName already exists.  If it does, rename it to use
  suffix of timestamp in first line.  Open LogFileName and save the
  pointer in LogFile.
*/
{ 
  char oneline[BUFLEN], tfull[BUFLEN], tbase[BUFLEN], *lfn, *dn; 
  struct tm *tp;
  time_t sec;  /** doesn't work if I use "int" */
  int msec, rc;
  struct stat sb;

  /* Obtain directory name */
  lfn = strdup(LogFileName);  /* need this intermediate because dirname() clobbers */
  dn = dirname(lfn);

  /* Check if LogFileName exists and obtain its first timestamp  */
  LogFile = fopen(LogFileName, "r");
  if (!LogFile) {
    if (errno == ENOENT) goto CreateNewFile; /* OK, no such file exists */
    perror(LogFileName); /* something more seriously wrong */
    exit(-1);
  }

  /* Is it a zero length file by any chance? */
  rc = fstat(fileno(LogFile), &sb);
  if (rc < 0) {
    perror(LogFileName);
    exit(-1);
  }
  if (sb.st_size == 0) {
    /* just delete it */
    if (DebugFlag) fprintf(stderr, "Zero length old log file\n");
    rc = unlink(LogFileName);
    if (rc < 0) {
      perror(LogFileName);
      exit(-1);
    }
    if (DebugFlag) fprintf(stderr, "Deleted %s\n", LogFileName);
    goto CreateNewFile;
  }


  if (!fgets(oneline, sizeof(oneline)-1, LogFile)) {
    /* file exists, but can't read it */
    perror(LogFileName);
    exit(-1);
  }

  if (sscanf(oneline, "%ld.%d: DOWN", &sec, &msec) != 2) {
    fprintf(stderr, "Can't understand log file format\n");
    fprintf(stderr, "First line is: \"%s\"\n", oneline);
    exit(-1);
  }
  if (DebugFlag) fprintf(stderr, "LogFile Timestamp = %ld.%d\n", sec, msec);
  
  /* Construct filename of rename() target */
  tp = localtime(&sec);
  strftime(tbase, sizeof(tbase)-1, "events-%Y-%m-%d-%H-%M-%S", tp);
  if (DebugFlag)  fprintf(stderr, "tbase = \"%s\"\n", tbase);
  snprintf(tfull, sizeof(tfull)-1, "%s/%s-%03d.log", dn, tbase, msec);
  if (DebugFlag)  fprintf(stderr, "tfull = \"%s\"\n", tfull);


  rc =  rename(LogFileName, tfull);
  if (rc < 0) {
    perror(LogFileName);
    exit(-1);
  }
  /* Old LogFile has been saved under new name; now open the fresh file */

 CreateNewFile:

  /* make the directory, ignoring error if it alreads exists */
  rc = mkdir(dn, 0755);
  if (rc < 0) {
    if (errno != EEXIST){
      perror(dn);
      exit(-1);
    }
  }

  LogFile = fopen(LogFileName, "w");
  if (!LogFile) {
    perror(LogFileName);
    exit(-1);
  }

}


int main(int argc, char **argv)
{
  int rc;

  if (getuid() != 0) {
    fprintf(stderr, "ERROR: footlog has to be run  as root\n");
    exit(-1);
  }

  /* Process command line args */
  parseargs(argc, argv);
  mktempfile();  /* Available for reuse until exit */

  /* Open the log file */
  OpenWithSave();

  /* Find the foot pedal among USB devices */
  rc = usbstuff_discover();
  if (rc < 0) {
    fprintf(stderr, "Can't find footpedal device\n");
    exit(-1);
  }
  else {
    fprintf(stderr, "footpedal found: Bus %3s Device %3s: ID %4s:%4s %s\n",
	    fpbus, fpdevice, fpid1, fpid2, fpdescription);
  }

  /* Disable the foot pedal as input device to X windows */
  rc = usbstuff_disable();
  if (DebugFlag) fprintf(stderr, "xinput devices disabled = %d\n", rc);

  /* Discover event devices corresponding to the foot pedal */
  scan_devices();

  /* Listen for events on devices */
  logevents();

  /* Do clean up */
  unlink(tmpfilename);
  exit(0);
}






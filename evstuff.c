/* 
   Copyright (c) 2021 Carnegie Mellon University

   This code is distributed "AS IS" without warranty of any kind under
   the terms of the GNU General Public Licence Version 2.

   This code was derived from the source code of evtest.c
   in Ubuntu 18.4 and retains the copyrights listed below:

   Copyright (c) 1999-2000 Vojtech Pavlik
   Copyright (c) 2009-2011 Red Hat, Inc

*/

#define _GNU_SOURCE /* for asprintf */
#include <stdio.h>
#include <stdint.h>

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <linux/version.h>
#include <linux/input.h>

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "footlog.h"

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)	((array[LONG(bit)] >> OFF(bit)) & 1)

#define DEV_INPUT_EVENT "/dev/input"
#define EVENT_DEV_NAME "event"

/*
   To find definitions of event codes such as EV_MAX, EV_SYN, etc. look in:
   #include <linux/input-event-codes.h>
   (Satya, 2021-11-07)
*/

#ifndef EV_SYN
#define EV_SYN 0
#endif
#ifndef SYN_MAX
#define SYN_MAX 3
#define SYN_CNT (SYN_MAX + 1)
#endif
#ifndef SYN_MT_REPORT
#define SYN_MT_REPORT 2
#endif
#ifndef SYN_DROPPED
#define SYN_DROPPED 3
#endif

#define NAME_ELEMENT(element) [element] = #element

static int grab_flag = 0;

/* evdevices are devices on which to listen for events */
#define EVDEVMAX  6 /* maximum number of event devices; 1 or 2 is typical */
static int evdevcount = 0; /* actual number of event devices discovered */
static char *evdevpathname[EVDEVMAX] = {0,}; /* pathnames of above devices, 
						all initialized to null */
static int evdevfd[EVDEVMAX] = {-1,}; /* array of open fds of above devices */
int evmilli = 100; /* Number of milliseconds to sleep between event checks */

/* The following structure keeps together all the parts relating to a
   sequence of events that will be reported in an "UP" log entry at
   the end of the sequence */
typedef struct sequence {
  /* FirstOne_{sec,msec} marks the first of a (possibly long) sequence
   of KEY_1 events.  LastOne_{sec,msec} marks the most recent KEY_1
   event.  The sequence is ended by a gap of at least GapSize
   milliseconds before the next KEY_1 event. All other events are
   ignored for purposes of timing/logging.  */
  int FirstOne_sec;
  int FirstOne_msec;
  int LastOne_sec;
  int LastOne_msec;

  /* Cumulative event stats within current sequence. Reset to zero at
     start of current sequence.  We keep track of KEY_1 events
     separately in key1count.  All other EV_KEY events are lumped
     together in evcount[]
 */
  int key1count; /* how many KEY_1 events */
  int evcount[EV_MAX]; /* how many of each type of event */
} seq_t;;


/* 
   All the static const char* definitions below are brutally shortened
   from the original code in evtest.c.  We assume that the foot pedal
   is programmed to continuously emit "1" when pressed.  So that is
   the only key that we look for.  We treat everything else as
   "other".  Similarly, the only events of interest are key presses
   and releases. 
   (Satya, 2021-11-07)
*/

static const char * const events[EV_MAX + 1] = {
  [0 ... EV_MAX] = NULL,
  NAME_ELEMENT(EV_SYN),			
  NAME_ELEMENT(EV_KEY),
  NAME_ELEMENT(EV_MSC)
};

static const int maxval[EV_MAX + 1] = {
  [0 ... EV_MAX] = -1,
  [EV_SYN] = SYN_MAX,
  [EV_KEY] = KEY_MAX,
  [EV_MSC] = MSC_MAX
};

static const char * const keys[KEY_MAX + 1] = {
  [0 ... KEY_MAX] = NULL,
  NAME_ELEMENT(KEY_1) /* only one retained from evtest.c */
};

static const char * const misc[MSC_MAX + 1] = {
  [ 0 ... MSC_MAX] = NULL,
  NAME_ELEMENT(MSC_SCAN) /* only one retained from evtest.c */
};


static const char * const syns[SYN_MAX + 1] = {
	[0 ... SYN_MAX] = NULL,
	NAME_ELEMENT(SYN_REPORT),
	NAME_ELEMENT(SYN_CONFIG),
	NAME_ELEMENT(SYN_MT_REPORT),
	NAME_ELEMENT(SYN_DROPPED)
};

static const char * const * const names[EV_MAX + 1] = {
  [0 ... EV_MAX] = NULL,
  [EV_SYN] = syns,  
  [EV_KEY] = keys,
  [EV_MSC] = misc
};

static const char *typename(unsigned int type) {
  if (type > EV_MAX) return("?");
  if (!events[type]) return ("?");
  return(events[type]);
}

static const char* codename(unsigned int type, unsigned int code) {
  if (type > EV_MAX) return ("?");
  if (!names[type]) return ("?");
  if (code > maxval[type]) return ("?");
  if (!names[type][code]) return ("?");
  return(names[type][code]);
}


static int test_grab(int fd, int grab_flag)
/* Grab and immediately ungrab the device specified by fd
   Return 0 if the grab was successful, 1 otherwise */
{
  int rc;

  rc = ioctl(fd, EVIOCGRAB, (void*)1);
  if (rc == 0 && !grab_flag)
    ioctl(fd, EVIOCGRAB, (void*)0);
  return rc;
}

static int is_event_device(const struct dirent *dir) {
/* Filter for scandir() in scan_devices() 
   Returns 1 (TRUE) if given directory entry starts with "event"
   Returns 0 (FALSE)  otherwise
*/
  int rc = 0;
  rc = strncmp(EVENT_DEV_NAME, dir->d_name, 5);
  if (!rc) return (1);
  else return(0);
}

void scan_devices()
/* Fills the globals pertaining to evdevices by discovering them
   in /dev/input/event* */
{
  struct dirent **namelist;
  int i, rc, fd, ndev, devnum;
  int max_device = 0;
  char fname[BUFLEN], name[256], target[BUFLEN];
  char *where;

  ndev = scandir(DEV_INPUT_EVENT, &namelist, is_event_device, versionsort);
  if (ndev <= 0) return;

  if (DebugFlag) fprintf(stderr, "%d available devices:\n", ndev);
  for (i = 0; i < ndev; i++) {
    snprintf(fname, sizeof(fname),
	     "%s/%s", DEV_INPUT_EVENT, namelist[i]->d_name);
    if (DebugFlag) fprintf(stderr, "%s", fname);
    fd = -1;
    fd = open(fname, O_RDONLY);
    if (fd < 0)	continue; /* don't know why it failed, just ignore */

    sprintf(name, "???");
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    if (DebugFlag) fprintf(stderr, ":%s\n", name);
    close(fd);

    snprintf (target, sizeof(target)-1, "HID %s:%s", fpid1, fpid2);
    where = strstr(name, target);
    if (where) {/* match found! */
      if (evdevcount >= EVDEVMAX) {
	fprintf(stderr, "Too many event devices, evdevcount = %d\n", 
		evdevcount);
	exit(-1);
      }
      if (DebugFlag) fprintf(stderr, "Found target: %s\n", where);
      evdevpathname[evdevcount] = strdup(fname);
      if (DebugFlag) fprintf(stderr, "evdevpathname[%d] = \"%s\"\n", 
			     evdevcount, evdevpathname[evdevcount]);
      evdevcount++;
    }

    free(namelist[i]); /* allocated in bulk by scandir (); free one by one */
  }

  /* Open the event devices, obtain fds for them, and grab them */

  for (i = 0; i < evdevcount; i++) {
    evdevfd[i] = open(evdevpathname[i], O_RDONLY); 
    if (evdevfd[i] < 0) {
      perror(evdevpathname[i]);
      exit(-1);
    }
    if (DebugFlag) fprintf(stderr, "evdevfd[%d] = %d\n", i, evdevfd[i]);

    rc = ioctl(evdevfd[i], EVIOCGRAB, (void *)1);
    if (rc) { /* grab unsuccessful */
      fprintf(stderr, "grab ioctl() on %d (%s)failed\n",
	      evdevfd[i], evdevpathname[i]);
      exit(-1);
    }
  }

}


static void StartSequence(seq_t *cs, int now_sec, int now_msec)
   /* Using now_sec and now_msec as current time, initialize the
      sequence pointed to by cs and log the start */
{
  cs->FirstOne_sec = now_sec;
  cs->FirstOne_msec = now_msec;
  cs->LastOne_sec = now_sec;
  cs->LastOne_msec = now_msec;

  /*We shouldn't yet log the start of this sequence because it may
    prove to be a runt; do both UP and DOWN in EndSequence()  */
}


static void EndSequence(seq_t *cs, int gapsize)
/* gapsize is input parameter, expressed in milliseconds */
{
  int seqlen, k;

  /* End previous sequence */
  seqlen = (cs->LastOne_sec - cs->FirstOne_sec)*1000 + (cs->LastOne_msec - cs->FirstOne_msec);

  /* Sequence ended at LastOne, which was at least GapSize milliseconds ago.
     Log the end of this sequence */

  /* Ignore very short sequences as runts */
#define RUNTMAX 10  /* Sequences shorter than this number of milliseconds are runts */
  if (seqlen > RUNTMAX) {
    fprintf(LogFile, "%u.%03u: DOWN\n", cs->FirstOne_sec, cs->FirstOne_msec);
    fprintf(LogFile, "%u.%03u: UP  seqlen = %d ms  key1count = %d  gap = %d ms  ", 
	   cs->LastOne_sec, cs->LastOne_msec, seqlen, cs->key1count, gapsize);
    fprintf(LogFile, "evcounts:  ");
    for (k = 0; k < EV_MAX; k++) {
      if (!cs->evcount[k]) continue;
      else fprintf(LogFile, "%s = %d  ", typename(k), cs->evcount[k]);
    }
    fprintf(LogFile, "\n");
    fflush(LogFile);
  }
  else {
    if (DebugFlag) fprintf(stderr, "Runt of seqlen %d ms ignored \n", seqlen);
  }

  /* Initialize to detect the next sequence.  All values in cs remain
     zero until first new KEY_1 event. That's when a a new sequence starts */

  bzero(cs, sizeof(seq_t));
}

void  logevents()
{
  struct input_event ev[64];
  int i, j, numev, rd, fdlimit, rc;
  fd_set fdmask;
  seq_t curseq;  /* bookeeping for current sequence */
  struct timeval select_timeout;
  div_t divresult;

  /* Initialize select_timeout; unchanged after start */
  divresult = div(GapSize, 1000);
  select_timeout.tv_sec = divresult.quot;
  select_timeout.tv_usec = (divresult.rem)*1000; /* in microseconds */

  /* find value of "nfds" to use in select()  */
  fdlimit = 0; /* one greater than highest numbered fd */
  for (i = 0; i < evdevcount; i++) {
    if (evdevfd[i] > fdlimit) fdlimit = evdevfd[i] + 1;
  }

  /* Initialize current sequence */
  bzero(&curseq, sizeof(curseq)); /* everything is an int, so safe */

  /* Listen forever until terminated by signal */
  while (1) {
    /* clean up fdmask from last iteration and set up for this one */
    FD_ZERO(&fdmask); 
    for (j = 0; j < evdevcount; j++) {
      FD_SET(evdevfd[j], &fdmask);
    }

    /* If a sequence has already started, we need to timeout after
       GapSize millieconds so that we can write out the log record in
       a timely manner.  Otherwise, if a sequence has not started, we
       can afford to block indefinitely. */

    if (curseq.FirstOne_sec) {
      rc = select(fdlimit, &fdmask, NULL, NULL, &select_timeout);
    }
    else {
      rc = select(fdlimit, &fdmask, NULL, NULL, NULL);
    }

    if (!rc) {/* timeout happened */
      EndSequence(&curseq, GapSize);
      continue; /* outer while loop */
    }
    
    /* Some event happened; process all the fds that unblocked */
    for (j = 0; j < evdevcount; j++) {
      if (!FD_ISSET(evdevfd[j], &fdmask)) continue;

      rd = read(evdevfd[j], ev, sizeof(ev));
      if (rd < (int) sizeof(struct input_event)) {
	fprintf(stderr, "expected %d bytes, got %d\n",
	       (int) sizeof(struct input_event), rd);
	perror("\nevtest: error reading");
	exit(-1);
      }

      numev = rd / sizeof(struct input_event);
      if (DebugFlag) fprintf(stderr, "read %d events\n", numev);

      for (i = 0; i < numev; i++) {
	unsigned int type, code, value, sec, msec, gap;

	/* Process next event */

	type = ev[i].type;
	code = ev[i].code;
	value = ev[i].value;
	sec = ev[i].time.tv_sec;
	msec = ev[i].time.tv_usec/1000; /* milliseconds */
	if (DebugFlag) fprintf(stderr, "FirstOne_sec = %d   FirstOne_msec = %d   LastOne_sec = %d  LastOne_msec = %d\n", 
		curseq.FirstOne_sec, curseq.FirstOne_msec, curseq.LastOne_sec, curseq.LastOne_msec);
	if (curseq.LastOne_sec) {
	  gap = (sec - curseq.LastOne_sec)*1000 + (msec - curseq.LastOne_msec);
	}
	else gap = 0;

	/* TODO: Add code here to handle the corner case that the
	   footpedal was already pressed before footlog was started */

	if (DebugFlag) fprintf(stderr,
			       "Event: %u.%03u   gap = %d\n", sec, msec, gap); 

	if (!curseq.FirstOne_sec) {/* very first event, no gap */
	  if (DebugFlag) fprintf(stderr, "No KEY_1 seen yet to start sequence\n");
	}
	else {
	  /* End current sequence; log it; reset counters */
	  if (gap >= GapSize) EndSequence(&curseq, gap);
	}

	/* Tolerate slight skew here; We are counting this event even
	   though new sequence has not yet begun. */
	curseq.evcount[type]++; /* keep track of how events many of each type */

	if (type == EV_SYN) {
	  if (DebugFlag) fprintf(stderr, " ---- EV_SYN ----\n");
	  continue;
	}

	/* Type is other than EV_SYN */
	if (DebugFlag)	fprintf(stderr, "type %d (%s), code %d (%s)", 
				type, typename(type), code, codename(type, code));

	switch (type) {
	case EV_MSC:
	  if (DebugFlag) {
	    if (code == MSC_RAW || code == MSC_SCAN)
	      fprintf(stderr, ", value %02x\n", value);
	    else
	      fprintf(stderr, ", value %d\n", value);
	  }
	  break;

	case EV_KEY:
	  if (DebugFlag) {
	    /* ignore value, seems to have no use */
	    fprintf(stderr, "\n");
	  }
	  if (!strcmp(codename(type,code), "KEY_1")) {
	    curseq.key1count++;
	    if (curseq.FirstOne_sec == 0) {/* Begin new sequence */
	      StartSequence (&curseq, sec, msec);
	    }
	    else{ /* continue current sequence */
	      curseq.LastOne_sec = sec;
	      curseq.LastOne_msec = msec;
	    }
	  }
	  break;

	default:
	  break;

	}
      }
    }

    /* sleep briefly before checking again */
    struct timespec tt;
    divresult = div(evmilli, 1000);
    tt.tv_sec = divresult.quot;
    tt.tv_nsec = (divresult.rem)*1000000; /* in nanoseconds */
    if (DebugFlag) fprintf(stderr, "Sleep is %d seconds and %d millieconds\n",
			 (int) tt.tv_sec, (int) (tt.tv_nsec/1000000));

    rc = nanosleep(&tt, 0);
    if (rc) {
      perror("nanosleep");
      exit(-1);
    }
  }
}


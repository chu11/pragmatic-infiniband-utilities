/*
 * Copyright (C) 2007 The Regents of the University of California.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Ira Weiny weiny2@llnl.gov
 * UCRL-CODE-235440
 * 
 * This file is part of pragmatic-infiniband-tools (PIU), useful tools to manage
 * Infiniband Clusters.
 * For details, see http://www.llnl.gov/linux/.
 * 
 * PIU is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 * 
 * PIU is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * PIU; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <netdb.h>
#include <sys/time.h>
#include <assert.h>

#include "simple_benchmark.h"
#include "simple_benchmark_common.h"

#define GETHOSTBYNAME_AUX_BUFLEN      1024

extern char *host;

void
gethostbyname_r_common (struct hostent *hent)
{
  struct hostent *hentptr = NULL;
  int tmpherrno;
  char auxbuf[GETHOSTBYNAME_AUX_BUFLEN];

  assert (hent);

  if (gethostbyname_r (host,
		       hent,
		       auxbuf,
		       GETHOSTBYNAME_AUX_BUFLEN,
		       &hentptr,
		       &tmpherrno)
      || !hentptr
      || hentptr != hent)
    {
      fprintf (stderr, "gethostbyname_r: %s", hstrerror (tmpherrno));
      exit (1);
    }
}

void
elapsed_time_output (struct timeval *starttime, struct timeval *endtime)
{
  uint64_t elapsedtime;

  assert (starttime);
  assert (endtime);

  if (starttime->tv_sec == endtime->tv_sec)
    elapsedtime = endtime->tv_usec - starttime->tv_usec;
  else
    elapsedtime = (((endtime->tv_sec - starttime->tv_sec) - 1) * MICROSECOND_IN_SECOND) + (MICROSECOND_IN_SECOND - starttime->tv_usec) + (endtime->tv_usec);

  printf ("Elapsed Time: %llu microseconds\n", elapsedtime);
}

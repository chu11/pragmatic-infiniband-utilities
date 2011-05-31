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
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <malloc.h>
#include <assert.h>

#include "simple_benchmark.h"
#include "simple_benchmark_common.h"

#define GETHOSTBYNAME_AUX_BUFLEN      1024

extern char *host;
extern unsigned int blocksize;
extern unsigned int transfersize;
extern int memalign_flag;
extern uint16_t port;

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
calc_bufsize (size_t *bufsize)
{
  assert (bufsize);

  (*bufsize) = (uint64_t)blocksize * KILOBYTE;
}

uint8_t *
create_buf (void)
{
  uint8_t *buf;
  size_t s;

  calc_bufsize (&s);

  if (!memalign_flag)
    {
      if (!(buf = (uint8_t *)malloc (s)))
	{
	  perror ("malloc");
	  exit (1);
	}
    }
  else
    {
      long pagesize;

      errno = 0;
      pagesize = sysconf(_SC_PAGESIZE);
      if (pagesize < 0 && errno)
	{
	  perror ("sysconf");
	  exit (1);
	}

      if (!(buf = (uint8_t *)memalign (pagesize, s)))
	{
	  perror ("memalign");
	  exit (1);
	}
    }

  memset (buf, BLOCK_PATTERN, s);

  return buf;
}

void
calc_blocks (unsigned int *blocks)
{
  size_t s;

  assert (blocks);

  calc_bufsize (&s);

  (*blocks) = ((uint64_t)transfersize * MEGABYTE) / s;
  if (((uint64_t)transfersize * MEGABYTE) % s)
    (*blocks)++;
}

void
setup_client_serveraddr (struct sockaddr_in *serveraddr)
{
  struct hostent hent;

  assert (serveraddr);

  gethostbyname_r_common (&hent);

  memset (serveraddr, '\0', sizeof (*serveraddr));
  serveraddr->sin_family = AF_INET;
  serveraddr->sin_addr = *(struct in_addr *)hent.h_addr;
  serveraddr->sin_port = htons (port);
}

void
setup_server_serveraddr (struct sockaddr_in *serveraddr)
{
  struct hostent hent;

  assert (serveraddr);

  memset (serveraddr, '\0', sizeof (*serveraddr));
  serveraddr->sin_family = AF_INET;
  if (host)
    {
      struct hostent hent;

      gethostbyname_r_common (&hent);

      serveraddr->sin_addr = *(struct in_addr *)hent.h_addr;
    }
  else
    serveraddr->sin_addr.s_addr = htonl (INADDR_ANY);
  serveraddr->sin_port = htons (port);
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

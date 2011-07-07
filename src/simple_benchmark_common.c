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
extern int verifydata;
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
create_buf (size_t bufsize)
{
  uint8_t *buf;

  assert (bufsize);

  if (!memalign_flag)
    {
      if (!(buf = (uint8_t *)malloc (bufsize)))
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

      if (!(buf = (uint8_t *)memalign (pagesize, bufsize)))
	{
	  perror ("memalign");
	  exit (1);
	}
    }

  memset (buf, BLOCK_PATTERN, bufsize);

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

int
check_data_correct (const uint8_t *buf, size_t bufsize)
{
  assert (buf);
  assert (bufsize);

  if (verifydata)
    {
      int databad_flag = 0;
      
      /* Lazy version, there has got to be a fast good version out
       * there in a lib
       */
      while (bufsize)
	{
	  if (buf[0] != BLOCK_PATTERN)
	    {
	      databad_flag++;
	      break;
	    }
	  bufsize--;
	  buf++;
	}

      return (databad_flag);
    }

  return (0);
}

void
device_info (struct ibv_context *ibv_context)
{
  struct ibv_device_attr device_attr;
  int err;

  assert (ibv_context);

  memset (&device_attr, '\0', sizeof (device_attr));

  if ((err = ibv_query_device (ibv_context, &device_attr)))
    {
      fprintf (stderr, "ibv_query_device: %s\n", strerror (err));
      exit (1);
    }

  printf (" Device Info:\n"
	  "   max mr size         : %llu\n"
	  "   max qp              : %d\n"
	  "   max qp wr           : %d\n"
	  "   max cq              : %d\n"
	  "   max cqe             : %d\n"
	  "   max mr              : %d\n"
	  "   max pd              : %d\n"
	  "   local ca ack delay  : %u\n"
	  "   phys port cnt       : %u\n"
	  ,
	  device_attr.max_mr_size,
	  device_attr.max_qp,
	  device_attr.max_qp_wr,
	  device_attr.max_cq,
	  device_attr.max_cqe,
	  device_attr.max_mr,
	  device_attr.max_pd,
	  device_attr.local_ca_ack_delay,
	  device_attr.phys_port_cnt
	  );
}

void
qp_info (struct ibv_qp *ibv_qp, const char *str, FILE *stream)
{
  struct ibv_qp_attr attr;
  struct ibv_qp_init_attr init_attr;
  int err;

  assert (ibv_qp);
  assert (str);
  assert (stream);

  memset (&attr, '\0', sizeof (attr));
  memset (&init_attr, '\0', sizeof (init_attr));
  
  if ((err = ibv_query_qp (ibv_qp,
			   &attr,
			   0xFFFFFFFF,
			   &init_attr)))
    {
      fprintf(stderr, "ibv_query_qp: %s\n", strerror (err));
      return;
    }
  
  fprintf (stream,
	   " %s\n"
	   " QP Data:\n"
	   "   qp num              : 0x%X\n"
	   "   events completed    : %d\n"
	   " QP Attr:\n"
	   "   State               : %u\n"
	   "   Cur State           : %u\n" 
	   "   rq psn              : %d\n"
	   "   sq psn              : %d\n"
	   "   dest qp num         : 0x%X\n"
	   "   cap.max send wr     : %d\n"
	   "   cap.max recv wr     : %d\n"
	   "   cap.max send sge    : %d\n"
	   "   cap.max recv sge    : %d\n"
	   "   cap.max inline data : %d\n"
	   "   sq draining         : %d\n"
	   "   min_rnr_timer       : %d\n"
	   "   port num            : %d\n"
	   "   timeout             : %d\n"
	   "   retry_cnt           : %u\n"
	   "   rnr retry           : %d\n"
	   ,
	   str,
	   ibv_qp->qp_num,
	   ibv_qp->events_completed,
	   attr.qp_state,
	   attr.cur_qp_state,
	   attr.rq_psn,
	   attr.sq_psn,
	   attr.dest_qp_num,
	   attr.cap.max_send_wr,
	   attr.cap.max_recv_wr,
	   attr.cap.max_send_sge,
	   attr.cap.max_recv_sge,
	   attr.cap.max_inline_data,
	   attr.sq_draining,
	   attr.min_rnr_timer,
	   attr.port_num,
	   attr.timeout,
	   attr.retry_cnt,
	   attr.rnr_retry
	   );
}

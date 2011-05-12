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
#define _GNU_SOURCE
#include <getopt.h>
#include <errno.h>

#include "simple_benchmark.h"
#include "simple_benchmark_tcp.h"
#include "simple_benchmark_udp.h"

/* Notes:
 *
 * This benchmark tool is predominantly an investigation on all the
 * Infiniband APIs, and how they compare (performance wise) to
 * traditional TCP vs UDP (done over IPoIB).
 *
 * Feel free to critique implementation for increased performance.  I
 * did not pay particular close attention to it, choosing instead to
 * implement "basic" implementations.
 */

benchmark_run_type_t benchmark_run_type = BENCHMARK_RUN_TYPE_UNINITIALIZED;

benchmark_test_type_t benchmark_test_type = BENCHMARK_TEST_TYPE_UNINITIALIZED;

char *host = NULL;

unsigned int blocksize = BLOCKSIZE_DEFAULT;

unsigned int transfersize = TRANSFERSIZE_DEFAULT;

unsigned int retransmissiontimeout = RETRANSMISSIONTIMEOUT_DEFAULT;

unsigned int sessiontimeout = SESSIONTIMEOUT_DEFAULT;

#if 0
int verifydata = 0;
#endif

uint16_t port = PORT_DEFAULT;

unsigned int verbose = 0;

static void
client(void)
{
  switch (benchmark_test_type)
    {
    case BENCHMARK_TEST_TYPE_TCP:
      client_tcp ();
      break;
    case BENCHMARK_TEST_TYPE_UDP:
      client_udp ();
      break;
    default:
      fprintf (stderr, "invalid benchmark_test_type: %u\n", benchmark_test_type);
      exit (1);
    }
}

static void
server (void)
{
  switch (benchmark_test_type)
    {
    case BENCHMARK_TEST_TYPE_TCP:
      server_tcp ();
      break;
    case BENCHMARK_TEST_TYPE_UDP:
      server_udp ();
      break;
    default:
      fprintf (stderr, "invalid benchmark_test_type: %u\n", benchmark_test_type);
      exit (1);
    }
}

static void
usage (const char *progname)
{
  fprintf (stderr,
	   "Usage: simple_benchmark --client|--server --tcp|--udp\n"
	   "\n"
	   "Test Types:\n"
	   "--tcp   basic TCP data streaming\n"
	   "--udp   basic UDP send/ack data transfer\n"
	   "\n"
	   "Options:\n"
	   " --host                   specify host to send to, required for client side\n"
	   "                          on server side, bind to specified hostname/IP\n"
	   " --blocksize              blocksize of transfers in kilobytes, default = %u\n"
	   " --transfersize           transfersize of data to send in megabytes, default = %u\n"
	   " --retransmissiontimeout  timeout in milliseconds for retries in datagram based tests, default = %u\n"
	   " --sessiontimeout         timeout in milliseconds for a server to give up, default = %u\n"
#if 0
	   " --verifydata             verify correctness of data on receive\n"
#endif
	   " --port                   port to use, default = %u\n"
	   " --verbose                increase verbosity of output, can be specified multiple times\n"
	   " --help                   help\n",
	   BLOCKSIZE_DEFAULT,
	   TRANSFERSIZE_DEFAULT,
	   RETRANSMISSIONTIMEOUT_DEFAULT,
	   SESSIONTIMEOUT_DEFAULT,
	   PORT_DEFAULT
	   );
  exit (0);
}

int
main (int argc, char *argv[])
{
  static const struct option long_opts[] =
    {
      {"client", 0, 0, 'C'},
      {"server", 0, 0, 'S'},
      {"tcp", 0, 0, 'T'},
      {"udp", 0, 0, 'U'},
      {"host", 1, NULL, 'H'},
      {"blocksize", 1, NULL, 'b'},
      {"transfersize", 1, NULL, 't'},
      {"retransmissionteimout", 1, NULL, 'r'},
      {"sessionteimout", 1, NULL, 's'},
#if 0
      {"verifydata", 0, 0, 128},
#endif
      {"port", 1, NULL, 'p'},
      {"verbose", 0, 0, 'v'},
      {"help", 0, 0, 'h'},
      {NULL, 0, 0, 0}
    };
  const char *stropts = "CSTUH:b:t:r:s:p:vh";
  char *endptr;
  char ch;

  while ((ch = getopt_long (argc, argv, stropts, long_opts, NULL)) != -1)
    {
      switch (ch)
	{
	case 'C':
	  benchmark_run_type = BENCHMARK_RUN_TYPE_CLIENT;
	  break;
	case 'S':
	  benchmark_run_type = BENCHMARK_RUN_TYPE_SERVER;
	  break;
	case 'T':
	  benchmark_test_type = BENCHMARK_TEST_TYPE_TCP;
	  break;
	case 'U':
	  benchmark_test_type = BENCHMARK_TEST_TYPE_UDP;
	  break;
	case 'H':
	  if (!(host = strdup (optarg)))
	    {
	      perror ("strdup");
	      exit (1);
	    }
	  break;
	case 'b':
	  blocksize = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !blocksize)
	    {
	      fprintf (stderr, "invalid blocksize input\n");
	      exit (1);
	    }
	  break;
	case 't':
	  transfersize = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !transfersize)
	    {
	      fprintf (stderr, "invalid transfersize input\n");
	      exit (1);
	    }
	  break;
	case 'r':
	  retransmissiontimeout = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !retransmissiontimeout)
	    {
	      fprintf (stderr, "invalid retransmissiontimeout input\n");
	      exit (1);
	    }
	  break;
	case 's':
	  sessiontimeout = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !sessiontimeout)
	    {
	      fprintf (stderr, "invalid sessiontimeout input\n");
	      exit (1);
	    }
	  break;
#if 0
	case 128:
	  verifydata = 1;
	  break;
#endif
	case 'p':
	  port = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !port)
	    {
	      fprintf (stderr, "invalid port input\n");
	      exit (1);
	    }
	  break;
	case 'v':
	  verbose++;
	  break;
	case 'h':
	default:
	  usage (argv[0]);
	}
    }

  /* Sanity check inputs */
  
  if (benchmark_test_type == BENCHMARK_TEST_TYPE_UNINITIALIZED
      || benchmark_run_type == BENCHMARK_RUN_TYPE_UNINITIALIZED)
    usage (argv[0]);
  
  /* Modify inputs appropriately to fix rounding/corner situations */

  if (((uint64_t)blocksize * KILOBYTE) > ((uint64_t)transfersize * MEGABYTE))
    {
      fprintf (stderr, "invalid transfersize, must be larger than blocksize\n");
      exit (1);
    }

  if (retransmissiontimeout > sessiontimeout)
    {
      fprintf (stderr, "invalid sessiontimeout, must be larger than retransmissiontimeout\n");
      exit (1);
    }

  if (benchmark_run_type == BENCHMARK_RUN_TYPE_CLIENT)
    {
      if (!host)
	{
	  fprintf (stderr, "must specify host\n");
	  exit (1);
	}

      client ();
    }
  else
    server ();
}


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
#include "simple_benchmark_ibrc.h"
#include "simple_benchmark_ibud.h"
#include "simple_benchmark_ibrdma.h"

/* Notes:
 *
 * This benchmark tool is predominantly an investigation on all the
 * Infiniband APIs, and how they compare (performance wise) to
 * traditional TCP and UDP mechanisms (done over IPoIB).
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

int verifydata = 0;

uint16_t port = PORT_DEFAULT;

int memalign_flag = 0;

unsigned int verbose = 0;

static void
usage (const char *progname)
{
  fprintf (stderr,
	   "Usage: simple_benchmark [OPTIONS] --client|--server TEST_TYPE\n"
	   "\n"
	   "Test Types:\n"
	   "--tcp        basic TCP data streaming\n"
	   "--tcpnodelay basic TCP data streaming, but disable Nagle\n"
	   "--udp        basic UDP streaming, no reliability handled\n"
	   "--udpsendack basic UDP send/ack data transfer\n"
	   "--ibrc       basic IB RC streaming\n"
	   "--ibrdma     basic IB RDMA streaming\n"
	   "\n"
	   "Options:\n"
	   " --host                   specify host to send to, required for client side\n"
	   "                          on server side, bind to specified hostname/IP\n"
	   " --blocksize              blocksize of transfers in kilobytes, default = %u\n"
	   " --transfersize           transfersize of data to send in megabytes, default = %u\n"
	   " --retransmissiontimeout  timeout in milliseconds for retries in datagram based tests, default = %u\n"
	   " --sessiontimeout         timeout in milliseconds for a server to give up, default = %u\n"
	   " --verifydata             verify correctness of data on receive\n"
	   " --port                   port to use, default = %u\n"
	   " --memalign               memalign buffer to send\n"
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
      {"client", 0, 0, CLIENT_ARGVAL},
      {"server", 0, 0, SERVER_ARGVAL},
      {"tcp", 0, 0, TCP_ARGVAL},
      {"tcpnodelay", 0, 0, TCPNODELAY_ARGVAL},
      {"udp", 0, 0, UDP_ARGVAL},
      {"udpsendack", 0, 0, UDPSENDACK_ARGVAL},
      {"ibrc", 0, 0, IBRC_ARGVAL},
      {"ibrdma", 0, 0, IBRDMA_ARGVAL},
      {"host", 1, NULL, HOST_ARGVAL},
      {"blocksize", 1, NULL, BLOCKSIZE_ARGVAL},
      {"transfersize", 1, NULL, TRANSFERSIZE_ARGVAL},
      {"retransmissionteimout", 1, NULL, RETRANSMISSIONTIMEOUT_ARGVAL},
      {"sessionteimout", 1, NULL, SESSIONTIMEOUT_ARGVAL},
      {"verifydata", 0, 0, VERIFYDATA_ARGVAL},
      {"port", 1, NULL, PORT_ARGVAL},
      {"memalign", 0, 0, MEMALIGN_ARGVAL},
      {"verbose", 0, 0, VERBOSE_ARGVAL},
      {"help", 0, 0, HELP_ARGVAL},
      {NULL, 0, 0, 0}
    };
  const char *stropts = GETOPTARGS;
  char *endptr;
  int ch;

  while ((ch = getopt_long (argc, argv, stropts, long_opts, NULL)) != -1)
    {
      switch (ch)
	{
	case CLIENT_ARGVAL:
	  benchmark_run_type = BENCHMARK_RUN_TYPE_CLIENT;
	  break;
	case SERVER_ARGVAL:
	  benchmark_run_type = BENCHMARK_RUN_TYPE_SERVER;
	  break;
	case TCP_ARGVAL:
	  benchmark_test_type = BENCHMARK_TEST_TYPE_TCP;
	  break;
	case TCPNODELAY_ARGVAL:
	  benchmark_test_type = BENCHMARK_TEST_TYPE_TCPNODELAY;
	  break;
	case UDP_ARGVAL:
	  benchmark_test_type = BENCHMARK_TEST_TYPE_UDP;
	  break;
	case UDPSENDACK_ARGVAL:
	  benchmark_test_type = BENCHMARK_TEST_TYPE_UDPSENDACK;
	  break;
	case IBRC_ARGVAL:
	  benchmark_test_type = BENCHMARK_TEST_TYPE_IBRC;
	  break;
	case IBRDMA_ARGVAL:
	  benchmark_test_type = BENCHMARK_TEST_TYPE_IBRDMA;
	  break;
	case HOST_ARGVAL:
	  if (!(host = strdup (optarg)))
	    {
	      perror ("strdup");
	      exit (1);
	    }
	  break;
	case BLOCKSIZE_ARGVAL:
	  blocksize = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !blocksize)
	    {
	      fprintf (stderr, "invalid blocksize input\n");
	      exit (1);
	    }
	  break;
	case TRANSFERSIZE_ARGVAL:
	  transfersize = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !transfersize)
	    {
	      fprintf (stderr, "invalid transfersize input\n");
	      exit (1);
	    }
	  break;
	case RETRANSMISSIONTIMEOUT_ARGVAL:
	  retransmissiontimeout = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !retransmissiontimeout)
	    {
	      fprintf (stderr, "invalid retransmissiontimeout input\n");
	      exit (1);
	    }
	  break;
	case SESSIONTIMEOUT_ARGVAL:
	  sessiontimeout = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !sessiontimeout)
	    {
	      fprintf (stderr, "invalid sessiontimeout input\n");
	      exit (1);
	    }
	  break;
	case VERIFYDATA_ARGVAL:
	  verifydata = 1;
	  break;
	case PORT_ARGVAL:
	  port = strtoul (optarg, &endptr, 0);
	  if (errno
              || endptr[0] != '\0'
	      || !port)
	    {
	      fprintf (stderr, "invalid port input\n");
	      exit (1);
	    }
	  break;
	case MEMALIGN_ARGVAL:
	  memalign_flag = 1;
	  break;
	case VERBOSE_ARGVAL:
	  verbose++;
	  break;
	case HELP_ARGVAL:
	default:
	  usage (argv[0]);
	}
    }

  /* Sanity check inputs */
  
  if (benchmark_run_type == BENCHMARK_RUN_TYPE_UNINITIALIZED)
    {
      fprintf (stderr, "Must specify --client or --server\n");
      usage (argv[0]);
    }

  if (benchmark_test_type == BENCHMARK_TEST_TYPE_UNINITIALIZED)
    {
      fprintf (stderr, "Must specify a test type\n");
      usage (argv[0]);
    }
  
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
    }

  switch (benchmark_test_type)
    {
    case BENCHMARK_TEST_TYPE_TCP:
    case BENCHMARK_TEST_TYPE_TCPNODELAY:
      if (benchmark_run_type == BENCHMARK_RUN_TYPE_CLIENT)
	client_tcp ();
      else
	server_tcp ();
      break;
    case BENCHMARK_TEST_TYPE_UDP:
    case BENCHMARK_TEST_TYPE_UDPSENDACK:
      if (benchmark_run_type == BENCHMARK_RUN_TYPE_CLIENT)
	client_udp ();
      else
	server_udp ();
      break;
    case BENCHMARK_TEST_TYPE_IBRC:
      if (benchmark_run_type == BENCHMARK_RUN_TYPE_CLIENT)
	client_ibrc ();
      else
	server_ibrc ();
      break;
    case BENCHMARK_TEST_TYPE_IBUD:
      if (benchmark_run_type == BENCHMARK_RUN_TYPE_CLIENT)
	client_ibud ();
      else
	server_ibud ();
      break;
    case BENCHMARK_TEST_TYPE_IBRDMA:
      if (benchmark_run_type == BENCHMARK_RUN_TYPE_CLIENT)
	client_ibrdma ();
      else
	server_ibrdma ();
      break;
      break;
    default:
      fprintf (stderr, "invalid benchmark_test_type: %u\n", benchmark_test_type);
      exit (1);
    }
}


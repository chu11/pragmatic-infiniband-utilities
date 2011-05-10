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
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

/* Notes:
 *
 * This benchmark tool is predominantly an investigation on all the
 * Infiniband APIs, and how they compare (performance wise) to
 * traditional TCP vs UDP.
 *
 * Feel free to critique implementation for increased performance.  I
 * did not pay particular close attention to it, choosing instead to
 * implement "basic" implementations.
 */

#define BLOCKSIZE_DEFAULT             4
#define TRANSFERSIZE_DEFAULT          4096
#define RETRANSMISSIONTIMEOUT_DEFAULT 1000
#define SESSIONTIMEOUT_DEFAULT        60000
#define PORT_DEFAULT                  12345

#define LISTEN_BACKLOG_DEFAULT        2

#define KILOBYTE                      1024
#define MEGABYTE                      (KILOBYTE * KILOBYTE)

#define MILLISECOND_IN_SECOND         1000
#define MICROSECOND_IN_MILLISECOND    1000

#define BLOCK_PATTERN                 0xC7

#define GETHOSTBYNAME_AUX_BUFLEN      1024

typedef enum {
  BENCHMARK_RUN_TYPE_UNINITIALIZED,
  BENCHMARK_RUN_TYPE_CLIENT,
  BENCHMARK_RUN_TYPE_SERVER,
} benchmark_run_type_t;

typedef enum {
  BENCHMARK_TEST_TYPE_UNINITIALIZED,
  BENCHMARK_TEST_TYPE_TCP,
  BENCHMARK_TEST_TYPE_UDP,
} benchmark_test_type_t;

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
client_tcp_sigpipe (int sig)
{
  /* Do nothing, just don't want program to crash */
}

static void
client_tcp (void)
{
  struct sockaddr_in serveraddr;
  unsigned int blocks_written = 0;
  unsigned int blocks_to_write = 0;
  char *buf = NULL;
  size_t writesize;
  struct hostent hent;
  struct hostent *hentptr = NULL;
  int tmpherrno;
  char auxbuf[GETHOSTBYNAME_AUX_BUFLEN];
  int fd;

  if ((fd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror("socket");
      exit(1);
    }
  
  if (gethostbyname_r (host,
		       &hent,
		       auxbuf,
		       GETHOSTBYNAME_AUX_BUFLEN,
		       &hentptr,
		       &tmpherrno)
      || !hentptr)
    {
      fprintf (stderr, "gethostbyname_r: %s", hstrerror (tmpherrno));
      exit (1);
    }

  memset (&serveraddr, '\0', sizeof (serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr = *(struct in_addr *)hent.h_addr;
  serveraddr.sin_port = htons (port);

  if (connect (fd, (struct sockaddr *)&serveraddr, sizeof (serveraddr)) < 0)
    {
      perror ("connect");
      exit (1);
    }

  writesize = (uint64_t)blocksize * KILOBYTE;

  if (!(buf = (uint8_t *)malloc (writesize)))
    {
      perror ("malloc");
      exit (1);
    }

  memset (buf, BLOCK_PATTERN, writesize);

  blocks_to_write = ((uint64_t)transfersize * MEGABYTE) / writesize;
  if (((uint64_t)transfersize * MEGABYTE) % writesize)
    blocks_to_write++;

  if (signal(SIGPIPE, client_tcp_sigpipe) == SIG_ERR)
    {
      perror ("signal");
      exit (1);
    }

  while (blocks_written < blocks_to_write)
    {
      ssize_t writelentotal = 0;

      while (writelentotal < writesize)
	{
	  fd_set writefds;
	  struct timeval timeout;
	  ssize_t writelen;
	  int ret;

	  FD_ZERO(&writefds);
	  FD_SET(fd, &writefds);

	  timeout.tv_sec = sessiontimeout / MILLISECOND_IN_SECOND;
	  timeout.tv_sec = (sessiontimeout % MILLISECOND_IN_SECOND) * MICROSECOND_IN_MILLISECOND;

	  if ((ret = select (fd + 1, NULL, &writefds, NULL, &timeout)) < 0)
	    {
	      perror ("select");
	      exit (1);
	    }

	  if (!ret)
	    {
	      if (verbose)
		printf ("Client session timeout\n");

	      return;
	    }

	  if (!FD_ISSET (fd, &writefds))
	    {
	      if (verbose)
		printf ("fd not set\n");
	      continue;
	    }

	  if ((writelen = write (fd, buf, writesize)) < 0)
	    {
	      if (errno == EINTR)
		continue;
	      
	      if (errno == EPIPE || errno == EINTR)
		{
		  if (verbose)
		    {
		      if (blocks_written < blocks_to_write)
			printf ("Did not send all blocks: written=%u, expected=%u\n",
				blocks_written,
				blocks_to_write);
		      
		      printf ("Server side closed\n");
		    }
		}
	      
	      perror ("write");
	      exit (1);
	    }
	  
	  writelentotal += writelen;
	}
      
      blocks_written++;

      if (verbose > 1)
	printf ("Wrote block %u of size %u\n", blocks_written, writesize);
    }

#if 0
  if (verifydata)
    {
    }
#endif

  if (verbose)
    printf ("Received %u blocks\n", blocks_written);

  free (buf);
}

static void
client_udp (void)
{
}

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
server_tcp_receive (int transferfd)
{
  uint8_t *buf = NULL;
  unsigned int blocks_read = 0;
  unsigned int blocks_to_read = 0;
  size_t readsize;

  assert (transferfd);

  readsize = (uint64_t)blocksize * KILOBYTE;

  if (!(buf = (uint8_t *)malloc (readsize)))
    {
      perror ("malloc");
      exit (1);
    }

  blocks_to_read = ((uint64_t)transfersize * MEGABYTE) / readsize;
  if (((uint64_t)transfersize * MEGABYTE) % readsize)
    blocks_to_read++;

  while (blocks_read < blocks_to_read)
    {
      ssize_t readlentotal = 0;

      while (readlentotal < readsize)
	{
	  fd_set readfds;
	  struct timeval timeout;
	  ssize_t readlen;
	  int ret;

	  FD_ZERO(&readfds);
	  FD_SET(transferfd, &readfds);

	  timeout.tv_sec = sessiontimeout / MILLISECOND_IN_SECOND;
	  timeout.tv_sec = (sessiontimeout % MILLISECOND_IN_SECOND) * MICROSECOND_IN_MILLISECOND;

	  if ((ret = select (transferfd + 1, &readfds, NULL, NULL, &timeout)) < 0)
	    {
	      perror ("select");
	      exit (1);
	    }

	  if (!ret)
	    {
	      if (verbose)
		printf ("Server session timeout\n");

	      return;
	    }

	  if (!FD_ISSET (transferfd, &readfds))
	    {
	      if (verbose)
		printf ("transferfd not set\n");
	      continue;
	    }

	  if ((readlen = read (transferfd, buf, readsize)) < 0)
	    {
	      if (errno == EINTR)
		continue;
	      
	      perror ("read");
	      exit (1);
	    }
	  
	  if (!readlen)
	    {
	      if (verbose)
		{
		  if (blocks_read < blocks_to_read)
		    printf ("Did not receive expected number of blocks: read=%u, expected=%u\n",
			    blocks_read,
			    blocks_to_read);

		  printf ("Connection closed\n");
		}
	      return;
	    }
	  
	  readlentotal += readlen;
	}
      
      blocks_read++;

      if (verbose > 1)
	printf ("Received block %u of size %u\n", blocks_read, readsize);
    }

#if 0
  if (verifydata)
    {
    }
#endif

  if (verbose)
    printf ("Received %u blocks\n", blocks_read);

  free (buf);
}

static void
server_tcp (void)
{
  struct sockaddr_in serveraddr;
  int listenfd;

  if ((listenfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror("socket");
      exit(1);
    }
  
  memset (&serveraddr, '\0', sizeof (serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl (INADDR_ANY);
  serveraddr.sin_port = htons (port);

  if (bind (listenfd, (struct sockaddr *)&serveraddr, sizeof (serveraddr)) < 0)
    {
      perror ("bind");
      exit (1);
    }

  if (listen (listenfd, LISTEN_BACKLOG_DEFAULT) < 0)
    {
      perror ("listen");
      exit (1);
    }

  while (1)
    {
      struct sockaddr_in remoteaddr;
      int remoteaddrlen; 
      int transferfd;
      pid_t childpid;

      remoteaddrlen = sizeof (remoteaddr);
      if ((transferfd = accept (listenfd, (struct sockaddr *)&remoteaddr, &remoteaddrlen)) < 0)
	{
	  perror ("accept");
	  exit (1);
	}
      
      if (verbose)
	printf ("Remote connection accepted\n");
      
      if ((childpid = fork ()) < 0)
	{
	  perror ("fork");
	  exit (1);
	}

      if (!childpid)
	{
	  if (close (listenfd) < 0)
	    {
	      perror ("close");
	      exit (1);
	    }

	  server_tcp_receive (transferfd);

	  if (close (transferfd) < 0)
	    {
	      perror ("close");
	      exit (1);
	    }

	  exit (0);
	}

      if (close (transferfd) < 0)
	{
 	  perror ("close");
	  exit (1);
	}
    }
}

static void
server_udp (void)
{
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


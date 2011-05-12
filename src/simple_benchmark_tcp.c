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
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include "simple_benchmark.h"
#include "simple_benchmark_common.h"
#include "simple_benchmark_tcp.h"

extern unsigned int sessiontimeout;
extern unsigned int verbose;

static void
_client_tcp_sigpipe (int sig)
{
  /* Do nothing, just don't want program to crash */
}

void
client_tcp (void)
{
  struct sockaddr_in serveraddr;
  unsigned int blocks_written = 0;
  unsigned int blocks_to_write = 0;
  struct timeval starttime, endtime;
  char *buf = NULL;
  size_t writesize;
  struct hostent hent;
  int opt, optlen;
  int fd;

  if ((fd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror("socket");
      exit(1);
    }
  
  setup_client_serveraddr (&serveraddr);

  if (connect (fd, (struct sockaddr *)&serveraddr, sizeof (serveraddr)) < 0)
    {
      perror ("connect");
      exit (1);
    }

  calc_bufsize (&writesize);

  buf = create_buf ();

  calc_blocks (&blocks_to_write);

  if (signal(SIGPIPE, _client_tcp_sigpipe) == SIG_ERR)
    {
      perror ("signal");
      exit (1);
    }

#if 0
  opt = 1;
  optlen = sizeof (opt);
  if (setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &opt, optlen) < 0)
    {
      perror ("setsockopt");
      exit (1);
    }

  opt = 0;
  optlen = sizeof (opt);
  if (getsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &opt, &optlen) < 0)
    {
      perror ("getsockopt");
      exit (1);
    }

#endif

  gettimeofday (&starttime, NULL);

  while (blocks_written < blocks_to_write)
    {
      ssize_t writelentotal = 0;

      while (writelentotal < writesize)
	{
	  fd_set writefds;
	  ssize_t writelen;
	  int ret;

	  FD_ZERO(&writefds);
	  FD_SET(fd, &writefds);

	  if ((ret = select (fd + 1, NULL, &writefds, NULL, NULL)) < 0)
	    {
	      perror ("select");
	      exit (1);
	    }

	  /* shouldn't ever be true */
	  if (!FD_ISSET (fd, &writefds))
	    {
	      if (verbose)
		printf ("fd not set\n");

	      continue;
	    }

	  if ((writelen = write (fd, buf, writesize - writelentotal)) < 0)
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

  printf ("Wrote %u blocks, each %llu bytes\n",
	  blocks_written,
	  writesize);

  printf ("Total written %llu bytes\n",
	  (uint64_t)blocks_written * writesize);

  gettimeofday (&endtime, NULL);

  elapsed_time_output (&starttime, &endtime);

  if (close (fd) < 0)
    {
      perror ("close");
      exit (1);
    }

  free (buf);
}

static void
_server_tcp_receive (int transferfd)
{
  uint8_t *buf = NULL;
  unsigned int blocks_read = 0;
  unsigned int blocks_to_read = 0;
  size_t readsize;

  assert (transferfd);

  calc_bufsize (&readsize);

  buf = create_buf ();

  calc_blocks (&blocks_to_read);

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
	  timeout.tv_usec = (sessiontimeout % MILLISECOND_IN_SECOND) * MICROSECOND_IN_MILLISECOND;

	  if ((ret = select (transferfd + 1, &readfds, NULL, NULL, &timeout)) < 0)
	    {
	      perror ("select");
	      exit (1);
	    }

	  if (!ret)
	    {
	      printf ("Server session timeout\n");
	      return;
	    }

	  /* shouldn't ever be true */
	  if (!FD_ISSET (transferfd, &readfds))
	    {
	      if (verbose)
		printf ("transferfd not set\n");

	      continue;
	    }

	  if ((readlen = read (transferfd, buf, readsize - readlentotal)) < 0)
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

  printf ("Received %u blocks, each %llu bytes\n",
	  blocks_read,
	  readsize);

  printf ("Total received %llu bytes\n",
	  (uint64_t)blocks_read * readsize);

  free (buf);
}

void
server_tcp (void)
{
  struct sockaddr_in serveraddr;
  unsigned int optlen;
  int listenfd;
  int optval;

  if ((listenfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror("socket");
      exit(1);
    }
  
  optval = 1;
  optlen = sizeof (optval);
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, optlen) < 0)
    {
      perror ("setsockopt");
      exit (1);
    }

  setup_server_serveraddr (&serveraddr);

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

  printf ("Starting server\n");
  
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

	  _server_tcp_receive (transferfd);

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

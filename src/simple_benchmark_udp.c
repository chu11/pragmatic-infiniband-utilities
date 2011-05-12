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
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include "simple_benchmark.h"
#include "simple_benchmark_common.h"
#include "simple_benchmark_udp.h"

extern unsigned int retransmissiontimeout;
extern unsigned int sessiontimeout;
extern unsigned int verbose;

/* Send/ACK protocol
 *
 * For this protocol, the sequence number will be a 1 byte sequence
 * number, prepended to the block.  Yeah, it probably will lead to
 * issues, particularly in regards to the fact that you might go
 * outside of a page size, but that's what we're going to do.  No
 * windowing, backoff, or anything fancy.
 */

/* return 0 on ack received, non-zero on need to retransmit */
static int
_client_udp_wait_ack (int fd, uint8_t seq)
{
  struct timeval waitstart;
  struct timeval waittimeout;
  struct timeval waitend;

  gettimeofday (&waitstart, NULL);

  waittimeout.tv_sec = retransmissiontimeout / MILLISECOND_IN_SECOND;
  waittimeout.tv_usec = (retransmissiontimeout % MILLISECOND_IN_SECOND) * MICROSECOND_IN_MILLISECOND;
  
  timeradd (&waitstart, &waittimeout, &waitend);
  
  while (1)
    {
      struct timeval current;
      struct timeval timeout;
      fd_set readfds;
      uint8_t recvseq;
      int ret;
      int recvlen;

      FD_ZERO(&readfds);
      FD_SET(fd, &readfds);
      
      gettimeofday (&current, NULL);

      /* Small chance could happen */
      if (timercmp (&waitend, &current, >))
	timersub (&waitend, &current, &timeout);
      else
	{
	  timeout.tv_sec = 0;
	  timeout.tv_usec = 0;
	}

      if ((ret = select (fd + 1, &readfds, NULL, NULL, &timeout)) < 0)
	{
	  perror ("select");
	  exit (1);
	}
      
      /* timeout, return and retransmit */
      if (!ret)
	{
	  if (verbose)
	    printf ("ACK timeout\n");

	  return 1;
	}
      
      /* shouldn't ever be true */
      if (!FD_ISSET (fd, &readfds))
	{
	  if (verbose)
	    printf ("fd not set\n");
	  
	  return 1;
	}
      
      if ((recvlen = recv (fd, &recvseq, 1, 0)) < 0)
	{
	  perror ("recv");
	  exit (1);
	}

      if (recvlen == 1)
	{
	  if (verbose > 1)
	    {
	      printf ("Received ACK w/ seq = %u\n", recvseq);

	      if (recvseq != seq)
		printf ("ACK not expected, expected = %u\n", seq);
	    }

	  if (recvseq == seq)
	    break;
	}
    }

  return 0;
}

void
client_udp (void)
{
  struct sockaddr_in serveraddr;
  unsigned int blocks_sent = 0;
  unsigned int blocks_to_send = 0;
  struct timeval starttime, endtime;
  unsigned int retransmissions_to_timeout;
  unsigned int retransmission_count = 0;
  char *buf = NULL;
  size_t sendsize;
  struct hostent hent;
  uint8_t seq = 0;
  int fd;

  if ((fd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
      perror("socket");
      exit(1);
    }
  
  setup_client_serveraddr (&serveraddr);

  calc_bufsize (&sendsize);
  /* for sequence number */
  sendsize++;

  buf = create_buf ();

  calc_blocks (&blocks_to_send);

  retransmissions_to_timeout = sessiontimeout / retransmissiontimeout;
  if (sessiontimeout % retransmissiontimeout)
    retransmissions_to_timeout++;

  gettimeofday (&starttime, NULL);

  while (blocks_sent < blocks_to_send)
    {
      fd_set writefds;
      ssize_t sendlen;
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
      
      buf[0] = seq;
      if ((sendlen = sendto (fd,
			     buf,
			     sendsize,
			     0,
			     (struct sockaddr *)&serveraddr,
			     sizeof (serveraddr))) < 0)
	{
	  perror ("sendto");
	  exit (1);
	}

      if (sendlen != sendsize)
	{
	  if (verbose)
	    printf ("Error sending packet: sendlen = %u, expected = %u\n",
		    sendlen,
		    sendsize);
	  continue;
	}
      
      if (verbose > 1)
	printf ("Wrote block %u of size %u\n", blocks_sent, sendsize);

      ret = _client_udp_wait_ack (fd, seq);
      if (ret)
	{
	  retransmission_count++;
	  
	  if (retransmission_count >= retransmissions_to_timeout)
	    {
	      printf ("Client ACK timeout\n");
	      return;
	    }
	}
      else
	{
	  seq++;
	  blocks_sent++;
	  retransmission_count = 0;
	}
    }

  printf ("Wrote %u blocks, each %llu bytes\n",
	  blocks_sent,
	  sendsize);
  
  printf ("Total written %llu bytes\n",
	  (uint64_t)blocks_sent * sendsize);
  
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
_server_udp_send_ack (int fd,
		      uint8_t seq,
		      struct sockaddr_in *remoteaddr)
{
  int sendlen;

  assert (remoteaddr);

  do
    {
      fd_set writefds;
      int ret;
      
      FD_ZERO(&writefds);
      FD_SET(fd, &writefds);
	  
      if ((ret = select (fd + 1, NULL, &writefds, NULL, NULL)) < 0)
	{
	  perror ("select");
	  exit (1);
	}
	  
      if (!ret)
	{
	  if (verbose)
	    printf ("fd not set\n");
	  
	  return;
	}
	  
      /* shouldn't ever be true */
      if (!FD_ISSET (fd, &writefds))
	{
	  if (verbose)
	    printf ("fd not set\n");
	  
	  continue;
	}

      if ((sendlen = sendto (fd,
			     &seq,
			     1,
			     0,
			      (struct sockaddr *)remoteaddr,
			     sizeof (*remoteaddr))) < 0)
	{
	  perror ("sendto");
	  exit (1);
	}
      
    } while (!sendlen);

  if (verbose > 1)
    printf ("Sending ACK w/ seq = %u\n", seq);
}

void
server_udp (void)
{
  struct sockaddr_in serveraddr;
  struct sockaddr_in remoteaddr;
  uint8_t *buf = NULL;
  unsigned int blocks_received = 0;
  unsigned int blocks_to_receive = 0;
  size_t recvsize;
  unsigned int retransmissions_to_timeout;
  unsigned int retransmission_count = 0;
  uint8_t seq = 0;
  int received_first_packet = 0;
  socklen_t address_len;
  int fd;

  if ((fd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
      perror("socket");
      exit(1);
    }

  setup_server_serveraddr (&serveraddr);

  if (bind (fd, (struct sockaddr *)&serveraddr, sizeof (serveraddr)) < 0)
    {
      perror ("bind");
      exit (1);
    }

  calc_bufsize (&recvsize);
  /* for sequence number */
  recvsize++;

  buf = create_buf ();

  calc_blocks (&blocks_to_receive);

  retransmissions_to_timeout = sessiontimeout / retransmissiontimeout;
  if (sessiontimeout % retransmissiontimeout)
    retransmissions_to_timeout++;

  printf ("Starting server\n");

  while (blocks_received < blocks_to_receive)
    {
      struct timeval recvstart;
      struct timeval recvtimeout;
      struct timeval recvend;

      gettimeofday (&recvstart, NULL);

      recvtimeout.tv_sec = retransmissiontimeout / MILLISECOND_IN_SECOND;
      recvtimeout.tv_usec = (retransmissiontimeout % MILLISECOND_IN_SECOND) * MICROSECOND_IN_MILLISECOND;
  
      timeradd (&recvstart, &recvtimeout, &recvend);

      while (1)
	{
	  struct timeval current;
	  struct timeval timeout;
	  fd_set readfds;
	  ssize_t recvlen;
	  int ret;

	  FD_ZERO(&readfds);
	  FD_SET(fd, &readfds);
	  
	  gettimeofday (&current, NULL);

	  /* Small chance could happen */
	  if (timercmp (&recvend, &current, >))
	    timersub (&recvend, &current, &timeout);
	  else
	    {
	      timeout.tv_sec = 0;
	      timeout.tv_usec = 0;
	    }
	      
	  if ((ret = select (fd + 1, &readfds, NULL, NULL, &timeout)) < 0)
	    {
	      perror ("select");
	      exit (1);
	    }
      
	  if (!ret)
	    {      
	      if (received_first_packet)
		{
		  retransmission_count++;

		  if (retransmission_count >= retransmissions_to_timeout)
		    {
		      printf ("Server timeout\n");
		      return;
		    }
		  
		  /* Resend the previou seq */
		  _server_udp_send_ack (fd, seq - 1, &remoteaddr);
		}
	      
	      break;
	    }

	  /* shouldn't ever be true */
	  if (!FD_ISSET (fd, &readfds))
	    {
	      if (verbose)
		printf ("fd not set\n");
	      
	      continue;
	    }
	      
	  address_len = sizeof (remoteaddr);
	  if ((recvlen = recvfrom (fd,
				   buf,
				   recvsize,
				   0,
				   (struct sockaddr *)&remoteaddr,
				   &address_len)) < 0)
	    {
	      if (errno == EINTR)
		continue;
	      
	      perror ("recvfrom");
	      exit (1);
	    }

	  if (address_len != sizeof (remoteaddr))
	    {
	      fprintf (stderr,
		       "Invalid address length returned: %u\n",
		       address_len);
	      
	      exit (1);
	    }
	  
	  if (recvlen != recvsize)
	    {
	      if (verbose)
		printf ("Discarding short packet: recvlen = %u, expected = %u\n",
			recvlen,
			recvsize);
	      
	      continue;
	    }

	  if (buf[0] != seq)
	    {
	      if (verbose)
		printf ("Discarding unexpected packet: seq = %u, expected = %u\n",
			buf[0],
			seq);
	      
	      continue;
	    }

	  received_first_packet = 1;
	  retransmission_count = 0;
      
	  blocks_received++;

	  if (verbose > 1)
	    printf ("Received block %u of size %u\n", blocks_received, recvsize);

	  _server_udp_send_ack (fd, seq, &remoteaddr);
	  seq++;

	  break;
	}
    }
      
  printf ("Received %u blocks, each %llu bytes\n",
	  blocks_received,
	  recvsize);
  
  printf ("Total received %llu bytes\n",
	  (uint64_t)blocks_received * recvsize);
  
  if (close (fd) < 0)
    {
      perror ("close");
      exit (1);
    }

  free (buf);
}

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
#include "simple_benchmark_ibrc.h"

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

extern benchmark_test_type_t benchmark_test_type;
extern unsigned int sessiontimeout;
extern unsigned int verbose;

struct ibdata {
  struct rdma_event_channel *cm_event_channel;
  struct rdma_cm_id         *cm_id;

  struct ibv_context *ibv_context;
  struct ibv_pd      *ibv_pd;
  struct ibv_mr      *ibv_mr;
  struct ibv_cq      *ibv_cq;

  uint8_t            *buf;
  size_t             bufsize;
};

#define RDMA_TIMEOUT  2000

#define CQE_DEFAULT   100

#define MAX_SEND_WR_DEFAULT  512
#define MAX_RECV_WR_DEFAULT  512
#define MAX_SEND_SGE_DEFAULT 1
#define MAX_RECV_SGE_DEFAULT 1

void
client_ibrc (void)
{
  struct ibdata ibdata;
  struct sockaddr_in serveraddr;
  unsigned int blocks_sent = 0;
  unsigned int blocks_to_send = 0;
  struct timeval starttime, endtime;
  size_t sendsize;
  struct hostent hent;
  struct rdma_cm_event *cm_event;
  struct ibv_qp_init_attr qp_init_attr;

  memset (&ibdata, '\0', sizeof (ibdata));

  if (!(ibdata.cm_event_channel = rdma_create_event_channel()))
    {
      fprintf (stderr, "rdma_create_event_channel failed\n");
      exit (1);
    }

  if (rdma_create_id (ibdata.cm_event_channel,
		      &ibdata.cm_id,
		      NULL,
		      RDMA_PS_TCP) < 0)
    {
      fprintf (stderr, "rdma_create_id failed\n");
      exit (1);
    }

  setup_client_serveraddr (&serveraddr);

  if (rdma_resolve_addr (ibdata.cm_id,
			 NULL,
			 (struct sockaddr *)&serveraddr,
			 RDMA_TIMEOUT) < 0)
    {
      fprintf (stderr, "rdma_resolve_addr failed\n");
      exit (1);
    }

  if (rdma_get_cm_event (ibdata.cm_event_channel,
			 &cm_event) < 0)
    {
      fprintf (stderr, "rdma_get_cm_event failed\n");
      exit (1);
    }

  if (cm_event->event != RDMA_CM_EVENT_ADDR_RESOLVED)
    {
      fprintf (stderr,
	       "Received unexpected event %d\n",
	       cm_event->event);
      exit (1);
    }
  
  if (rdma_ack_cm_event (cm_event) < 0)
    {
      fprintf (stderr, "rdma_ack_cm_event failed\n");
      exit (1);
    }

  if (rdma_resolve_route (ibdata.cm_id, RDMA_TIMEOUT) < 0)
    {
      fprintf (stderr, "rdma_resolve_route failed\n");
      exit (1);
    }

  if (rdma_get_cm_event (ibdata.cm_event_channel,
                         &cm_event) < 0)
    {
      fprintf (stderr, "rdma_get_cm_event failed\n");
      exit (1);
    }

  if (cm_event->event != RDMA_CM_EVENT_ROUTE_RESOLVED)
    {
      fprintf (stderr,
               "Received unexpected event %d\n",
               cm_event->event);
      exit (1);
    }

  if (rdma_ack_cm_event (cm_event) < 0)
    {
      fprintf (stderr, "rdma_ack_cm_event failed\n");
      exit (1);
    }

  calc_bufsize (&(ibdata.bufsize));

  calc_blocks (&blocks_to_send);

  ibdata.buf = create_buf (ibdata.bufsize);

  /* ibv_context */
  ibdata.ibv_context = ibdata.cm_id->verbs;

  if (!(ibdata.ibv_pd = ibv_alloc_pd (ibdata.ibv_context)))
    {
      fprintf (stderr, "ibv_alloc_pd failed\n");
      exit (1);
    }

  if (!(ibdata.ibv_mr = ibv_reg_mr (ibdata.ibv_pd,
			      ibdata.buf,
			      ibdata.bufsize,
			      IBV_ACCESS_LOCAL_WRITE)))
    {
      fprintf (stderr, "ibv_reg_mr failed\n");
      exit (1);
    }

  if (!(ibdata.ibv_cq = ibv_create_cq (ibdata.ibv_context,
				 CQE_DEFAULT,
				 NULL,
				 NULL,
				 0)))
    {
      fprintf (stderr, "ibv_create_cq failed\n");
      exit (1);
    }

  memset (&qp_init_attr, '\0', sizeof (qp_init_attr));
  qp_init_attr.send_cq = ibdata.ibv_cq;
  qp_init_attr.recv_cq = ibdata.ibv_cq;
  qp_init_attr.cap.max_send_wr = MAX_SEND_WR_DEFAULT;
  qp_init_attr.cap.max_recv_wr = MAX_RECV_WR_DEFAULT;
  qp_init_attr.cap.max_send_sge = MAX_SEND_SGE_DEFAULT;
  qp_init_attr.cap.max_recv_sge = MAX_RECV_SGE_DEFAULT;
  qp_init_attr.cap.max_inline_data = 0;
  qp_init_attr.sq_sig_all = 1;	/* generate CE for all WR */
  qp_init_attr.qp_type = IBV_QPT_RC;

  if (rdma_create_qp (ibdata.cm_id, ibdata.ibv_pd, &qp_init_attr) < 0)
    {
      fprintf (stderr, "rdma_create_qp failed\n");
      exit (1);
    }

  gettimeofday (&starttime, NULL);
  
}

void
server_ibrc (void)
{
}

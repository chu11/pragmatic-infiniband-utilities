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
  struct rdma_cm_id         *cm_accept_id;
  struct rdma_conn_param    cm_conn_param;

  struct ibv_context *ibv_context;
  struct ibv_pd      *ibv_pd;
  struct ibv_mr      *ibv_mr;
  struct ibv_cq      *ibv_cq;
  struct ibv_qp      *ibv_qp;

  uint8_t            *buf;
  size_t             bufsize;
};

#define RDMA_TIMEOUT  2000

#define CQE_DEFAULT   100

#define MAX_SEND_WR_DEFAULT  512
#define MAX_RECV_WR_DEFAULT  512
#define MAX_SEND_SGE_DEFAULT 32
#define MAX_RECV_SGE_DEFAULT 32

#define RESPONDER_RESOURCES_DEFAULT 1
#define INITIATOR_DEPTH_DEFAULT     1
#define RETRY_COUNT_DEFAULT         10
#define RNR_RETRY_COUNT_DEFAULT     10

#define BACKLOG_DEFAULT             0

#define RECEIVE_WR_ID               0
#define SEND_WR_ID                  1

static void
_init_cm (struct ibdata *ibdata)
{
  assert (ibdata);

  if (!(ibdata->cm_event_channel = rdma_create_event_channel()))
    {
      fprintf (stderr, "rdma_create_event_channel failed\n");
      exit (1);
    }

  if (rdma_create_id (ibdata->cm_event_channel,
		      &ibdata->cm_id,
		      NULL,
		      RDMA_PS_TCP) < 0)
    {
      fprintf (stderr, "rdma_create_id failed\n");
      exit (1);
    }
}

static void
_cm_event (struct ibdata *ibdata, enum rdma_cm_event_type expected_event)
{
  struct rdma_cm_event *cm_event;

  assert (ibdata);

  if (rdma_get_cm_event (ibdata->cm_event_channel,
			 &cm_event) < 0)
    {
      fprintf (stderr, "rdma_get_cm_event failed\n");
      exit (1);
    }
  
  if (cm_event->event != expected_event)
    {
      fprintf (stderr,
	       "Received unexpected event %d, expected %d\n",
	       cm_event->event, expected_event);
      exit (1);
    }
  
  if (rdma_ack_cm_event (cm_event) < 0)
    {
      fprintf (stderr, "rdma_ack_cm_event failed\n");
      exit (1);
    }
}

void
client_ibrc (void)
{
  struct ibdata ibdata;
  struct sockaddr_in serveraddr;
  unsigned int blocks_sent = 0;
  unsigned int blocks_to_send = 0;
  struct timeval starttime, endtime;
  struct ibv_qp_init_attr qp_init_attr;

  memset (&ibdata, '\0', sizeof (ibdata));

  _init_cm (&ibdata);

  setup_client_serveraddr (&serveraddr);

  if (rdma_resolve_addr (ibdata.cm_id,
			 NULL,
			 (struct sockaddr *)&serveraddr,
			 RDMA_TIMEOUT) < 0)
    {
      fprintf (stderr, "rdma_resolve_addr failed\n");
      exit (1);
    }

  _cm_event (&ibdata, RDMA_CM_EVENT_ADDR_RESOLVED);

  if (rdma_resolve_route (ibdata.cm_id, RDMA_TIMEOUT) < 0)
    {
      fprintf (stderr, "rdma_resolve_route failed\n");
      exit (1);
    }

  _cm_event (&ibdata, RDMA_CM_EVENT_ROUTE_RESOLVED);

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

  ibdata.cm_conn_param.responder_resources = RESPONDER_RESOURCES_DEFAULT;
  ibdata.cm_conn_param.initiator_depth = INITIATOR_DEPTH_DEFAULT;
  ibdata.cm_conn_param.retry_count = RETRY_COUNT_DEFAULT;
  ibdata.cm_conn_param.rnr_retry_count = RNR_RETRY_COUNT_DEFAULT;

  if (rdma_connect (ibdata.cm_id, &ibdata.cm_conn_param) < 0)
    {
      fprintf (stderr, "rdma_connect failed\n");
      exit (1);
    }

  _cm_event (&ibdata, RDMA_CM_EVENT_ESTABLISHED);

  ibdata.ibv_qp = ibdata.cm_id->qp;

  gettimeofday (&starttime, NULL);

  {
    struct ibv_sge sge;
    struct ibv_send_wr send_wr;
    struct ibv_send_wr *bad_wr;
    struct ibv_wc wc;
    int wcs;

    sge.addr = (uint64_t)ibdata.buf;
    sge.length = ibdata.bufsize;
    sge.lkey = ibdata.ibv_mr->lkey;
    
    send_wr.wr_id = SEND_WR_ID;
    send_wr.next = NULL;
    send_wr.sg_list = &sge;
    send_wr.num_sge = 1;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    
    if (ibv_post_send (ibdata.ibv_qp, &send_wr, &bad_wr) < 0)
      {
	fprintf (stderr, "ibv_post_send failed\n");
	exit (1);
      }
    
    do {
      if ((wcs = ibv_poll_cq (ibdata.ibv_cq, 1, &wc)) < 0)
	{
	  fprintf (stderr, "ibv_poll_cq failed\n");
	  exit (1);
	}
    } while (!wcs);
    
    if (wcs != 1)
      {
	fprintf (stderr, "Unexpected wcs count %d\n");
	exit (1);
      }

    if (wc.wr_id != SEND_WR_ID)
      {
	fprintf (stderr,
		 "Unexpected wr id %u, expected %u\n",
		 wc.wr_id, SEND_WR_ID);
	exit (1);
      }

    if (wc.status != IBV_WC_SUCCESS)
      {
	fprintf (stderr, "Bad wc status %u\n", wc.status);
	exit (1);
      }
    
    if (wc.opcode != IBV_WC_SEND)
      {
	fprintf (stderr,
		 "Unexpected wc opcode %u, expected %u\n",
		 wc.opcode, IBV_WC_SEND);
	exit (1);
      }

    if (verbose)
      printf ("Sent data\n");
  }

  printf ("Wrote %u blocks, each %llu bytes\n",
          blocks_sent,
          ibdata.bufsize);
  
  printf ("Total sent %llu bytes\n",
          (uint64_t)blocks_sent * ibdata.bufsize);
  
  gettimeofday (&endtime, NULL);
  
  elapsed_time_output (&starttime, &endtime);
  
  if (rdma_disconnect (ibdata.cm_id) < 0)
    {
      fprintf (stderr, "rdma_disconnect failed\n");
      exit (1);
    }

  _cm_event (&ibdata, RDMA_CM_EVENT_DISCONNECTED);

  rdma_destroy_qp (ibdata.cm_id);

  if (ibv_dealloc_pd (ibdata.ibv_pd) < 0)
    {
      fprintf (stderr, "ibv_dealloc_pd failed\n");
      exit (1);
    }

  if (ibv_destroy_cq (ibdata.ibv_cq) < 0)
    {
      fprintf (stderr, "ibv_destroy_cq failed\n");
      exit (1);
    }

  if (ibv_dereg_mr (ibdata.ibv_mr) < 0)
    {
      fprintf (stderr, "ibv_dereg_mr failed\n");
      exit (1);
    }

  if (rdma_destroy_id (ibdata.cm_id) < 0)
    {
      fprintf (stderr, "rdma_destroy_id failed\n");
      exit (1);
    }

  rdma_destroy_event_channel (ibdata.cm_event_channel);
  
  free (ibdata.buf);
}

void
server_ibrc (void)
{
  struct ibdata ibdata;
  struct sockaddr_in serveraddr;
  struct rdma_cm_event *cm_connect_event;
  unsigned int blocks_received = 0;
  unsigned int blocks_to_receive = 0;
  struct ibv_qp_init_attr qp_init_attr;

  memset (&ibdata, '\0', sizeof (ibdata));

  _init_cm (&ibdata);

  setup_server_serveraddr (&serveraddr);

  if (rdma_bind_addr (ibdata.cm_id, (struct sockaddr *)&serveraddr) < 0)
    {
      fprintf (stderr, "rdma_bind_addr failed\n");
      exit (1);
    }

  printf ("Starting server listening\n");

  if (rdma_listen (ibdata.cm_id, BACKLOG_DEFAULT) < 0)
    {
      fprintf (stderr, "rdma_listen failed\n");
      exit (1);
    }

  if (rdma_get_cm_event (ibdata.cm_event_channel,
			 &cm_connect_event) < 0)
    {
      fprintf (stderr, "rdma_get_cm_event failed\n");
      exit (1);
    }
  
  if (cm_connect_event->event != RDMA_CM_EVENT_CONNECT_REQUEST)
    {
      fprintf (stderr,
	       "Received unexpected event %d, expected %d\n",
	       cm_connect_event->event, RDMA_CM_EVENT_CONNECT_REQUEST);
      exit (1);
    }
  
  ibdata.cm_accept_id = cm_connect_event->id;

  calc_bufsize (&(ibdata.bufsize));

  calc_blocks (&blocks_to_receive);

  ibdata.buf = create_buf (ibdata.bufsize);

  /* ibv_context */
  ibdata.ibv_context = ibdata.cm_accept_id->verbs;
 
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

  if (rdma_create_qp (ibdata.cm_accept_id, ibdata.ibv_pd, &qp_init_attr) < 0)
    {
      fprintf (stderr, "rdma_create_qp failed\n");
      exit (1);
    }

  ibdata.ibv_qp = ibdata.cm_accept_id->qp;

  {
    struct ibv_sge sge;
    struct ibv_recv_wr recv_wr;
    struct ibv_recv_wr *bad_wr;
    struct ibv_wc wc;
    int wcs;

    sge.addr = (uint64_t)ibdata.buf;
    sge.length = ibdata.bufsize;
    sge.lkey = ibdata.ibv_mr->lkey;
    
    recv_wr.wr_id = RECEIVE_WR_ID;
    recv_wr.next = NULL;
    recv_wr.sg_list = &sge;
    recv_wr.num_sge = 1;
    
    if (ibv_post_recv (ibdata.ibv_qp, &recv_wr, &bad_wr) < 0)
      {
	fprintf (stderr, "ibv_post_recv failed\n");
	exit (1);
      }
    
    ibdata.cm_conn_param.responder_resources = RESPONDER_RESOURCES_DEFAULT;
    ibdata.cm_conn_param.initiator_depth = INITIATOR_DEPTH_DEFAULT;
  
    if (rdma_accept (ibdata.cm_accept_id, &ibdata.cm_conn_param) < 0)
      {
	fprintf (stderr, "rdma_accept failed\n");
	exit (1);
      }
    
    _cm_event (&ibdata, RDMA_CM_EVENT_ESTABLISHED);
    
    printf ("Accepted connection\n");
  
    do {
      if ((wcs = ibv_poll_cq (ibdata.ibv_cq, 1, &wc)) < 0)
	{
	  fprintf (stderr, "ibv_poll_cq failed\n");
	  exit (1);
	}
    } while (!wcs);
    
    if (wcs != 1)
      {
	fprintf (stderr, "Unexpected wcs count %d\n");
	exit (1);
      }

    if (wc.wr_id != RECEIVE_WR_ID)
      {
	fprintf (stderr,
		 "Unexpected wr id %u, expected %u\n",
		 wc.wr_id, RECEIVE_WR_ID);
	exit (1);
      }

    if (wc.status != IBV_WC_SUCCESS)
      {
	fprintf (stderr, "Bad wc status %u\n", wc.status);
	exit (1);
      }
   
    if (wc.opcode != IBV_WC_RECV)
      {
	fprintf (stderr,
		 "Unexpected wc opcode %u, expected %u\n",
		 wc.opcode, IBV_WC_RECV);
	exit (1);
      }

    if (verbose)
      printf ("Received data\n");

    if (check_data_correct (ibdata.buf, ibdata.bufsize))
      printf ("Block has invalid data\n");
  }

  if (rdma_ack_cm_event (cm_connect_event) < 0)
    {
      fprintf (stderr, "rdma_ack_cm_event failed\n");
      exit (1);
    }

  rdma_destroy_qp (ibdata.cm_accept_id);

  if (ibv_dealloc_pd (ibdata.ibv_pd) < 0)
    {
      fprintf (stderr, "ibv_dealloc_pd failed\n");
      exit (1);
    }

  if (ibv_destroy_cq (ibdata.ibv_cq) < 0)
    {
      fprintf (stderr, "ibv_destroy_cq failed\n");
      exit (1);
    }

  if (ibv_dereg_mr (ibdata.ibv_mr) < 0)
    {
      fprintf (stderr, "ibv_dereg_mr failed\n");
      exit (1);
    }

  if (rdma_destroy_id (ibdata.cm_id) < 0)
    {
      fprintf (stderr, "rdma_destroy_id failed\n");
      exit (1);
    }

  if (rdma_destroy_id (ibdata.cm_accept_id) < 0)
    {
      fprintf (stderr, "rdma_destroy_id failed\n");
      exit (1);
    }

  rdma_destroy_event_channel (ibdata.cm_event_channel);

  free (ibdata.buf);
}

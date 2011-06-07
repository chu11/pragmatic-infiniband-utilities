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

struct client_ibdata {
  struct rdma_event_channel *cm_event_channel;
  struct rdma_cm_id         *cm_id;
  struct rdma_cm_id         *cm_connected_id;
  struct rdma_conn_param    cm_conn_param;

  struct ibv_context *ibv_context;
  struct ibv_pd      *ibv_pd;
  struct ibv_mr      *ibv_mr;
  struct ibv_cq      *ibv_cq;
  struct ibv_qp      *ibv_qp;

  uint8_t            *buf;
  size_t             bufsize;
};

struct server_ibdata {
  struct rdma_event_channel *cm_event_channel;
  struct rdma_cm_id         *cm_id;
  struct rdma_cm_id         *cm_connected_id;
  struct rdma_conn_param    cm_conn_param;

  struct ibv_context *ibv_context;
  struct ibv_pd      *ibv_pd;
  struct ibv_mr      **ibv_mrs;
  struct ibv_cq      *ibv_cq;
  struct ibv_qp      *ibv_qp;

  uint8_t            **bufs;
  size_t             bufsize;

  struct ibv_sge     *ibv_sges;
  struct ibv_recv_wr *ibv_recv_wrs;
};

#define RDMA_TIMEOUT  2000

#define CQE_DEFAULT   100

#define MAX_SEND_WR_DEFAULT  1
#define MAX_RECV_WR_DEFAULT  256
#define MAX_SEND_SGE_DEFAULT 1
#define MAX_RECV_SGE_DEFAULT 1

#define RESPONDER_RESOURCES_DEFAULT 1
#define INITIATOR_DEPTH_DEFAULT     1
#define RETRY_COUNT_DEFAULT         7
#define RNR_RETRY_COUNT_DEFAULT     7

#define BACKLOG_DEFAULT             0

#define MICROSECONDS_IN_SECOND      1000000
#define MICROSECONDS_IN_MILLISECOND 1000

#define SEND_WR_ID                  1

static void
_device_info (struct ibv_context *ibv_context)
{
  struct ibv_device_attr device_attr;
  int err;

  assert (ibv_context);

  if ((err = ibv_query_device (ibv_context, &device_attr)))
    {
      fprintf (stderr, "ibv_query_device failed: %s\n", strerror (err));
      exit (1);
    }

  fprintf (stderr,
	   " Device Info:\n"
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

static void
_qp_info (struct ibv_qp *ibv_qp)
{
  struct ibv_qp_attr attr;
  struct ibv_qp_init_attr init_attr;
  int err;

  assert (ibv_qp);

  memset (&attr, '\0', sizeof(attr));
  
  if ((err = ibv_query_qp (ibv_qp,
			   &attr,
			   0xFFFFFFFF,
			   &init_attr)))
    {
      fprintf(stderr, "failed to query qp: %s\n", strerror (err));
      return;
    }
  
  fprintf (stderr,
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

static void
_cm_event (struct rdma_event_channel *cm_event_channel,
	   enum rdma_cm_event_type expected_event)
{
  struct rdma_cm_event *cm_event;

  assert (cm_event_channel);

  if (rdma_get_cm_event (cm_event_channel,
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
  struct client_ibdata ibdata;
  struct sockaddr_in serveraddr;
  unsigned int blocks_sent = 0;
  unsigned int blocks_to_send = 0;
  struct timeval starttime, endtime;
  struct ibv_qp_init_attr qp_init_attr;
  unsigned int i;
  int err;

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

  _cm_event (ibdata.cm_event_channel, RDMA_CM_EVENT_ADDR_RESOLVED);

  if (rdma_resolve_route (ibdata.cm_id, RDMA_TIMEOUT) < 0)
    {
      fprintf (stderr, "rdma_resolve_route failed\n");
      exit (1);
    }

  _cm_event (ibdata.cm_event_channel, RDMA_CM_EVENT_ROUTE_RESOLVED);

  calc_bufsize (&(ibdata.bufsize));

  calc_blocks (&blocks_to_send);

  ibdata.buf = create_buf (ibdata.bufsize);

  /* ibv_context */
  ibdata.ibv_context = ibdata.cm_id->verbs;

  _device_info (ibdata.ibv_context);

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

  _cm_event (ibdata.cm_event_channel, RDMA_CM_EVENT_ESTABLISHED);

  /* ibv_qp */
  ibdata.ibv_qp = ibdata.cm_id->qp;

  if (verbose > 1)
    {
      fprintf (stderr, "Client QP info\n");
      _qp_info (ibdata.ibv_qp);
    }

  gettimeofday (&starttime, NULL);

  while (blocks_sent < blocks_to_send)
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
      
      /* Store sequence number into first bytes */
      memcpy (ibdata.buf, &blocks_sent, sizeof (blocks_sent));

      if ((err = ibv_post_send (ibdata.ibv_qp, &send_wr, &bad_wr)))
	{
	  fprintf (stderr, "ibv_post_send failed: %s\n", strerror (err));
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
	  if (verbose > 1)
	    {
	      fprintf (stderr, "Client QP info\n");
	      _qp_info (ibdata.ibv_qp);
	      _device_info (ibdata.ibv_context);
	    }
	  exit (1);
	}
      
      if (wc.opcode != IBV_WC_SEND)
	{
	  fprintf (stderr,
		   "Unexpected wc opcode %u, expected %u\n",
		   wc.opcode, IBV_WC_SEND);
	  exit (1);
	}
      
      if (verbose > 1)
	printf ("Wrote block %u of size %u\n", blocks_sent, ibdata.bufsize);
      
      blocks_sent++;
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

  _cm_event (ibdata.cm_event_channel, RDMA_CM_EVENT_DISCONNECTED);

  rdma_destroy_qp (ibdata.cm_id);

  if ((err = ibv_destroy_cq (ibdata.ibv_cq)))
    {
      fprintf (stderr, "ibv_destroy_cq failed: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_dereg_mr (ibdata.ibv_mr)))
    {
      fprintf (stderr, "ibv_dereg_mr failed: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_dealloc_pd (ibdata.ibv_pd)))
    {
      fprintf (stderr, "ibv_dealloc_pd failed: %s\n", strerror (err));
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

static unsigned int
_millisecond_timeval_diff (struct timeval *start, struct timeval *end)
{
  unsigned long t;

  assert (start);
  assert (end);

  if (end->tv_sec == start->tv_sec)
    t = end->tv_usec - start->tv_usec;
  else
    {
      t = (end->tv_sec - start->tv_sec - 1) * MICROSECONDS_IN_SECOND;
      t += (MICROSECONDS_IN_SECOND - start->tv_usec);
      t += end->tv_usec;
    }

  return (t / MICROSECONDS_IN_MILLISECOND);
}

static void
_server_post_recv (struct server_ibdata *ibdata, uint64_t wr_id)
{
  struct ibv_recv_wr *bad_wr;
  int err;

  assert (ibdata);

  ibdata->ibv_sges[wr_id].addr = (uint64_t)ibdata->bufs[wr_id];
  ibdata->ibv_sges[wr_id].length = ibdata->bufsize;
  ibdata->ibv_sges[wr_id].lkey = ibdata->ibv_mrs[wr_id]->lkey;
	
  ibdata->ibv_recv_wrs[wr_id].wr_id = wr_id;
  ibdata->ibv_recv_wrs[wr_id].next = NULL;
  ibdata->ibv_recv_wrs[wr_id].sg_list = &ibdata->ibv_sges[wr_id];
  ibdata->ibv_recv_wrs[wr_id].num_sge = 1;
  
  if ((err = ibv_post_recv (ibdata->ibv_qp, &ibdata->ibv_recv_wrs[wr_id], &bad_wr)))
    {
      fprintf (stderr, "ibv_post_recv failed: %s\n", strerror (err));
      exit (1);
    }
}

void
server_ibrc (void)
{
  struct server_ibdata ibdata;
  struct sockaddr_in serveraddr;
  struct rdma_cm_event *cm_connect_event;
  unsigned int blocks_received = 0;
  unsigned int blocks_to_receive = 0;
  struct ibv_qp_init_attr qp_init_attr;
  unsigned int i;
  int err;

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
  
  ibdata.cm_connected_id = cm_connect_event->id;

  calc_bufsize (&(ibdata.bufsize));

  calc_blocks (&blocks_to_receive);

  if (!(ibdata.bufs = (uint8_t **)malloc (sizeof (uint8_t *) * MAX_RECV_WR_DEFAULT)))
    {
      perror ("malloc");
      exit (1);
    }

  for (i = 0; i < MAX_RECV_WR_DEFAULT; i++)
    ibdata.bufs[i] = create_buf (ibdata.bufsize);

  /* ibv_context - from connected id*/
  ibdata.ibv_context = ibdata.cm_connected_id->verbs;
 
  _device_info (ibdata.ibv_context);

  if (!(ibdata.ibv_pd = ibv_alloc_pd (ibdata.ibv_context)))
    {
      fprintf (stderr, "ibv_alloc_pd failed\n");
      exit (1);
    }

  if (!(ibdata.ibv_mrs = (struct ibv_mr **)malloc (sizeof (struct ibv_mr *) * MAX_RECV_WR_DEFAULT)))
    {
      perror ("malloc");
      exit (1);
    }

  for (i = 0; i < MAX_RECV_WR_DEFAULT; i++)
    {
      if (!(ibdata.ibv_mrs[i] = ibv_reg_mr (ibdata.ibv_pd,
					    ibdata.bufs[i],
					    ibdata.bufsize,
					    IBV_ACCESS_LOCAL_WRITE)))
	{
	  fprintf (stderr, "ibv_reg_mr failed\n");
	  exit (1);
	}
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
  
  if (rdma_create_qp (ibdata.cm_connected_id, ibdata.ibv_pd, &qp_init_attr) < 0)
    {
      fprintf (stderr, "rdma_create_qp failed\n");
      exit (1);
    }
  
  /* ibv_qp - from connected id */
  ibdata.ibv_qp = ibdata.cm_connected_id->qp;

  if (verbose > 1)
    {
      fprintf (stderr, "Server pre-accept QP info\n");
      _qp_info (ibdata.ibv_qp);
    }

  if (!(ibdata.ibv_sges = (struct ibv_sge *)malloc (sizeof (struct ibv_sge) * MAX_RECV_WR_DEFAULT)))
    {
      perror ("malloc");
      exit (1);
    }

  if (!(ibdata.ibv_recv_wrs = (struct ibv_recv_wr *)malloc (sizeof (struct ibv_recv_wr) * MAX_RECV_WR_DEFAULT)))
    {
      perror ("malloc");
      exit (1);
    }

  for (i = 0; i < MAX_RECV_WR_DEFAULT; i++)
    _server_post_recv (&ibdata, i);
    
  ibdata.cm_conn_param.responder_resources = RESPONDER_RESOURCES_DEFAULT;
  ibdata.cm_conn_param.initiator_depth = INITIATOR_DEPTH_DEFAULT;
  ibdata.cm_conn_param.retry_count = RETRY_COUNT_DEFAULT;
  ibdata.cm_conn_param.rnr_retry_count = RNR_RETRY_COUNT_DEFAULT;
  
  if (rdma_accept (ibdata.cm_connected_id, &ibdata.cm_conn_param) < 0)
    {
      fprintf (stderr, "rdma_accept failed\n");
      exit (1);
    }
    
  _cm_event (ibdata.cm_event_channel, RDMA_CM_EVENT_ESTABLISHED);
  
  if (verbose > 1)
    {
      fprintf (stderr, "Server post-accept QP info\n");
      _qp_info (ibdata.ibv_qp);
    }

  printf ("Accepted connection\n");
  
  while (blocks_received < blocks_to_receive)
    {
      struct timeval spinstart;
      struct timeval spinend;
      struct ibv_wc wc;
      int wcs;
      
      gettimeofday (&spinstart, NULL);
      
      do {
	unsigned long t;

	if ((wcs = ibv_poll_cq (ibdata.ibv_cq, 1, &wc)) < 0)
	  {
	    fprintf (stderr, "ibv_poll_cq failed\n");
	    exit (1);
	  }
	
	gettimeofday (&spinend, NULL);
	
	t = _millisecond_timeval_diff (&spinstart, &spinend);
	if (t > sessiontimeout)
	  {
	    fprintf (stderr, "Server timeout\n");
	    if (verbose > 1)
	      {
		fprintf (stderr, "Server QP info\n");
		_qp_info (ibdata.ibv_qp);
	      }
	    goto breakout;
	  }
      } while (!wcs);
      
      if (wcs != 1)
	{
	  fprintf (stderr, "Unexpected wcs count %d\n");
	  exit (1);
	}
	
      if (wc.status != IBV_WC_SUCCESS)
	{
	  fprintf (stderr, "Bad wc status %u\n", wc.status);
	  if (verbose > 1)
	    {
	      fprintf (stderr, "Server QP info\n");
	      _qp_info (ibdata.ibv_qp);
	    }
	  exit (1);
	}
	
      if (wc.opcode != IBV_WC_RECV)
	{
	  fprintf (stderr,
		   "Unexpected wc opcode %u, expected %u\n",
		   wc.opcode, IBV_WC_RECV);
	  exit (1);
	}
	
      blocks_received++;
	
      if (verbose > 1)
	printf ("Received block %u (of %u) of size %u (wr_id = %u, seq = %u)\n",
		blocks_received,
		blocks_to_receive,
		ibdata.bufsize,
		wc.wr_id,
		*(unsigned int *)ibdata.bufs[wc.wr_id]);
      
      if (check_data_correct (ibdata.bufs[wc.wr_id] + sizeof (unsigned int),
			      ibdata.bufsize - sizeof (unsigned int)))
	printf ("Block %u has invalid data\n", blocks_received);

      /* put back WR that was taken out */
      _server_post_recv (&ibdata, wc.wr_id);
    }

 breakout:

  printf ("Received %u blocks, each %llu bytes\n",
          blocks_received,
          ibdata.bufsize);
  
  printf ("Total received %llu bytes\n",
          (uint64_t)blocks_received * ibdata.bufsize);

  if (rdma_ack_cm_event (cm_connect_event) < 0)
    {
      fprintf (stderr, "rdma_ack_cm_event failed\n");
      exit (1);
    }

  rdma_destroy_qp (ibdata.cm_connected_id);

  if ((err = ibv_destroy_cq (ibdata.ibv_cq)))
    {
      fprintf (stderr, "ibv_destroy_cq failed: %s\n", strerror (err));
      exit (1);
    }

  for (i = 0; i < MAX_RECV_WR_DEFAULT; i++)
    {
      if ((err = ibv_dereg_mr (ibdata.ibv_mrs[i])))
	{
	  fprintf (stderr, "ibv_dereg_mr failed: %s\n", strerror (err));
	  exit (1);
	}
    }

  free (ibdata.ibv_mrs);

  if ((err = ibv_dealloc_pd (ibdata.ibv_pd)))
    {
      fprintf (stderr, "ibv_dealloc_pd failed: %s\n", strerror (err));
      exit (1);
    }

  if (rdma_destroy_id (ibdata.cm_id) < 0)
    {
      fprintf (stderr, "rdma_destroy_id failed\n");
      exit (1);
    }

  if (rdma_destroy_id (ibdata.cm_connected_id) < 0)
    {
      fprintf (stderr, "rdma_destroy_id failed\n");
      exit (1);
    }

  rdma_destroy_event_channel (ibdata.cm_event_channel);

  for (i = 0; i < MAX_RECV_WR_DEFAULT; i++)
    free (ibdata.bufs[i]);

  free (ibdata.bufs);

  free (ibdata.ibv_sges);
  free (ibdata.ibv_recv_wrs);
}

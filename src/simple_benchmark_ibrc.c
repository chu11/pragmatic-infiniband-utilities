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

struct qpdata {
  uint16_t lid;
  uint32_t qp_num;
  uint32_t psn;
} __attribute__ ((packed));

struct client_ibdata {
  struct ibv_device       *ibv_device;
  struct ibv_context      *ibv_context;
  struct ibv_pd           *ibv_pd;
  struct ibv_mr           *ibv_mr;
  struct ibv_cq           *ibv_cq;
  struct ibv_qp_init_attr ibv_qp_init_attr;
  struct ibv_qp           *ibv_qp;
  struct ibv_qp_attr      ibv_qp_attr;

  uint8_t                 *buf;
  size_t                  bufsize;

  int                     fd;
};

struct server_ibdata {
  struct ibv_device       *ibv_device;
  struct ibv_context      *ibv_context;
  struct ibv_pd           *ibv_pd;
  struct ibv_mr           *ibv_mr;
  struct ibv_cq           *ibv_cq;
  struct ibv_qp_init_attr ibv_qp_init_attr;
  struct ibv_qp           *ibv_qp;
  struct ibv_qp_attr      ibv_qp_attr;

  uint8_t                 *buf;
  size_t                  bufsize;

  int                     listenfd;
  int                     connectionfd;

  struct ibv_sge          ibv_sge;
  struct ibv_recv_wr      ibv_recv_wr;
};

#define RDMA_TIMEOUT  2000

#define CQE_DEFAULT   100

#define MAX_SEND_WR_DEFAULT  1
#define MAX_RECV_WR_DEFAULT  256
#define MAX_SEND_SGE_DEFAULT 1
#define MAX_RECV_SGE_DEFAULT 1

#define PKEY_INDEX_DEFAULT          0
#define PORT_NUM_DEFAULT            1
#define MAX_DEST_RD_ATOMIC_DEFAULT  1
#define MIN_RNR_TIMER_DEFAULT       12
#define SL_DEFAULT                  0
#define MAX_RD_ATOMIC_DEFAULT       1
#define TIMEOUT_DEFAULT             14
#define RETRY_COUNT_DEFAULT         7
#define RNR_RETRY_COUNT_DEFAULT     7

#define MICROSECONDS_IN_SECOND      1000000
#define MICROSECONDS_IN_MILLISECOND 1000

#define SEND_WR_ID                  0
#define RECEIVE_WR_ID               1

#define TMPBUF_LEN                  1024

static void
_device_info (struct ibv_context *ibv_context)
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
_qp_info (struct ibv_qp *ibv_qp, const char *str)
{
  struct ibv_qp_attr attr;
  struct ibv_qp_init_attr init_attr;
  int err;

  assert (ibv_qp);
  assert (str);

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
  
  fprintf (stderr,
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

static void
_output_qpdata (struct qpdata *qpdata, const char *str)
{
  assert (qpdata);
  assert (str);

  printf ("%s\n", str);
  printf ("  lid = %u\n", 
	  qpdata->lid);
  printf ("  qp num = %u\n",
	  qpdata->qp_num);
  printf ("  psn = %u\n",
	  qpdata->psn);
}

void
client_ibrc (void)
{
  struct client_ibdata client_ibdata;
  struct ibv_device **ibv_device_list;
  int num_devices;
  unsigned int blocks_sent = 0;
  unsigned int blocks_to_send = 0;
  struct ibv_port_attr port_attr;
  struct qpdata client_qpdata;
  struct qpdata server_qpdata;
  struct sockaddr_in serveraddr;
  struct timeval starttime, endtime;
  unsigned int i;
  int total_len, desired_len, len, err;
  uint8_t tmpbuf[TMPBUF_LEN];

  memset (&client_ibdata, '\0', sizeof (client_ibdata));
  memset (&client_qpdata, '\0', sizeof (client_qpdata));
  memset (&server_qpdata, '\0', sizeof (server_qpdata));
  memset (&serveraddr, '\0', sizeof (serveraddr));

  if (!(ibv_device_list = ibv_get_device_list (&num_devices)))
    {
      perror ("ibv_get_device_list");
      exit (1);
    }
  
  if (!num_devices)
    {
      fprintf (stderr, "No IB devices found\n");
      exit (1);
    }
  
  client_ibdata.ibv_device = ibv_device_list[0];
  
  if (!(client_ibdata.ibv_context = ibv_open_device (client_ibdata.ibv_device)))
    {
      /* Sets errno?? */
      fprintf (stderr, "ibv_open_device: %s\n", strerror (errno));
      exit (1);
    }

  _device_info (client_ibdata.ibv_context);

  calc_bufsize (&(client_ibdata.bufsize));

  calc_blocks (&blocks_to_send);

  client_ibdata.buf = create_buf (client_ibdata.bufsize);

  if (!(client_ibdata.ibv_pd = ibv_alloc_pd (client_ibdata.ibv_context)))
    {
      fprintf (stderr, "ibv_alloc_pd failed\n");
      exit (1);
    }

  if (!(client_ibdata.ibv_mr = ibv_reg_mr (client_ibdata.ibv_pd,
					   client_ibdata.buf,
					   client_ibdata.bufsize,
					   IBV_ACCESS_LOCAL_WRITE)))
    {
      fprintf (stderr, "ibv_reg_mr failed\n");
      exit (1);
    }
  
  if (!(client_ibdata.ibv_cq = ibv_create_cq (client_ibdata.ibv_context,
					      CQE_DEFAULT,
					      NULL,
					      NULL,
					      0)))
    {
      fprintf (stderr, "ibv_create_cq failed\n");
      exit (1);
    }

  client_ibdata.ibv_qp_init_attr.send_cq = client_ibdata.ibv_cq;
  client_ibdata.ibv_qp_init_attr.recv_cq = client_ibdata.ibv_cq;
  client_ibdata.ibv_qp_init_attr.cap.max_send_wr = MAX_SEND_WR_DEFAULT;
  client_ibdata.ibv_qp_init_attr.cap.max_recv_wr = MAX_RECV_WR_DEFAULT;
  client_ibdata.ibv_qp_init_attr.cap.max_send_sge = MAX_SEND_SGE_DEFAULT;
  client_ibdata.ibv_qp_init_attr.cap.max_recv_sge = MAX_RECV_SGE_DEFAULT;
  client_ibdata.ibv_qp_init_attr.cap.max_inline_data = 0;
  client_ibdata.ibv_qp_init_attr.sq_sig_all = 1;	/* generate CE for all WR */
  client_ibdata.ibv_qp_init_attr.qp_type = IBV_QPT_RC;

  if (!(client_ibdata.ibv_qp = ibv_create_qp (client_ibdata.ibv_pd,
					      &client_ibdata.ibv_qp_init_attr)))
    {
      /* Sets errno?? */
      fprintf (stderr, "ibv_create_qp: %s\n", strerror (errno));
      exit (1);
    }

  memset (&client_ibdata.ibv_qp_attr, '\0', sizeof (client_ibdata.ibv_qp_attr));
  client_ibdata.ibv_qp_attr.qp_state = IBV_QPS_INIT;
  client_ibdata.ibv_qp_attr.pkey_index = PKEY_INDEX_DEFAULT;
  client_ibdata.ibv_qp_attr.port_num = PORT_NUM_DEFAULT;
  client_ibdata.ibv_qp_attr.qp_access_flags = 0;

  if ((err = ibv_modify_qp (client_ibdata.ibv_qp,
			    &client_ibdata.ibv_qp_attr,
			    IBV_QP_STATE
			    | IBV_QP_PKEY_INDEX
			    | IBV_QP_PORT
			    | IBV_QP_ACCESS_FLAGS)))
    {
      fprintf (stderr, "ibv_modify_qp: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_query_port (client_ibdata.ibv_context,
			     PORT_NUM_DEFAULT,
			     &port_attr)))
    {
      fprintf (stderr, "ibv_query_port: %s\n", strerror (err));
      exit (1);
    }

  client_qpdata.lid = port_attr.lid;
  client_qpdata.qp_num = client_ibdata.ibv_qp->qp_num;
  client_qpdata.psn = lrand48() & 0xFFFFFF;

  if (verbose)
    _output_qpdata (&client_qpdata, "Client QP Data");

  setup_client_serveraddr (&serveraddr);

  if ((client_ibdata.fd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror ("socket");
      exit (1);
    }

  if (connect (client_ibdata.fd,
	       (struct sockaddr *)&serveraddr,
	       sizeof (serveraddr)) < 0)
    {
      perror ("connect");
      exit (1);
    }

  total_len = 0;
  desired_len = sizeof (client_qpdata);

  memcpy (tmpbuf, &client_qpdata, desired_len);

  while (total_len < desired_len)
    {
      if ((len = write (client_ibdata.fd,
			tmpbuf + total_len,
			desired_len - total_len)) < 0)
	{
	  perror ("write");
	  exit (1);
	}
      total_len += len;
    }

  total_len = 0;
  desired_len = sizeof (server_qpdata);

  while (total_len < desired_len)
    {
      if ((len = read (client_ibdata.fd,
		       tmpbuf + total_len,
		       desired_len - total_len)) < 0)
	{
	  perror ("read");
	  exit (1);
	}
      total_len += len;
    }

  memcpy (&server_qpdata, tmpbuf, desired_len);

  if (verbose)
    _output_qpdata (&server_qpdata, "Server QP Data");

  memset (&client_ibdata.ibv_qp_attr, '\0', sizeof (client_ibdata.ibv_qp_attr));
  client_ibdata.ibv_qp_attr.qp_state = IBV_QPS_RTR;
  client_ibdata.ibv_qp_attr.path_mtu = port_attr.active_mtu;
  client_ibdata.ibv_qp_attr.dest_qp_num = server_qpdata.qp_num;
  client_ibdata.ibv_qp_attr.rq_psn = server_qpdata.psn;
  client_ibdata.ibv_qp_attr.max_dest_rd_atomic = MAX_DEST_RD_ATOMIC_DEFAULT;
  client_ibdata.ibv_qp_attr.min_rnr_timer = MIN_RNR_TIMER_DEFAULT;
  client_ibdata.ibv_qp_attr.ah_attr.dlid = server_qpdata.lid;
  client_ibdata.ibv_qp_attr.ah_attr.is_global = 0;
  client_ibdata.ibv_qp_attr.ah_attr.sl = SL_DEFAULT;
  client_ibdata.ibv_qp_attr.ah_attr.src_path_bits = 0;
  client_ibdata.ibv_qp_attr.ah_attr.port_num = PORT_NUM_DEFAULT;

  if ((err = ibv_modify_qp (client_ibdata.ibv_qp,
			    &client_ibdata.ibv_qp_attr,
			    IBV_QP_STATE
			    | IBV_QP_AV
			    | IBV_QP_PATH_MTU
			    | IBV_QP_DEST_QPN
			    | IBV_QP_RQ_PSN
			    | IBV_QP_MAX_DEST_RD_ATOMIC
			    | IBV_QP_MIN_RNR_TIMER)))
    {
      fprintf (stderr, "ibv_modify_qp: %s\n", strerror (err));
      exit (1);
    }

  memset (&client_ibdata.ibv_qp_attr, '\0', sizeof (client_ibdata.ibv_qp_attr));
  client_ibdata.ibv_qp_attr.qp_state = IBV_QPS_RTS;
  client_ibdata.ibv_qp_attr.sq_psn = client_qpdata.psn;
  client_ibdata.ibv_qp_attr.max_rd_atomic = MAX_RD_ATOMIC_DEFAULT;
  client_ibdata.ibv_qp_attr.retry_cnt = RETRY_COUNT_DEFAULT;
  client_ibdata.ibv_qp_attr.rnr_retry = RNR_RETRY_COUNT_DEFAULT;
  client_ibdata.ibv_qp_attr.timeout = TIMEOUT_DEFAULT;

  if ((err = ibv_modify_qp (client_ibdata.ibv_qp,
			    &client_ibdata.ibv_qp_attr,
			    IBV_QP_STATE
			    | IBV_QP_SQ_PSN
			    | IBV_QP_MAX_QP_RD_ATOMIC
			    | IBV_QP_RETRY_CNT
			    | IBV_QP_RNR_RETRY
			    | IBV_QP_TIMEOUT)))
    {
      fprintf (stderr, "ibv_modify_qp: %s\n", strerror (err));
      exit (1);
    }
  
  if (verbose > 1)
    _qp_info (client_ibdata.ibv_qp, "Client QP Info");

  /* sync with server to know when it's ready */
  if ((len = read (client_ibdata.fd,
		   tmpbuf,
		   1)) < 0)
    {
      perror ("read");
      exit (1);
    }
  
  if (len != 1)
    {
      fprintf (stderr, "Protocol mismatch\n");
      exit (1);
    }

  gettimeofday (&starttime, NULL);

  while (blocks_sent < blocks_to_send)
    {
      struct ibv_sge sge;
      struct ibv_send_wr send_wr;
      struct ibv_send_wr *bad_wr;
      struct ibv_wc wc;
      int wcs;

      memset (&sge, '\0', sizeof (sge));
      memset (&send_wr, '\0', sizeof (send_wr));
      memset (&wc, '\0', sizeof (wc));

      sge.addr = (uint64_t)client_ibdata.buf;
      sge.length = client_ibdata.bufsize;
      sge.lkey = client_ibdata.ibv_mr->lkey;
    
      send_wr.wr_id = SEND_WR_ID;
      send_wr.next = NULL;
      send_wr.sg_list = &sge;
      send_wr.num_sge = 1;
      send_wr.opcode = IBV_WR_SEND;
      send_wr.send_flags = IBV_SEND_SIGNALED;
      
      /* Store sequence number into first bytes */
      memcpy (client_ibdata.buf, &blocks_sent, sizeof (blocks_sent));

      if ((err = ibv_post_send (client_ibdata.ibv_qp, &send_wr, &bad_wr)))
	{
	  fprintf (stderr, "ibv_post_send: %s\n", strerror (err));
	  exit (1);
	}
    
      do {
	if ((wcs = ibv_poll_cq (client_ibdata.ibv_cq, 1, &wc)) < 0)
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
	    _qp_info (client_ibdata.ibv_qp, "Client QP Info");
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
	printf ("Wrote block %u of size %u\n", blocks_sent, client_ibdata.bufsize);
      
      blocks_sent++;
    }

  printf ("Wrote %u blocks, each %llu bytes\n",
          blocks_sent,
          client_ibdata.bufsize);
  
  printf ("Total sent %llu bytes\n",
          (uint64_t)blocks_sent * client_ibdata.bufsize);
  
  gettimeofday (&endtime, NULL);
  
  elapsed_time_output (&starttime, &endtime);
  
  if ((err = ibv_destroy_qp (client_ibdata.ibv_qp)))
    {
      fprintf (stderr, "ibv_destroy_qp: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_destroy_cq (client_ibdata.ibv_cq)))
    {
      fprintf (stderr, "ibv_destroy_cq: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_dereg_mr (client_ibdata.ibv_mr)))
    {
      fprintf (stderr, "ibv_dereg_mr: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_dealloc_pd (client_ibdata.ibv_pd)))
    {
      fprintf (stderr, "ibv_dealloc_pd: %s\n", strerror (err));
      exit (1);
    }
  
  if (ibv_close_device (client_ibdata.ibv_context) < 0)
    {
      /* Sets errno?? */
      fprintf (stderr, "ibv_close_device: %s\n", strerror (errno));
      exit (1);
    }

  ibv_free_device_list (ibv_device_list);

  free (client_ibdata.buf);

  if (close (client_ibdata.fd) < 0)
    {
      perror ("close");
      exit (1);
    }
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
_server_post_recv (struct server_ibdata *server_ibdata)
{
  struct ibv_recv_wr *bad_wr;
  int err;

  assert (server_ibdata);

  memset (&server_ibdata->ibv_sge, '\0', sizeof (server_ibdata->ibv_sge));
  memset (&server_ibdata->ibv_recv_wr, '\0', sizeof (server_ibdata->ibv_recv_wr));

  server_ibdata->ibv_sge.addr = (uint64_t)server_ibdata->buf;
  server_ibdata->ibv_sge.length = server_ibdata->bufsize;
  server_ibdata->ibv_sge.lkey = server_ibdata->ibv_mr->lkey;
	
  server_ibdata->ibv_recv_wr.wr_id = RECEIVE_WR_ID;
  server_ibdata->ibv_recv_wr.next = NULL;
  server_ibdata->ibv_recv_wr.sg_list = &server_ibdata->ibv_sge;
  server_ibdata->ibv_recv_wr.num_sge = 1;
  
  if ((err = ibv_post_recv (server_ibdata->ibv_qp, &server_ibdata->ibv_recv_wr, &bad_wr)))
    {
      fprintf (stderr, "ibv_post_recv: %s\n", strerror (err));
      exit (1);
    }
}

void
server_ibrc (void)
{
  struct server_ibdata server_ibdata;
  struct ibv_device **ibv_device_list;
  int num_devices;
  unsigned int blocks_received = 0;
  unsigned int blocks_to_receive = 0;
  struct ibv_port_attr port_attr;
  struct qpdata server_qpdata;
  struct qpdata client_qpdata;
  struct sockaddr_in serveraddr;
  unsigned int optlen;
  int optval;
  unsigned int i;
  int total_len, desired_len, len, err;
  uint8_t tmpbuf[TMPBUF_LEN];

  memset (&server_ibdata, '\0', sizeof (server_ibdata));
  memset (&server_qpdata, '\0', sizeof (server_qpdata));
  memset (&client_qpdata, '\0', sizeof (client_qpdata));
  memset (&serveraddr, '\0', sizeof (serveraddr));

  if (!(ibv_device_list = ibv_get_device_list (&num_devices)))
    {
      perror ("ibv_get_device_list");
      exit (1);
    }
  
  if (!num_devices)
    {
      fprintf (stderr, "No IB devices found\n");
      exit (1);
    }
  
  server_ibdata.ibv_device = ibv_device_list[0];

  if (!(server_ibdata.ibv_context = ibv_open_device (server_ibdata.ibv_device)))
    {
      /* Sets errno?? */
      fprintf (stderr, "ibv_open_device: %s\n", strerror (errno));
      exit (1);
    }

  _device_info (server_ibdata.ibv_context);

  calc_bufsize (&(server_ibdata.bufsize));

  calc_blocks (&blocks_to_receive);
  
  server_ibdata.buf = create_buf (server_ibdata.bufsize);
  
  if (!(server_ibdata.ibv_pd = ibv_alloc_pd (server_ibdata.ibv_context)))
    {
      fprintf (stderr, "ibv_alloc_pd failed\n");
      exit (1);
    }

  if (!(server_ibdata.ibv_mr = ibv_reg_mr (server_ibdata.ibv_pd,
                                           server_ibdata.buf,
                                           server_ibdata.bufsize,
                                           IBV_ACCESS_LOCAL_WRITE)))
    {
      fprintf (stderr, "ibv_reg_mr failed\n");
      exit (1);
    }
  
  if (!(server_ibdata.ibv_cq = ibv_create_cq (server_ibdata.ibv_context,
                                              CQE_DEFAULT,
                                              NULL,
                                              NULL,
                                              0)))
    {
      fprintf (stderr, "ibv_create_cq failed\n");
      exit (1);
    }
  
  server_ibdata.ibv_qp_init_attr.send_cq = server_ibdata.ibv_cq;
  server_ibdata.ibv_qp_init_attr.recv_cq = server_ibdata.ibv_cq;
  server_ibdata.ibv_qp_init_attr.cap.max_send_wr = MAX_SEND_WR_DEFAULT;
  server_ibdata.ibv_qp_init_attr.cap.max_recv_wr = MAX_RECV_WR_DEFAULT;
  server_ibdata.ibv_qp_init_attr.cap.max_send_sge = MAX_SEND_SGE_DEFAULT;
  server_ibdata.ibv_qp_init_attr.cap.max_recv_sge = MAX_RECV_SGE_DEFAULT;
  server_ibdata.ibv_qp_init_attr.cap.max_inline_data = 0;
  server_ibdata.ibv_qp_init_attr.sq_sig_all = 1;        /* generate CE for all WR */
  server_ibdata.ibv_qp_init_attr.qp_type = IBV_QPT_RC;

  if (!(server_ibdata.ibv_qp = ibv_create_qp (server_ibdata.ibv_pd,
                                              &server_ibdata.ibv_qp_init_attr)))
    {
      /* Sets errno?? */
      fprintf (stderr, "ibv_create_qp: %s\n", strerror (errno));
      exit (1);
    }
  
  memset (&server_ibdata.ibv_qp_attr, '\0', sizeof (server_ibdata.ibv_qp_attr));
  server_ibdata.ibv_qp_attr.qp_state = IBV_QPS_INIT;
  server_ibdata.ibv_qp_attr.pkey_index = PKEY_INDEX_DEFAULT;
  server_ibdata.ibv_qp_attr.port_num = PORT_NUM_DEFAULT;
  server_ibdata.ibv_qp_attr.qp_access_flags = 0;
  
  if ((err = ibv_modify_qp (server_ibdata.ibv_qp,
                            &server_ibdata.ibv_qp_attr,
                            IBV_QP_STATE
                            | IBV_QP_PKEY_INDEX
                            | IBV_QP_PORT
                            | IBV_QP_ACCESS_FLAGS)))
    {
      fprintf (stderr, "ibv_modify_qp: %s\n", strerror (err));
      exit (1);
    }
  
  if ((err = ibv_query_port (server_ibdata.ibv_context,
                             PORT_NUM_DEFAULT,
                             &port_attr)))
    {
      fprintf (stderr, "ibv_query_port: %s\n", strerror (err));
      exit (1);
    }

  server_qpdata.lid = port_attr.lid;
  server_qpdata.qp_num = server_ibdata.ibv_qp->qp_num;
  server_qpdata.psn = lrand48() & 0xFFFFFF;

  if (verbose)
    _output_qpdata (&server_qpdata, "Server QP Data");

  setup_server_serveraddr (&serveraddr);
  
  if ((server_ibdata.listenfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror ("socket");
      exit (1);
    }

  optval = 1;
  optlen = sizeof (optval);

  if (setsockopt (server_ibdata.listenfd,
		  SOL_SOCKET,
		  SO_REUSEADDR,
		  &optval,
		  optlen) < 0)
    {
      perror ("setsockopt");
      exit (1);
    }

  if (bind (server_ibdata.listenfd,
	    (struct sockaddr *)&serveraddr,
	    sizeof (serveraddr)) < 0)
    {
      perror ("bind");
      exit (1);
    }

  if (listen (server_ibdata.listenfd, LISTEN_BACKLOG_DEFAULT) < 0)
    {
      perror ("listen");
      exit (1);
    }

  printf ("Starting server\n");
  
  if ((server_ibdata.connectionfd = accept (server_ibdata.listenfd,
					    NULL,
					    0)) < 0)
    {
      perror ("accept");
      exit (1);
    }

  printf ("Accepted connection\n");

  total_len = 0;
  desired_len = sizeof (client_qpdata);

  while (total_len < desired_len)
    {
      if ((len = read (server_ibdata.connectionfd,
                       tmpbuf + total_len,
                       desired_len - total_len)) < 0)
        {
          perror ("read");
          exit (1);
        }
      total_len += len;
    }
  
  memcpy (&client_qpdata, tmpbuf, desired_len);
  
  if (verbose)
    _output_qpdata (&client_qpdata, "Client QP Data");

  total_len = 0;
  desired_len = sizeof (server_qpdata);

  memcpy (tmpbuf, &server_qpdata, desired_len);

  while (total_len < desired_len)
    {
      if ((len = write (server_ibdata.connectionfd,
                        tmpbuf + total_len,
                        desired_len - total_len)) < 0)
        {
          perror ("write");
          exit (1);
        }
      total_len += len;
    }
  
  if (verbose > 1)
    _qp_info (server_ibdata.ibv_qp, "Server QP Info");

  memset (&server_ibdata.ibv_qp_attr, '\0', sizeof (server_ibdata.ibv_qp_attr));
  server_ibdata.ibv_qp_attr.qp_state = IBV_QPS_RTR;
  server_ibdata.ibv_qp_attr.path_mtu = port_attr.active_mtu;
  server_ibdata.ibv_qp_attr.dest_qp_num = client_qpdata.qp_num;
  server_ibdata.ibv_qp_attr.rq_psn = client_qpdata.psn;
  server_ibdata.ibv_qp_attr.max_dest_rd_atomic = MAX_DEST_RD_ATOMIC_DEFAULT;
  server_ibdata.ibv_qp_attr.min_rnr_timer = MIN_RNR_TIMER_DEFAULT;
  server_ibdata.ibv_qp_attr.ah_attr.dlid = client_qpdata.lid;
  server_ibdata.ibv_qp_attr.ah_attr.is_global = 0;
  server_ibdata.ibv_qp_attr.ah_attr.sl = SL_DEFAULT;
  server_ibdata.ibv_qp_attr.ah_attr.src_path_bits = 0;
  server_ibdata.ibv_qp_attr.ah_attr.port_num = PORT_NUM_DEFAULT;

  if ((err = ibv_modify_qp (server_ibdata.ibv_qp,
                            &server_ibdata.ibv_qp_attr,
                            IBV_QP_STATE
                            | IBV_QP_AV
                            | IBV_QP_PATH_MTU
                            | IBV_QP_DEST_QPN
                            | IBV_QP_RQ_PSN
                            | IBV_QP_MAX_DEST_RD_ATOMIC
                            | IBV_QP_MIN_RNR_TIMER)))
    {
      fprintf (stderr, "ibv_modify_qp: %s\n", strerror (err));
      exit (1);
    }

  for (i = 0; i < MAX_RECV_WR_DEFAULT; i++)
    _server_post_recv (&server_ibdata);

  /* sync with client to know when server is ready */
  if ((len = write (server_ibdata.connectionfd,
		    "a",
		    1)) < 0)
    {
      perror ("write");
      exit (1);
    }

  if (len != 1)
    {
      fprintf (stderr, "Failed write\n");
      exit (1);
    }
   
  printf ("Ready to receive data\n");

  while (blocks_received < blocks_to_receive)
    {
      struct timeval spinstart;
      struct timeval spinend;
      struct ibv_wc wc;
      int wcs;
      
      memset (&wc, '\0', sizeof (wc));

      gettimeofday (&spinstart, NULL);
      
      do {
	unsigned long t;

	if ((wcs = ibv_poll_cq (server_ibdata.ibv_cq, 1, &wc)) < 0)
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
	      _qp_info (server_ibdata.ibv_qp, "Server QP Info");
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
	    _qp_info (server_ibdata.ibv_qp, "Server QP Info");
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
	printf ("Received block %u (of %u) of size %u (seq = %u)\n",
		blocks_received,
		blocks_to_receive,
		server_ibdata.bufsize,
		*(unsigned int *)server_ibdata.buf);
      
      if (check_data_correct (server_ibdata.buf + sizeof (unsigned int),
			      server_ibdata.bufsize - sizeof (unsigned int)))
	printf ("Block %u has invalid data\n", blocks_received);

      /* put back WR that was taken out */
      _server_post_recv (&server_ibdata);
    }

 breakout:

  printf ("Received %u blocks, each %llu bytes\n",
          blocks_received,
          server_ibdata.bufsize);
  
  printf ("Total received %llu bytes\n",
          (uint64_t)blocks_received * server_ibdata.bufsize);

  if ((err = ibv_destroy_qp (server_ibdata.ibv_qp)))
    {
      fprintf (stderr, "ibv_destroy_qp: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_destroy_cq (server_ibdata.ibv_cq)))
    {
      fprintf (stderr, "ibv_destroy_cq: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_dereg_mr (server_ibdata.ibv_mr)))
    {
      fprintf (stderr, "ibv_dereg_mr: %s\n", strerror (err));
      exit (1);
    }

  if ((err = ibv_dealloc_pd (server_ibdata.ibv_pd)))
    {
      fprintf (stderr, "ibv_dealloc_pd: %s\n", strerror (err));
      exit (1);
    }
  
  if (ibv_close_device (server_ibdata.ibv_context) < 0)
    {
      /* Sets errno?? */
      fprintf (stderr, "ibv_close_device: %s\n", strerror (errno));
      exit (1);
    }

  ibv_free_device_list (ibv_device_list);

  free (server_ibdata.buf);

  if (close (server_ibdata.listenfd) < 0)
    {
      perror ("close");
      exit (1);
    }

  if (close (server_ibdata.connectionfd) < 0)
    {
      perror ("close");
      exit (1);
    }
}

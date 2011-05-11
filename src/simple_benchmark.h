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

#ifndef _SIMPLE_BENCHMARK_H
#define _SIMPLE_BENCHMARK_H

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
#define MICROSECOND_IN_SECOND         (MILLISECOND_IN_SECOND * MICROSECOND_IN_MILLISECOND)

#define BLOCK_PATTERN                 0xC7

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

#endif /* _SIMPLE_BENCHMARK_H */
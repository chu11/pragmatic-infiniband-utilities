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

#ifndef _SIMPLE_BENCHMARK_COMMON_H
#define _SIMPLE_BENCHMARK_COMMON_H

#include <stdint.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>

void gethostbyname_r_common (struct hostent *hent);

void calc_bufsize (size_t *bufsize);

uint8_t *create_buf (size_t bufsize);

void calc_blocks (unsigned int *blocks);

void setup_client_serveraddr (struct sockaddr_in *serveraddr);

void setup_server_serveraddr (struct sockaddr_in *serveraddr);

void elapsed_time_output (struct timeval *starttime, struct timeval *endtime);

int check_data_correct (const uint8_t *buf, size_t bufsize);

#endif /* _SIMPLE_BENCHMARK_COMMON_H */

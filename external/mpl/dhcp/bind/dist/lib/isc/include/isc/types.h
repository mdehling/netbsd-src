/*	$NetBSD: types.h,v 1.1.2.2 2024/02/24 13:07:27 martin Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#ifndef ISC_TYPES_H
#define ISC_TYPES_H 1

#include <isc/bind9.h>

/*! \file isc/types.h
 * \brief
 * OS-specific types, from the OS-specific include directories.
 */
#include <inttypes.h>
#include <stdbool.h>

#include <isc/offset.h>

/*
 * XXXDCL This is just for ISC_LIST and ISC_LINK, but gets all of the other
 * list macros too.
 */
#include <isc/list.h>

/* Core Types.  Alphabetized by defined type. */

typedef struct isc_astack	    isc_astack_t; /*%< Array-based fast stack */
typedef struct isc_appctx	    isc_appctx_t; /*%< Application context */
typedef struct isc_backtrace_symmap isc_backtrace_symmap_t; /*%< Symbol Table
							     * Entry */
typedef struct isc_buffer isc_buffer_t;			    /*%< Buffer */
typedef ISC_LIST(isc_buffer_t) isc_bufferlist_t;	    /*%< Buffer List */
typedef struct isc_constregion	   isc_constregion_t;	    /*%< Const region */
typedef struct isc_consttextregion isc_consttextregion_t; /*%< Const Text Region
							   */
typedef struct isc_counter isc_counter_t;		  /*%< Counter */
typedef int16_t		   isc_dscp_t;	       /*%< Diffserv code point */
typedef struct isc_event   isc_event_t;	       /*%< Event */
typedef ISC_LIST(isc_event_t) isc_eventlist_t; /*%< Event List */
typedef unsigned int	 isc_eventtype_t;      /*%< Event Type */
typedef uint32_t	 isc_fsaccess_t;       /*%< FS Access */
typedef struct isc_hash	 isc_hash_t;	       /*%< Hash */
typedef struct isc_httpd isc_httpd_t;	       /*%< HTTP client */
typedef void(isc_httpdfree_t)(isc_buffer_t *, void *); /*%< HTTP free function
							*/
typedef struct isc_httpdmgr isc_httpdmgr_t;	       /*%< HTTP manager */
typedef struct isc_httpdurl isc_httpdurl_t;	       /*%< HTTP URL */
typedef void(isc_httpdondestroy_t)(void *); /*%< Callback on destroying httpd */
typedef struct isc_interface	 isc_interface_t;     /*%< Interface */
typedef struct isc_interfaceiter isc_interfaceiter_t; /*%< Interface Iterator */
typedef struct isc_interval	 isc_interval_t;      /*%< Interval */
typedef struct isc_lex		 isc_lex_t;	      /*%< Lex */
typedef struct isc_log		 isc_log_t;	      /*%< Log */
typedef struct isc_logcategory	 isc_logcategory_t;   /*%< Log Category */
typedef struct isc_logconfig	 isc_logconfig_t;     /*%< Log Configuration */
typedef struct isc_logmodule	 isc_logmodule_t;     /*%< Log Module */
typedef struct isc_mem		 isc_mem_t;	      /*%< Memory */
typedef struct isc_mempool	 isc_mempool_t;	      /*%< Memory Pool */
typedef struct isc_netaddr	 isc_netaddr_t;	      /*%< Net Address */
typedef struct isc_nm		 isc_nm_t;	      /*%< Network manager */
typedef struct isc_nmsocket	 isc_nmsocket_t; /*%< Network manager socket */
typedef struct isc_nmhandle	 isc_nmhandle_t; /*%< Network manager handle */
typedef struct isc_portset	 isc_portset_t;	 /*%< Port Set */
typedef struct isc_quota	 isc_quota_t;	 /*%< Quota */
typedef struct isc_ratelimiter	 isc_ratelimiter_t;   /*%< Rate Limiter */
typedef struct isc_region	 isc_region_t;	      /*%< Region */
typedef uint64_t		 isc_resourcevalue_t; /*%< Resource Value */
typedef unsigned int		 isc_result_t;	      /*%< Result */
typedef struct isc_rwlock	 isc_rwlock_t;	      /*%< Read Write Lock */
typedef struct isc_sockaddr	 isc_sockaddr_t;      /*%< Socket Address */
typedef ISC_LIST(isc_sockaddr_t) isc_sockaddrlist_t;  /*%< Socket Address List
						       * */
typedef struct isc_socket      isc_socket_t;	      /*%< Socket */
typedef struct isc_socketevent isc_socketevent_t;     /*%< Socket Event */
typedef struct isc_socketmgr   isc_socketmgr_t;	      /*%< Socket Manager */
typedef struct isc_stats       isc_stats_t;	      /*%< Statistics */
#if defined(_WIN32) && !defined(_WIN64) || !defined(_LP64)
typedef int_fast32_t isc_statscounter_t; /*%< Statistics Counter */
#else  /* if defined(_WIN32) && !defined(_WIN64) */
typedef int_fast64_t isc_statscounter_t;
#endif /* if defined(_WIN32) && !defined(_WIN64) */
typedef struct isc_symtab isc_symtab_t;		/*%< Symbol Table */
typedef struct isc_task	  isc_task_t;		/*%< Task */
typedef ISC_LIST(isc_task_t) isc_tasklist_t;	/*%< Task List */
typedef struct isc_taskmgr    isc_taskmgr_t;	/*%< Task Manager */
typedef struct isc_textregion isc_textregion_t; /*%< Text Region */
typedef struct isc_time	      isc_time_t;	/*%< Time */
typedef struct isc_timer      isc_timer_t;	/*%< Timer */
typedef struct isc_timermgr   isc_timermgr_t;	/*%< Timer Manager */

typedef void (*isc_taskaction_t)(isc_task_t *, isc_event_t *);
typedef int (*isc_sockfdwatch_t)(isc_task_t *, isc_socket_t *, void *, int);

/* The following cannot be listed alphabetically due to forward reference */
typedef isc_result_t(isc_httpdaction_t)(
	const char *url, isc_httpdurl_t *urlinfo, const char *querystring,
	const char *headers, void *arg, unsigned int *retcode,
	const char **retmsg, const char **mimetype, isc_buffer_t *body,
	isc_httpdfree_t **freecb, void **freecb_args);
typedef bool(isc_httpdclientok_t)(const isc_sockaddr_t *, void *);

/*% Resource */
typedef enum {
	isc_resource_coresize = 1,
	isc_resource_cputime,
	isc_resource_datasize,
	isc_resource_filesize,
	isc_resource_lockedmemory,
	isc_resource_openfiles,
	isc_resource_processes,
	isc_resource_residentsize,
	isc_resource_stacksize
} isc_resource_t;

/*% Statistics formats (text file or XML) */
typedef enum {
	isc_statsformat_file,
	isc_statsformat_xml,
	isc_statsformat_json
} isc_statsformat_t;

#endif /* ISC_TYPES_H */

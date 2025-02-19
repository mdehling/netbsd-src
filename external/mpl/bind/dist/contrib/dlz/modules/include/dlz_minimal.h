/*	$NetBSD: dlz_minimal.h,v 1.5.2.1 2024/02/25 15:45:52 martin Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright
 * notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This header provides a minimal set of defines and typedefs needed
 * for building an external DLZ module for bind9. When creating a new
 * external DLZ driver, please copy this header into your own source
 * tree.
 */

#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

typedef unsigned int isc_result_t;
typedef uint32_t     dns_ttl_t;

/*
 * Define DLZ_DLOPEN_VERSION to different values to use older versions
 * of the interface
 */
#ifndef DLZ_DLOPEN_VERSION
#define DLZ_DLOPEN_VERSION 3
#define DLZ_DLOPEN_AGE	   0
#endif /* ifndef DLZ_DLOPEN_VERSION */

/* return these in flags from dlz_version() */
#define DNS_SDLZFLAG_THREADSAFE	   0x00000001U
#define DNS_SDLZFLAG_RELATIVEOWNER 0x00000002U
#define DNS_SDLZFLAG_RELATIVERDATA 0x00000004U

/* result codes */
#define ISC_R_SUCCESS	     0
#define ISC_R_NOMEMORY	     1
#define ISC_R_NOPERM	     6
#define ISC_R_NOSPACE	     19
#define ISC_R_NOTFOUND	     23
#define ISC_R_FAILURE	     25
#define ISC_R_NOTIMPLEMENTED 27
#define ISC_R_NOMORE	     29
#define ISC_R_INVALIDFILE    30
#define ISC_R_UNEXPECTED     34
#define ISC_R_FILENOTFOUND   38

/* log levels */
#define ISC_LOG_INFO	     (-1)
#define ISC_LOG_NOTICE	     (-2)
#define ISC_LOG_WARNING	     (-3)
#define ISC_LOG_ERROR	     (-4)
#define ISC_LOG_CRITICAL     (-5)
#define ISC_LOG_DEBUG(level) (level)

/* other useful definitions */
#define UNUSED(x) (void)(x)
#define DE_CONST(konst, var)           \
	do {                           \
		union {                \
			const void *k; \
			void	   *v; \
		} _u;                  \
		_u.k = konst;          \
		var = _u.v;            \
	} while (0)

#if !defined(__has_attribute)
#define __has_attribute(x) 0
#endif /* if !defined(__has_attribute) */

#if __GNUC__ >= 7 || __has_attribute(fallthrough)
#define FALLTHROUGH __attribute__((fallthrough))
#else
/* clang-format off */
#define FALLTHROUGH do {} while (0) /* FALLTHROUGH */
/* clang-format on */
#endif

#ifdef __GNUC__
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE() abort()
#endif

/* opaque structures */
typedef void *dns_sdlzlookup_t;
typedef void *dns_sdlzallnodes_t;
typedef void *dns_view_t;
typedef void *dns_dlzdb_t;

#if DLZ_DLOPEN_VERSION > 1
/*
 * Method and type definitions needed for retrieval of client info
 * from the caller.
 */
typedef struct isc_sockaddr {
	union {
		struct sockaddr	    sa;
		struct sockaddr_in  sin;
		struct sockaddr_in6 sin6;
		struct sockaddr_un  sunix;
	} type;
	unsigned int length;
	void	    *link;
} isc_sockaddr_t;

typedef struct isc_netaddr {
	unsigned int family;
	union {
		struct in_addr	in;
		struct in6_addr in6;
		char		un[sizeof(((struct sockaddr_un *)0)->sun_path)];
	} type;
	uint32_t zone;
} isc_netaddr_t;

typedef struct dns_ecs {
	isc_netaddr_t addr;
	uint8_t	      source;
	uint8_t	      scope;
} dns_ecs_t;

#define DNS_CLIENTINFO_VERSION 3
typedef struct dns_clientinfo {
	uint16_t  version;
	void	 *data;
	void	 *dbversion;
	dns_ecs_t ecs;
} dns_clientinfo_t;

typedef isc_result_t (*dns_clientinfo_sourceip_t)(dns_clientinfo_t *client,
						  isc_sockaddr_t  **addrp);

typedef isc_result_t (*dns_clientinfo_version_t)(dns_clientinfo_t *client,
						 void		 **addrp);

#define DNS_CLIENTINFOMETHODS_VERSION 2
#define DNS_CLIENTINFOMETHODS_AGE     1
typedef struct dns_clientinfomethods {
	uint16_t		  version;
	uint16_t		  age;
	dns_clientinfo_sourceip_t sourceip;
} dns_clientinfomethods_t;
#endif /* DLZ_DLOPEN_VERSION > 1 */

#define DNS_ECS_FORMATSIZE                                                \
	sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:XXX.XXX.XXX.XXX%SSSSSSSSSS" \
	       "/NNN/NNN")

/*
 * Method definitions for callbacks provided by the dlopen driver
 */
typedef void
log_t(int level, const char *fmt, ...);

typedef isc_result_t
dns_sdlz_putrr_t(dns_sdlzlookup_t *lookup, const char *type, dns_ttl_t ttl,
		 const char *data);

typedef isc_result_t
dns_sdlz_putnamedrr_t(dns_sdlzallnodes_t *allnodes, const char *name,
		      const char *type, dns_ttl_t ttl, const char *data);

#if DLZ_DLOPEN_VERSION < 3
typedef isc_result_t
dns_dlz_writeablezone_t(dns_view_t *view, const char *zone_name);
#else  /* DLZ_DLOPEN_VERSION >= 3 */
typedef isc_result_t
dns_dlz_writeablezone_t(dns_view_t *view, dns_dlzdb_t *dlzdb,
			const char *zone_name);
#endif /* DLZ_DLOPEN_VERSION */

/*
 * prototypes for the functions you can include in your module
 */

/*
 * dlz_version() is required for all DLZ external drivers. It should
 * return DLZ_DLOPEN_VERSION.  'flags' is updated to indicate capabilities
 * of the module.  In particular, if the module is thread-safe then it
 * sets 'flags' to include DNS_SDLZFLAG_THREADSAFE.  Other capability
 * flags may be added in the future.
 */
int
dlz_version(unsigned int *flags);

/*
 * dlz_create() is required for all DLZ external drivers.
 */
isc_result_t
dlz_create(const char *dlzname, unsigned int argc, char *argv[], void **dbdata,
	   ...);

/*
 * dlz_destroy() is optional, and will be called when the driver is
 * unloaded if supplied
 */
void
dlz_destroy(void *dbdata);

/*
 * dlz_findzonedb is required for all DLZ external drivers
 */
#if DLZ_DLOPEN_VERSION < 3
isc_result_t
dlz_findzonedb(void *dbdata, const char *name);
#else  /* DLZ_DLOPEN_VERSION >= 3 */
isc_result_t
dlz_findzonedb(void *dbdata, const char *name, dns_clientinfomethods_t *methods,
	       dns_clientinfo_t *clientinfo);
#endif /* DLZ_DLOPEN_VERSION */

/*
 * dlz_lookup is required for all DLZ external drivers
 */
#if DLZ_DLOPEN_VERSION == 1
isc_result_t
dlz_lookup(const char *zone, const char *name, void *dbdata,
	   dns_sdlzlookup_t *lookup);
#else  /* DLZ_DLOPEN_VERSION > 1 */
isc_result_t
dlz_lookup(const char *zone, const char *name, void *dbdata,
	   dns_sdlzlookup_t *lookup, dns_clientinfomethods_t *methods,
	   dns_clientinfo_t *clientinfo);
#endif /* DLZ_DLOPEN_VERSION */

/*
 * dlz_authority() is optional if dlz_lookup() supplies
 * authority information (i.e., SOA, NS) for the dns record
 */
isc_result_t
dlz_authority(const char *zone, void *dbdata, dns_sdlzlookup_t *lookup);

/*
 * dlz_allowzonexfr() is optional, and should be supplied if you want to
 * support zone transfers
 */
isc_result_t
dlz_allowzonexfr(void *dbdata, const char *name, const char *client);

/*
 * dlz_allnodes() is optional, but must be supplied if supply a
 * dlz_allowzonexfr() function
 */
isc_result_t
dlz_allnodes(const char *zone, void *dbdata, dns_sdlzallnodes_t *allnodes);

/*
 * dlz_newversion() is optional. It should be supplied if you want to
 * support dynamic updates.
 */
isc_result_t
dlz_newversion(const char *zone, void *dbdata, void **versionp);

/*
 * dlz_closeversion() is optional, but must be supplied if you supply a
 * dlz_newversion() function
 */
void
dlz_closeversion(const char *zone, bool commit, void *dbdata, void **versionp);

/*
 * dlz_configure() is optional, but must be supplied if you want to support
 * dynamic updates
 */
#if DLZ_DLOPEN_VERSION < 3
isc_result_t
dlz_configure(dns_view_t *view, void *dbdata);
#else  /* DLZ_DLOPEN_VERSION >= 3 */
isc_result_t
dlz_configure(dns_view_t *view, dns_dlzdb_t *dlzdb, void *dbdata);
#endif /* DLZ_DLOPEN_VERSION */

/*
 * dlz_ssumatch() is optional, but must be supplied if you want to support
 * dynamic updates
 */
bool
dlz_ssumatch(const char *signer, const char *name, const char *tcpaddr,
	     const char *type, const char *key, uint32_t keydatalen,
	     uint8_t *keydata, void *dbdata);

/*
 * dlz_addrdataset() is optional, but must be supplied if you want to
 * support dynamic updates
 */
isc_result_t
dlz_addrdataset(const char *name, const char *rdatastr, void *dbdata,
		void *version);

/*
 * dlz_subrdataset() is optional, but must be supplied if you want to
 * support dynamic updates
 */
isc_result_t
dlz_subrdataset(const char *name, const char *rdatastr, void *dbdata,
		void *version);

/*
 * dlz_delrdataset() is optional, but must be supplied if you want to
 * support dynamic updates
 */
isc_result_t
dlz_delrdataset(const char *name, const char *type, void *dbdata,
		void *version);

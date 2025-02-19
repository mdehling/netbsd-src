/*	$NetBSD: interfaceiter.c,v 1.1.2.2 2024/02/24 13:07:30 martin Exp $	*/

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

/*! \file */

#include <sys/ioctl.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h> /* Required for ifiter_ioctl.c. */
#endif			/* ifdef HAVE_SYS_SOCKIO_H */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <isc/interfaceiter.h>
#include <isc/log.h>
#include <isc/magic.h>
#include <isc/mem.h>
#include <isc/net.h>
#include <isc/print.h>
#include <isc/result.h>
#include <isc/strerr.h>
#include <isc/string.h>
#include <isc/types.h>
#include <isc/util.h>

/* Must follow <isc/net.h>. */
#ifdef HAVE_NET_IF6_H
#include <net/if6.h>
#endif /* ifdef HAVE_NET_IF6_H */
#include <net/if.h>

/* Common utility functions */

/*%
 * Extract the network address part from a "struct sockaddr".
 * \brief
 * The address family is given explicitly
 * instead of using src->sa_family, because the latter does not work
 * for copying a network mask obtained by SIOCGIFNETMASK (it does
 * not have a valid address family).
 */

static void
get_addr(unsigned int family, isc_netaddr_t *dst, struct sockaddr *src,
	 char *ifname) {
	struct sockaddr_in6 *sa6;

#if !defined(HAVE_IF_NAMETOINDEX)
	UNUSED(ifname);
#endif /* if !defined(HAVE_IF_NAMETOINDEX) */

	/* clear any remaining value for safety */
	memset(dst, 0, sizeof(*dst));

	dst->family = family;
	switch (family) {
	case AF_INET:
		memmove(&dst->type.in, &((struct sockaddr_in *)src)->sin_addr,
			sizeof(struct in_addr));
		break;
	case AF_INET6:
		sa6 = (struct sockaddr_in6 *)src;
		memmove(&dst->type.in6, &sa6->sin6_addr,
			sizeof(struct in6_addr));
		if (sa6->sin6_scope_id != 0) {
			isc_netaddr_setzone(dst, sa6->sin6_scope_id);
		} else {
			/*
			 * BSD variants embed scope zone IDs in the 128bit
			 * address as a kernel internal form.  Unfortunately,
			 * the embedded IDs are not hidden from applications
			 * when getting access to them by sysctl or ioctl.
			 * We convert the internal format to the pure address
			 * part and the zone ID part.
			 * Since multicast addresses should not appear here
			 * and they cannot be distinguished from netmasks,
			 * we only consider unicast link-local addresses.
			 */
			if (IN6_IS_ADDR_LINKLOCAL(&sa6->sin6_addr)) {
				uint16_t zone16;

				memmove(&zone16, &sa6->sin6_addr.s6_addr[2],
					sizeof(zone16));
				zone16 = ntohs(zone16);
				if (zone16 != 0) {
					/* the zone ID is embedded */
					isc_netaddr_setzone(dst,
							    (uint32_t)zone16);
					dst->type.in6.s6_addr[2] = 0;
					dst->type.in6.s6_addr[3] = 0;
#ifdef HAVE_IF_NAMETOINDEX
				} else if (ifname != NULL) {
					unsigned int zone;

					/*
					 * sin6_scope_id is still not provided,
					 * but the corresponding interface name
					 * is know.  Use the interface ID as
					 * the link ID.
					 */
					zone = if_nametoindex(ifname);
					if (zone != 0) {
						isc_netaddr_setzone(
							dst, (uint32_t)zone);
					}
#endif /* ifdef HAVE_IF_NAMETOINDEX */
				}
			}
		}
		break;
	default:
		UNREACHABLE();
	}
}

/*
 * Include system-dependent code.
 */

#ifdef __linux
#define ISC_IF_INET6_SZ                                        \
	sizeof("00000000000000000000000000000001 01 80 10 80 " \
	       "XXXXXXloXXXXXXXX\n")
static isc_result_t
linux_if_inet6_next(isc_interfaceiter_t *);
static isc_result_t
linux_if_inet6_current(isc_interfaceiter_t *);
static void
linux_if_inet6_first(isc_interfaceiter_t *iter);
#endif /* ifdef __linux */

#include "ifiter_getifaddrs.c"

#ifdef __linux
static void
linux_if_inet6_first(isc_interfaceiter_t *iter) {
	if (iter->proc != NULL) {
		rewind(iter->proc);
		(void)linux_if_inet6_next(iter);
	} else {
		iter->valid = ISC_R_NOMORE;
	}
}

static isc_result_t
linux_if_inet6_next(isc_interfaceiter_t *iter) {
	if (iter->proc != NULL &&
	    fgets(iter->entry, sizeof(iter->entry), iter->proc) != NULL)
	{
		iter->valid = ISC_R_SUCCESS;
	} else {
		iter->valid = ISC_R_NOMORE;
	}
	return (iter->valid);
}

static isc_result_t
linux_if_inet6_current(isc_interfaceiter_t *iter) {
	char address[33];
	char name[IF_NAMESIZE + 1];
	struct in6_addr addr6;
	unsigned int ifindex, prefix, flag3, flag4;
	int res;
	unsigned int i;

	if (iter->valid != ISC_R_SUCCESS) {
		return (iter->valid);
	}
	if (iter->proc == NULL) {
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_INTERFACE, ISC_LOG_ERROR,
			      "/proc/net/if_inet6:iter->proc == NULL");
		return (ISC_R_FAILURE);
	}

	res = sscanf(iter->entry, "%32[a-f0-9] %x %x %x %x %16s\n", address,
		     &ifindex, &prefix, &flag3, &flag4, name);
	if (res != 6) {
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_INTERFACE, ISC_LOG_ERROR,
			      "/proc/net/if_inet6:sscanf() -> %d (expected 6)",
			      res);
		return (ISC_R_FAILURE);
	}
	if (strlen(address) != 32) {
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_INTERFACE, ISC_LOG_ERROR,
			      "/proc/net/if_inet6:strlen(%s) != 32", address);
		return (ISC_R_FAILURE);
	}
	for (i = 0; i < 16; i++) {
		unsigned char byte;
		static const char hex[] = "0123456789abcdef";
		byte = ((strchr(hex, address[i * 2]) - hex) << 4) |
		       (strchr(hex, address[i * 2 + 1]) - hex);
		addr6.s6_addr[i] = byte;
	}
	iter->current.af = AF_INET6;
	iter->current.flags = INTERFACE_F_UP;
	isc_netaddr_fromin6(&iter->current.address, &addr6);
	if (isc_netaddr_islinklocal(&iter->current.address)) {
		isc_netaddr_setzone(&iter->current.address, (uint32_t)ifindex);
	}
	for (i = 0; i < 16; i++) {
		if (prefix > 8) {
			addr6.s6_addr[i] = 0xff;
			prefix -= 8;
		} else {
			addr6.s6_addr[i] = (0xff << (8 - prefix)) & 0xff;
			prefix = 0;
		}
	}
	isc_netaddr_fromin6(&iter->current.netmask, &addr6);
	strlcpy(iter->current.name, name, sizeof(iter->current.name));
	return (ISC_R_SUCCESS);
}
#endif /* ifdef __linux */

/*
 * The remaining code is common to the sysctl and ioctl case.
 */

isc_result_t
isc_interfaceiter_current(isc_interfaceiter_t *iter, isc_interface_t *ifdata) {
	REQUIRE(iter->result == ISC_R_SUCCESS);
	memmove(ifdata, &iter->current, sizeof(*ifdata));
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_interfaceiter_first(isc_interfaceiter_t *iter) {
	isc_result_t result;

	REQUIRE(VALID_IFITER(iter));

	internal_first(iter);
	for (;;) {
		result = internal_current(iter);
		if (result != ISC_R_IGNORE) {
			break;
		}
		result = internal_next(iter);
		if (result != ISC_R_SUCCESS) {
			break;
		}
	}
	iter->result = result;
	return (result);
}

isc_result_t
isc_interfaceiter_next(isc_interfaceiter_t *iter) {
	isc_result_t result;

	REQUIRE(VALID_IFITER(iter));
	REQUIRE(iter->result == ISC_R_SUCCESS);

	for (;;) {
		result = internal_next(iter);
		if (result != ISC_R_SUCCESS) {
			break;
		}
		result = internal_current(iter);
		if (result != ISC_R_IGNORE) {
			break;
		}
	}
	iter->result = result;
	return (result);
}

void
isc_interfaceiter_destroy(isc_interfaceiter_t **iterp) {
	isc_interfaceiter_t *iter;
	REQUIRE(iterp != NULL);
	iter = *iterp;
	*iterp = NULL;
	REQUIRE(VALID_IFITER(iter));

	internal_destroy(iter);
	if (iter->buf != NULL) {
		isc_mem_put(iter->mctx, iter->buf, iter->bufsize);
	}

	iter->magic = 0;
	isc_mem_put(iter->mctx, iter, sizeof(*iter));
}

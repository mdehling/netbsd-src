/*	$NetBSD: dns64.c,v 1.1.2.2 2024/02/24 13:06:56 martin Exp $	*/

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

#include <stdbool.h>
#include <string.h>

#include <isc/list.h>
#include <isc/mem.h>
#include <isc/netaddr.h>
#include <isc/string.h>
#include <isc/util.h>

#include <dns/acl.h>
#include <dns/dns64.h>
#include <dns/rdata.h>
#include <dns/rdataset.h>
#include <dns/result.h>

struct dns_dns64 {
	unsigned char bits[16]; /*
				 * Prefix + suffix bits.
				 */
	dns_acl_t *clients;	/*
				 * Which clients get mapped
				 * addresses.
				 */
	dns_acl_t *mapped;	/*
				 * IPv4 addresses to be mapped.
				 */
	dns_acl_t *excluded;	/*
				 * IPv6 addresses that are
				 * treated as not existing.
				 */
	unsigned int prefixlen; /*
				 * Start of mapped address.
				 */
	unsigned int flags;
	isc_mem_t *mctx;
	ISC_LINK(dns_dns64_t) link;
};

isc_result_t
dns_dns64_create(isc_mem_t *mctx, const isc_netaddr_t *prefix,
		 unsigned int prefixlen, const isc_netaddr_t *suffix,
		 dns_acl_t *clients, dns_acl_t *mapped, dns_acl_t *excluded,
		 unsigned int flags, dns_dns64_t **dns64p) {
	dns_dns64_t *dns64;
	unsigned int nbytes = 16;

	REQUIRE(prefix != NULL && prefix->family == AF_INET6);
	/* Legal prefix lengths from rfc6052.txt. */
	REQUIRE(prefixlen == 32 || prefixlen == 40 || prefixlen == 48 ||
		prefixlen == 56 || prefixlen == 64 || prefixlen == 96);
	REQUIRE(isc_netaddr_prefixok(prefix, prefixlen) == ISC_R_SUCCESS);
	REQUIRE(dns64p != NULL && *dns64p == NULL);

	if (suffix != NULL) {
		static const unsigned char zeros[16];
		REQUIRE(prefix->family == AF_INET6);
		nbytes = prefixlen / 8 + 4;
		/* Bits 64-71 are zeros. rfc6052.txt */
		if (prefixlen >= 32 && prefixlen <= 64) {
			nbytes++;
		}
		REQUIRE(memcmp(suffix->type.in6.s6_addr, zeros, nbytes) == 0);
	}

	dns64 = isc_mem_get(mctx, sizeof(dns_dns64_t));
	memset(dns64->bits, 0, sizeof(dns64->bits));
	memmove(dns64->bits, prefix->type.in6.s6_addr, prefixlen / 8);
	if (suffix != NULL) {
		memmove(dns64->bits + nbytes, suffix->type.in6.s6_addr + nbytes,
			16 - nbytes);
	}
	dns64->clients = NULL;
	if (clients != NULL) {
		dns_acl_attach(clients, &dns64->clients);
	}
	dns64->mapped = NULL;
	if (mapped != NULL) {
		dns_acl_attach(mapped, &dns64->mapped);
	}
	dns64->excluded = NULL;
	if (excluded != NULL) {
		dns_acl_attach(excluded, &dns64->excluded);
	}
	dns64->prefixlen = prefixlen;
	dns64->flags = flags;
	ISC_LINK_INIT(dns64, link);
	dns64->mctx = NULL;
	isc_mem_attach(mctx, &dns64->mctx);
	*dns64p = dns64;
	return (ISC_R_SUCCESS);
}

void
dns_dns64_destroy(dns_dns64_t **dns64p) {
	dns_dns64_t *dns64;

	REQUIRE(dns64p != NULL && *dns64p != NULL);

	dns64 = *dns64p;
	*dns64p = NULL;

	REQUIRE(!ISC_LINK_LINKED(dns64, link));

	if (dns64->clients != NULL) {
		dns_acl_detach(&dns64->clients);
	}
	if (dns64->mapped != NULL) {
		dns_acl_detach(&dns64->mapped);
	}
	if (dns64->excluded != NULL) {
		dns_acl_detach(&dns64->excluded);
	}
	isc_mem_putanddetach(&dns64->mctx, dns64, sizeof(*dns64));
}

isc_result_t
dns_dns64_aaaafroma(const dns_dns64_t *dns64, const isc_netaddr_t *reqaddr,
		    const dns_name_t *reqsigner, const dns_aclenv_t *env,
		    unsigned int flags, unsigned char *a, unsigned char *aaaa) {
	unsigned int nbytes, i;
	isc_result_t result;
	int match;

	if ((dns64->flags & DNS_DNS64_RECURSIVE_ONLY) != 0 &&
	    (flags & DNS_DNS64_RECURSIVE) == 0)
	{
		return (DNS_R_DISALLOWED);
	}

	if ((dns64->flags & DNS_DNS64_BREAK_DNSSEC) == 0 &&
	    (flags & DNS_DNS64_DNSSEC) != 0)
	{
		return (DNS_R_DISALLOWED);
	}

	if (dns64->clients != NULL) {
		result = dns_acl_match(reqaddr, reqsigner, dns64->clients, env,
				       &match, NULL);
		if (result != ISC_R_SUCCESS) {
			return (result);
		}
		if (match <= 0) {
			return (DNS_R_DISALLOWED);
		}
	}

	if (dns64->mapped != NULL) {
		struct in_addr ina;
		isc_netaddr_t netaddr;

		memmove(&ina.s_addr, a, 4);
		isc_netaddr_fromin(&netaddr, &ina);
		result = dns_acl_match(&netaddr, NULL, dns64->mapped, env,
				       &match, NULL);
		if (result != ISC_R_SUCCESS) {
			return (result);
		}
		if (match <= 0) {
			return (DNS_R_DISALLOWED);
		}
	}

	nbytes = dns64->prefixlen / 8;
	INSIST(nbytes <= 12);
	/* Copy prefix. */
	memmove(aaaa, dns64->bits, nbytes);
	/* Bits 64-71 are zeros. rfc6052.txt */
	if (nbytes == 8) {
		aaaa[nbytes++] = 0;
	}
	/* Copy mapped address. */
	for (i = 0; i < 4U; i++) {
		aaaa[nbytes++] = a[i];
		/* Bits 64-71 are zeros. rfc6052.txt */
		if (nbytes == 8) {
			aaaa[nbytes++] = 0;
		}
	}
	/* Copy suffix. */
	memmove(aaaa + nbytes, dns64->bits + nbytes, 16 - nbytes);
	return (ISC_R_SUCCESS);
}

dns_dns64_t *
dns_dns64_next(dns_dns64_t *dns64) {
	dns64 = ISC_LIST_NEXT(dns64, link);
	return (dns64);
}

void
dns_dns64_append(dns_dns64list_t *list, dns_dns64_t *dns64) {
	ISC_LIST_APPEND(*list, dns64, link);
}

void
dns_dns64_unlink(dns_dns64list_t *list, dns_dns64_t *dns64) {
	ISC_LIST_UNLINK(*list, dns64, link);
}

bool
dns_dns64_aaaaok(const dns_dns64_t *dns64, const isc_netaddr_t *reqaddr,
		 const dns_name_t *reqsigner, const dns_aclenv_t *env,
		 unsigned int flags, dns_rdataset_t *rdataset, bool *aaaaok,
		 size_t aaaaoklen) {
	struct in6_addr in6;
	isc_netaddr_t netaddr;
	isc_result_t result;
	int match;
	bool answer = false;
	bool found = false;
	unsigned int i, ok;

	REQUIRE(rdataset != NULL);
	REQUIRE(rdataset->type == dns_rdatatype_aaaa);
	REQUIRE(rdataset->rdclass == dns_rdataclass_in);
	if (aaaaok != NULL) {
		REQUIRE(aaaaoklen == dns_rdataset_count(rdataset));
	}

	for (; dns64 != NULL; dns64 = ISC_LIST_NEXT(dns64, link)) {
		if ((dns64->flags & DNS_DNS64_RECURSIVE_ONLY) != 0 &&
		    (flags & DNS_DNS64_RECURSIVE) == 0)
		{
			continue;
		}

		if ((dns64->flags & DNS_DNS64_BREAK_DNSSEC) == 0 &&
		    (flags & DNS_DNS64_DNSSEC) != 0)
		{
			continue;
		}
		/*
		 * Work out if this dns64 structure applies to this client.
		 */
		if (dns64->clients != NULL) {
			result = dns_acl_match(reqaddr, reqsigner,
					       dns64->clients, env, &match,
					       NULL);
			if (result != ISC_R_SUCCESS) {
				continue;
			}
			if (match <= 0) {
				continue;
			}
		}

		if (!found && aaaaok != NULL) {
			for (i = 0; i < aaaaoklen; i++) {
				aaaaok[i] = false;
			}
		}
		found = true;

		/*
		 * If we are not excluding any addresses then any AAAA
		 * will do.
		 */
		if (dns64->excluded == NULL) {
			answer = true;
			if (aaaaok == NULL) {
				goto done;
			}
			for (i = 0; i < aaaaoklen; i++) {
				aaaaok[i] = true;
			}
			goto done;
		}

		i = 0;
		ok = 0;
		for (result = dns_rdataset_first(rdataset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(rdataset))
		{
			dns_rdata_t rdata = DNS_RDATA_INIT;
			if (aaaaok == NULL || !aaaaok[i]) {
				dns_rdataset_current(rdataset, &rdata);
				memmove(&in6.s6_addr, rdata.data, 16);
				isc_netaddr_fromin6(&netaddr, &in6);

				result = dns_acl_match(&netaddr, NULL,
						       dns64->excluded, env,
						       &match, NULL);
				if (result == ISC_R_SUCCESS && match <= 0) {
					answer = true;
					if (aaaaok == NULL) {
						goto done;
					}
					aaaaok[i] = true;
					ok++;
				}
			} else {
				ok++;
			}
			i++;
		}
		/*
		 * Are all addresses ok?
		 */
		if (aaaaok != NULL && ok == aaaaoklen) {
			goto done;
		}
	}

done:
	if (!found && aaaaok != NULL) {
		for (i = 0; i < aaaaoklen; i++) {
			aaaaok[i] = true;
		}
	}
	return (found ? answer : true);
}

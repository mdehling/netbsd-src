/*	$NetBSD: resolver.h,v 1.8.2.1 2024/02/25 15:46:58 martin Exp $	*/

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

#pragma once

/*****
***** Module Info
*****/

/*! \file dns/resolver.h
 *
 * \brief
 * This is the BIND 9 resolver, the module responsible for resolving DNS
 * requests by iteratively querying authoritative servers and following
 * referrals.  This is a "full resolver", not to be confused with
 * the stub resolvers most people associate with the word "resolver".
 * The full resolver is part of the caching name server or resolver
 * daemon the stub resolver talks to.
 *
 * MP:
 *\li	The module ensures appropriate synchronization of data structures it
 *	creates and manipulates.
 *
 * Reliability:
 *\li	No anticipated impact.
 *
 * Resources:
 *\li	TBS
 *
 * Security:
 *\li	No anticipated impact.
 *
 * Standards:
 *\li	RFCs:	1034, 1035, 2181, TBS
 *\li	Drafts:	TBS
 */

#include <inttypes.h>
#include <stdbool.h>

#include <isc/event.h>
#include <isc/lang.h>
#include <isc/stats.h>

#include <dns/fixedname.h>
#include <dns/types.h>

ISC_LANG_BEGINDECLS

/*%
 * A dns_fetchevent_t is sent when a 'fetch' completes.  Any of 'db',
 * 'node', 'rdataset', and 'sigrdataset' may be bound.  It is the
 * receiver's responsibility to detach before freeing the event.
 * \brief
 * 'rdataset', 'sigrdataset', 'client' and 'id' are the values that were
 * supplied when dns_resolver_createfetch() was called.  They are returned
 *  to the caller so that they may be freed.
 */
typedef struct dns_fetchevent {
	ISC_EVENT_COMMON(struct dns_fetchevent);
	dns_fetch_t	     *fetch;
	isc_result_t	      result;
	dns_rdatatype_t	      qtype;
	dns_db_t	     *db;
	dns_dbnode_t	     *node;
	dns_rdataset_t	     *rdataset;
	dns_rdataset_t	     *sigrdataset;
	dns_fixedname_t	      fname;
	dns_name_t	     *foundname;
	const isc_sockaddr_t *client;
	dns_messageid_t	      id;
	isc_result_t	      vresult;
} dns_fetchevent_t;

/*%
 * The two quota types (fetches-per-zone and fetches-per-server)
 */
typedef enum { dns_quotatype_zone = 0, dns_quotatype_server } dns_quotatype_t;

/*
 * Options that modify how a 'fetch' is done.
 */
enum {
	DNS_FETCHOPT_TCP = 1 << 0,	       /*%< Use TCP. */
	DNS_FETCHOPT_UNSHARED = 1 << 1,	       /*%< See below. */
	DNS_FETCHOPT_RECURSIVE = 1 << 2,       /*%< Set RD? */
	DNS_FETCHOPT_NOEDNS0 = 1 << 3,	       /*%< Do not use EDNS. */
	DNS_FETCHOPT_FORWARDONLY = 1 << 4,     /*%< Only use forwarders. */
	DNS_FETCHOPT_NOVALIDATE = 1 << 5,      /*%< Disable validation. */
	DNS_FETCHOPT_WANTNSID = 1 << 6,	       /*%< Request NSID */
	DNS_FETCHOPT_PREFETCH = 1 << 7,	       /*%< Do prefetch */
	DNS_FETCHOPT_NOCDFLAG = 1 << 8,	       /*%< Don't set CD flag. */
	DNS_FETCHOPT_NONTA = 1 << 9,	       /*%< Ignore NTA table. */
	DNS_FETCHOPT_NOCACHED = 1 << 10,       /*%< Force cache update. */
	DNS_FETCHOPT_QMINIMIZE = 1 << 11,      /*%< Use qname minimization. */
	DNS_FETCHOPT_NOFOLLOW = 1 << 12,       /*%< Don't retrieve the NS RRset
						* from the child zone when a
						* delegation is returned in
						* response to a NS query. */
	DNS_FETCHOPT_QMIN_STRICT = 1 << 13,    /*%< Do not work around servers
						* that return errors on
						* non-empty terminals. */
	DNS_FETCHOPT_QMIN_SKIP_IP6A = 1 << 14, /*%< Skip some labels when
						* doing qname minimization
						* on ip6.arpa. */
	DNS_FETCHOPT_NOFORWARD = 1 << 15,      /*%< Do not use forwarders if
						* possible. */
	DNS_FETCHOPT_TRYSTALE_ONTIMEOUT = 1 << 16,

	/*% EDNS version bits: */
	DNS_FETCHOPT_EDNSVERSIONSET = 1 << 23,
	DNS_FETCHOPT_EDNSVERSIONSHIFT = 24,
	DNS_FETCHOPT_EDNSVERSIONMASK = 0xff000000,
};

/*
 * Upper bounds of class of query RTT (ms).  Corresponds to
 * dns_resstatscounter_queryrttX statistics counters.
 */
#define DNS_RESOLVER_QRYRTTCLASS0    10
#define DNS_RESOLVER_QRYRTTCLASS0STR "10"
#define DNS_RESOLVER_QRYRTTCLASS1    100
#define DNS_RESOLVER_QRYRTTCLASS1STR "100"
#define DNS_RESOLVER_QRYRTTCLASS2    500
#define DNS_RESOLVER_QRYRTTCLASS2STR "500"
#define DNS_RESOLVER_QRYRTTCLASS3    800
#define DNS_RESOLVER_QRYRTTCLASS3STR "800"
#define DNS_RESOLVER_QRYRTTCLASS4    1600
#define DNS_RESOLVER_QRYRTTCLASS4STR "1600"

/*
 * XXXRTH  Should this API be made semi-private?  (I.e.
 * _dns_resolver_create()).
 */

#define DNS_RESOLVER_CHECKNAMES	    0x01
#define DNS_RESOLVER_CHECKNAMESFAIL 0x02

#define DNS_QMIN_MAXLABELS	   7
#define DNS_QMIN_MAX_NO_DELEGATION 3
#define DNS_MAX_LABELS		   127

isc_result_t
dns_resolver_create(dns_view_t *view, isc_taskmgr_t *taskmgr,
		    unsigned int ntasks, unsigned int ndisp, isc_nm_t *nm,
		    isc_timermgr_t *timermgr, unsigned int options,
		    dns_dispatchmgr_t *dispatchmgr, dns_dispatch_t *dispatchv4,
		    dns_dispatch_t *dispatchv6, dns_resolver_t **resp);

/*%<
 * Create a resolver.
 *
 * Notes:
 *
 *\li	Generally, applications should not create a resolver directly, but
 *	should instead call dns_view_createresolver().
 *
 * Requires:
 *
 *\li	'view' is a valid view.
 *
 *\li	'taskmgr' is a valid task manager.
 *
 *\li	'ntasks' > 0.
 *
 *\li	'nm' is a valid network manager.
 *
 *\li	'timermgr' is a valid timer manager.
 *
 *\li	'dispatchv4' is a dispatch with an IPv4 UDP socket, or is NULL.
 *	If not NULL, 'ndisp' clones of it will be created by the resolver.
 *
 *\li	'dispatchv6' is a dispatch with an IPv6 UDP socket, or is NULL.
 *	If not NULL, 'ndisp' clones of it will be created by the resolver.
 *
 *\li	resp != NULL && *resp == NULL.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS				On success.
 *
 *\li	Anything else				Failure.
 */

void
dns_resolver_freeze(dns_resolver_t *res);
/*%<
 * Freeze resolver.
 *
 * Notes:
 *
 *\li	Certain configuration changes cannot be made after the resolver
 *	is frozen.  Fetches cannot be created until the resolver is frozen.
 *
 * Requires:
 *
 *\li	'res' is a valid resolver.
 *
 * Ensures:
 *
 *\li	'res' is frozen.
 */

void
dns_resolver_prime(dns_resolver_t *res);
/*%<
 * Prime resolver.
 *
 * Notes:
 *
 *\li	Resolvers which have a forwarding policy other than dns_fwdpolicy_only
 *	need to be primed with the root nameservers, otherwise the root
 *	nameserver hints data may be used indefinitely.  This function requests
 *	that the resolver start a priming fetch, if it isn't already priming.
 *
 * Requires:
 *
 *\li	'res' is a valid, frozen resolver.
 */

void
dns_resolver_whenshutdown(dns_resolver_t *res, isc_task_t *task,
			  isc_event_t **eventp);
/*%<
 * Send '*eventp' to 'task' when 'res' has completed shutdown.
 *
 * Notes:
 *
 *\li	It is not safe to detach the last reference to 'res' until
 *	shutdown is complete.
 *
 * Requires:
 *
 *\li	'res' is a valid resolver.
 *
 *\li	'task' is a valid task.
 *
 *\li	*eventp is a valid event.
 *
 * Ensures:
 *
 *\li	*eventp == NULL.
 */

void
dns_resolver_shutdown(dns_resolver_t *res);
/*%<
 * Start the shutdown process for 'res'.
 *
 * Notes:
 *
 *\li	This call has no effect if the resolver is already shutting down.
 *
 * Requires:
 *
 *\li	'res' is a valid resolver.
 */

void
dns_resolver_attach(dns_resolver_t *source, dns_resolver_t **targetp);

void
dns_resolver_detach(dns_resolver_t **resp);

isc_result_t
dns_resolver_createfetch(dns_resolver_t *res, const dns_name_t *name,
			 dns_rdatatype_t type, const dns_name_t *domain,
			 dns_rdataset_t	      *nameservers,
			 dns_forwarders_t     *forwarders,
			 const isc_sockaddr_t *client, dns_messageid_t id,
			 unsigned int options, unsigned int depth,
			 isc_counter_t *qc, isc_task_t *task,
			 isc_taskaction_t action, void *arg,
			 dns_rdataset_t *rdataset, dns_rdataset_t *sigrdataset,
			 dns_fetch_t **fetchp);
/*%<
 * Recurse to answer a question.
 *
 * Notes:
 *
 *\li	This call starts a query for 'name', type 'type'.
 *
 *\li	The 'domain' is a parent domain of 'name' for which
 *	a set of name servers 'nameservers' is known.  If no
 *	such name server information is available, set
 * 	'domain' and 'nameservers' to NULL.
 *
 *\li	'forwarders' is unimplemented, and subject to change when
 *	we figure out how selective forwarding will work.
 *
 *\li	When the fetch completes (successfully or otherwise), a
 *	#DNS_EVENT_FETCHDONE event with action 'action' and arg 'arg' will be
 *	posted to 'task'.
 *
 *\li	The values of 'rdataset' and 'sigrdataset' will be returned in
 *	the FETCHDONE event.
 *
 *\li	'client' and 'id' are used for duplicate query detection.  '*client'
 *	must remain stable until after 'action' has been called or
 *	dns_resolver_cancelfetch() is called.
 *
 * Requires:
 *
 *\li	'res' is a valid resolver that has been frozen.
 *
 *\li	'name' is a valid name.
 *
 *\li	'type' is not a meta type other than ANY.
 *
 *\li	'domain' is a valid name or NULL.
 *
 *\li	'nameservers' is a valid NS rdataset (whose owner name is 'domain')
 *	iff. 'domain' is not NULL.
 *
 *\li	'forwarders' is NULL.
 *
 *\li	'client' is a valid sockaddr or NULL.
 *
 *\li	'options' contains valid options.
 *
 *\li	'rdataset' is a valid, disassociated rdataset.
 *
 *\li	'sigrdataset' is NULL, or is a valid, disassociated rdataset.
 *
 *\li	fetchp != NULL && *fetchp == NULL.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS					Success
 *\li	#DNS_R_DUPLICATE
 *\li	#DNS_R_DROP
 *
 *\li	Many other values are possible, all of which indicate failure.
 */

void
dns_resolver_cancelfetch(dns_fetch_t *fetch);
/*%<
 * Cancel 'fetch'.
 *
 * Notes:
 *
 *\li	If 'fetch' has not completed, post its FETCHDONE event with a
 *	result code of #ISC_R_CANCELED.
 *
 * Requires:
 *
 *\li	'fetch' is a valid fetch.
 */

void
dns_resolver_destroyfetch(dns_fetch_t **fetchp);
/*%<
 * Destroy 'fetch'.
 *
 * Requires:
 *
 *\li	'*fetchp' is a valid fetch.
 *
 *\li	The caller has received the FETCHDONE event (either because the
 *	fetch completed or because dns_resolver_cancelfetch() was called).
 *
 * Ensures:
 *
 *\li	*fetchp == NULL.
 */

void
dns_resolver_logfetch(dns_fetch_t *fetch, isc_log_t *lctx,
		      isc_logcategory_t *category, isc_logmodule_t *module,
		      int level, bool duplicateok);
/*%<
 * Dump a log message on internal state at the completion of given 'fetch'.
 * 'lctx', 'category', 'module', and 'level' are used to write the log message.
 * By default, only one log message is written even if the corresponding fetch
 * context serves multiple clients; if 'duplicateok' is true the suppression
 * is disabled and the message can be written every time this function is
 * called.
 *
 * Requires:
 *
 *\li	'fetch' is a valid fetch, and has completed.
 */

dns_dispatchmgr_t *
dns_resolver_dispatchmgr(dns_resolver_t *resolver);

dns_dispatch_t *
dns_resolver_dispatchv4(dns_resolver_t *resolver);

dns_dispatch_t *
dns_resolver_dispatchv6(dns_resolver_t *resolver);

isc_taskmgr_t *
dns_resolver_taskmgr(dns_resolver_t *resolver);

uint32_t
dns_resolver_getlamettl(dns_resolver_t *resolver);
/*%<
 * Get the resolver's lame-ttl.  zero => no lame processing.
 *
 * Requires:
 *\li	'resolver' to be valid.
 */

void
dns_resolver_setlamettl(dns_resolver_t *resolver, uint32_t lame_ttl);
/*%<
 * Set the resolver's lame-ttl.  zero => no lame processing.
 *
 * Requires:
 *\li	'resolver' to be valid.
 */

void
dns_resolver_addalternate(dns_resolver_t *resolver, const isc_sockaddr_t *alt,
			  const dns_name_t *name, in_port_t port);
/*%<
 * Add alternate addresses to be tried in the event that the nameservers
 * for a zone are not available in the address families supported by the
 * operating system.
 *
 * Require:
 * \li	only one of 'name' or 'alt' to be valid.
 */

void
dns_resolver_setudpsize(dns_resolver_t *resolver, uint16_t udpsize);
/*%<
 * Set the EDNS UDP buffer size advertised by the server.
 */

uint16_t
dns_resolver_getudpsize(dns_resolver_t *resolver);
/*%<
 * Get the current EDNS UDP buffer size.
 */

void
dns_resolver_reset_algorithms(dns_resolver_t *resolver);
/*%<
 * Clear the disabled DNSSEC algorithms.
 */

void
dns_resolver_reset_ds_digests(dns_resolver_t *resolver);
/*%<
 * Clear the disabled DS digest types.
 */

isc_result_t
dns_resolver_disable_algorithm(dns_resolver_t *resolver, const dns_name_t *name,
			       unsigned int alg);
/*%<
 * Mark the given DNSSEC algorithm as disabled and below 'name'.
 * Valid algorithms are less than 256.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_RANGE
 *\li	#ISC_R_NOMEMORY
 */

isc_result_t
dns_resolver_disable_ds_digest(dns_resolver_t *resolver, const dns_name_t *name,
			       unsigned int digest_type);
/*%<
 * Mark the given DS digest type as disabled and below 'name'.
 * Valid types are less than 256.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_RANGE
 *\li	#ISC_R_NOMEMORY
 */

bool
dns_resolver_algorithm_supported(dns_resolver_t	  *resolver,
				 const dns_name_t *name, unsigned int alg);
/*%<
 * Check if the given algorithm is supported by this resolver.
 * This checks whether the algorithm has been disabled via
 * dns_resolver_disable_algorithm(), then checks the underlying
 * crypto libraries if it was not specifically disabled.
 */

bool
dns_resolver_ds_digest_supported(dns_resolver_t	  *resolver,
				 const dns_name_t *name,
				 unsigned int	   digest_type);
/*%<
 * Check if the given digest type is supported by this resolver.
 * This checks whether the digest type has been disabled via
 * dns_resolver_disable_ds_digest(), then checks the underlying
 * crypto libraries if it was not specifically disabled.
 */

void
dns_resolver_resetmustbesecure(dns_resolver_t *resolver);

isc_result_t
dns_resolver_setmustbesecure(dns_resolver_t *resolver, const dns_name_t *name,
			     bool value);

bool
dns_resolver_getmustbesecure(dns_resolver_t *resolver, const dns_name_t *name);

void
dns_resolver_settimeout(dns_resolver_t *resolver, unsigned int timeout);
/*%<
 * Set the length of time the resolver will work on a query, in milliseconds.
 *
 * 'timeout' was originally defined in seconds, and later redefined to be in
 * milliseconds.  Values less than or equal to 300 are treated as seconds.
 *
 * If timeout is 0, the default timeout will be applied.
 *
 * Requires:
 * \li  resolver to be valid.
 */

unsigned int
dns_resolver_gettimeout(dns_resolver_t *resolver);
/*%<
 * Get the current length of time the resolver will work on a query,
 * in milliseconds.
 *
 * Requires:
 * \li  resolver to be valid.
 */

void
dns_resolver_setclientsperquery(dns_resolver_t *resolver, uint32_t min,
				uint32_t max);
void
dns_resolver_setfetchesperzone(dns_resolver_t *resolver, uint32_t clients);

void
dns_resolver_getclientsperquery(dns_resolver_t *resolver, uint32_t *cur,
				uint32_t *min, uint32_t *max);

bool
dns_resolver_getzeronosoattl(dns_resolver_t *resolver);

void
dns_resolver_setzeronosoattl(dns_resolver_t *resolver, bool state);

unsigned int
dns_resolver_getretryinterval(dns_resolver_t *resolver);

void
dns_resolver_setretryinterval(dns_resolver_t *resolver, unsigned int interval);
/*%<
 * Sets the amount of time, in milliseconds, that is waited for a reply
 * to a server before another server is tried.  Interacts with the
 * value of dns_resolver_getnonbackofftries() by trying that number of times
 * at this interval, before doing exponential backoff and doubling the interval
 * on each subsequent try, to a maximum of 10 seconds.  Defaults to 800 ms;
 * silently capped at 2000 ms.
 *
 * Requires:
 * \li	resolver to be valid.
 * \li  interval > 0.
 */

unsigned int
dns_resolver_getnonbackofftries(dns_resolver_t *resolver);

void
dns_resolver_setnonbackofftries(dns_resolver_t *resolver, unsigned int tries);
/*%<
 * Sets the number of failures of getting a reply from remote servers for
 * a query before backing off by doubling the retry interval for each
 * subsequent request sent.  Defaults to 3.
 *
 * Requires:
 * \li	resolver to be valid.
 * \li  tries > 0.
 */

unsigned int
dns_resolver_getoptions(dns_resolver_t *resolver);
/*%<
 * Get the resolver options.
 *
 * Requires:
 * \li	resolver to be valid.
 */

void
dns_resolver_addbadcache(dns_resolver_t *resolver, const dns_name_t *name,
			 dns_rdatatype_t type, isc_time_t *expire);
/*%<
 * Add a entry to the bad cache for <name,type> that will expire at 'expire'.
 *
 * Requires:
 * \li	resolver to be valid.
 * \li	name to be valid.
 */

bool
dns_resolver_getbadcache(dns_resolver_t *resolver, const dns_name_t *name,
			 dns_rdatatype_t type, isc_time_t *now);
/*%<
 * Check to see if there is a unexpired entry in the bad cache for
 * <name,type>.
 *
 * Requires:
 * \li	resolver to be valid.
 * \li	name to be valid.
 */

void
dns_resolver_flushbadcache(dns_resolver_t *resolver, const dns_name_t *name);
/*%<
 * Flush the bad cache of all entries at 'name' if 'name' is non NULL.
 * Flush the entire bad cache if 'name' is NULL.
 *
 * Requires:
 * \li	resolver to be valid.
 */

void
dns_resolver_flushbadnames(dns_resolver_t *resolver, const dns_name_t *name);
/*%<
 * Flush the bad cache of all entries at or below 'name'.
 *
 * Requires:
 * \li	resolver to be valid.
 * \li  name != NULL
 */

void
dns_resolver_printbadcache(dns_resolver_t *resolver, FILE *fp);
/*%
 * Print out the contents of the bad cache to 'fp'.
 *
 * Requires:
 * \li	resolver to be valid.
 */

void
dns_resolver_setmaxdepth(dns_resolver_t *resolver, unsigned int maxdepth);
unsigned int
dns_resolver_getmaxdepth(dns_resolver_t *resolver);
/*%
 * Get and set how many NS indirections will be followed when looking for
 * nameserver addresses.
 *
 * Requires:
 * \li	resolver to be valid.
 */

void
dns_resolver_setmaxqueries(dns_resolver_t *resolver, unsigned int queries);
unsigned int
dns_resolver_getmaxqueries(dns_resolver_t *resolver);
/*%
 * Get and set how many iterative queries will be allowed before
 * terminating a recursive query.
 *
 * Requires:
 * \li	resolver to be valid.
 */

void
dns_resolver_setquotaresponse(dns_resolver_t *resolver, dns_quotatype_t which,
			      isc_result_t resp);
isc_result_t
dns_resolver_getquotaresponse(dns_resolver_t *resolver, dns_quotatype_t which);
/*%
 * Get and set the result code that will be used when quotas
 * are exceeded. If 'which' is set to quotatype "zone", then the
 * result specified in 'resp' will be used when the fetches-per-zone
 * quota is exceeded by a fetch.  If 'which' is set to quotatype "server",
 * then the result specified in 'resp' will be used when the
 * fetches-per-server quota has been exceeded for all the
 * authoritative servers for a zone.  Valid choices are
 * DNS_R_DROP or DNS_R_SERVFAIL.
 *
 * Requires:
 * \li	'resolver' to be valid.
 * \li	'which' to be dns_quotatype_zone or dns_quotatype_server
 * \li	'resp' to be DNS_R_DROP or DNS_R_SERVFAIL.
 */

void
dns_resolver_dumpfetches(dns_resolver_t *resolver, isc_statsformat_t format,
			 FILE *fp);

#ifdef ENABLE_AFL
/*%
 * Enable fuzzing of resolver, changes behaviour and eliminates retries
 */
void
dns_resolver_setfuzzing(void);
#endif /* ifdef ENABLE_AFL */

ISC_LANG_ENDDECLS

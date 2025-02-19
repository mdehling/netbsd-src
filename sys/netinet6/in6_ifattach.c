/*	$NetBSD: in6_ifattach.c,v 1.120.12.1 2024/04/18 16:22:28 martin Exp $	*/
/*	$KAME: in6_ifattach.c,v 1.124 2001/07/18 08:32:51 jinmei Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: in6_ifattach.c,v 1.120.12.1 2024/04/18 16:22:28 martin Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/md5.h>
#include <sys/socketvar.h>
#include <sys/cprng.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#include <netinet/ip6.h>
#include <netinet6/in6_ifattach.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6_mroute.h>
#include <netinet6/scope6_var.h>

int ip6_auto_linklocal = 1;	/* enable by default */

#if 0
static int get_hostid_ifid(struct ifnet *, struct in6_addr *);
#endif
static int get_ifid(struct ifnet *, struct ifnet *, struct in6_addr *);
static int in6_ifattach_linklocal(struct ifnet *, struct ifnet *);
static int in6_ifattach_loopback(struct ifnet *);

#define EUI64_GBIT	0x01
#define EUI64_UBIT	0x02
#define EUI64_TO_IFID(in6)	do {(in6)->s6_addr[8] ^= EUI64_UBIT; } while (/*CONSTCOND*/ 0)
#define EUI64_GROUP(in6)	((in6)->s6_addr[8] & EUI64_GBIT)
#define EUI64_INDIVIDUAL(in6)	(!EUI64_GROUP(in6))
#define EUI64_LOCAL(in6)	((in6)->s6_addr[8] & EUI64_UBIT)
#define EUI64_UNIVERSAL(in6)	(!EUI64_LOCAL(in6))

#define IFID_LOCAL(in6)		(!EUI64_LOCAL(in6))
#define IFID_UNIVERSAL(in6)	(!EUI64_UNIVERSAL(in6))

#if 0
/*
 * Generate a last-resort interface identifier from hostid.
 * works only for certain architectures (like sparc).
 * also, using hostid itself may constitute a privacy threat, much worse
 * than MAC addresses (hostids are used for software licensing).
 * maybe we should use MD5(hostid) instead.
 *
 * in6 - upper 64bits are preserved
 */
static int
get_hostid_ifid(struct ifnet *ifp, struct in6_addr *in6)
{
	int off, len;
	static const uint8_t allzero[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	static const uint8_t allone[8] =
	    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (!hostid)
		return -1;

	/* get up to 8 bytes from the hostid field - should we get */
	len = (sizeof(hostid) > 8) ? 8 : sizeof(hostid);
	off = sizeof(*in6) - len;
	memcpy(&in6->s6_addr[off], &hostid, len);

	/* make sure we do not return anything bogus */
	if (memcmp(&in6->s6_addr[8], allzero, sizeof(allzero)))
		return -1;
	if (memcmp(&in6->s6_addr[8], allone, sizeof(allone)))
		return -1;

	/* make sure to set "u" bit to local, and "g" bit to individual. */
	in6->s6_addr[8] &= ~EUI64_GBIT;	/* g bit to "individual" */
	in6->s6_addr[8] |= EUI64_UBIT;	/* u bit to "local" */

	/* convert EUI64 into IPv6 interface identifier */
	EUI64_TO_IFID(in6);

	return 0;
}
#endif

/*
 * Generate a last-resort interface identifier, when the machine has no
 * IEEE802/EUI64 address sources.
 * The goal here is to get an interface identifier that is
 * (1) random enough and (2) does not change across reboot.
 * We currently use MD5(hostname) for it.
 */
static int
get_rand_ifid(struct in6_addr *in6)	/* upper 64bits are preserved */
{
	MD5_CTX ctxt;
	u_int8_t digest[16];

#if 0
	/* we need at least several letters as seed for ifid */
	if (hostnamelen < 3)
		return -1;
#endif

	/* generate 8 bytes of pseudo-random value. */
	memset(&ctxt, 0, sizeof(ctxt));
	MD5Init(&ctxt);
	MD5Update(&ctxt, (u_char *)hostname, hostnamelen);
	MD5Final(digest, &ctxt);

	/* assumes sizeof(digest) > sizeof(ifid) */
	memcpy(&in6->s6_addr[8], digest, 8);

	/* make sure to set "u" bit to local, and "g" bit to individual. */
	in6->s6_addr[8] &= ~EUI64_GBIT;	/* g bit to "individual" */
	in6->s6_addr[8] |= EUI64_UBIT;	/* u bit to "local" */

	/* convert EUI64 into IPv6 interface identifier */
	EUI64_TO_IFID(in6);

	return 0;
}

/*
 * Get interface identifier for the specified interface.
 *
 * in6 - upper 64bits are preserved
 */
int
in6_get_hw_ifid(struct ifnet *ifp, struct in6_addr *in6)
{
	struct ifaddr *ifa;
	const struct sockaddr_dl *sdl = NULL;
	const char *addr = NULL; /* XXX gcc 4.8 -Werror=maybe-uninitialized */
	size_t addrlen = 0; /* XXX gcc 4.8 -Werror=maybe-uninitialized */
	static u_int8_t allzero[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	static u_int8_t allone[8] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	int s;

	s = pserialize_read_enter();
	IFADDR_READER_FOREACH(ifa, ifp) {
		const struct sockaddr_dl *tsdl;
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		tsdl = satocsdl(ifa->ifa_addr);
		if (tsdl == NULL || tsdl->sdl_alen == 0)
			continue;
		if (sdl == NULL || ifa == ifp->if_dl || ifa == ifp->if_hwdl) {
			sdl = tsdl;
			addr = CLLADDR(sdl);
			addrlen = sdl->sdl_alen;
		}
		if (ifa == ifp->if_hwdl)
			break;
	}
	pserialize_read_exit(s);

	if (sdl == NULL)
		return -1;

	switch (ifp->if_type) {
	case IFT_IEEE1394:
	case IFT_IEEE80211:
		/* IEEE1394 uses 16byte length address starting with EUI64 */
		if (addrlen > 8)
			addrlen = 8;
		break;
	default:
		break;
	}

	/* get EUI64 */
	switch (ifp->if_type) {
	/* IEEE802/EUI64 cases - what others? */
	case IFT_ETHER:
	case IFT_ATM:
	case IFT_IEEE1394:
	case IFT_IEEE80211:
		/* look at IEEE802/EUI64 only */
		if (addrlen != 8 && addrlen != 6)
			return -1;

		/*
		 * check for invalid MAC address - on bsdi, we see it a lot
		 * since wildboar configures all-zero MAC on pccard before
		 * card insertion.
		 */
		if (memcmp(addr, allzero, addrlen) == 0)
			return -1;
		if (memcmp(addr, allone, addrlen) == 0)
			return -1;

		/* make EUI64 address */
		if (addrlen == 8)
			memcpy(&in6->s6_addr[8], addr, 8);
		else if (addrlen == 6) {
			in6->s6_addr[8] = addr[0];
			in6->s6_addr[9] = addr[1];
			in6->s6_addr[10] = addr[2];
			in6->s6_addr[11] = 0xff;
			in6->s6_addr[12] = 0xfe;
			in6->s6_addr[13] = addr[3];
			in6->s6_addr[14] = addr[4];
			in6->s6_addr[15] = addr[5];
		}
		break;

	case IFT_ARCNET:
		if (addrlen != 1)
			return -1;
		if (!addr[0])
			return -1;

		memset(&in6->s6_addr[8], 0, 8);
		in6->s6_addr[15] = addr[0];

		/*
		 * due to insufficient bitwidth, we mark it local.
		 */
		in6->s6_addr[8] &= ~EUI64_GBIT;	/* g bit to "individual" */
		in6->s6_addr[8] |= EUI64_UBIT;	/* u bit to "local" */
		break;

	case IFT_GIF:
	case IFT_IPSEC:
#ifdef IFT_STF
	case IFT_STF:
#endif
		/*
		 * RFC2893 says: "SHOULD use IPv4 address as ifid source".
		 * however, IPv4 address is not very suitable as unique
		 * identifier source (can be renumbered).
		 * we don't do this.
		 */
		return -1;

	default:
		return -1;
	}

	/* sanity check: g bit must not indicate "group" */
	if (EUI64_GROUP(in6))
		return -1;

	/* convert EUI64 into IPv6 interface identifier */
	EUI64_TO_IFID(in6);

	/*
	 * sanity check: ifid must not be all zero, avoid conflict with
	 * subnet router anycast
	 */
	if ((in6->s6_addr[8] & ~(EUI64_GBIT | EUI64_UBIT)) == 0x00 &&
	    memcmp(&in6->s6_addr[9], allzero, 7) == 0) {
		return -1;
	}

	return 0;
}

/*
 * Get interface identifier for the specified interface.  If it is not
 * available on ifp0, borrow interface identifier from other information
 * sources.
 *
 * altifp - secondary EUI64 source
 */
static int
get_ifid(struct ifnet *ifp0, struct ifnet *altifp, 
	struct in6_addr *in6)
{
	struct ifnet *ifp;
	int s;

	/* first, try to get it from the interface itself */
	if (in6_get_hw_ifid(ifp0, in6) == 0) {
		nd6log(LOG_DEBUG, "%s: got interface identifier from itself\n",
		    if_name(ifp0));
		goto success;
	}

	/* try secondary EUI64 source. this basically is for ATM PVC */
	if (altifp && in6_get_hw_ifid(altifp, in6) == 0) {
		nd6log(LOG_DEBUG, "%s: got interface identifier from %s\n",
		    if_name(ifp0), if_name(altifp));
		goto success;
	}

	/* next, try to get it from some other hardware interface */
	s = pserialize_read_enter();
	IFNET_READER_FOREACH(ifp) {
		if (ifp == ifp0)
			continue;
		if (in6_get_hw_ifid(ifp, in6) != 0)
			continue;

		/*
		 * to borrow ifid from other interface, ifid needs to be
		 * globally unique
		 */
		if (IFID_UNIVERSAL(in6)) {
			nd6log(LOG_DEBUG,
			    "%s: borrow interface identifier from %s\n",
			    if_name(ifp0), if_name(ifp));
			pserialize_read_exit(s);
			goto success;
		}
	}
	pserialize_read_exit(s);

#if 0
	/* get from hostid - only for certain architectures */
	if (get_hostid_ifid(ifp, in6) == 0) {
		nd6log(LOG_DEBUG,
		    "%s: interface identifier generated by hostid\n",
		    if_name(ifp0));
		goto success;
	}
#endif

	/* last resort: get from random number source */
	if (get_rand_ifid(in6) == 0) {
		nd6log(LOG_DEBUG,
		    "%s: interface identifier generated by random number\n",
		    if_name(ifp0));
		goto success;
	}

	printf("%s: failed to get interface identifier\n", if_name(ifp0));
	return -1;

success:
	nd6log(LOG_INFO, "%s: ifid: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
	    if_name(ifp0), in6->s6_addr[8], in6->s6_addr[9], in6->s6_addr[10],
	    in6->s6_addr[11], in6->s6_addr[12], in6->s6_addr[13],
	    in6->s6_addr[14], in6->s6_addr[15]);
	return 0;
}

/*
 * altifp - secondary EUI64 source
 */

static int
in6_ifattach_linklocal(struct ifnet *ifp, struct ifnet *altifp)
{
	struct in6_aliasreq ifra;
	int error;

	/*
	 * configure link-local address.
	 */
	memset(&ifra, 0, sizeof(ifra));

	/*
	 * in6_update_ifa() does not use ifra_name, but we accurately set it
	 * for safety.
	 */
	strncpy(ifra.ifra_name, if_name(ifp), sizeof(ifra.ifra_name));

	ifra.ifra_addr.sin6_family = AF_INET6;
	ifra.ifra_addr.sin6_len = sizeof(struct sockaddr_in6);
	ifra.ifra_addr.sin6_addr.s6_addr32[0] = htonl(0xfe800000);
	ifra.ifra_addr.sin6_addr.s6_addr32[1] = 0;
	if ((ifp->if_flags & IFF_LOOPBACK) != 0) {
		ifra.ifra_addr.sin6_addr.s6_addr32[2] = 0;
		ifra.ifra_addr.sin6_addr.s6_addr32[3] = htonl(1);
	} else {
		if (get_ifid(ifp, altifp, &ifra.ifra_addr.sin6_addr) != 0) {
			nd6log(LOG_ERR,
			    "%s: no ifid available\n", if_name(ifp));
			return -1;
		}
	}
	if (in6_setscope(&ifra.ifra_addr.sin6_addr, ifp, NULL))
		return -1;

	sockaddr_in6_init(&ifra.ifra_prefixmask, &in6mask64, 0, 0, 0);
	/* link-local addresses should NEVER expire. */
	ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
	ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

	/*
	 * Now call in6_update_ifa() to do a bunch of procedures to configure
	 * a link-local address. We can set the 3rd argument to NULL, because
	 * we know there's no other link-local address on the interface
	 * and therefore we are adding one (instead of updating one).
	 */
	if ((error = in6_update_ifa(ifp, &ifra, IN6_IFAUPDATE_DADDELAY)) != 0) {
		/*
		 * XXX: When the interface does not support IPv6, this call
		 * would fail in the SIOCINITIFADDR ioctl.  I believe the
		 * notification is rather confusing in this case, so just
		 * suppress it.  (jinmei@kame.net 20010130)
		 */
		if (error != EAFNOSUPPORT)
			nd6log(LOG_NOTICE,
			    "failed to configure a link-local address on %s "
			    "(errno=%d)\n",
			    if_name(ifp), error);
		return -1;
	}

	return 0;
}

/*
 * ifp - mut be IFT_LOOP
 */

static int
in6_ifattach_loopback(struct ifnet *ifp)
{
	struct in6_aliasreq ifra;
	int error;

	memset(&ifra, 0, sizeof(ifra));

	/*
	 * in6_update_ifa() does not use ifra_name, but we accurately set it
	 * for safety.
	 */
	strncpy(ifra.ifra_name, if_name(ifp), sizeof(ifra.ifra_name));

	sockaddr_in6_init(&ifra.ifra_prefixmask, &in6mask128, 0, 0, 0);

	/*
	 * Always initialize ia_dstaddr (= broadcast address) to loopback
	 * address.  Follows IPv4 practice - see in_ifinit().
	 */
	sockaddr_in6_init(&ifra.ifra_dstaddr, &in6addr_loopback, 0, 0, 0);

	sockaddr_in6_init(&ifra.ifra_addr, &in6addr_loopback, 0, 0, 0);

	/* the loopback  address should NEVER expire. */
	ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
	ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

	/* we don't need to perform DAD on loopback interfaces. */
	ifra.ifra_flags |= IN6_IFF_NODAD;

	/*
	 * We are sure that this is a newly assigned address, so we can set
	 * NULL to the 3rd arg.
	 */
	if ((error = in6_update_ifa(ifp, &ifra, 0)) != 0) {
		nd6log(LOG_ERR, "failed to configure "
		    "the loopback address on %s (errno=%d)\n",
		    if_name(ifp), error);
		return -1;
	}

	return 0;
}

/*
 * compute NI group address, based on the current hostname setting.
 * see draft-ietf-ipngwg-icmp-name-lookup-* (04 and later).
 *
 * when ifp == NULL, the caller is responsible for filling scopeid.
 */
int
in6_nigroup(struct ifnet *ifp, const char *name, int namelen, 
	struct sockaddr_in6 *sa6)
{
	const char *p;
	u_int8_t *q;
	MD5_CTX ctxt;
	u_int8_t digest[16];
	u_int8_t l;
	u_int8_t n[64];	/* a single label must not exceed 63 chars */

	if (!namelen || !name)
		return -1;

	p = name;
	while (p && *p && *p != '.' && p - name < namelen)
		p++;
	if (p - name > sizeof(n) - 1)
		return -1;	/* label too long */
	l = p - name;
	strncpy((char *)n, name, l);
	n[(int)l] = '\0';
	for (q = n; *q; q++) {
		if ('A' <= *q && *q <= 'Z')
			*q = *q - 'A' + 'a';
	}

	/* generate 8 bytes of pseudo-random value. */
	memset(&ctxt, 0, sizeof(ctxt));
	MD5Init(&ctxt);
	MD5Update(&ctxt, &l, sizeof(l));
	MD5Update(&ctxt, n, l);
	MD5Final(digest, &ctxt);

	memset(sa6, 0, sizeof(*sa6));
	sa6->sin6_family = AF_INET6;
	sa6->sin6_len = sizeof(*sa6);
	sa6->sin6_addr.s6_addr16[0] = htons(0xff02);
	sa6->sin6_addr.s6_addr8[11] = 2;
	memcpy(&sa6->sin6_addr.s6_addr32[3], digest,
	    sizeof(sa6->sin6_addr.s6_addr32[3]));
	if (in6_setscope(&sa6->sin6_addr, ifp, NULL))
		return -1; /* XXX: should not fail */

	return 0;
}

/*
 * XXX multiple loopback interface needs more care.  for instance,
 * nodelocal address needs to be configured onto only one of them.
 * XXX multiple link-local address case
 *
 * altifp - secondary EUI64 source
 */
void
in6_ifattach(struct ifnet *ifp, struct ifnet *altifp)
{
	struct in6_ifaddr *ia;
	struct in6_addr in6;

	KASSERT(IFNET_LOCKED(ifp));

	/* some of the interfaces are inherently not IPv6 capable */
	switch (ifp->if_type) {
	case IFT_BRIDGE:
	case IFT_L2TP:
	case IFT_IEEE8023ADLAG:
#ifdef IFT_PFLOG
	case IFT_PFLOG:
#endif
#ifdef IFT_PFSYNC
	case IFT_PFSYNC:
#endif
		ND_IFINFO(ifp)->flags &= ~ND6_IFF_AUTO_LINKLOCAL;
		ND_IFINFO(ifp)->flags |= ND6_IFF_IFDISABLED;
		return;
	}

	/*
	 * if link mtu is too small, don't try to configure IPv6.
	 * remember there could be some link-layer that has special
	 * fragmentation logic.
	 */
	if (ifp->if_mtu < IPV6_MMTU) {
		nd6log(LOG_INFO, "%s has too small MTU, IPv6 not enabled\n",
		    if_name(ifp));
		return;
	}

	/*
	 * quirks based on interface type
	 */
	switch (ifp->if_type) {
#ifdef IFT_STF
	case IFT_STF:
		/*
		 * 6to4 interface is a very special kind of beast.
		 * no multicast, no linklocal.  RFC2529 specifies how to make
		 * linklocals for 6to4 interface, but there's no use and
		 * it is rather harmful to have one.
		 */
		ND_IFINFO(ifp)->flags &= ~ND6_IFF_AUTO_LINKLOCAL;
		return;
#endif
	case IFT_CARP:
		return;
	default:
		break;
	}

	/*
	 * usually, we require multicast capability to the interface
	 */
	if ((ifp->if_flags & IFF_MULTICAST) == 0) {
		nd6log(LOG_INFO,
		    "%s is not multicast capable, IPv6 not enabled\n",
		    if_name(ifp));
		return;
	}

	/*
	 * assign loopback address for loopback interface.
	 * XXX multiple loopback interface case.
	 */
	if ((ifp->if_flags & IFF_LOOPBACK) != 0) {
		in6 = in6addr_loopback;
		/* These are safe and atomic thanks to IFNET_LOCK */
		if (in6ifa_ifpwithaddr(ifp, &in6) == NULL) {
			if (in6_ifattach_loopback(ifp) != 0)
				return;
		}
	}

	/*
	 * assign a link-local address, if there's none.
	 */
	if (!(ND_IFINFO(ifp)->flags & ND6_IFF_IFDISABLED) &&
	    ND_IFINFO(ifp)->flags & ND6_IFF_AUTO_LINKLOCAL) {
		int bound = curlwp_bind();
		struct psref psref;
		ia = in6ifa_ifpforlinklocal_psref(ifp, 0, &psref);
		if (ia == NULL && in6_ifattach_linklocal(ifp, altifp) != 0) {
			printf("%s: cannot assign link-local address\n",
			    ifp->if_xname);
		}
		ia6_release(ia, &psref);
		curlwp_bindx(bound);
	}
}

/*
 * NOTE: in6_ifdetach() does not support loopback if at this moment.
 * We don't need this function in bsdi, because interfaces are never removed
 * from the ifnet list in bsdi.
 */
void
in6_ifdetach(struct ifnet *ifp)
{

	/* nuke any of IPv6 addresses we have */
	if_purgeaddrs(ifp, AF_INET6, in6_purgeaddr);

	in6_purge_multi(ifp);

	/* remove ip6_mrouter stuff */
	ip6_mrouter_detach(ifp);

	/* remove neighbor management table */
	nd6_purge(ifp, NULL);
}

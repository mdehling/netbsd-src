/*	$NetBSD: autoconf.c,v 1.25.20.1 2023/10/18 16:53:03 martin Exp $	*/
/*	NetBSD: autoconf.c,v 1.75 2003/12/30 12:33:22 pk Exp 	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)autoconf.c	7.1 (Berkeley) 5/9/91
 */

/*
 * Setup the system to run on the current machine.
 *
 * Configure() is called at boot time and initializes the vba
 * device tables and the memory controller monitoring.  Available
 * devices are determined (from possibilities mentioned in ioconf.c),
 * and the drivers are initialized.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: autoconf.c,v 1.25.20.1 2023/10/18 16:53:03 martin Exp $");

#include "opt_xen.h"
#include "opt_multiprocessor.h"
#include "opt_nfs_boot.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/dkio.h>
#include <sys/proc.h>
#include <sys/kauth.h>

#ifdef NFS_BOOT_BOOTSTATIC
#include <net/if.h>
#include <net/if_ether.h>
#include <netinet/in.h>
#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsmount.h>
#include <nfs/nfsdiskless.h>
#include <xen/if_xennetvar.h>
#endif

#include <machine/pte.h>
#include <machine/cpu.h>
#include <machine/gdt.h>
#include <machine/pcb.h>
#include <machine/bootinfo.h>

#include <x86/autoconf.h>

struct disklist *x86_alldisks;
int x86_ndisks;
int x86_found_console;

#include "bios32.h"
#if NBIOS32 > 0
#include <machine/bios32.h>
/* XXX */
extern void platform_init(void);
#endif

#include "opt_pcibios.h"
#ifdef PCIBIOS
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <i386/pci/pcibios.h>
#endif

#ifdef DEBUG_GEOM
#define DPRINTF(a) printf a
#else
#define DPRINTF(a)
#endif

/*
 * Determine i/o configuration for a machine.
 */
void
cpu_configure(void)
{
	struct pcb *pcb;

	xen_startrtclock();

#if defined(DOM0OPS)
	if (xendomain_is_dom0()) {
#if NBIOS32 > 0
		bios32_init();
		platform_init();
		/* identify hypervisor type from SMBIOS */
		identify_hypervisor();
#endif /* NBIOS32 > 0 */
	}
#endif /* DOM0OPS */
#ifdef PCIBIOS
	pcibios_init();
#endif

	if (config_rootfound("mainbus", NULL) == NULL)
		panic("configure: mainbus not configured");

#ifdef INTRDEBUG
	intr_printconfig();
#endif

#if NIOAPIC > 0
	ioapic_enable();
#endif
	/* resync cr0 after FPU configuration */
	pcb = lwp_getpcb(&lwp0);
	pcb->pcb_cr0 = rcr0();
#ifdef MULTIPROCESSOR
	/* propagate this to the idle pcb's. */
	cpu_init_idle_lwps();
#endif

	spl0();
}

void
cpu_rootconf(void)
{
	cpu_bootconf();

	printf("boot device: %s\n",
	    booted_device ? device_xname(booted_device) :
	    bootspec ? bootspec : "<unknown>");
	rootconf();
}


/*
 * Attempt to find the device from which we were booted.
 */
void
cpu_bootconf(void)
{
	xen_bootconf();
}
#include "pci.h"

#include <dev/isa/isavar.h>
#if NPCI > 0
#include <dev/pci/pcivar.h>
#endif


#if defined(NFS_BOOT_BOOTSTATIC) && defined(DOM0OPS)
static int
dom0_bootstatic_callback(struct nfs_diskless *nd)
{
#if 0
	struct ifnet *ifp = nd->nd_ifp;
#endif
	int flags = 0;
	union xen_cmdline_parseinfo xcp;
	struct sockaddr_in *sin;

	memset(&xcp, 0, sizeof(xcp.xcp_netinfo));
	xcp.xcp_netinfo.xi_ifno = 0; /* XXX first interface hardcoded */
	xcp.xcp_netinfo.xi_root = nd->nd_root.ndm_host;
	xen_parse_cmdline(XEN_PARSE_NETINFO, &xcp);

	if (xcp.xcp_netinfo.xi_root[0] != '\0') {
		flags |= NFS_BOOT_HAS_SERVER;
		if (strchr(xcp.xcp_netinfo.xi_root, ':') != NULL)
			flags |= NFS_BOOT_HAS_ROOTPATH;
	}

	nd->nd_myip.s_addr = ntohl(xcp.xcp_netinfo.xi_ip[0]);
	nd->nd_gwip.s_addr = ntohl(xcp.xcp_netinfo.xi_ip[2]);
	nd->nd_mask.s_addr = ntohl(xcp.xcp_netinfo.xi_ip[3]);

	sin = (struct sockaddr_in *) &nd->nd_root.ndm_saddr;
	memset((void *)sin, 0, sizeof(*sin));
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = ntohl(xcp.xcp_netinfo.xi_ip[1]);

	if (nd->nd_myip.s_addr)
		flags |= NFS_BOOT_HAS_MYIP;
	if (nd->nd_gwip.s_addr)
		flags |= NFS_BOOT_HAS_GWIP;
	if (nd->nd_mask.s_addr)
		flags |= NFS_BOOT_HAS_MASK;
	if (sin->sin_addr.s_addr)
		flags |= NFS_BOOT_HAS_SERVADDR;

	return flags;
}
#endif

void
device_register(device_t dev, void *aux)
{
	device_t isaboot, pciboot;
	device_t found = NULL;

	isaboot = device_isa_register(dev, aux);
	pciboot = device_pci_register(dev, aux);

	/*
	 * Handle network interfaces here, the attachment information is
	 * not available driver independently later.
	 * For disks, there is nothing useful available at attach time.
	 */
#if NXENNET_HYPERVISOR > 0 || NXENNET_XENBUS > 0 || defined(DOM0OPS)
	if (device_class(dev) == DV_IFNET) {
		union xen_cmdline_parseinfo xcp;

#ifdef NFS_BOOT_BOOTSTATIC
#ifdef DOM0OPS
		if (xendomain_is_privileged()) {
			nfs_bootstatic_callback = dom0_bootstatic_callback;
		} else
#endif
#if NXENNET_HYPERVISOR > 0 || NXENNET_XENBUS > 0
		nfs_bootstatic_callback = xennet_bootstatic_callback;
#endif
#endif
		xen_parse_cmdline(XEN_PARSE_BOOTDEV, &xcp);
		if (strncmp(xcp.xcp_bootdev, device_xname(dev),
		    sizeof(xcp.xcp_bootdev)) == 0)
		{
			found = dev;
		}
	}
#endif
	if (found == NULL) {
		if (isaboot != NULL) 
			found = isaboot;
		else if (pciboot != NULL)
			found = pciboot;
	}

	if (found) {
		if (booted_device) {
			/* XXX should be a "panic()" */
			printf("warning: double match for boot device (%s, %s)\n",
			    device_xname(booted_device), device_xname(dev));
			return;
		}
		booted_device = found;
		booted_method = "device/register";
	}
}

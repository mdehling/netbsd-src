/*	$NetBSD: xen.h,v 1.47.20.1 2023/03/30 11:45:34 martin Exp $	*/

/*
 *
 * Copyright (c) 2003, 2004 Keir Fraser (on behalf of the Xen team)
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 */


#ifndef _XEN_H
#define _XEN_H

#ifdef _KERNEL_OPT
#include "opt_xen.h"
#endif


#ifndef _LOCORE

#include <machine/cpufunc.h>

struct xen_netinfo {
	uint32_t xi_ifno;
	char *xi_root;
	uint32_t xi_ip[5];
};

union xen_cmdline_parseinfo {
	char			xcp_bootdev[144];
	struct xen_netinfo	xcp_netinfo;
	char			xcp_console[16];
	char			xcp_pcidevs[64];
};

#define	XEN_PARSE_BOOTDEV	0
#define	XEN_PARSE_NETINFO	1
#define	XEN_PARSE_CONSOLE	2
#define	XEN_PARSE_BOOTFLAGS	3
#define	XEN_PARSE_PCIBACK	4

void	xen_parse_cmdline(int, union xen_cmdline_parseinfo *);

void	xenconscn_attach(void);

int 	xen_pvh_consinit(void);

void	xenprivcmd_init(void);

void	xbdback_init(void);
void	xennetback_init(void);
void	xen_shm_init(void);

void	xenevt_event(int);
void	xenevt_setipending(int, int);
void	xenevt_notify(void);

/* xen_machdep.c */
void	sysctl_xen_suspend_setup(void);

#include <sys/stdarg.h>
void printk(const char *, ...);

#endif

#endif /* _XEN_H */

/******************************************************************************
 * os.h
 * 
 * random collection of macros and definition
 */

#ifndef _OS_H_
#define _OS_H_

/*
 * These are the segment descriptors provided for us by the hypervisor.
 * For now, these are hardwired -- guest OSes cannot update the GDT
 * or LDT.
 * 
 * It shouldn't be hard to support descriptor-table frobbing -- let me 
 * know if the BSD or XP ports require flexibility here.
 */


/*
 * these are also defined in xen-public/xen.h but can't be pulled in as
 * they are used in start of day assembly. Need to clean up the .h files
 * a bit more...
 */

#ifndef FLAT_RING1_CS
#define FLAT_RING1_CS 0xe019    /* GDT index 259 */
#define FLAT_RING1_DS 0xe021    /* GDT index 260 */
#define FLAT_RING1_SS 0xe021    /* GDT index 260 */
#define FLAT_RING3_CS 0xe02b    /* GDT index 261 */
#define FLAT_RING3_DS 0xe033    /* GDT index 262 */
#define FLAT_RING3_SS 0xe033    /* GDT index 262 */
#endif

#define __KERNEL_CS        FLAT_RING1_CS
#define __KERNEL_DS        FLAT_RING1_DS

/* Everything below this point is not included by assembler (.S) files. */
#ifndef _LOCORE

/* Version Specific Glue */
#if __XEN_INTERFACE_VERSION__ >= 0x00030203
#define console_mfn    console.domU.mfn
#define console_evtchn console.domU.evtchn
#endif

/* some function prototypes */
void trap_init(void);
void xpq_flush_cache(void);

#define xendomain_is_dom0()		(xen_start_info.flags & SIF_INITDOMAIN)
#define xendomain_is_privileged()	(xen_start_info.flags & SIF_PRIVILEGED)

/*
 * always assume we're on multiprocessor. We don't know how many CPU the
 * underlying hardware has.
 */
#define __LOCK_PREFIX "lock; "

#define XATOMIC_T u_long
#ifdef __x86_64__
#define LONG_SHIFT 6
#define LONG_MASK 63
#else /* __x86_64__ */
#define LONG_SHIFT 5
#define LONG_MASK 31
#endif /* __x86_64__ */

#define xen_ffs __builtin_ffsl

static __inline XATOMIC_T
xen_atomic_xchg(volatile XATOMIC_T *ptr, unsigned long val)
{
	unsigned long result;

	__asm volatile(__LOCK_PREFIX
#ifdef __x86_64__
	    "xchgq %0,%1"
#else
	    "xchgl %0,%1"
#endif
	    :"=r" (result)
	    :"m" (*ptr), "0" (val)
	    :"memory");

	return result;
}

static __inline void
xen_atomic_setbits_l (volatile XATOMIC_T *ptr, unsigned long bits) {  
#ifdef __x86_64__
	__asm volatile("lock ; orq %1,%0" :  "=m" (*ptr) : "ir" (bits));
#else
	__asm volatile("lock ; orl %1,%0" :  "=m" (*ptr) : "ir" (bits));
#endif
}
     
static __inline void
xen_atomic_clearbits_l (volatile XATOMIC_T *ptr, unsigned long bits) {  
#ifdef __x86_64__
	__asm volatile("lock ; andq %1,%0" :  "=m" (*ptr) : "ir" (~bits));
#else
	__asm volatile("lock ; andl %1,%0" :  "=m" (*ptr) : "ir" (~bits));
#endif
}

static __inline XATOMIC_T
xen_atomic_test_and_clear_bit(volatile void *ptr, unsigned long bitno)
{
	long result;

	__asm volatile(__LOCK_PREFIX
#ifdef __x86_64__
	    "btrq %2,%1 ;"
	    "sbbq %0,%0"
#else
	    "btrl %2,%1 ;"
	    "sbbl %0,%0"
#endif
	    :"=r" (result), "=m" (*(volatile XATOMIC_T *)(ptr))
	    :"Ir" (bitno) : "memory");
	return result;
}

static __inline XATOMIC_T
xen_atomic_test_and_set_bit(volatile void *ptr, unsigned long bitno)
{
	long result;

	__asm volatile(__LOCK_PREFIX
#ifdef __x86_64__
	    "btsq %2,%1 ;"
	    "sbbq %0,%0"
#else
	    "btsl %2,%1 ;"
	    "sbbl %0,%0"
#endif
	    :"=r" (result), "=m" (*(volatile XATOMIC_T *)(ptr))
	    :"Ir" (bitno) : "memory");
	return result;
}

static __inline int
xen_constant_test_bit(const volatile void *ptr, unsigned long bitno)
{
	return ((1UL << (bitno & LONG_MASK)) &
	    (((const volatile XATOMIC_T *) ptr)[bitno >> LONG_SHIFT])) != 0;
}

static __inline XATOMIC_T
xen_variable_test_bit(const volatile void *ptr, unsigned long bitno)
{
	long result;
    
	__asm volatile(
#ifdef __x86_64__
		"btq %2,%1 ;"
		"sbbq %0,%0"
#else
		"btl %2,%1 ;"
		"sbbl %0,%0"
#endif
		:"=r" (result)
		:"m" (*(const volatile XATOMIC_T *)(ptr)), "Ir" (bitno));
	return result;
}

#define xen_atomic_test_bit(ptr, bitno) \
	(__builtin_constant_p(bitno) ? \
	 xen_constant_test_bit((ptr),(bitno)) : \
	 xen_variable_test_bit((ptr),(bitno)))

static __inline void
xen_atomic_set_bit(volatile void *ptr, unsigned long bitno)
{
	__asm volatile(__LOCK_PREFIX
#ifdef __x86_64__
	    "btsq %1,%0"
#else
	    "btsl %1,%0"
#endif
	    :"=m" (*(volatile XATOMIC_T *)(ptr))
	    :"Ir" (bitno));
}

static __inline void
xen_atomic_clear_bit(volatile void *ptr, unsigned long bitno)
{
	__asm volatile(__LOCK_PREFIX
#ifdef __x86_64__
	    "btrq %1,%0"
#else
	    "btrl %1,%0"
#endif
	    :"=m" (*(volatile XATOMIC_T *)(ptr))
	    :"Ir" (bitno));
}

#undef XATOMIC_T

void	wbinvd(void);

#include <xen/include/public/features.h>
#include <sys/systm.h>

extern bool xen_feature_tables[];
void xen_init_features(void);
static __inline bool
xen_feature(int f)
{
	KASSERT(f < XENFEAT_NR_SUBMAPS * 32);
	return xen_feature_tables[f];
}

void xen_bootconf(void);

#endif /* !__ASSEMBLY__ */

#endif /* _OS_H_ */

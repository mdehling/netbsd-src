/*	$NetBSD: marvellvar.h,v 1.6.48.1 2023/08/11 10:13:50 martin Exp $	*/
/*
 * Copyright (c) 2009 KIYOHARA Takashi
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_MARVELL_MARVELLVAR_H_
#define _DEV_MARVELL_MARVELLVAR_H_

enum marvell_tags {
	MARVELL_TAG_SDRAM_CS0,
	MARVELL_TAG_SDRAM_CS1,
	MARVELL_TAG_SDRAM_CS2,
	MARVELL_TAG_SDRAM_CS3,

	MARVELL_TAG_AXI_CS0,	/* Advanced eXtensible Interface */
	MARVELL_TAG_AXI_CS1,	/* Advanced eXtensible Interface */

	MARVELL_TAG_DDR3_CS0,
	MARVELL_TAG_DDR3_CS1,
	MARVELL_TAG_DDR3_CS2,
	MARVELL_TAG_DDR3_CS3,

	MARVELL_TAG_MAX,

	MARVELL_TAG_UNDEFINED = -1,
};


#include <sys/bus.h>

struct marvell_attach_args {
	const char *mva_name;
	int mva_model;
	int mva_revision;
	bus_space_tag_t mva_iot;
	bus_space_handle_t mva_ioh;
	int mva_unit;
	bus_size_t mva_addr;
	bus_size_t mva_offset;
	bus_size_t mva_size;
	bus_dma_tag_t mva_dmat;
	int mva_irq;
	enum marvell_tags *mva_tags;
};

#include "locators.h"

#define MVA_UNIT_DEFAULT	GTCF_UNIT_DEFAULT
#define MVA_OFFSET_DEFAULT	GTCF_OFFSET_DEFAULT
#define MVA_IRQ_DEFAULT		GTCF_IRQ_DEFAULT

#ifdef __powerpc__
/*
 * Note: arm and powerpc Marvell platforms have a conflicting idea of
 * marvell_intr_establish.
 *
 * On arm-based Marvell platforms, there is a static inline
 * marvell_intr_establish defined in mvsoc_intr.h.
 *
 * On powerpc-based Marvell platforms, there is an out-of-line
 * marvell_intr_establish defined in a SoC-specific gt_mainbus.c
 * (evbppc/ev64260, ofppc) and declared in dev/marvell/marvellvar.h.
 */
void *marvell_intr_establish(int, int, int (*)(void *), void *);
#endif

int marvell_winparams_by_tag(device_t, int, int *, int *, uint64_t *,
			     uint32_t *);

#endif	/* _DEV_MARVELL_MARVELLVAR_H_ */

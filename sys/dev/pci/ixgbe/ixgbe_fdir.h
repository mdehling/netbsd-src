/* $NetBSD: ixgbe_fdir.h,v 1.4.4.1 2023/10/13 18:16:51 martin Exp $ */
/******************************************************************************

  Copyright (c) 2001-2020, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD: head/sys/dev/ixgbe/ixgbe_fdir.h 327031 2017-12-20 18:15:06Z erj $*/

#ifndef _IXGBE_FDIR_H_
#define _IXGBE_FDIR_H_

#ifdef IXGBE_FDIR

/*
 * Flow Director actually 'steals' part of the packet buffer
 * as its filter pool, this variable controls how much it uses:
 *  0 = 64K, 1 = 128K, 2 = 256K
 */
int fdir_pballoc = 1;

void ixgbe_init_fdir(struct ixgbe_softc *);

#else

#define ixgbe_init_fdir(_a)

#endif

void ixgbe_reinit_fdir(void *);
void ixgbe_atr(struct tx_ring *, struct mbuf *);

#endif /* _IXGBE_FDIR_H_ */

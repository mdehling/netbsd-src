/* 	$NetBSD: viornd.c,v 1.18.4.2 2023/08/11 14:35:24 martin Exp $ */
/*	$OpenBSD: viornd.c,v 1.1 2014/01/21 21:14:58 sf Exp $	*/

/*
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Thor Lancelot Simon (tls@NetBSD.org).
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2014 Stefan Fritsch <sf@sfritsch.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/callout.h>
#include <sys/rndsource.h>
#include <sys/mutex.h>
#include <dev/pci/virtioreg.h>
#include <dev/pci/virtiovar.h>

#define	VIORND_BUFSIZE			32

#define VIORND_DEBUG 0

struct viornd_softc {
	device_t		sc_dev;
	struct virtio_softc	*sc_virtio;

	kmutex_t		sc_mutex;
	bool			sc_active;

	void			*sc_buf;
	struct virtqueue	sc_vq;
	bus_dmamap_t		sc_dmamap;
	krndsource_t		sc_rndsource;
};

int	viornd_match(device_t, cfdata_t, void *);
void	viornd_attach(device_t, device_t, void *);
int	viornd_vq_done(struct virtqueue *);

CFATTACH_DECL_NEW(viornd, sizeof(struct viornd_softc),
		  viornd_match, viornd_attach, NULL, NULL);

static void
viornd_get(size_t bytes, void *priv)
{
        struct viornd_softc *sc = priv;
        struct virtio_softc *vsc = sc->sc_virtio;
        struct virtqueue *vq = &sc->sc_vq;
        int slot;

#if VIORND_DEBUG
	aprint_normal("%s: asked for %d bytes of entropy\n", __func__,
		      VIORND_BUFSIZE);
#endif
	mutex_enter(&sc->sc_mutex);

	if (sc->sc_active) {
		goto out;
	}

        bus_dmamap_sync(virtio_dmat(vsc), sc->sc_dmamap, 0, VIORND_BUFSIZE,
            BUS_DMASYNC_PREREAD);
	if (virtio_enqueue_prep(vsc, vq, &slot)) {
		goto out;
	}
        if (virtio_enqueue_reserve(vsc, vq, slot, 1)) {
		goto out;
	}
        virtio_enqueue(vsc, vq, slot, sc->sc_dmamap, 0);
        virtio_enqueue_commit(vsc, vq, slot, 1);
	sc->sc_active = true;
out:
	mutex_exit(&sc->sc_mutex);
}

int
viornd_match(device_t parent, cfdata_t match, void *aux)
{
	struct virtio_attach_args *va = aux;

	if (va->sc_childdevid == VIRTIO_DEVICE_ID_ENTROPY)
		return 1;

	return 0;
}

void
viornd_attach(device_t parent, device_t self, void *aux)
{
	struct viornd_softc *sc = device_private(self);
	struct virtio_softc *vsc = device_private(parent);
	bus_dma_segment_t segs[1];
	int nsegs;
	int error;

	if (virtio_child(vsc) != NULL)
		panic("already attached to something else");

	sc->sc_dev = self;
	sc->sc_virtio = vsc;

	mutex_init(&sc->sc_mutex, MUTEX_DEFAULT, IPL_VM);

	error = bus_dmamem_alloc(virtio_dmat(vsc),
				 VIRTIO_PAGE_SIZE, 0, 0, segs, 1, &nsegs,
				 BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW);
	if (error) {
		aprint_error_dev(sc->sc_dev, "can't alloc dmamem: %d\n",
				 error);
		goto alloc_failed;
	}

	error = bus_dmamem_map(virtio_dmat(vsc), segs, nsegs, VIORND_BUFSIZE,
			       &sc->sc_buf, BUS_DMA_NOWAIT);
	if (error) {
		aprint_error_dev(sc->sc_dev, "can't map dmamem: %d\n", error);
		goto map_failed;
	}

	error = bus_dmamap_create(virtio_dmat(vsc), VIORND_BUFSIZE, 1,
				  VIORND_BUFSIZE, 0,
				  BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW,
				  &sc->sc_dmamap);
	if (error) {
		aprint_error_dev(sc->sc_dev, "can't alloc dmamap: %d\n",
				 error);
		goto create_failed;
	}

	error = bus_dmamap_load(virtio_dmat(vsc), sc->sc_dmamap,
	    			sc->sc_buf, VIORND_BUFSIZE, NULL,
				BUS_DMA_NOWAIT|BUS_DMA_READ);
	if (error) {
		aprint_error_dev(sc->sc_dev, "can't load dmamap: %d\n",
				 error);
		goto load_failed;
	}

	virtio_child_attach_start(vsc, self, IPL_NET,
	    0, VIRTIO_COMMON_FLAG_BITS);

	virtio_init_vq_vqdone(vsc, &sc->sc_vq, 0, viornd_vq_done);

	error = virtio_alloc_vq(vsc, &sc->sc_vq, VIORND_BUFSIZE, 1,
	    "Entropy request");
	if (error) {
		aprint_error_dev(sc->sc_dev, "can't alloc virtqueue: %d\n",
				 error);
		goto vio_failed;
	}
	sc->sc_vq.vq_done = viornd_vq_done;

	error = virtio_child_attach_finish(vsc, &sc->sc_vq, 1,
	    NULL, VIRTIO_F_INTR_MPSAFE);
	if (error) {
		virtio_free_vq(vsc, &sc->sc_vq);
		goto vio_failed;
	}

	rndsource_setcb(&sc->sc_rndsource, viornd_get, sc);
	rnd_attach_source(&sc->sc_rndsource, device_xname(sc->sc_dev),
			  RND_TYPE_RNG,
			  RND_FLAG_COLLECT_VALUE|RND_FLAG_HASCB);

	return;

vio_failed:
	bus_dmamap_unload(virtio_dmat(vsc), sc->sc_dmamap);
load_failed:
	bus_dmamap_destroy(virtio_dmat(vsc), sc->sc_dmamap);
create_failed:
	bus_dmamem_unmap(virtio_dmat(vsc), sc->sc_buf, VIORND_BUFSIZE);
map_failed:
	bus_dmamem_free(virtio_dmat(vsc), segs, nsegs);
alloc_failed:
	virtio_child_attach_failed(vsc);
	return;
}

int
viornd_vq_done(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viornd_softc *sc = device_private(virtio_child(vsc));
	int slot, len;

	mutex_enter(&sc->sc_mutex);

	if (virtio_dequeue(vsc, vq, &slot, &len) != 0) {
		mutex_exit(&sc->sc_mutex);
		return 0;
	}

	sc->sc_active = false;

	bus_dmamap_sync(virtio_dmat(vsc), sc->sc_dmamap, 0, VIORND_BUFSIZE,
	    BUS_DMASYNC_POSTREAD);
	if (len > VIORND_BUFSIZE) {
		aprint_error_dev(sc->sc_dev,
				 "inconsistent descriptor length %d > %d\n",
				 len, VIORND_BUFSIZE);
		goto out;
	}

#if VIORND_DEBUG
	aprint_normal("%s: got %d bytes of entropy\n", __func__, len);
#endif
	/* XXX Shouldn't this be len instead of VIORND_BUFSIZE?  */
	rnd_add_data_intr(&sc->sc_rndsource, sc->sc_buf, VIORND_BUFSIZE,
	    VIORND_BUFSIZE * NBBY);
out:
	virtio_dequeue_commit(vsc, vq, slot);
	mutex_exit(&sc->sc_mutex);

	return 1;
}

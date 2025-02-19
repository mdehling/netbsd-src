/*	$NetBSD: viomb.c,v 1.13.4.1 2023/05/13 10:56:10 martin Exp $	*/

/*
 * Copyright (c) 2010 Minoura Makoto.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: viomb.c,v 1.13.4.1 2023/05/13 10:56:10 martin Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <uvm/uvm_page.h>
#include <sys/module.h>

#include <dev/pci/virtioreg.h>
#include <dev/pci/virtiovar.h>

#include "ioconf.h"

/* Configuration registers */
#define VIRTIO_BALLOON_CONFIG_NUM_PAGES	0 /* 32bit */
#define VIRTIO_BALLOON_CONFIG_ACTUAL	4 /* 32bit */

/* Feature bits */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST (1<<0)
#define VIRTIO_BALLOON_F_STATS_VQ	(1<<1)

#define VIRTIO_BALLOON_FLAG_BITS		\
	VIRTIO_COMMON_FLAG_BITS			\
	"b\x01" "STATS_VQ\0"			\
	"b\x00" "MUST_TELL_HOST\0"

#define PGS_PER_REQ		(256) /* 1MB, 4KB/page */
#define VQ_INFLATE	0
#define VQ_DEFLATE	1


CTASSERT((PAGE_SIZE) == (VIRTIO_PAGE_SIZE)); /* XXX */

struct balloon_req {
	bus_dmamap_t			bl_dmamap;
	struct pglist			bl_pglist;
	int				bl_nentries;
	uint32_t			bl_pages[PGS_PER_REQ];
};

struct viomb_softc {
	device_t		sc_dev;

	struct virtio_softc	*sc_virtio;
	struct virtqueue	sc_vq[2];

	unsigned int		sc_npages;
	unsigned int		sc_actual;
	int			sc_inflight;
	struct balloon_req	sc_req;
	struct pglist		sc_balloon_pages;

	int			sc_inflate_done;
	int			sc_deflate_done;

	kcondvar_t		sc_wait;
	kmutex_t		sc_waitlock;
};

static int	balloon_initialized = 0; /* multiple balloon is not allowed */

static int	viomb_match(device_t, cfdata_t, void *);
static void	viomb_attach(device_t, device_t, void *);
static void	viomb_read_config(struct viomb_softc *);
static int	viomb_config_change(struct virtio_softc *);
static int	inflate(struct viomb_softc *);
static int	inflateq_done(struct virtqueue *);
static int	inflate_done(struct viomb_softc *);
static int	deflate(struct viomb_softc *);
static int	deflateq_done(struct virtqueue *);
static int	deflate_done(struct viomb_softc *);
static void	viomb_thread(void *);

CFATTACH_DECL_NEW(viomb, sizeof(struct viomb_softc),
    viomb_match, viomb_attach, NULL, NULL);

static int
viomb_match(device_t parent, cfdata_t match, void *aux)
{
	struct virtio_attach_args *va = aux;

	if (va->sc_childdevid == VIRTIO_DEVICE_ID_BALLOON)
		return 1;

	return 0;
}

static void
viomb_attach(device_t parent, device_t self, void *aux)
{
	struct viomb_softc *sc = device_private(self);
	struct virtio_softc *vsc = device_private(parent);
	const struct sysctlnode *node;
	uint64_t features;

	if (virtio_child(vsc) != NULL) {
		aprint_normal(": child already attached for %s; "
			      "something wrong...\n", device_xname(parent));
		return;
	}

	if (balloon_initialized++) {
		aprint_normal(": balloon already exists; something wrong...\n");
		return;
	}

	/* fail on non-4K page size archs */
	if (VIRTIO_PAGE_SIZE != PAGE_SIZE){
		aprint_normal("non-4K page size arch found, needs %d, got %d\n",
		    VIRTIO_PAGE_SIZE, PAGE_SIZE);
		return;
	}

	sc->sc_dev = self;
	sc->sc_virtio = vsc;

	virtio_child_attach_start(vsc, self, IPL_VM,
	    VIRTIO_BALLOON_F_MUST_TELL_HOST, VIRTIO_BALLOON_FLAG_BITS);

	features = virtio_features(vsc);
	if (features == 0)
		goto err_none;

	viomb_read_config(sc);
	sc->sc_inflight = 0;
	TAILQ_INIT(&sc->sc_balloon_pages);

	sc->sc_inflate_done = sc->sc_deflate_done = 0;
	mutex_init(&sc->sc_waitlock, MUTEX_DEFAULT, IPL_VM); /* spin */
	cv_init(&sc->sc_wait, "balloon");

	virtio_init_vq_vqdone(vsc, &sc->sc_vq[VQ_INFLATE], VQ_INFLATE,
	    inflateq_done);
	virtio_init_vq_vqdone(vsc, &sc->sc_vq[VQ_DEFLATE], VQ_DEFLATE,
	    deflateq_done);

	if (virtio_alloc_vq(vsc, &sc->sc_vq[VQ_INFLATE],
			     sizeof(uint32_t)*PGS_PER_REQ, 1,
			     "inflate") != 0)
		goto err_mutex;
	if (virtio_alloc_vq(vsc, &sc->sc_vq[VQ_DEFLATE],
			     sizeof(uint32_t)*PGS_PER_REQ, 1,
			     "deflate") != 0)
		goto err_vq0;

	if (bus_dmamap_create(virtio_dmat(vsc), sizeof(uint32_t)*PGS_PER_REQ,
			      1, sizeof(uint32_t)*PGS_PER_REQ, 0,
			      BUS_DMA_NOWAIT, &sc->sc_req.bl_dmamap)) {
		aprint_error_dev(sc->sc_dev, "dmamap creation failed.\n");
		goto err_vq;
	}
	if (bus_dmamap_load(virtio_dmat(vsc), sc->sc_req.bl_dmamap,
			    &sc->sc_req.bl_pages[0],
			    sizeof(uint32_t) * PGS_PER_REQ,
			    NULL, BUS_DMA_NOWAIT)) {
		aprint_error_dev(sc->sc_dev, "dmamap load failed.\n");
		goto err_dmamap;
	}

	if (virtio_child_attach_finish(vsc, sc->sc_vq, __arraycount(sc->sc_vq),
	    viomb_config_change, VIRTIO_F_INTR_MPSAFE) != 0)
		goto err_out;

	if (kthread_create(PRI_IDLE, KTHREAD_MPSAFE, NULL,
			   viomb_thread, sc, NULL, "viomb")) {
		aprint_error_dev(sc->sc_dev, "cannot create kthread.\n");
		goto err_out;
	}

	sysctl_createv(NULL, 0, NULL, &node, 0, CTLTYPE_NODE,
		       "viomb", SYSCTL_DESCR("VirtIO Balloon status"),
		       NULL, 0, NULL, 0,
		       CTL_HW, CTL_CREATE, CTL_EOL);
	sysctl_createv(NULL, 0, NULL, NULL, 0, CTLTYPE_INT,
		       "npages", SYSCTL_DESCR("VirtIO Balloon npages value"),
		       NULL, 0, &sc->sc_npages, 0,
		       CTL_HW, node->sysctl_num, CTL_CREATE, CTL_EOL);
	sysctl_createv(NULL, 0, NULL, NULL, 0, CTLTYPE_INT,
		       "actual", SYSCTL_DESCR("VirtIO Balloon actual value"),
		       NULL, 0, &sc->sc_actual, 0,
		       CTL_HW, node->sysctl_num, CTL_CREATE, CTL_EOL);
	return;

err_out:
err_dmamap:
	bus_dmamap_destroy(virtio_dmat(vsc), sc->sc_req.bl_dmamap);
err_vq:
	virtio_free_vq(vsc, &sc->sc_vq[VQ_DEFLATE]);
err_vq0:
	virtio_free_vq(vsc, &sc->sc_vq[VQ_INFLATE]);
err_mutex:
	cv_destroy(&sc->sc_wait);
	mutex_destroy(&sc->sc_waitlock);
err_none:
	virtio_child_attach_failed(vsc);
	return;
}

static void
viomb_read_config(struct viomb_softc *sc)
{
	/* these values are explicitly specified as little-endian */
	sc->sc_npages = virtio_read_device_config_le_4(sc->sc_virtio,
		  VIRTIO_BALLOON_CONFIG_NUM_PAGES);

	sc->sc_actual = virtio_read_device_config_le_4(sc->sc_virtio,
		  VIRTIO_BALLOON_CONFIG_ACTUAL);
}

/*
 * Config change callback: wakeup the kthread.
 */
static int
viomb_config_change(struct virtio_softc *vsc)
{
	struct viomb_softc *sc = device_private(virtio_child(vsc));
	unsigned int old;

	old = sc->sc_npages;
	viomb_read_config(sc);
	mutex_enter(&sc->sc_waitlock);
	cv_signal(&sc->sc_wait);
	mutex_exit(&sc->sc_waitlock);
	if (sc->sc_npages > old)
		printf("%s: inflating balloon from %u to %u.\n",
		       device_xname(sc->sc_dev), old, sc->sc_npages);
	else if  (sc->sc_npages < old)
		printf("%s: deflating balloon from %u to %u.\n",
		       device_xname(sc->sc_dev), old, sc->sc_npages);

	return 1;
}

/*
 * Inflate: consume some amount of physical memory.
 */
static int
inflate(struct viomb_softc *sc)
{
	struct virtio_softc *vsc = sc->sc_virtio;
	int i, slot;
	uint64_t nvpages, nhpages;
	struct balloon_req *b;
	struct vm_page *p;
	struct virtqueue *vq = &sc->sc_vq[VQ_INFLATE];

	if (sc->sc_inflight)
		return 0;
	nvpages = sc->sc_npages - sc->sc_actual;
	if (nvpages > PGS_PER_REQ)
		nvpages = PGS_PER_REQ;
	nhpages = nvpages * VIRTIO_PAGE_SIZE / PAGE_SIZE;

	b = &sc->sc_req;
	if (uvm_pglistalloc(nhpages*PAGE_SIZE, 0, UINT32_MAX*PAGE_SIZE,
			    0, 0, &b->bl_pglist, nhpages, 1)) {
		printf("%s: %" PRIu64 " pages of physical memory "
		       "could not be allocated, retrying...\n",
		       device_xname(sc->sc_dev), nhpages);
		return 1;	/* sleep longer */
	}

	b->bl_nentries = nvpages;
	i = 0;
	TAILQ_FOREACH(p, &b->bl_pglist, pageq.queue) {
		b->bl_pages[i++] =
			htole32(VM_PAGE_TO_PHYS(p) / VIRTIO_PAGE_SIZE);
	}
	KASSERT(i == nvpages);

	if (virtio_enqueue_prep(vsc, vq, &slot) != 0) {
		printf("%s: inflate enqueue failed.\n",
		       device_xname(sc->sc_dev));
		uvm_pglistfree(&b->bl_pglist);
		return 0;
	}
	if (virtio_enqueue_reserve(vsc, vq, slot, 1)) {
		printf("%s: inflate enqueue failed.\n",
		       device_xname(sc->sc_dev));
		uvm_pglistfree(&b->bl_pglist);
		return 0;
	}
	bus_dmamap_sync(virtio_dmat(vsc), b->bl_dmamap, 0,
	    sizeof(uint32_t)*nvpages, BUS_DMASYNC_PREWRITE);
	virtio_enqueue(vsc, vq, slot, b->bl_dmamap, true);
	virtio_enqueue_commit(vsc, vq, slot, true);
	sc->sc_inflight += nvpages;

	return 0;
}

static int
inflateq_done(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viomb_softc *sc = device_private(virtio_child(vsc));

	mutex_enter(&sc->sc_waitlock);
	sc->sc_inflate_done = 1;
	cv_signal(&sc->sc_wait);
	mutex_exit(&sc->sc_waitlock);

	return 1;
}

static int
inflate_done(struct viomb_softc *sc)
{
	struct virtio_softc *vsc = sc->sc_virtio;
	struct virtqueue *vq = &sc->sc_vq[VQ_INFLATE];
	struct balloon_req *b;
	int r, slot;
	uint64_t nvpages;
	struct vm_page *p;

	r = virtio_dequeue(vsc, vq, &slot, NULL);
	if (r != 0) {
		printf("%s: inflate dequeue failed, errno %d.\n",
		       device_xname(sc->sc_dev), r);
		return 1;
	}
	virtio_dequeue_commit(vsc, vq, slot);

	b = &sc->sc_req;
	nvpages = b->bl_nentries;
	bus_dmamap_sync(virtio_dmat(vsc), b->bl_dmamap,
			0,
			sizeof(uint32_t)*nvpages,
			BUS_DMASYNC_POSTWRITE);
	while (!TAILQ_EMPTY(&b->bl_pglist)) {
		p = TAILQ_FIRST(&b->bl_pglist);
		TAILQ_REMOVE(&b->bl_pglist, p, pageq.queue);
		TAILQ_INSERT_TAIL(&sc->sc_balloon_pages, p, pageq.queue);
	}

	sc->sc_inflight -= nvpages;
	virtio_write_device_config_le_4(vsc,
		     VIRTIO_BALLOON_CONFIG_ACTUAL,
		     sc->sc_actual + nvpages);
	viomb_read_config(sc);

	return 1;
}
	
/*
 * Deflate: free previously allocated memory.
 */
static int
deflate(struct viomb_softc *sc)
{
	struct virtio_softc *vsc = sc->sc_virtio;
	int i, slot;
	uint64_t nvpages, nhpages;
	struct balloon_req *b;
	struct vm_page *p;
	struct virtqueue *vq = &sc->sc_vq[VQ_DEFLATE];

	nvpages = (sc->sc_actual + sc->sc_inflight) - sc->sc_npages;
	if (nvpages > PGS_PER_REQ)
		nvpages = PGS_PER_REQ;
	nhpages = nvpages * VIRTIO_PAGE_SIZE / PAGE_SIZE;

	b = &sc->sc_req;

	b->bl_nentries = nvpages;
	TAILQ_INIT(&b->bl_pglist);
	for (i = 0; i < nhpages; i++) {
		p = TAILQ_FIRST(&sc->sc_balloon_pages);
		if (p == NULL)
			break;
		TAILQ_REMOVE(&sc->sc_balloon_pages, p, pageq.queue);
		TAILQ_INSERT_TAIL(&b->bl_pglist, p, pageq.queue);
		b->bl_pages[i] =
			htole32(VM_PAGE_TO_PHYS(p) / VIRTIO_PAGE_SIZE);
	}

	if (virtio_enqueue_prep(vsc, vq, &slot) != 0) {
		printf("%s: deflate enqueue failed.\n",
		       device_xname(sc->sc_dev));
		TAILQ_FOREACH_REVERSE(p, &b->bl_pglist, pglist, pageq.queue) {
			TAILQ_REMOVE(&b->bl_pglist, p, pageq.queue);
			TAILQ_INSERT_HEAD(&sc->sc_balloon_pages, p,
			    pageq.queue);
		}
		return 0;
	}
	if (virtio_enqueue_reserve(vsc, vq, slot, 1) != 0) {
		printf("%s: deflate enqueue failed.\n",
		       device_xname(sc->sc_dev));
		TAILQ_FOREACH_REVERSE(p, &b->bl_pglist, pglist, pageq.queue) {
			TAILQ_REMOVE(&b->bl_pglist, p, pageq.queue);
			TAILQ_INSERT_HEAD(&sc->sc_balloon_pages, p,
			    pageq.queue);
		}
		return 0;
	}
	bus_dmamap_sync(virtio_dmat(vsc), b->bl_dmamap, 0,
	    sizeof(uint32_t)*nvpages, BUS_DMASYNC_PREWRITE);
	virtio_enqueue(vsc, vq, slot, b->bl_dmamap, true);
	virtio_enqueue_commit(vsc, vq, slot, true);
	sc->sc_inflight -= nvpages;

	if (!(virtio_features(vsc) & VIRTIO_BALLOON_F_MUST_TELL_HOST))
		uvm_pglistfree(&b->bl_pglist);

	return 0;
}

static int
deflateq_done(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viomb_softc *sc = device_private(virtio_child(vsc));

	mutex_enter(&sc->sc_waitlock);
	sc->sc_deflate_done = 1;
	cv_signal(&sc->sc_wait);
	mutex_exit(&sc->sc_waitlock);

	return 1;
}
	
static int
deflate_done(struct viomb_softc *sc)
{
	struct virtio_softc *vsc = sc->sc_virtio;
	struct virtqueue *vq = &sc->sc_vq[VQ_DEFLATE];
	struct balloon_req *b;
	int r, slot;
	uint64_t nvpages;

	r = virtio_dequeue(vsc, vq, &slot, NULL);
	if (r != 0) {
		printf("%s: deflate dequeue failed, errno %d\n",
		       device_xname(sc->sc_dev), r);
		return 1;
	}
	virtio_dequeue_commit(vsc, vq, slot);

	b = &sc->sc_req;
	nvpages = b->bl_nentries;
	bus_dmamap_sync(virtio_dmat(vsc), b->bl_dmamap,
			0,
			sizeof(uint32_t)*nvpages,
			BUS_DMASYNC_POSTWRITE);

	if (virtio_features(vsc) & VIRTIO_BALLOON_F_MUST_TELL_HOST)
		uvm_pglistfree(&b->bl_pglist);

	sc->sc_inflight += nvpages;
	virtio_write_device_config_le_4(vsc,
		     VIRTIO_BALLOON_CONFIG_ACTUAL,
		     sc->sc_actual - nvpages);
	viomb_read_config(sc);

	return 1;
}

/*
 * Kthread: sleeps, eventually inflate and deflate.
 */
static void
viomb_thread(void *arg)
{
	struct viomb_softc *sc = arg;
	int sleeptime, r;

	for ( ; ; ) {
		sleeptime = 30000;
		if (sc->sc_npages > sc->sc_actual + sc->sc_inflight) {
			if (sc->sc_inflight == 0) {
				r = inflate(sc);
				if (r != 0)
					sleeptime = 10000;
				else
					sleeptime = 1000;
			} else
				sleeptime = 100;
		} else if (sc->sc_npages < sc->sc_actual + sc->sc_inflight) {
			if (sc->sc_inflight == 0)
				r = deflate(sc);
			sleeptime = 100;
		}

	again:
		mutex_enter(&sc->sc_waitlock);
		if (sc->sc_inflate_done) {
			sc->sc_inflate_done = 0;
			mutex_exit(&sc->sc_waitlock);
			inflate_done(sc);
			goto again;
		}
		if (sc->sc_deflate_done) {
			sc->sc_deflate_done = 0;
			mutex_exit(&sc->sc_waitlock);
			deflate_done(sc);
			goto again;
		}
		cv_timedwait(&sc->sc_wait, &sc->sc_waitlock,
			     mstohz(sleeptime));
		mutex_exit(&sc->sc_waitlock);
	}
}

MODULE(MODULE_CLASS_DRIVER, viomb, "virtio");
 
#ifdef _MODULE
#include "ioconf.c"
#endif
 
static int 
viomb_modcmd(modcmd_t cmd, void *opaque)
{
	int error = 0;
 
#ifdef _MODULE
	switch (cmd) {
	case MODULE_CMD_INIT:
		error = config_init_component(cfdriver_ioconf_viomb, 
		    cfattach_ioconf_viomb, cfdata_ioconf_viomb); 
		break;
	case MODULE_CMD_FINI:
		error = config_fini_component(cfdriver_ioconf_viomb,
		    cfattach_ioconf_viomb, cfdata_ioconf_viomb);
		break;
	default:
		error = ENOTTY;
		break; 
	}
#endif
   
	return error;
}

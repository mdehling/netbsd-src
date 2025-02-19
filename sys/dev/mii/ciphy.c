/* $NetBSD: ciphy.c,v 1.41.20.1 2023/06/21 22:11:29 martin Exp $ */

/*-
 * Copyright (c) 2004
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * FreeBSD: src/sys/dev/mii/ciphy.c,v 1.2 2005/01/06 01:42:55 imp Exp
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: ciphy.c,v 1.41.20.1 2023/06/21 22:11:29 martin Exp $");

/*
 * Driver for the Cicada CS8201 10/100/1000 copper PHY.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

#include <dev/mii/ciphyreg.h>

static int ciphymatch(device_t, cfdata_t, void *);
static void ciphyattach(device_t, device_t, void *);

CFATTACH_DECL_NEW(ciphy, sizeof(struct mii_softc),
    ciphymatch, ciphyattach, mii_phy_detach, mii_phy_activate);

static int	ciphy_service(struct mii_softc *, struct mii_data *, int);
static void	ciphy_status(struct mii_softc *);
static void	ciphy_reset(struct mii_softc *);
static void	ciphy_fixup(struct mii_softc *);

static const struct mii_phy_funcs ciphy_funcs = {
	ciphy_service, ciphy_status, mii_phy_reset,
};

static const struct mii_phydesc ciphys[] = {
	MII_PHY_DESC(xxCICADA, CIS8201),
	MII_PHY_DESC(xxCICADA, CIS8201A),
	MII_PHY_DESC(xxCICADA, CIS8201B),
	MII_PHY_DESC(xxCICADA, CIS8204),
	MII_PHY_DESC(xxCICADA, VSC8211),
	MII_PHY_DESC(xxCICADA, VSC8221),
	MII_PHY_DESC(xxCICADA, VSC8234),
	MII_PHY_DESC(xxCICADA, VSC8244),
	MII_PHY_DESC(xxVITESSE, VSC8601),
	MII_PHY_DESC(xxVITESSE, VSC8641),
	MII_PHY_END,
};

static int
ciphymatch(device_t parent, cfdata_t match,
    void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, ciphys) != NULL)
		return 10;

	return 0;
}

static void
ciphyattach(device_t parent, device_t self, void *aux)
{
	struct mii_softc *sc = device_private(self);
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, ciphys);
	aprint_naive(": Media interface\n");
	aprint_normal(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_dev = self;
	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &ciphy_funcs;
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;
	sc->mii_flags |= MIIF_NOISOLATE;

	mii_lock(mii);

	ciphy_reset(sc);

	PHY_READ(sc, MII_BMSR, &sc->mii_capabilities);
	sc->mii_capabilities &= ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		PHY_READ(sc, MII_EXTSR, &sc->mii_extcapabilities);

	mii_unlock(mii);

	mii_phy_add_media(sc);
}

static int
ciphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t reg, speed, gig;

	KASSERT(mii_locked(mii));

	switch (cmd) {
	case MII_POLLSTAT:
		/* If we're not polling our PHY instance, just return. */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return 0;
		break;

	case MII_MEDIACHG:
		/*
		 * If the media indicates a different PHY instance,
		 * isolate ourselves.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst) {
			PHY_READ(sc, MII_BMCR, &reg);
			PHY_WRITE(sc, MII_BMCR, reg | BMCR_ISO);
			return 0;
		}

		/* If the interface is not up, don't do anything. */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		ciphy_fixup(sc);	/* XXX hardware bug work-around */

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
#ifdef foo
			/* If we're already in auto mode, just return. */
			PHY_READ(sc, MII_BMCR, &reg);
			if (reg & BMCR_AUTOEN)
				return 0;
#endif
			(void) mii_phy_auto(sc);
			break;
		case IFM_1000_T:
			speed = BMCR_S1000;
			goto setit;
		case IFM_100_TX:
			speed = BMCR_S100;
			goto setit;
		case IFM_10_T:
			speed = BMCR_S10;
setit:
			if ((ife->ifm_media & IFM_FDX) != 0) {
				speed |= BMCR_FDX;
				gig = GTCR_ADV_1000TFDX;
			} else
				gig = GTCR_ADV_1000THDX;

			PHY_WRITE(sc, MII_GTCR, 0);
			PHY_WRITE(sc, MII_BMCR, speed);
			PHY_WRITE(sc, MII_ANAR, ANAR_CSMA);

			if (IFM_SUBTYPE(ife->ifm_media) != IFM_1000_T)
				break;

			PHY_WRITE(sc, MII_GTCR, gig);
			PHY_WRITE(sc, MII_BMCR,
			    speed | BMCR_AUTOEN | BMCR_STARTNEG);

			/*
			 * When setting the link manually, one side must
			 * be the master and the other the slave. However
			 * ifmedia doesn't give us a good way to specify
			 * this, so we fake it by using one of the LINK
			 * flags. If LINK0 is set, we program the PHY to
			 * be a master, otherwise it's a slave.
			 */
			if ((mii->mii_ifp->if_flags & IFF_LINK0)) {
				PHY_WRITE(sc, MII_GTCR,
				    gig | GTCR_MAN_MS | GTCR_ADV_MS);
			} else
				PHY_WRITE(sc, MII_GTCR, gig | GTCR_MAN_MS);
			break;
		case IFM_NONE:
			PHY_WRITE(sc, MII_BMCR, BMCR_ISO | BMCR_PDOWN);
			break;
		case IFM_100_T4:
		default:
			return EINVAL;
		}
		break;

	case MII_TICK:
		/* If we're not currently selected, just return. */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return 0;

		/* Is the interface even up? */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			return 0;

		/* Only used for autonegotiation. */
		if ((IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO) &&
		    (IFM_SUBTYPE(ife->ifm_media) != IFM_1000_T)) {
			/*
			 * Reset autonegotiation timer to 0 just to make sure
			 * the future autonegotiation start with 0.
			 */
			sc->mii_ticks = 0;
			break;
		}

		/*
		 * Check to see if we have link.  If we do, we don't
		 * need to restart the autonegotiation process.  Read
		 * the BMSR twice in case it's latched.
		 */
		PHY_READ(sc, MII_BMSR, &reg);
		PHY_READ(sc, MII_BMSR, &reg);
		if (reg & BMSR_LINK) {
			/*
			 * Reset autonegotiation timer to 0 in case the link
			 * goes down in the next tick.
			 */
			sc->mii_ticks = 0;
			/* See above. */
			break;
		}

		/*
		 * mii_ticks == 0 means it's the first tick after changing the
		 * media or the link became down since the last tick
		 * (see above), so return with 0 to update the status.
		 */
		if (sc->mii_ticks++ == 0)
			break;

		/* Only retry autonegotiation every N seconds. */
		if (sc->mii_ticks < sc->mii_anegticks)
			break;

		mii_phy_auto_restart(sc);
		return 0;
	}

	/* Update the media status. */
	ciphy_status(sc);

	/*
	 * Callback if something changed. Note that we need to poke
	 * apply fixups for certain PHY revs.
	 */
	if (sc->mii_media_active != mii->mii_media_active ||
	    sc->mii_media_status != mii->mii_media_status ||
	    cmd == MII_MEDIACHG) {
		ciphy_fixup(sc);
	}
	mii_phy_update(sc, cmd);
	return 0;
}

static void
ciphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	uint16_t bmsr, bmcr, gtsr;

	KASSERT(mii_locked(mii));

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	PHY_READ(sc, MII_BMSR, &bmsr);
	PHY_READ(sc, MII_BMSR, &bmsr);

	if (bmsr & BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	PHY_READ(sc, MII_BMCR, &bmcr);

	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & BMCR_AUTOEN) {
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}
	}

	PHY_READ(sc, CIPHY_MII_AUXCSR, &bmsr);
	switch (bmsr & CIPHY_AUXCSR_SPEED) {
	case CIPHY_SPEED10:
		mii->mii_media_active |= IFM_10_T;
		break;
	case CIPHY_SPEED100:
		mii->mii_media_active |= IFM_100_TX;
		break;
	case CIPHY_SPEED1000:
		mii->mii_media_active |= IFM_1000_T;
		break;
	default:
		aprint_error_dev(sc->mii_dev, "unknown PHY speed %x\n",
		    bmsr & CIPHY_AUXCSR_SPEED);
		break;
	}

	if (bmsr & CIPHY_AUXCSR_FDX)
		mii->mii_media_active |= IFM_FDX | mii_phy_flowstatus(sc);
	else
		mii->mii_media_active |= IFM_HDX;

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
		PHY_READ(sc, MII_GTSR, &gtsr);
		if ((gtsr & GTSR_MS_RES) != 0)
			mii->mii_media_active |= IFM_ETH_MASTER;
	}
}

static void
ciphy_reset(struct mii_softc *sc)
{

	KASSERT(mii_locked(sc->mii_pdata));

	mii_phy_reset(sc);
	DELAY(1000);
}

static inline int
PHY_SETBIT(struct mii_softc *sc, int y, uint16_t z)
{
	uint16_t _tmp;
	int rv;

	if ((rv = PHY_READ(sc, y, &_tmp)) != 0)
		return rv;
	return PHY_WRITE(sc, y, _tmp | z);
}

static inline int
PHY_CLRBIT(struct mii_softc *sc, int y, uint16_t z)
{
	uint16_t _tmp;
	int rv;

	if ((rv = PHY_READ(sc, y, &_tmp)) != 0)
	    return rv;
	return PHY_WRITE(sc, y, _tmp & ~z);
}

static void
ciphy_fixup(struct mii_softc *sc)
{
	uint16_t	model, status, speed;
	uint16_t	reg;

	PHY_READ(sc, MII_PHYIDR2, &reg);
	model = MII_MODEL(reg);
	PHY_READ(sc, CIPHY_MII_AUXCSR, &status);
	speed = status & CIPHY_AUXCSR_SPEED;

	if (device_is_a(device_parent(sc->mii_dev), "nfe")) {
		/* Need to set for 2.5V RGMII for NVIDIA adapters */
		PHY_SETBIT(sc, CIPHY_MII_ECTL1, CIPHY_INTSEL_RGMII);
		PHY_SETBIT(sc, CIPHY_MII_ECTL1, CIPHY_IOVOL_2500MV);
	}

	switch (model) {
	case MII_MODEL_xxCICADA_CIS8201:
	case MII_MODEL_xxCICADA_CIS8204:
		/* Turn off "aux mode" (whatever that means) */
		PHY_SETBIT(sc, CIPHY_MII_AUXCSR, CIPHY_AUXCSR_MDPPS);

		/*
		 * Work around speed polling bug in VT3119/VT3216
		 * when using MII in full duplex mode.
		 */
		if ((speed == CIPHY_SPEED10 || speed == CIPHY_SPEED100) &&
		    (status & CIPHY_AUXCSR_FDX))
			PHY_SETBIT(sc, CIPHY_MII_10BTCSR, CIPHY_10BTCSR_ECHO);
		else
			PHY_CLRBIT(sc, CIPHY_MII_10BTCSR, CIPHY_10BTCSR_ECHO);

		/* Enable link/activity LED blink. */
		PHY_SETBIT(sc, CIPHY_MII_LED, CIPHY_LED_LINKACTBLINK);

		break;

	case MII_MODEL_xxCICADA_CIS8201A:
	case MII_MODEL_xxCICADA_CIS8201B:
		/*
		 * Work around speed polling bug in VT3119/VT3216
		 * when using MII in full duplex mode.
		 */
		if ((speed == CIPHY_SPEED10 || speed == CIPHY_SPEED100) &&
		    (status & CIPHY_AUXCSR_FDX))
			PHY_SETBIT(sc, CIPHY_MII_10BTCSR, CIPHY_10BTCSR_ECHO);
		else
			PHY_CLRBIT(sc, CIPHY_MII_10BTCSR, CIPHY_10BTCSR_ECHO);

		break;
	case MII_MODEL_xxCICADA_VSC8211:
	case MII_MODEL_xxCICADA_VSC8221:
	case MII_MODEL_xxCICADA_VSC8234:
	case MII_MODEL_xxCICADA_VSC8244:
	case MII_MODEL_xxVITESSE_VSC8601:
	case MII_MODEL_xxVITESSE_VSC8641:
		break;
	default:
		aprint_error_dev(sc->mii_dev, "unknown CICADA PHY model %x\n",
		    model);
		break;
	}
}

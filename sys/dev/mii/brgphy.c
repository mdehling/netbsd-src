/*	$NetBSD: brgphy.c,v 1.90.20.1 2023/06/21 22:11:29 martin Exp $	*/

/*-
 * Copyright (c) 1998, 1999, 2000, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * Copyright (c) 1997 Manuel Bouyer.  All rights reserved.
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

/*
 * driver for the Broadcom BCM5400 and BCM5700 Gig-E PHYs.
 *
 * Programming information for this PHY was gleaned from FreeBSD
 * (they were apparently able to get a datasheet from Broadcom).
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: brgphy.c,v 1.90.20.1 2023/06/21 22:11:29 martin Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <prop/proplib.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>
#include <dev/mii/brgphyreg.h>

#include <dev/pci/if_bgereg.h>
#include <dev/pci/if_bnxreg.h>

static int	brgphymatch(device_t, cfdata_t, void *);
static void	brgphyattach(device_t, device_t, void *);

struct brgphy_softc {
	struct mii_softc sc_mii;
	bool sc_isbge;
	bool sc_isbnx;
	uint32_t sc_chipid;	/* parent's chipid */
	uint32_t sc_phyflags;	/* parent's phyflags */
	uint32_t sc_shared_hwcfg; /* shared hw config */
	uint32_t sc_port_hwcfg;	/* port specific hw config */
};

CFATTACH_DECL_NEW(brgphy, sizeof(struct brgphy_softc),
    brgphymatch, brgphyattach, mii_phy_detach, mii_phy_activate);

static int	brgphy_service(struct mii_softc *, struct mii_data *, int);
static void	brgphy_copper_status(struct mii_softc *);
static void	brgphy_fiber_status(struct mii_softc *);
static void	brgphy_5708s_status(struct mii_softc *);
static void	brgphy_5709s_status(struct mii_softc *);
static int	brgphy_mii_phy_auto(struct mii_softc *);
static void	brgphy_loop(struct mii_softc *);
static void	brgphy_reset(struct mii_softc *);
static void	brgphy_bcm5401_dspcode(struct mii_softc *);
static void	brgphy_bcm5411_dspcode(struct mii_softc *);
static void	brgphy_bcm5421_dspcode(struct mii_softc *);
static void	brgphy_bcm54k2_dspcode(struct mii_softc *);
static void	brgphy_adc_bug(struct mii_softc *);
static void	brgphy_5704_a0_bug(struct mii_softc *);
static void	brgphy_ber_bug(struct mii_softc *);
static void	brgphy_crc_bug(struct mii_softc *);
static void	brgphy_disable_early_dac(struct mii_softc *);
static void	brgphy_jumbo_settings(struct mii_softc *);
static void	brgphy_eth_wirespeed(struct mii_softc *);
static void	brgphy_bcm54xx_clock_delay(struct mii_softc *);

static const struct mii_phy_funcs brgphy_copper_funcs = {
	brgphy_service, brgphy_copper_status, brgphy_reset,
};

static const struct mii_phy_funcs brgphy_fiber_funcs = {
	brgphy_service, brgphy_fiber_status, brgphy_reset,
};

static const struct mii_phy_funcs brgphy_5708s_funcs = {
	brgphy_service, brgphy_5708s_status, brgphy_reset,
};

static const struct mii_phy_funcs brgphy_5709s_funcs = {
	brgphy_service, brgphy_5709s_status, brgphy_reset,
};

static const struct mii_phydesc brgphys[] = {
	MII_PHY_DESC(BROADCOM, BCM5400),
	MII_PHY_DESC(BROADCOM, BCM5401),
	MII_PHY_DESC(BROADCOM, BCM5402),
	MII_PHY_DESC(BROADCOM, BCM5404),
	MII_PHY_DESC(BROADCOM, BCM5411),
	MII_PHY_DESC(BROADCOM, BCM5421),
	MII_PHY_DESC(BROADCOM, BCM5424),
	MII_PHY_DESC(BROADCOM, BCM5461),
	MII_PHY_DESC(BROADCOM, BCM5462),
	MII_PHY_DESC(BROADCOM, BCM5464),
	MII_PHY_DESC(BROADCOM, BCM5466),
	MII_PHY_DESC(BROADCOM, BCM54K2),
	MII_PHY_DESC(BROADCOM, BCM5701),
	MII_PHY_DESC(BROADCOM, BCM5703),
	MII_PHY_DESC(BROADCOM, BCM5704),
	MII_PHY_DESC(BROADCOM, BCM5705),
	MII_PHY_DESC(BROADCOM, BCM5706),
	MII_PHY_DESC(BROADCOM, BCM5714),
	MII_PHY_DESC(BROADCOM, BCM5750),
	MII_PHY_DESC(BROADCOM, BCM5752),
	MII_PHY_DESC(BROADCOM, BCM5780),
	MII_PHY_DESC(BROADCOM, BCM5708C),
	MII_PHY_DESC(BROADCOM2, BCM5481),
	MII_PHY_DESC(BROADCOM2, BCM5482),
	MII_PHY_DESC(BROADCOM2, BCM5708S),
	MII_PHY_DESC(BROADCOM2, BCM5709C),
	MII_PHY_DESC(BROADCOM2, BCM5709S),
	MII_PHY_DESC(BROADCOM2, BCM5709CAX),
	MII_PHY_DESC(BROADCOM2, BCM5722),
	MII_PHY_DESC(BROADCOM2, BCM5754),
	MII_PHY_DESC(BROADCOM2, BCM5755),
	MII_PHY_DESC(BROADCOM2, BCM5756),
	MII_PHY_DESC(BROADCOM2, BCM5761),
	MII_PHY_DESC(BROADCOM2, BCM5784),
	MII_PHY_DESC(BROADCOM2, BCM5785),
	MII_PHY_DESC(BROADCOM3, BCM5717C),
	MII_PHY_DESC(BROADCOM3, BCM5719C),
	MII_PHY_DESC(BROADCOM3, BCM5720C),
	MII_PHY_DESC(BROADCOM3, BCM57765),
	MII_PHY_DESC(BROADCOM3, BCM57780),
	MII_PHY_DESC(BROADCOM4, BCM54213PE),
	MII_PHY_DESC(BROADCOM4, BCM5725C),
	MII_PHY_DESC(xxBROADCOM_ALT1, BCM5906),
	MII_PHY_END,
};

static int
brgphymatch(device_t parent, cfdata_t match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, brgphys) != NULL)
		return 10;

	return 0;
}

static void
brgphyattach(device_t parent, device_t self, void *aux)
{
	struct brgphy_softc *bsc = device_private(self);
	struct mii_softc *sc = &bsc->sc_mii;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;
	prop_dictionary_t dict;

	mpd = mii_phy_match(ma, brgphys);
	aprint_naive(": Media interface\n");
	aprint_normal(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_dev = self;
	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_mpd_oui = MII_OUI(ma->mii_id1, ma->mii_id2);
	sc->mii_mpd_model = MII_MODEL(ma->mii_id2);
	sc->mii_mpd_rev = MII_REV(ma->mii_id2);
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;

	if (device_is_a(parent, "bge"))
		bsc->sc_isbge = true;
	else if (device_is_a(parent, "bnx"))
		bsc->sc_isbnx = true;

	dict = device_properties(parent);
	if (bsc->sc_isbge || bsc->sc_isbnx) {
		if (!prop_dictionary_get_uint32(dict, "phyflags",
		    &bsc->sc_phyflags))
			aprint_error_dev(self, "failed to get phyflags\n");
		if (!prop_dictionary_get_uint32(dict, "chipid",
		    &bsc->sc_chipid))
			aprint_error_dev(self, "failed to get chipid\n");
	}

	if (bsc->sc_isbnx) {
		/* Currently, only bnx use sc_shared_hwcfg and sc_port_hwcfg */
		if (!prop_dictionary_get_uint32(dict, "shared_hwcfg",
			&bsc->sc_shared_hwcfg))
			aprint_error_dev(self, "failed to get shared_hwcfg\n");
		if (!prop_dictionary_get_uint32(dict, "port_hwcfg",
			&bsc->sc_port_hwcfg))
			aprint_error_dev(self, "failed to get port_hwcfg\n");
	}

	if (sc->mii_flags & MIIF_HAVEFIBER) {
		if ((sc->mii_mpd_oui == MII_OUI_BROADCOM2)
		    && sc->mii_mpd_model == MII_MODEL_BROADCOM2_BCM5708S)
			sc->mii_funcs = &brgphy_5708s_funcs;
		else if ((sc->mii_mpd_oui == MII_OUI_BROADCOM2)
		    && (sc->mii_mpd_model ==  MII_MODEL_BROADCOM2_BCM5709S)) {
			if (bsc->sc_isbnx)
				sc->mii_funcs = &brgphy_5709s_funcs;
			else {
				/*
				 * XXX
				 * 5720S and 5709S shares the same PHY id.
				 * Assume 5720S PHY if parent device is bge(4).
				 */
				sc->mii_funcs = &brgphy_5708s_funcs;
			}
		} else
			sc->mii_funcs = &brgphy_fiber_funcs;
	} else
		sc->mii_funcs = &brgphy_copper_funcs;

	mii_lock(mii);

	PHY_RESET(sc);

	PHY_READ(sc, MII_BMSR, &sc->mii_capabilities);
	sc->mii_capabilities &= ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		PHY_READ(sc, MII_EXTSR, &sc->mii_extcapabilities);

	mii_unlock(mii);

	if (sc->mii_flags & MIIF_HAVEFIBER) {
		mii_lock(mii);
		sc->mii_flags |= MIIF_NOISOLATE | MIIF_NOLOOP;

		/*
		 * Set the proper bits for capabilities so that the
		 * correct media get selected by mii_phy_add_media()
		 */
		sc->mii_capabilities |= BMSR_ANEG;
		sc->mii_capabilities &= ~BMSR_100T4;
		sc->mii_extcapabilities |= EXTSR_1000XFDX;
		mii_unlock(mii);

		if (bsc->sc_isbnx) {
			/*
			 * 2.5Gb support is a software enabled feature
			 * on the BCM5708S and BCM5709S controllers.
			 */
#define	ADD(m, c)	ifmedia_add(&mii->mii_media, (m), (c), NULL)
			if (bsc->sc_phyflags
			    & BNX_PHY_2_5G_CAPABLE_FLAG) {
				ADD(IFM_MAKEWORD(IFM_ETHER, IFM_2500_SX,
					IFM_FDX, sc->mii_inst), 0);
				aprint_normal_dev(self, "2500baseSX-FDX\n");
#undef ADD
			}
		}
	}
	mii_phy_add_media(sc);
}

static int
brgphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
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

		PHY_RESET(sc); /* XXX hardware bug work-around */

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			(void) brgphy_mii_phy_auto(sc);
			break;
		case IFM_2500_SX:
			speed = BRGPHY_5708S_BMCR_2500;
			goto setit;
		case IFM_1000_SX:
		case IFM_1000_T:
			speed = BMCR_S1000;
			goto setit;
		case IFM_100_TX:
			speed = BMCR_S100;
			goto setit;
		case IFM_10_T:
			speed = BMCR_S10;
setit:
			brgphy_loop(sc);
			if ((ife->ifm_media & IFM_FDX) != 0) {
				speed |= BMCR_FDX;
				gig = GTCR_ADV_1000TFDX;
			} else
				gig = GTCR_ADV_1000THDX;

			PHY_WRITE(sc, MII_100T2CR, 0);
			PHY_WRITE(sc, MII_ANAR, ANAR_CSMA);
			PHY_WRITE(sc, MII_BMCR, speed);

			if ((IFM_SUBTYPE(ife->ifm_media) != IFM_1000_T) &&
			    (IFM_SUBTYPE(ife->ifm_media) != IFM_1000_SX) &&
			    (IFM_SUBTYPE(ife->ifm_media) != IFM_2500_SX))
				break;

			PHY_WRITE(sc, MII_100T2CR, gig);
			PHY_WRITE(sc, MII_BMCR,
			    speed | BMCR_AUTOEN | BMCR_STARTNEG);

			if ((sc->mii_mpd_oui != MII_OUI_BROADCOM)
			    || (sc->mii_mpd_model != MII_MODEL_BROADCOM_BCM5701))
				break;

			if (mii->mii_media.ifm_media & IFM_ETH_MASTER)
				gig |= GTCR_MAN_MS | GTCR_ADV_MS;
			PHY_WRITE(sc, MII_100T2CR, gig);
			break;
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
			sc->mii_ticks = 0;
			break;
		}

		/*
		 * Check for link.
		 * Read the status register twice; BMSR_LINK is latch-low.
		 */
		PHY_READ(sc, MII_BMSR, &reg);
		PHY_READ(sc, MII_BMSR, &reg);
		if (reg & BMSR_LINK) {
			sc->mii_ticks = 0;
			break;
		}

		/*
		 * mii_ticks == 0 means it's the first tick after changing the
		 * media or the link became down since the last tick
		 * (see above), so break to update the status.
		 */
		if (sc->mii_ticks++ == 0)
			break;

		/* Only retry autonegotiation every mii_anegticks seconds. */
		KASSERT(sc->mii_anegticks != 0);
		if (sc->mii_ticks < sc->mii_anegticks)
			break;

		brgphy_mii_phy_auto(sc);
		break;

	case MII_DOWN:
		mii_phy_down(sc);
		return 0;
	}

	/* Update the media status. */
	mii_phy_status(sc);

	/*
	 * Callback if something changed. Note that we need to poke the DSP on
	 * the Broadcom PHYs if the media changes.
	 */
	if (sc->mii_media_active != mii->mii_media_active ||
	    sc->mii_media_status != mii->mii_media_status ||
	    cmd == MII_MEDIACHG) {
		switch (sc->mii_mpd_oui) {
		case MII_OUI_BROADCOM:
			switch (sc->mii_mpd_model) {
			case MII_MODEL_BROADCOM_BCM5400:
				brgphy_bcm5401_dspcode(sc);
				break;
			case MII_MODEL_BROADCOM_BCM5401:
				if (sc->mii_mpd_rev == 1 || sc->mii_mpd_rev == 3)
					brgphy_bcm5401_dspcode(sc);
				break;
			case MII_MODEL_BROADCOM_BCM5411:
				brgphy_bcm5411_dspcode(sc);
				break;
			}
			break;
		case MII_OUI_BROADCOM4:
			switch (sc->mii_mpd_model) {
			case MII_MODEL_BROADCOM4_BCM54213PE:
				brgphy_bcm54xx_clock_delay(sc);
				break;
			}
		}
	}

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return 0;
}

static void
brgphy_copper_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t bmcr, bmsr, auxsts, gtsr;

	KASSERT(mii_locked(mii));

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	PHY_READ(sc, MII_BMSR, &bmsr);
	PHY_READ(sc, MII_BMSR, &bmsr);
	if (bmsr & BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	PHY_READ(sc, MII_BMCR, &bmcr);
	if (bmcr & BMCR_ISO) {
		mii->mii_media_active |= IFM_NONE;
		mii->mii_media_status = 0;
		return;
	}

	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & BMCR_AUTOEN) {
		/*
		 * The media status bits are only valid if autonegotiation
		 * has completed (or it's disabled).
		 */
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		PHY_READ(sc, BRGPHY_MII_AUXSTS, &auxsts);

		switch (auxsts & BRGPHY_AUXSTS_AN_RES) {
		case BRGPHY_RES_1000FD:
			mii->mii_media_active |= IFM_1000_T | IFM_FDX;
			PHY_READ(sc, MII_100T2SR, &gtsr);
			if (gtsr & GTSR_MS_RES)
				mii->mii_media_active |= IFM_ETH_MASTER;
			break;

		case BRGPHY_RES_1000HD:
			mii->mii_media_active |= IFM_1000_T | IFM_HDX;
			PHY_READ(sc, MII_100T2SR, &gtsr);
			if (gtsr & GTSR_MS_RES)
				mii->mii_media_active |= IFM_ETH_MASTER;
			break;

		case BRGPHY_RES_100FD:
			mii->mii_media_active |= IFM_100_TX | IFM_FDX;
			break;

		case BRGPHY_RES_100T4:
			mii->mii_media_active |= IFM_100_T4 | IFM_HDX;
			break;

		case BRGPHY_RES_100HD:
			mii->mii_media_active |= IFM_100_TX | IFM_HDX;
			break;

		case BRGPHY_RES_10FD:
			mii->mii_media_active |= IFM_10_T | IFM_FDX;
			break;

		case BRGPHY_RES_10HD:
			mii->mii_media_active |= IFM_10_T | IFM_HDX;
			break;

		default:
			mii->mii_media_active |= IFM_NONE;
			mii->mii_media_status = 0;
		}

		if (mii->mii_media_active & IFM_FDX)
			mii->mii_media_active |= mii_phy_flowstatus(sc);

	} else
		mii->mii_media_active = ife->ifm_media;
}

void
brgphy_fiber_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t bmcr, bmsr, anar, anlpar, result;

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

		mii->mii_media_active |= IFM_1000_SX;

		PHY_READ(sc, MII_ANAR, &anar);
		PHY_READ(sc, MII_ANLPAR, &anlpar);
		result = anar & anlpar;

		if (result & ANAR_X_FD)
			mii->mii_media_active |= IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;

		if (mii->mii_media_active & IFM_FDX)
			mii->mii_media_active |= mii_phy_flowstatus(sc);
	} else
		mii->mii_media_active = ife->ifm_media;
}

void
brgphy_5708s_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t bmcr, bmsr;

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
		uint16_t xstat;

		if ((bmsr & BMSR_ACOMP) == 0) {
			PHY_WRITE(sc, BRGPHY_5708S_BLOCK_ADDR,
			    BRGPHY_5708S_DIG_PG0);
			PHY_READ(sc, BRGPHY_5708S_PG0_1000X_STAT1, &xstat);
			if ((xstat & BRGPHY_5708S_PG0_1000X_STAT1_LINK) == 0) {
				/* Erg, still trying, I guess... */
				mii->mii_media_active |= IFM_NONE;
				return;
			}
		}

		PHY_WRITE(sc, BRGPHY_5708S_BLOCK_ADDR,
		    BRGPHY_5708S_DIG_PG0);
		PHY_READ(sc, BRGPHY_5708S_PG0_1000X_STAT1, &xstat);

		switch (xstat & BRGPHY_5708S_PG0_1000X_STAT1_SPEED_MASK) {
		case BRGPHY_5708S_PG0_1000X_STAT1_SPEED_10:
			mii->mii_media_active |= IFM_10_FL;
			break;
		case BRGPHY_5708S_PG0_1000X_STAT1_SPEED_100:
			mii->mii_media_active |= IFM_100_FX;
			break;
		case BRGPHY_5708S_PG0_1000X_STAT1_SPEED_1G:
			mii->mii_media_active |= IFM_1000_SX;
			break;
		case BRGPHY_5708S_PG0_1000X_STAT1_SPEED_25G:
			mii->mii_media_active |= IFM_2500_SX;
			break;
		}

		if (xstat & BRGPHY_5708S_PG0_1000X_STAT1_FDX)
			mii->mii_media_active |= IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;

		if (mii->mii_media_active & IFM_FDX) {
			if (xstat & BRGPHY_5708S_PG0_1000X_STAT1_TX_PAUSE)
				mii->mii_media_active
				    |= IFM_FLOW | IFM_ETH_TXPAUSE;
			if (xstat & BRGPHY_5708S_PG0_1000X_STAT1_RX_PAUSE)
				mii->mii_media_active
				    |= IFM_FLOW | IFM_ETH_RXPAUSE;
		}
	} else
		mii->mii_media_active = ife->ifm_media;
}

static void
brgphy_5709s_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t bmcr, bmsr, auxsts;

	KASSERT(mii_locked(mii));

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	PHY_READ(sc, MII_BMSR, &bmsr);
	PHY_READ(sc, MII_BMSR, &bmsr);
	if (bmsr & BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	PHY_READ(sc, MII_BMCR, &bmcr);
	if (bmcr & BMCR_ISO) {
		mii->mii_media_active |= IFM_NONE;
		mii->mii_media_status = 0;
		return;
	}

	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & BMCR_AUTOEN) {
		/*
		 * The media status bits are only valid of autonegotiation
		 * has completed (or it's disabled).
		 */
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		/* 5709S has its own general purpose status registers */
		PHY_WRITE(sc, BRGPHY_BLOCK_ADDR, BRGPHY_BLOCK_ADDR_GP_STATUS);
		PHY_READ(sc, BRGPHY_GP_STATUS_TOP_ANEG_STATUS, &auxsts);

		PHY_WRITE(sc, BRGPHY_BLOCK_ADDR,
		    BRGPHY_BLOCK_ADDR_COMBO_IEEE0);

		switch (auxsts & BRGPHY_GP_STATUS_TOP_ANEG_SPEED_MASK) {
		case BRGPHY_GP_STATUS_TOP_ANEG_SPEED_10:
			mii->mii_media_active |= IFM_10_FL;
			break;
		case BRGPHY_GP_STATUS_TOP_ANEG_SPEED_100:
			mii->mii_media_active |= IFM_100_FX;
			break;
		case BRGPHY_GP_STATUS_TOP_ANEG_SPEED_1G:
			mii->mii_media_active |= IFM_1000_SX;
			break;
		case BRGPHY_GP_STATUS_TOP_ANEG_SPEED_25G:
			mii->mii_media_active |= IFM_2500_SX;
			break;
		default:
			mii->mii_media_active |= IFM_NONE;
			mii->mii_media_status = 0;
			break;
		}

		if (auxsts & BRGPHY_GP_STATUS_TOP_ANEG_FDX)
			mii->mii_media_active |= IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;

		if (mii->mii_media_active & IFM_FDX)
			mii->mii_media_active |= mii_phy_flowstatus(sc);
	} else
		mii->mii_media_active = ife->ifm_media;
}

static int
brgphy_mii_phy_auto(struct mii_softc *sc)
{
	uint16_t anar, ktcr = 0;

	KASSERT(mii_locked(sc->mii_pdata));

	sc->mii_ticks = 0;
	brgphy_loop(sc);
	PHY_RESET(sc);

	if (sc->mii_flags & MIIF_HAVEFIBER) {
		anar = ANAR_X_FD | ANAR_X_HD;
		if (sc->mii_flags & MIIF_DOPAUSE)
			anar |= ANAR_X_PAUSE_TOWARDS;
	} else {
		anar = BMSR_MEDIA_TO_ANAR(sc->mii_capabilities) | ANAR_CSMA;
		if (sc->mii_flags & MIIF_DOPAUSE)
			anar |= ANAR_FC | ANAR_PAUSE_ASYM;
		ktcr = GTCR_ADV_1000TFDX | GTCR_ADV_1000THDX;
		if ((sc->mii_mpd_oui == MII_OUI_BROADCOM)
		    && (sc->mii_mpd_model == MII_MODEL_BROADCOM_BCM5701))
			ktcr |= GTCR_MAN_MS | GTCR_ADV_MS;
		PHY_WRITE(sc, MII_100T2CR, ktcr);
	}
	PHY_WRITE(sc, MII_ANAR, anar);

	/* Start autonegotiation */
	PHY_WRITE(sc, MII_BMCR, BMCR_AUTOEN | BMCR_STARTNEG);
	PHY_WRITE(sc, BRGPHY_MII_IMR, 0xFF00);

	return EJUSTRETURN;
}

static void
brgphy_loop(struct mii_softc *sc)
{
	uint16_t bmsr;
	int i;

	KASSERT(mii_locked(sc->mii_pdata));

	PHY_WRITE(sc, MII_BMCR, BMCR_LOOP);
	for (i = 0; i < 15000; i++) {
		PHY_READ(sc, MII_BMSR, &bmsr);
		if (!(bmsr & BMSR_LINK))
			break;
		DELAY(10);
	}
}

static void
brgphy_reset(struct mii_softc *sc)
{
	struct brgphy_softc *bsc = device_private(sc->mii_dev);
	uint16_t reg;

	KASSERT(mii_locked(sc->mii_pdata));

	mii_phy_reset(sc);
	switch (sc->mii_mpd_oui) {
	case MII_OUI_BROADCOM:
		switch (sc->mii_mpd_model) {
		case MII_MODEL_BROADCOM_BCM5400:
			brgphy_bcm5401_dspcode(sc);
			break;
		case MII_MODEL_BROADCOM_BCM5401:
			if (sc->mii_mpd_rev == 1 || sc->mii_mpd_rev == 3)
				brgphy_bcm5401_dspcode(sc);
			break;
		case MII_MODEL_BROADCOM_BCM5411:
			brgphy_bcm5411_dspcode(sc);
			break;
		case MII_MODEL_BROADCOM_BCM5421:
			brgphy_bcm5421_dspcode(sc);
			break;
		case MII_MODEL_BROADCOM_BCM54K2:
			brgphy_bcm54k2_dspcode(sc);
			break;
		}
		break;
	case MII_OUI_BROADCOM3:
		switch (sc->mii_mpd_model) {
		case MII_MODEL_BROADCOM3_BCM5717C:
		case MII_MODEL_BROADCOM3_BCM5719C:
		case MII_MODEL_BROADCOM3_BCM5720C:
		case MII_MODEL_BROADCOM3_BCM57765:
			return;
		}
		break;
	default:
		break;
	}

	/* Handle any bge (NetXtreme/NetLink) workarounds. */
	if (bsc->sc_isbge) {
		if (!(sc->mii_flags & MIIF_HAVEFIBER)) {

			if (bsc->sc_phyflags & BGEPHYF_ADC_BUG)
				brgphy_adc_bug(sc);
			if (bsc->sc_phyflags & BGEPHYF_5704_A0_BUG)
				brgphy_5704_a0_bug(sc);
			if (bsc->sc_phyflags & BGEPHYF_BER_BUG)
				brgphy_ber_bug(sc);
			else if (bsc->sc_phyflags & BGEPHYF_JITTER_BUG) {
				PHY_WRITE(sc, BRGPHY_MII_AUXCTL, 0x0c00);
				PHY_WRITE(sc, BRGPHY_MII_DSP_ADDR_REG, 0x000a);

				if (bsc->sc_phyflags
				    & BGEPHYF_ADJUST_TRIM) {
					PHY_WRITE(sc, BRGPHY_MII_DSP_RW_PORT,
					    0x110b);
					PHY_WRITE(sc, BRGPHY_TEST1,
					    BRGPHY_TEST1_TRIM_EN | 0x4);
				} else {
					PHY_WRITE(sc, BRGPHY_MII_DSP_RW_PORT,
					    0x010b);
				}

				PHY_WRITE(sc, BRGPHY_MII_AUXCTL, 0x0400);
			}
			if (bsc->sc_phyflags & BGEPHYF_CRC_BUG)
				brgphy_crc_bug(sc);

			/* Set Jumbo frame settings in the PHY. */
			if (bsc->sc_phyflags & BGEPHYF_JUMBO_CAPABLE)
				brgphy_jumbo_settings(sc);

			/* Adjust output voltage */
			if ((sc->mii_mpd_oui == MII_OUI_BROADCOM2)
			    && (sc->mii_mpd_model == MII_MODEL_BROADCOM2_BCM5906))
				PHY_WRITE(sc, BRGPHY_MII_EPHY_PTEST, 0x12);

			/* Enable Ethernet@Wirespeed */
			if (!(bsc->sc_phyflags & BGEPHYF_NO_WIRESPEED))
				brgphy_eth_wirespeed(sc);

#if 0
			/* Enable Link LED on Dell boxes */
			if (bsc->sc_phyflags & BGEPHYF_NO_3LED) {
				PHY_READ(sc, BRGPHY_MII_PHY_EXTCTL, &reg);
				PHY_WRITE(sc, BRGPHY_MII_PHY_EXTCTL,
				    reg & ~BRGPHY_PHY_EXTCTL_3_LED);
			}
#endif
		}
	/* Handle any bnx (NetXtreme II) workarounds. */
	} else if (bsc->sc_isbnx) {
		uint32_t chip_num = _BNX_CHIP_NUM(bsc->sc_chipid);
		uint32_t chip_id = _BNX_CHIP_ID(bsc->sc_chipid);
		uint32_t chip_rev = _BNX_CHIP_REV(bsc->sc_chipid);

		if ((sc->mii_mpd_oui == MII_OUI_BROADCOM2)
		    && sc->mii_mpd_model == MII_MODEL_BROADCOM2_BCM5708S) {
			/*
			 * Store autoneg capabilities/results in digital block
			 * (Page 0)
			 */
			PHY_WRITE(sc, BRGPHY_5708S_BLOCK_ADDR,
			    BRGPHY_5708S_DIG3_PG2);
			PHY_WRITE(sc, BRGPHY_5708S_PG2_DIGCTL_3_0,
			    BRGPHY_5708S_PG2_DIGCTL_3_0_USE_IEEE);
			PHY_WRITE(sc, BRGPHY_5708S_BLOCK_ADDR,
			    BRGPHY_5708S_DIG_PG0);

			/* Enable fiber mode and autodetection */
			PHY_READ(sc, BRGPHY_5708S_PG0_1000X_CTL1, &reg);
			PHY_WRITE(sc, BRGPHY_5708S_PG0_1000X_CTL1, reg |
			    BRGPHY_5708S_PG0_1000X_CTL1_AUTODET_EN |
			    BRGPHY_5708S_PG0_1000X_CTL1_FIBER_MODE);

			/* Enable parallel detection */
			PHY_READ(sc, BRGPHY_5708S_PG0_1000X_CTL2, &reg);
			PHY_WRITE(sc, BRGPHY_5708S_PG0_1000X_CTL2,
			    reg | BRGPHY_5708S_PG0_1000X_CTL2_PAR_DET_EN);

			/*
			 * Advertise 2.5G support through next page during
			 * autoneg
			 */
			if (bsc->sc_phyflags & BNX_PHY_2_5G_CAPABLE_FLAG) {
				PHY_READ(sc, BRGPHY_5708S_ANEG_NXT_PG_XMIT1,
				    &reg);
				PHY_WRITE(sc, BRGPHY_5708S_ANEG_NXT_PG_XMIT1,
				    reg | BRGPHY_5708S_ANEG_NXT_PG_XMIT1_25G);
			}

			/* Increase TX signal amplitude */
			if ((chip_id == BNX_CHIP_ID_5708_A0) ||
			    (chip_id == BNX_CHIP_ID_5708_B0) ||
			    (chip_id == BNX_CHIP_ID_5708_B1)) {
				PHY_WRITE(sc, BRGPHY_5708S_BLOCK_ADDR,
					BRGPHY_5708S_TX_MISC_PG5);
				PHY_READ(sc, BRGPHY_5708S_PG5_TXACTL1, &reg);
				PHY_WRITE(sc, BRGPHY_5708S_PG5_TXACTL1,
				    reg & ~BRGPHY_5708S_PG5_TXACTL1_VCM);
				PHY_WRITE(sc, BRGPHY_5708S_BLOCK_ADDR,
					BRGPHY_5708S_DIG_PG0);
			}

			/*
			 * Backplanes use special
			 * driver/pre-driver/pre-emphasis values.
			 */
			if ((bsc->sc_shared_hwcfg & BNX_SHARED_HW_CFG_PHY_BACKPLANE) &&
			    (bsc->sc_port_hwcfg & BNX_PORT_HW_CFG_CFG_TXCTL3_MASK)) {
				PHY_WRITE(sc, BRGPHY_5708S_BLOCK_ADDR,
				    BRGPHY_5708S_TX_MISC_PG5);
				PHY_WRITE(sc, BRGPHY_5708S_PG5_TXACTL3,
				    bsc->sc_port_hwcfg &
				    BNX_PORT_HW_CFG_CFG_TXCTL3_MASK);
				PHY_WRITE(sc, BRGPHY_5708S_BLOCK_ADDR,
				    BRGPHY_5708S_DIG_PG0);
			}
		} else if ((sc->mii_mpd_oui == MII_OUI_BROADCOM2)
		    && (sc->mii_mpd_model ==  MII_MODEL_BROADCOM2_BCM5709S)) {
			/* Select the SerDes Digital block of the AN MMD. */
			PHY_WRITE(sc, BRGPHY_BLOCK_ADDR,
			    BRGPHY_BLOCK_ADDR_SERDES_DIG);

			PHY_READ(sc, BRGPHY_SERDES_DIG_1000X_CTL1, &reg);
			PHY_WRITE(sc, BRGPHY_SERDES_DIG_1000X_CTL1,
			    (reg & ~BRGPHY_SD_DIG_1000X_CTL1_AUTODET) |
			    BRGPHY_SD_DIG_1000X_CTL1_FIBER);

			if (bsc->sc_phyflags & BNX_PHY_2_5G_CAPABLE_FLAG) {
				/* Select the Over 1G block of the AN MMD. */
				PHY_WRITE(sc, BRGPHY_BLOCK_ADDR,
				    BRGPHY_BLOCK_ADDR_OVER_1G);

				/*
				 * Enable autoneg "Next Page" to advertise
				 * 2.5G support.
				 */
				PHY_READ(sc, BRGPHY_OVER_1G_UNFORMAT_PG1,
				    &reg);
				PHY_WRITE(sc, BRGPHY_OVER_1G_UNFORMAT_PG1,
				    reg | BRGPHY_5708S_ANEG_NXT_PG_XMIT1_25G);
			}

			/*
			 * Select the Multi-Rate Backplane Ethernet block of
			 * the AN MMD.
			 */
			PHY_WRITE(sc, BRGPHY_BLOCK_ADDR,
			    BRGPHY_BLOCK_ADDR_MRBE);

			/* Enable MRBE speed autoneg. */
			PHY_READ(sc, BRGPHY_MRBE_MSG_PG5_NP, &reg);
			PHY_WRITE(sc, BRGPHY_MRBE_MSG_PG5_NP,
			    reg | BRGPHY_MRBE_MSG_PG5_NP_MBRE |
			    BRGPHY_MRBE_MSG_PG5_NP_T2);

			/* Select the Clause 73 User B0 block of the AN MMD. */
			PHY_WRITE(sc, BRGPHY_BLOCK_ADDR,
			    BRGPHY_BLOCK_ADDR_CL73_USER_B0);

			/* Enable MRBE speed autoneg. */
			PHY_WRITE(sc, BRGPHY_CL73_USER_B0_MBRE_CTL1,
			    BRGPHY_CL73_USER_B0_MBRE_CTL1_NP_AFT_BP |
			    BRGPHY_CL73_USER_B0_MBRE_CTL1_STA_MGR |
			    BRGPHY_CL73_USER_B0_MBRE_CTL1_ANEG);

			PHY_WRITE(sc, BRGPHY_BLOCK_ADDR,
			    BRGPHY_BLOCK_ADDR_COMBO_IEEE0);

		} else if (chip_num == BNX_CHIP_NUM_5709) {
			if ((chip_rev == BNX_CHIP_REV_Ax) ||
			    (chip_rev == BNX_CHIP_REV_Bx))
				brgphy_disable_early_dac(sc);

			/* Set Jumbo frame settings in the PHY. */
			brgphy_jumbo_settings(sc);

			/* Enable Ethernet@Wirespeed */
			brgphy_eth_wirespeed(sc);
		} else {
			if (!(sc->mii_flags & MIIF_HAVEFIBER)) {
				brgphy_ber_bug(sc);

				/* Set Jumbo frame settings in the PHY. */
				brgphy_jumbo_settings(sc);

				/* Enable Ethernet@Wirespeed */
				brgphy_eth_wirespeed(sc);
			}
		}
	}
}

/* Turn off tap power management on 5401. */
static void
brgphy_bcm5401_dspcode(struct mii_softc *sc)
{
	static const struct {
		int		reg;
		uint16_t	val;
	} dspcode[] = {
		{ BRGPHY_MII_AUXCTL,		0x0c20 },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x0012 },
		{ BRGPHY_MII_DSP_RW_PORT,	0x1804 },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x0013 },
		{ BRGPHY_MII_DSP_RW_PORT,	0x1204 },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x8006 },
		{ BRGPHY_MII_DSP_RW_PORT,	0x0132 },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x8006 },
		{ BRGPHY_MII_DSP_RW_PORT,	0x0232 },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x201f },
		{ BRGPHY_MII_DSP_RW_PORT,	0x0a20 },
		{ 0,				0 },
	};
	int i;

	for (i = 0; dspcode[i].reg != 0; i++)
		PHY_WRITE(sc, dspcode[i].reg, dspcode[i].val);
	delay(40);
}

static void
brgphy_bcm5411_dspcode(struct mii_softc *sc)
{
	static const struct {
		int		reg;
		uint16_t	val;
	} dspcode[] = {
		{ 0x1c,				0x8c23 },
		{ 0x1c,				0x8ca3 },
		{ 0x1c,				0x8c23 },
		{ 0,				0 },
	};
	int i;

	for (i = 0; dspcode[i].reg != 0; i++)
		PHY_WRITE(sc, dspcode[i].reg, dspcode[i].val);
}

static void
brgphy_bcm5421_dspcode(struct mii_softc *sc)
{
	uint16_t data;

	/* Set Class A mode */
	PHY_WRITE(sc, BRGPHY_MII_AUXCTL, 0x1007);
	PHY_READ(sc, BRGPHY_MII_AUXCTL, &data);
	PHY_WRITE(sc, BRGPHY_MII_AUXCTL, data | 0x0400);

	/* Set FFE gamma override to -0.125 */
	PHY_WRITE(sc, BRGPHY_MII_AUXCTL, 0x0007);
	PHY_READ(sc, BRGPHY_MII_AUXCTL, &data);
	PHY_WRITE(sc, BRGPHY_MII_AUXCTL, data | 0x0800);
	PHY_WRITE(sc, BRGPHY_MII_DSP_ADDR_REG, 0x000a);
	PHY_READ(sc, BRGPHY_MII_DSP_RW_PORT, &data);
	PHY_WRITE(sc, BRGPHY_MII_DSP_RW_PORT, data | 0x0200);
}

static void
brgphy_bcm54k2_dspcode(struct mii_softc *sc)
{
	static const struct {
		int		reg;
		uint16_t	val;
	} dspcode[] = {
		{ 4,				0x01e1 },
		{ 9,				0x0300 },
		{ 0,				0 },
	};
	int i;

	for (i = 0; dspcode[i].reg != 0; i++)
		PHY_WRITE(sc, dspcode[i].reg, dspcode[i].val);
}

static void
brgphy_adc_bug(struct mii_softc *sc)
{
	static const struct {
		int		reg;
		uint16_t	val;
	} dspcode[] = {
		{ BRGPHY_MII_AUXCTL,		0x0c00 },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x201f },
		{ BRGPHY_MII_DSP_RW_PORT,	0x2aaa },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x000a },
		{ BRGPHY_MII_DSP_RW_PORT,	0x0323 },
		{ BRGPHY_MII_AUXCTL,		0x0400 },
		{ 0,				0 },
	};
	int i;

	for (i = 0; dspcode[i].reg != 0; i++)
		PHY_WRITE(sc, dspcode[i].reg, dspcode[i].val);
}

static void
brgphy_5704_a0_bug(struct mii_softc *sc)
{
	static const struct {
		int		reg;
		uint16_t	val;
	} dspcode[] = {
		{ 0x1c,				0x8d68 },
		{ 0x1c,				0x8d68 },
		{ 0,				0 },
	};
	int i;

	for (i = 0; dspcode[i].reg != 0; i++)
		PHY_WRITE(sc, dspcode[i].reg, dspcode[i].val);
}

static void
brgphy_ber_bug(struct mii_softc *sc)
{
	static const struct {
		int		reg;
		uint16_t	val;
	} dspcode[] = {
		{ BRGPHY_MII_AUXCTL,		0x0c00 },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x000a },
		{ BRGPHY_MII_DSP_RW_PORT,	0x310b },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x201f },
		{ BRGPHY_MII_DSP_RW_PORT,	0x9506 },
		{ BRGPHY_MII_DSP_ADDR_REG,	0x401f },
		{ BRGPHY_MII_DSP_RW_PORT,	0x14e2 },
		{ BRGPHY_MII_AUXCTL,		0x0400 },
		{ 0,				0 },
	};
	int i;

	for (i = 0; dspcode[i].reg != 0; i++)
		PHY_WRITE(sc, dspcode[i].reg, dspcode[i].val);
}

/* BCM5701 A0/B0 CRC bug workaround */
static void
brgphy_crc_bug(struct mii_softc *sc)
{
	static const struct {
		int		reg;
		uint16_t	val;
	} dspcode[] = {
		{ BRGPHY_MII_DSP_ADDR_REG,	0x0a75 },
		{ 0x1c,				0x8c68 },
		{ 0x1c,				0x8d68 },
		{ 0x1c,				0x8c68 },
		{ 0,				0 },
	};
	int i;

	for (i = 0; dspcode[i].reg != 0; i++)
		PHY_WRITE(sc, dspcode[i].reg, dspcode[i].val);
}

static void
brgphy_disable_early_dac(struct mii_softc *sc)
{
	uint16_t val;

	PHY_WRITE(sc, BRGPHY_MII_DSP_ADDR_REG, 0x0f08);
	PHY_READ(sc, BRGPHY_MII_DSP_RW_PORT, &val);
	val &= ~(1 << 8);
	PHY_WRITE(sc, BRGPHY_MII_DSP_RW_PORT, val);

}

static void
brgphy_jumbo_settings(struct mii_softc *sc)
{
	uint16_t val;

	/* Set Jumbo frame settings in the PHY. */
	if ((sc->mii_mpd_oui == MII_OUI_BROADCOM)
	    && (sc->mii_mpd_model == MII_MODEL_BROADCOM_BCM5401)) {
		/* Cannot do read-modify-write on the BCM5401 */
		PHY_WRITE(sc, BRGPHY_MII_AUXCTL, 0x4c20);
	} else {
		PHY_WRITE(sc, BRGPHY_MII_AUXCTL, 0x7);
		PHY_READ(sc, BRGPHY_MII_AUXCTL, &val);
		PHY_WRITE(sc, BRGPHY_MII_AUXCTL,
		    val & ~(BRGPHY_AUXCTL_LONG_PKT | 0x7));
	}

	PHY_READ(sc, BRGPHY_MII_PHY_EXTCTL, &val);
	PHY_WRITE(sc, BRGPHY_MII_PHY_EXTCTL, val & ~BRGPHY_PHY_EXTCTL_HIGH_LA);
}

static void
brgphy_eth_wirespeed(struct mii_softc *sc)
{
	uint16_t val;

	/* Enable Ethernet@Wirespeed */
	PHY_WRITE(sc, BRGPHY_MII_AUXCTL, 0x7007);
	PHY_READ(sc, BRGPHY_MII_AUXCTL, &val);
	PHY_WRITE(sc, BRGPHY_MII_AUXCTL, val | (1 << 15) | (1 << 4));
}

static void
brgphy_bcm54xx_clock_delay(struct mii_softc *sc)
{
	uint16_t val;

	PHY_WRITE(sc, BRGPHY_MII_AUXCTL, BRGPHY_AUXCTL_SHADOW_MISC |
	    BRGPHY_AUXCTL_SHADOW_MISC << BRGPHY_AUXCTL_MISC_READ_SHIFT);
	PHY_READ(sc, BRGPHY_MII_AUXCTL, &val);
	val &= BRGPHY_AUXCTL_MISC_DATA_MASK;
	if (sc->mii_flags & MIIF_RXID)
		val |= BRGPHY_AUXCTL_MISC_RGMII_SKEW_EN;
	else
		val &= ~BRGPHY_AUXCTL_MISC_RGMII_SKEW_EN;
	PHY_WRITE(sc, BRGPHY_MII_AUXCTL, BRGPHY_AUXCTL_MISC_WRITE_EN |
	    BRGPHY_AUXCTL_SHADOW_MISC | val);

	PHY_WRITE(sc, BRGPHY_MII_SHADOW_1C, BRGPHY_SHADOW_1C_CLK_CTRL);
	PHY_READ(sc, BRGPHY_MII_SHADOW_1C, &val);
	val &= BRGPHY_SHADOW_1C_DATA_MASK;
	if (sc->mii_flags & MIIF_TXID)
		val |= BRGPHY_SHADOW_1C_GTXCLK_EN;
	else
		val &= ~BRGPHY_SHADOW_1C_GTXCLK_EN;
	PHY_WRITE(sc, BRGPHY_MII_SHADOW_1C, BRGPHY_SHADOW_1C_WRITE_EN |
	    BRGPHY_SHADOW_1C_CLK_CTRL | val);
}

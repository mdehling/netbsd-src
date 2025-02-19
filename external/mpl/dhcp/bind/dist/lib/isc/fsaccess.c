/*	$NetBSD: fsaccess.c,v 1.1.2.2 2024/02/24 13:07:19 martin Exp $	*/

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

/*! \file
 * \brief
 * This file contains the OS-independent functionality of the API.
 */
#include <stdbool.h>

#include <isc/fsaccess.h>
#include <isc/print.h>
#include <isc/result.h>
#include <isc/util.h>

/*!
 * Shorthand.  Maybe ISC__FSACCESS_PERMISSIONBITS should not even be in
 * <isc/fsaccess.h>.  Could check consistency with sizeof(isc_fsaccess_t)
 * and the number of bits in each function.
 */
#define STEP  (ISC__FSACCESS_PERMISSIONBITS)
#define GROUP (STEP)
#define OTHER (STEP * 2)

void
isc_fsaccess_add(int trustee, int permission, isc_fsaccess_t *access) {
	REQUIRE(trustee <= 0x7);
	REQUIRE(permission <= 0xFF);

	if ((trustee & ISC_FSACCESS_OWNER) != 0) {
		*access |= permission;
	}

	if ((trustee & ISC_FSACCESS_GROUP) != 0) {
		*access |= (permission << GROUP);
	}

	if ((trustee & ISC_FSACCESS_OTHER) != 0) {
		*access |= (permission << OTHER);
	}
}

void
isc_fsaccess_remove(int trustee, int permission, isc_fsaccess_t *access) {
	REQUIRE(trustee <= 0x7);
	REQUIRE(permission <= 0xFF);

	if ((trustee & ISC_FSACCESS_OWNER) != 0) {
		*access &= ~permission;
	}

	if ((trustee & ISC_FSACCESS_GROUP) != 0) {
		*access &= ~(permission << GROUP);
	}

	if ((trustee & ISC_FSACCESS_OTHER) != 0) {
		*access &= ~(permission << OTHER);
	}
}

static isc_result_t
check_bad_bits(isc_fsaccess_t access, bool is_dir) {
	isc_fsaccess_t bits;

	/*
	 * Check for disallowed user bits.
	 */
	if (is_dir) {
		bits = ISC_FSACCESS_READ | ISC_FSACCESS_WRITE |
		       ISC_FSACCESS_EXECUTE;
	} else {
		bits = ISC_FSACCESS_CREATECHILD | ISC_FSACCESS_ACCESSCHILD |
		       ISC_FSACCESS_DELETECHILD | ISC_FSACCESS_LISTDIRECTORY;
	}

	/*
	 * Set group bad bits.
	 */
	bits |= bits << STEP;
	/*
	 * Set other bad bits.
	 */
	bits |= bits << STEP;

	if ((access & bits) != 0) {
		if (is_dir) {
			return (ISC_R_NOTFILE);
		} else {
			return (ISC_R_NOTDIRECTORY);
		}
	}

	return (ISC_R_SUCCESS);
}

/*	$NetBSD: bufferlist.c,v 1.1.2.2 2024/02/24 13:07:19 martin Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

/*! \file */

#include <stddef.h>

#include <isc/buffer.h>
#include <isc/bufferlist.h>
#include <isc/util.h>

unsigned int
isc_bufferlist_usedcount(isc_bufferlist_t *bl) {
	isc_buffer_t *buffer;
	unsigned int length;

	REQUIRE(bl != NULL);

	length = 0;
	buffer = ISC_LIST_HEAD(*bl);
	while (buffer != NULL) {
		REQUIRE(ISC_BUFFER_VALID(buffer));
		length += isc_buffer_usedlength(buffer);
		buffer = ISC_LIST_NEXT(buffer, link);
	}

	return (length);
}

unsigned int
isc_bufferlist_availablecount(isc_bufferlist_t *bl) {
	isc_buffer_t *buffer;
	unsigned int length;

	REQUIRE(bl != NULL);

	length = 0;
	buffer = ISC_LIST_HEAD(*bl);
	while (buffer != NULL) {
		REQUIRE(ISC_BUFFER_VALID(buffer));
		length += isc_buffer_availablelength(buffer);
		buffer = ISC_LIST_NEXT(buffer, link);
	}

	return (length);
}

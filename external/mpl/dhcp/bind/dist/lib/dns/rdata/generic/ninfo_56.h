/*	$NetBSD: ninfo_56.h,v 1.1.2.2 2024/02/24 13:07:13 martin Exp $	*/

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

/* */
#ifndef GENERIC_NINFO_56_H
#define GENERIC_NINFO_56_H 1

typedef struct dns_rdata_txt_string dns_rdata_ninfo_string_t;

typedef struct dns_rdata_txt dns_rdata_ninfo_t;

/*
 * ISC_LANG_BEGINDECLS and ISC_LANG_ENDDECLS are already done
 * via rdatastructpre.h and rdatastructsuf.h.
 */

isc_result_t
dns_rdata_ninfo_first(dns_rdata_ninfo_t *);

isc_result_t
dns_rdata_ninfo_next(dns_rdata_ninfo_t *);

isc_result_t
dns_rdata_ninfo_current(dns_rdata_ninfo_t *, dns_rdata_ninfo_string_t *);

#endif /* GENERIC_NINFO_16_H */

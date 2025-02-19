/*	$NetBSD: mutex.c,v 1.1.2.2 2024/02/24 13:07:29 martin Exp $	*/

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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include <isc/mutex.h>
#include <isc/once.h>
#include <isc/print.h>
#include <isc/strerr.h>
#include <isc/string.h>
#include <isc/util.h>

#if ISC_MUTEX_PROFILE

/*@{*/
/*% Operations on timevals; adapted from FreeBSD's sys/time.h */
#define timevalclear(tvp) ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timevaladd(vvp, uvp)                       \
	do {                                       \
		(vvp)->tv_sec += (uvp)->tv_sec;    \
		(vvp)->tv_usec += (uvp)->tv_usec;  \
		if ((vvp)->tv_usec >= 1000000) {   \
			(vvp)->tv_sec++;           \
			(vvp)->tv_usec -= 1000000; \
		}                                  \
	} while (0)
#define timevalsub(vvp, uvp)                       \
	do {                                       \
		(vvp)->tv_sec -= (uvp)->tv_sec;    \
		(vvp)->tv_usec -= (uvp)->tv_usec;  \
		if ((vvp)->tv_usec < 0) {          \
			(vvp)->tv_sec--;           \
			(vvp)->tv_usec += 1000000; \
		}                                  \
	} while (0)

/*@}*/

#define ISC_MUTEX_MAX_LOCKERS 32

typedef struct {
	const char *file;
	int line;
	unsigned count;
	struct timeval locked_total;
	struct timeval wait_total;
} isc_mutexlocker_t;

struct isc_mutexstats {
	const char *file; /*%< File mutex was created in. */
	int line;	  /*%< Line mutex was created on. */
	unsigned count;
	struct timeval lock_t;
	struct timeval locked_total;
	struct timeval wait_total;
	isc_mutexlocker_t *cur_locker;
	isc_mutexlocker_t lockers[ISC_MUTEX_MAX_LOCKERS];
};

#ifndef ISC_MUTEX_PROFTABLESIZE
#define ISC_MUTEX_PROFTABLESIZE (1024 * 1024)
#endif /* ifndef ISC_MUTEX_PROFTABLESIZE */
static isc_mutexstats_t stats[ISC_MUTEX_PROFTABLESIZE];
static int stats_next = 0;
static bool stats_init = false;
static pthread_mutex_t statslock = PTHREAD_MUTEX_INITIALIZER;

void
isc_mutex_init_profile(isc_mutex_t *mp, const char *file, int line) {
	int i, err;

	err = pthread_mutex_init(&mp->mutex, NULL);
	if (err != 0) {
		strerror_r(err, strbuf, sizeof(strbuf));
		isc_error_fatal(file, line, "pthread_mutex_init failed: %s",
				strbuf);
	}

	RUNTIME_CHECK(pthread_mutex_lock(&statslock) == 0);

	if (!stats_init) {
		stats_init = true;
	}

	/*
	 * If all statistics entries have been used, give up and trigger an
	 * assertion failure.  There would be no other way to deal with this
	 * because we'd like to keep record of all locks for the purpose of
	 * debugging and the number of necessary locks is unpredictable.
	 * If this failure is triggered while debugging, named should be
	 * rebuilt with an increased ISC_MUTEX_PROFTABLESIZE.
	 */
	RUNTIME_CHECK(stats_next < ISC_MUTEX_PROFTABLESIZE);
	mp->stats = &stats[stats_next++];

	RUNTIME_CHECK(pthread_mutex_unlock(&statslock) == 0);

	mp->stats->file = file;
	mp->stats->line = line;
	mp->stats->count = 0;
	timevalclear(&mp->stats->locked_total);
	timevalclear(&mp->stats->wait_total);
	for (i = 0; i < ISC_MUTEX_MAX_LOCKERS; i++) {
		mp->stats->lockers[i].file = NULL;
		mp->stats->lockers[i].line = 0;
		mp->stats->lockers[i].count = 0;
		timevalclear(&mp->stats->lockers[i].locked_total);
		timevalclear(&mp->stats->lockers[i].wait_total);
	}
}

isc_result_t
isc_mutex_lock_profile(isc_mutex_t *mp, const char *file, int line) {
	struct timeval prelock_t;
	struct timeval postlock_t;
	isc_mutexlocker_t *locker = NULL;
	int i;

	gettimeofday(&prelock_t, NULL);

	if (pthread_mutex_lock(&mp->mutex) != 0) {
		return (ISC_R_UNEXPECTED);
	}

	gettimeofday(&postlock_t, NULL);
	mp->stats->lock_t = postlock_t;

	timevalsub(&postlock_t, &prelock_t);

	mp->stats->count++;
	timevaladd(&mp->stats->wait_total, &postlock_t);

	for (i = 0; i < ISC_MUTEX_MAX_LOCKERS; i++) {
		if (mp->stats->lockers[i].file == NULL) {
			locker = &mp->stats->lockers[i];
			locker->file = file;
			locker->line = line;
			break;
		} else if (mp->stats->lockers[i].file == file &&
			   mp->stats->lockers[i].line == line)
		{
			locker = &mp->stats->lockers[i];
			break;
		}
	}

	if (locker != NULL) {
		locker->count++;
		timevaladd(&locker->wait_total, &postlock_t);
	}

	mp->stats->cur_locker = locker;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_mutex_unlock_profile(isc_mutex_t *mp, const char *file, int line) {
	struct timeval unlock_t;

	UNUSED(file);
	UNUSED(line);

	if (mp->stats->cur_locker != NULL) {
		gettimeofday(&unlock_t, NULL);
		timevalsub(&unlock_t, &mp->stats->lock_t);
		timevaladd(&mp->stats->locked_total, &unlock_t);
		timevaladd(&mp->stats->cur_locker->locked_total, &unlock_t);
		mp->stats->cur_locker = NULL;
	}

	return ((pthread_mutex_unlock((&mp->mutex)) == 0) ? ISC_R_SUCCESS
							  : ISC_R_UNEXPECTED);
}

void
isc_mutex_statsprofile(FILE *fp) {
	isc_mutexlocker_t *locker;
	int i, j;

	fprintf(fp, "Mutex stats (in us)\n");
	for (i = 0; i < stats_next; i++) {
		fprintf(fp, "%-12s %4d: %10u  %lu.%06lu %lu.%06lu %5d\n",
			stats[i].file, stats[i].line, stats[i].count,
			stats[i].locked_total.tv_sec,
			stats[i].locked_total.tv_usec,
			stats[i].wait_total.tv_sec, stats[i].wait_total.tv_usec,
			i);
		for (j = 0; j < ISC_MUTEX_MAX_LOCKERS; j++) {
			locker = &stats[i].lockers[j];
			if (locker->file == NULL) {
				continue;
			}
			fprintf(fp,
				" %-11s %4d: %10u  %lu.%06lu %lu.%06lu %5d\n",
				locker->file, locker->line, locker->count,
				locker->locked_total.tv_sec,
				locker->locked_total.tv_usec,
				locker->wait_total.tv_sec,
				locker->wait_total.tv_usec, i);
		}
	}
}

#endif /* ISC_MUTEX_PROFILE */

#if ISC_MUTEX_DEBUG && defined(PTHREAD_MUTEX_ERRORCHECK)

static bool errcheck_initialized = false;
static pthread_mutexattr_t errcheck;
static isc_once_t once_errcheck = ISC_ONCE_INIT;

static void
initialize_errcheck(void) {
	RUNTIME_CHECK(pthread_mutexattr_init(&errcheck) == 0);
	RUNTIME_CHECK(pthread_mutexattr_settype(&errcheck,
						PTHREAD_MUTEX_ERRORCHECK) == 0);
	errcheck_initialized = true;
}

void
isc_mutex_init_errcheck(isc_mutex_t *mp) {
	isc_result_t result;
	int err;

	result = isc_once_do(&once_errcheck, initialize_errcheck);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);

	err = pthread_mutex_init(mp, &errcheck);
	if (err != 0) {
		strerror_r(err, strbuf, sizeof(strbuf));
		isc_error_fatal(file, line, "pthread_mutex_init failed: %s",
				strbuf);
	}
}
#endif /* if ISC_MUTEX_DEBUG && defined(PTHREAD_MUTEX_ERRORCHECK) */

#if ISC_MUTEX_DEBUG && defined(__NetBSD__) && defined(PTHREAD_MUTEX_ERRORCHECK)
pthread_mutexattr_t isc__mutex_attrs = {
	PTHREAD_MUTEX_ERRORCHECK, /* m_type */
	0			  /* m_flags, which appears to be unused. */
};
#endif /* if ISC_MUTEX_DEBUG && defined(__NetBSD__) && \
	* defined(PTHREAD_MUTEX_ERRORCHECK) */

#if !(ISC_MUTEX_DEBUG && defined(PTHREAD_MUTEX_ERRORCHECK)) && \
	!ISC_MUTEX_PROFILE

#ifdef HAVE_PTHREAD_MUTEX_ADAPTIVE_NP
static bool attr_initialized = false;
static pthread_mutexattr_t attr;
static isc_once_t once_attr = ISC_ONCE_INIT;
#endif /* HAVE_PTHREAD_MUTEX_ADAPTIVE_NP */

#ifdef HAVE_PTHREAD_MUTEX_ADAPTIVE_NP
static void
initialize_attr(void) {
	RUNTIME_CHECK(pthread_mutexattr_init(&attr) == 0);
	RUNTIME_CHECK(pthread_mutexattr_settype(
			      &attr, PTHREAD_MUTEX_ADAPTIVE_NP) == 0);
	attr_initialized = true;
}
#endif /* HAVE_PTHREAD_MUTEX_ADAPTIVE_NP */

void
isc__mutex_init(isc_mutex_t *mp, const char *file, unsigned int line) {
	int err;

#ifdef HAVE_PTHREAD_MUTEX_ADAPTIVE_NP
	isc_result_t result = ISC_R_SUCCESS;
	result = isc_once_do(&once_attr, initialize_attr);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);

	err = pthread_mutex_init(mp, &attr);
#else  /* HAVE_PTHREAD_MUTEX_ADAPTIVE_NP */
	err = pthread_mutex_init(mp, ISC__MUTEX_ATTRS);
#endif /* HAVE_PTHREAD_MUTEX_ADAPTIVE_NP */
	if (err != 0) {
		char strbuf[ISC_STRERRORSIZE];
		strerror_r(err, strbuf, sizeof(strbuf));
		isc_error_fatal(file, line, "pthread_mutex_init failed: %s",
				strbuf);
	}
}
#endif /* if !(ISC_MUTEX_DEBUG && defined(PTHREAD_MUTEX_ERRORCHECK)) && \
	* !ISC_MUTEX_PROFILE */

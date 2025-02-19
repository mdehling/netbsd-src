/*	$NetBSD: taskpool.h,v 1.1.2.2 2024/02/24 13:07:27 martin Exp $	*/

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

#ifndef ISC_TASKPOOL_H
#define ISC_TASKPOOL_H 1

/*****
***** Module Info
*****/

/*! \file isc/taskpool.h
 * \brief A task pool is a mechanism for sharing a small number of tasks
 * among a large number of objects such that each object is
 * assigned a unique task, but each task may be shared by several
 * objects.
 *
 * Task pools are used to let objects that can exist in large
 * numbers (e.g., zones) use tasks for synchronization without
 * the memory overhead and unfair scheduling competition that
 * could result from creating a separate task for each object.
 */

/***
 *** Imports.
 ***/

#include <stdbool.h>

#include <isc/lang.h>
#include <isc/task.h>

ISC_LANG_BEGINDECLS

/*****
***** Types.
*****/

typedef struct isc_taskpool isc_taskpool_t;

/*****
***** Functions.
*****/

isc_result_t
isc_taskpool_create(isc_taskmgr_t *tmgr, isc_mem_t *mctx, unsigned int ntasks,
		    unsigned int quantum, bool priv, isc_taskpool_t **poolp);
/*%<
 * Create a task pool of "ntasks" tasks, each with quantum
 * "quantum".
 *
 * Requires:
 *
 *\li	'tmgr' is a valid task manager.
 *
 *\li	'mctx' is a valid memory context.
 *
 *\li	poolp != NULL && *poolp == NULL
 *
 * Ensures:
 *
 *\li	On success, '*taskp' points to the new task pool.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY
 *\li	#ISC_R_UNEXPECTED
 */

void
isc_taskpool_gettask(isc_taskpool_t *pool, isc_task_t **targetp);
/*%<
 * Attach to a task from the pool.  Currently the next task is chosen
 * from the pool at random.  (This may be changed in the future to
 * something that guaratees balance.)
 */

int
isc_taskpool_size(isc_taskpool_t *pool);
/*%<
 * Returns the number of tasks in the task pool 'pool'.
 */

isc_result_t
isc_taskpool_expand(isc_taskpool_t **sourcep, unsigned int size, bool priv,
		    isc_taskpool_t **targetp);

/*%<
 * If 'size' is larger than the number of tasks in the pool pointed to by
 * 'sourcep', then a new taskpool of size 'size' is allocated, the existing
 * tasks from are moved into it, additional tasks are created to bring the
 * total number up to 'size', and the resulting pool is attached to
 * 'targetp'.
 *
 * If 'size' is less than or equal to the tasks in pool 'source', then
 * 'sourcep' is attached to 'targetp' without any other action being taken.
 *
 * In either case, 'sourcep' is detached.
 *
 * Requires:
 *
 * \li	'sourcep' is not NULL and '*source' is not NULL
 * \li	'targetp' is not NULL and '*source' is NULL
 *
 * Ensures:
 *
 * \li	On success, '*targetp' points to a valid task pool.
 * \li	On success, '*sourcep' points to NULL.
 *
 * Returns:
 *
 * \li	#ISC_R_SUCCESS
 * \li	#ISC_R_NOMEMORY
 */

void
isc_taskpool_destroy(isc_taskpool_t **poolp);
/*%<
 * Destroy a task pool.  The tasks in the pool are detached but not
 * shut down.
 *
 * Requires:
 * \li	'*poolp' is a valid task pool.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_TASKPOOL_H */

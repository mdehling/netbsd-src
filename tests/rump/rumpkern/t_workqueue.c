/*	$NetBSD: t_workqueue.c,v 1.2.16.1 2023/09/04 16:57:56 martin Exp $	*/

/*-
 * Copyright (c) 2017 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <rump/rump.h>

#include <atf-c.h>

#include "h_macros.h"
#include "../kernspace/kernspace.h"

ATF_TC(workqueue1);
ATF_TC_HEAD(workqueue1, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks workqueue basics");
}

ATF_TC_BODY(workqueue1, tc)
{

	rump_init();

	rump_schedule();
	rumptest_workqueue1(); /* panics if fails */
	rump_unschedule();
}

ATF_TC(workqueue_wait);
ATF_TC_HEAD(workqueue_wait, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks workqueue_wait");
}

ATF_TC_BODY(workqueue_wait, tc)
{

	rump_init();

	rump_schedule();
	rumptest_workqueue_wait(); /* panics if fails */
	rump_unschedule();
}

static void
sigsegv(int signo)
{
	atf_tc_fail("SIGSEGV");
}

ATF_TC(workqueue_wait_pause);
ATF_TC_HEAD(workqueue_wait_pause, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks workqueue_wait with pause");
}

ATF_TC_BODY(workqueue_wait_pause, tc)
{

	REQUIRE_LIBC(signal(SIGSEGV, &sigsegv), SIG_ERR);

	rump_init();

	rump_schedule();
	rumptest_workqueue_wait_pause(); /* panics or SIGSEGVs if fails */
	rump_unschedule();
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, workqueue1);
	ATF_TP_ADD_TC(tp, workqueue_wait);
	ATF_TP_ADD_TC(tp, workqueue_wait_pause);

	return atf_no_error();
}

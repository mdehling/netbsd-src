/*	$NetBSD: procfs_vfsops.c,v 1.111.4.1 2024/04/18 18:22:10 martin Exp $	*/

/*
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)procfs_vfsops.c	8.7 (Berkeley) 5/10/95
 */

/*
 * Copyright (c) 1993 Jan-Simon Pendry
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)procfs_vfsops.c	8.7 (Berkeley) 5/10/95
 */

/*
 * procfs VFS interface
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: procfs_vfsops.c,v 1.111.4.1 2024/04/18 18:22:10 martin Exp $");

#if defined(_KERNEL_OPT)
#include "opt_compat_netbsd.h"
#endif

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/fstrans.h>
#include <sys/kauth.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/vnode.h>

#include <miscfs/genfs/genfs.h>

#include <miscfs/procfs/procfs.h>

#include <uvm/uvm_extern.h>			/* for PAGE_SIZE */

MODULE(MODULE_CLASS_VFS, procfs, "ptrace_common");

VFS_PROTOS(procfs);

#define PROCFS_HASHSIZE	256
#define PROCFS_EXEC_HOOK ((void *)1)
#define PROCFS_EXIT_HOOK ((void *)2)

static kauth_listener_t procfs_listener;
static void *procfs_exechook;
static void *procfs_exithook;
LIST_HEAD(hashhead, pfsnode);
static u_long procfs_hashmask;
static struct hashhead *procfs_hashtab;
static kmutex_t procfs_hashlock;

static struct hashhead *
procfs_hashhead(pid_t pid)
{

	return &procfs_hashtab[pid & procfs_hashmask];
}

void
procfs_hashrem(struct pfsnode *pfs)
{

	mutex_enter(&procfs_hashlock);
	LIST_REMOVE(pfs, pfs_hash);
	mutex_exit(&procfs_hashlock);
}

/*
 * VFS Operations.
 *
 * mount system call
 */
/* ARGSUSED */
int
procfs_mount(
    struct mount *mp,
    const char *path,
    void *data,
    size_t *data_len)
{
	struct lwp *l = curlwp;
	struct procfsmount *pmnt;
	struct procfs_args *args = data;
	int error;

	if (args == NULL)
		return EINVAL;

	if (UIO_MX & (UIO_MX-1)) {
		log(LOG_ERR, "procfs: invalid directory entry size");
		return (EINVAL);
	}

	if (mp->mnt_flag & MNT_GETARGS) {
		if (*data_len < sizeof *args)
			return EINVAL;

		pmnt = VFSTOPROC(mp);
		if (pmnt == NULL)
			return EIO;
		args->version = PROCFS_ARGSVERSION;
		args->flags = pmnt->pmnt_flags;
		*data_len = sizeof *args;
		return 0;
	}

	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);

	if (*data_len >= sizeof *args && args->version != PROCFS_ARGSVERSION)
		return EINVAL;

	pmnt = kmem_zalloc(sizeof(struct procfsmount), KM_SLEEP);

	mp->mnt_stat.f_namemax = PROCFS_MAXNAMLEN;
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_data = pmnt;
	vfs_getnewfsid(mp);

	error = set_statvfs_info(path, UIO_USERSPACE, "procfs", UIO_SYSSPACE,
	    mp->mnt_op->vfs_name, mp, l);
	if (*data_len >= sizeof *args)
		pmnt->pmnt_flags = args->flags;
	else
		pmnt->pmnt_flags = 0;

	mp->mnt_iflag |= IMNT_MPSAFE | IMNT_SHRLOOKUP;
	return error;
}

/*
 * unmount system call
 */
int
procfs_unmount(struct mount *mp, int mntflags)
{
	int error;
	int flags = 0;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	if ((error = vflush(mp, 0, flags)) != 0)
		return (error);

	kmem_free(mp->mnt_data, sizeof(struct procfsmount));
	mp->mnt_data = NULL;

	return 0;
}

int
procfs_root(struct mount *mp, int lktype, struct vnode **vpp)
{
	int error;

	error = procfs_allocvp(mp, vpp, 0, PFSroot, -1);
	if (error == 0) {
		error = vn_lock(*vpp, lktype);
		if (error != 0) {
			vrele(*vpp);
			*vpp = NULL;
		}
	}

	return error;
}

/* ARGSUSED */
int
procfs_start(struct mount *mp, int flags)
{

	return (0);
}

/*
 * Get file system statistics.
 */
int
procfs_statvfs(struct mount *mp, struct statvfs *sbp)
{

	genfs_statvfs(mp, sbp);

	sbp->f_bsize = PAGE_SIZE;
	sbp->f_frsize = PAGE_SIZE;
	sbp->f_iosize = PAGE_SIZE;
	sbp->f_blocks = 1;
	sbp->f_files = maxproc;					/* approx */
	sbp->f_ffree = maxproc - atomic_load_relaxed(&nprocs);	/* approx */
	sbp->f_favail = maxproc - atomic_load_relaxed(&nprocs);	/* approx */

	return (0);
}

/*ARGSUSED*/
int
procfs_sync(
    struct mount *mp,
    int waitfor,
    kauth_cred_t uc)
{

	return (0);
}

/*ARGSUSED*/
int
procfs_vget(struct mount *mp, ino_t ino, int lktype,
    struct vnode **vpp)
{
	return (EOPNOTSUPP);
}

int
procfs_loadvnode(struct mount *mp, struct vnode *vp,
    const void *key, size_t key_len, const void **new_key)
{
	int error;
	struct pfskey pfskey;
	struct pfsnode *pfs;

	KASSERT(key_len == sizeof(pfskey));
	memcpy(&pfskey, key, key_len);

	pfs = kmem_alloc(sizeof(*pfs), KM_SLEEP);
	pfs->pfs_pid = pfskey.pk_pid;
	pfs->pfs_type = pfskey.pk_type;
	pfs->pfs_fd = pfskey.pk_fd;
	pfs->pfs_vnode = vp;
	pfs->pfs_mount = mp;
	pfs->pfs_flags = 0;
	pfs->pfs_fileno =
	    PROCFS_FILENO(pfs->pfs_pid, pfs->pfs_type, pfs->pfs_fd);
	vp->v_tag = VT_PROCFS;
	vp->v_op = procfs_vnodeop_p;
	vp->v_data = pfs;

	switch (pfs->pfs_type) {
	case PFSroot:	/* /proc = dr-xr-xr-x */
		vp->v_vflag |= VV_ROOT;
		/*FALLTHROUGH*/
	case PFSproc:	/* /proc/N = dr-xr-xr-x */
		pfs->pfs_mode = S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
		vp->v_type = VDIR;
		break;

	case PFStask:	/* /proc/N/task = dr-xr-xr-x */
		if (pfs->pfs_fd == -1) {
			pfs->pfs_mode = S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|
			    S_IROTH|S_IXOTH;
			vp->v_type = VDIR;
			break;
		}
		/*FALLTHROUGH*/
	case PFScurproc:	/* /proc/curproc = lr-xr-xr-x */
	case PFSself:	/* /proc/self    = lr-xr-xr-x */
	case PFScwd:	/* /proc/N/cwd = lr-xr-xr-x */
	case PFSchroot:	/* /proc/N/chroot = lr-xr-xr-x */
	case PFSexe:	/* /proc/N/exe = lr-xr-xr-x */
		pfs->pfs_mode = S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
		vp->v_type = VLNK;
		break;

	case PFSfd:
		if (pfs->pfs_fd == -1) {	/* /proc/N/fd = dr-x------ */
			pfs->pfs_mode = S_IRUSR|S_IXUSR;
			vp->v_type = VDIR;
		} else {	/* /proc/N/fd/M = [ps-]rw------- */
			file_t *fp;
			vnode_t *vxp;
			struct proc *p;

			mutex_enter(&proc_lock);
			p = procfs_proc_find(mp, pfs->pfs_pid);
			mutex_exit(&proc_lock);
			if (p == NULL) {
				error = ENOENT;
				goto bad;
			}
			KASSERT(rw_read_held(&p->p_reflock));
			if ((fp = fd_getfile2(p, pfs->pfs_fd)) == NULL) {
				error = EBADF;
				goto bad;
			}

			pfs->pfs_mode = S_IRUSR|S_IWUSR;
			switch (fp->f_type) {
			case DTYPE_VNODE:
				vxp = fp->f_vnode;

				/*
				 * We make symlinks for directories
				 * to avoid cycles.
				 */
				if (vxp->v_type == VDIR ||
				    procfs_proc_is_linux_compat())
					goto symlink;
				vp->v_type = vxp->v_type;
				break;
			case DTYPE_PIPE:
				vp->v_type = VFIFO;
				break;
			case DTYPE_SOCKET:
				vp->v_type = VSOCK;
				break;
			case DTYPE_KQUEUE:
			case DTYPE_MISC:
			case DTYPE_SEM:
			symlink:
				pfs->pfs_mode = S_IRUSR|S_IXUSR|S_IRGRP|
				    S_IXGRP|S_IROTH|S_IXOTH;
				vp->v_type = VLNK;
				break;
			default:
				error = EOPNOTSUPP;
				closef(fp);
				goto bad;
			}
			closef(fp);
		}
		break;

	case PFSfile:	/* /proc/N/file = -rw------- */
	case PFSmem:	/* /proc/N/mem = -rw------- */
	case PFSregs:	/* /proc/N/regs = -rw------- */
	case PFSfpregs:	/* /proc/N/fpregs = -rw------- */
		pfs->pfs_mode = S_IRUSR|S_IWUSR;
		vp->v_type = VREG;
		break;

	case PFSnote:	/* /proc/N/note = --w------ */
	case PFSnotepg:	/* /proc/N/notepg = --w------ */
		pfs->pfs_mode = S_IWUSR;
		vp->v_type = VREG;
		break;

	case PFSmap:		/* /proc/N/map = -r-------- */
	case PFSmaps:		/* /proc/N/maps = -r-------- */
	case PFSauxv:		/* /proc/N/auxv = -r-------- */
	case PFSenviron:	/* /proc/N/environ = -r-------- */
		pfs->pfs_mode = S_IRUSR;
		vp->v_type = VREG;
		break;

	case PFSstatus:		/* /proc/N/status = -r--r--r-- */
	case PFSstat:		/* /proc/N/stat = -r--r--r-- */
	case PFScmdline:	/* /proc/N/cmdline = -r--r--r-- */
	case PFSemul:		/* /proc/N/emul = -r--r--r-- */
	case PFSmeminfo:	/* /proc/meminfo = -r--r--r-- */
	case PFScpustat:	/* /proc/stat = -r--r--r-- */
	case PFSdevices:	/* /proc/devices = -r--r--r-- */
	case PFScpuinfo:	/* /proc/cpuinfo = -r--r--r-- */
	case PFSuptime:		/* /proc/uptime = -r--r--r-- */
	case PFSmounts:		/* /proc/mounts = -r--r--r-- */
	case PFSloadavg:	/* /proc/loadavg = -r--r--r-- */
	case PFSstatm:		/* /proc/N/statm = -r--r--r-- */
	case PFSversion:	/* /proc/version = -r--r--r-- */
	case PFSlimit:		/* /proc/limit = -r--r--r-- */
		pfs->pfs_mode = S_IRUSR|S_IRGRP|S_IROTH;
		vp->v_type = VREG;
		break;

#ifdef __HAVE_PROCFS_MACHDEP
	PROCFS_MACHDEP_NODETYPE_CASES
		procfs_machdep_allocvp(vp);
		break;
#endif

	default:
		panic("procfs_allocvp");
	}

	mutex_enter(&procfs_hashlock);
	LIST_INSERT_HEAD(procfs_hashhead(pfs->pfs_pid), pfs, pfs_hash);
	mutex_exit(&procfs_hashlock);

	uvm_vnp_setsize(vp, 0);
	*new_key = &pfs->pfs_key;

	return 0;

bad:
	vp->v_tag =VT_NON;
	vp->v_type = VNON;
	vp->v_op = NULL;
	vp->v_data = NULL;
	kmem_free(pfs, sizeof(*pfs));
	return error;
}

void
procfs_init(void)
{

}

void
procfs_reinit(void)
{

}

void
procfs_done(void)
{

}

extern const struct vnodeopv_desc procfs_vnodeop_opv_desc;

const struct vnodeopv_desc * const procfs_vnodeopv_descs[] = {
	&procfs_vnodeop_opv_desc,
	NULL,
};

struct vfsops procfs_vfsops = {
	.vfs_name = MOUNT_PROCFS,
	.vfs_min_mount_data = sizeof (struct procfs_args),
	.vfs_mount = procfs_mount,
	.vfs_start = procfs_start,
	.vfs_unmount = procfs_unmount,
	.vfs_root = procfs_root,
	.vfs_quotactl = (void *)eopnotsupp,
	.vfs_statvfs = procfs_statvfs,
	.vfs_sync = procfs_sync,
	.vfs_vget = procfs_vget,
	.vfs_loadvnode = procfs_loadvnode,
	.vfs_fhtovp = (void *)eopnotsupp,
	.vfs_vptofh = (void *)eopnotsupp,
	.vfs_init = procfs_init,
	.vfs_reinit = procfs_reinit,
	.vfs_done = procfs_done,
	.vfs_snapshot = (void *)eopnotsupp,
	.vfs_extattrctl = vfs_stdextattrctl,
	.vfs_suspendctl = genfs_suspendctl,
	.vfs_renamelock_enter = genfs_renamelock_enter,
	.vfs_renamelock_exit = genfs_renamelock_exit,
	.vfs_fsync = (void *)eopnotsupp,
	.vfs_opv_descs = procfs_vnodeopv_descs
};

static void
procfs_exechook_cb(struct proc *p, void *arg)
{
	struct hashhead *head;
	struct pfsnode *pfs;
	struct mount *mp;
	struct pfskey key;
	struct vnode *vp;
	int error;

	if (arg == PROCFS_EXEC_HOOK && !(p->p_flag & PK_SUGID))
		return;

	head = procfs_hashhead(p->p_pid);

again:
	mutex_enter(&procfs_hashlock);
	LIST_FOREACH(pfs, head, pfs_hash) {
		if (pfs->pfs_pid != p->p_pid)
			continue;
		mp = pfs->pfs_mount;
		key = pfs->pfs_key;
		vfs_ref(mp);
		mutex_exit(&procfs_hashlock);

		error = vcache_get(mp, &key, sizeof(key), &vp);
		vfs_rele(mp);
		if (error != 0)
			goto again;
		if (vrecycle(vp))
			goto again;
		do {
			error = vfs_suspend(mp, 0);
		} while (error == EINTR || error == ERESTART);
		vgone(vp);
		if (error == 0)
			vfs_resume(mp);
		goto again;
	}
	mutex_exit(&procfs_hashlock);
}

static int
procfs_listener_cb(kauth_cred_t cred, kauth_action_t action, void *cookie,
    void *arg0, void *arg1, void *arg2, void *arg3)
{
	struct proc *p;
	struct pfsnode *pfs;
	int result;

	result = KAUTH_RESULT_DEFER;
	p = arg0;
	pfs = arg1;

	if (action != KAUTH_PROCESS_PROCFS)
		return result;

	switch (pfs->pfs_type) {
	case PFSregs:
	case PFSfpregs:
	case PFSmem:
		if (kauth_cred_getuid(cred) != kauth_cred_getuid(p->p_cred) ||
		    ISSET(p->p_flag, PK_SUGID))
			break;

		/*FALLTHROUGH*/
	default:
		result = KAUTH_RESULT_ALLOW;
		break;
	}

	return result;
}

SYSCTL_SETUP(procfs_sysctl_setup, "procfs sysctl")
{

	sysctl_createv(clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT,
		       CTLTYPE_NODE, "procfs",
		       SYSCTL_DESCR("Process file system"),
		       NULL, 0, NULL, 0,
		       CTL_VFS, 12, CTL_EOL);
	/*
	 * XXX the "12" above could be dynamic, thereby eliminating
	 * one more instance of the "number to vfs" mapping problem,
	 * but "12" is the order as taken from sys/mount.h
	 */
}

static int
procfs_modcmd(modcmd_t cmd, void *arg)
{
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		error = vfs_attach(&procfs_vfsops);
		if (error != 0)
			break;

		procfs_listener = kauth_listen_scope(KAUTH_SCOPE_PROCESS,
		    procfs_listener_cb, NULL);

		procfs_exechook = exechook_establish(procfs_exechook_cb,
		    PROCFS_EXEC_HOOK);
		procfs_exithook = exithook_establish(procfs_exechook_cb,
		    PROCFS_EXIT_HOOK);

		mutex_init(&procfs_hashlock, MUTEX_DEFAULT, IPL_NONE);
		procfs_hashtab = hashinit(PROCFS_HASHSIZE, HASH_LIST, true,
		    &procfs_hashmask);

		break;
	case MODULE_CMD_FINI:
		error = vfs_detach(&procfs_vfsops);
		if (error != 0)
			break;
		kauth_unlisten_scope(procfs_listener);
		exechook_disestablish(procfs_exechook);
		exithook_disestablish(procfs_exithook);
		mutex_destroy(&procfs_hashlock);
		hashdone(procfs_hashtab, HASH_LIST, procfs_hashmask);
		break;
	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SYS_ZFS_CONTEXT_H
#define	_SYS_ZFS_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This code compiles in three different contexts. When _KERNEL is defined,
 * the code uses "unix-like" kernel interfaces. When _STANDALONE is defined, the
 * code is running in a reduced capacity environment of the boot loader which is
 * generally a subset of both POSIX and kernel interfaces (with a few unique
 * interfaces too). When neither are defined, it's in a userland POSIX or
 * similar environment.
 */
#if defined(_KERNEL) || defined(_STANDALONE)
#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/vmsystm.h>
#include <sys/condvar.h>
#include <sys/cmn_err.h>
#include <sys/kmem_cache.h>
#include <sys/vmem.h>
#include <sys/misc.h>
#include <sys/param.h>
#include <sys/disp.h>
#include <sys/debug.h>
#include <sys/random.h>
#include <sys/string.h>
#include <sys/byteorder.h>
#include <sys/list.h>
#include <sys/time.h>
#include <sys/zone.h>
#include <sys/kstat.h>
#include <sys/zfs_debug.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/zfs_delay.h>
#include <sys/sunddi.h>
#include <sys/ctype.h>
#include <sys/disp.h>
#include <sys/trace.h>
#include <sys/procfs_list.h>
#include <sys/mod.h>
#include <sys/uio_impl.h>
#include <sys/zfs_context_os.h>
#else /* _KERNEL || _STANDALONE */

#define	_SYS_VNODE_H
#define	_SYS_VFS_H
#define	_SYS_SUNDDI_H
#define	_SYS_CALLB_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <setjmp.h>
#include <assert.h>
#include <umem.h>
#include <limits.h>
#include <atomic.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/byteorder.h>
#include <sys/list.h>
#include <sys/mod.h>
#include <sys/uio.h>
#include <sys/zfs_debug.h>
#include <sys/kstat.h>
#include <sys/u8_textprep.h>
#include <sys/taskq.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sunddi.h>
#include <sys/debug.h>
#include <sys/utsname.h>
#include <sys/trace_zfs.h>

#include <sys/zfs_context_os.h>

/*
 * Stack
 */

#define	noinline	__attribute__((noinline))
#define	likely(x)	__builtin_expect((x), 1)
#define	unlikely(x)	__builtin_expect((x), 0)

/*
 * Debugging
 */

/*
 * Note that we are not using the debugging levels.
 */

#define	CE_CONT		0	/* continuation		*/
#define	CE_NOTE		1	/* notice		*/
#define	CE_WARN		2	/* warning		*/
#define	CE_PANIC	3	/* panic		*/
#define	CE_IGNORE	4	/* print nothing	*/

/*
 * ZFS debugging
 */

extern void dprintf_setup(int *argc, char **argv);

extern void cmn_err(int, const char *, ...)
    __attribute__((format(printf, 2, 3)));
extern void vcmn_err(int, const char *, va_list)
    __attribute__((format(printf, 2, 0)));
extern void panic(const char *, ...)
    __attribute__((format(printf, 1, 2), noreturn));
extern void vpanic(const char *, va_list)
    __attribute__((format(printf, 1, 0), noreturn));

#define	fm_panic	panic

/*
 * DTrace SDT probes have different signatures in userland than they do in
 * the kernel.  If they're being used in kernel code, re-define them out of
 * existence for their counterparts in libzpool.
 *
 * Here's an example of how to use the set-error probes in userland:
 * zfs$target:::set-error /arg0 == EBUSY/ {stack();}
 *
 * Here's an example of how to use DTRACE_PROBE probes in userland:
 * If there is a probe declared as follows:
 * DTRACE_PROBE2(zfs__probe_name, uint64_t, blkid, dnode_t *, dn);
 * Then you can use it as follows:
 * zfs$target:::probe2 /copyinstr(arg0) == "zfs__probe_name"/
 *     {printf("%u %p\n", arg1, arg2);}
 */

#ifdef DTRACE_PROBE
#undef	DTRACE_PROBE
#endif	/* DTRACE_PROBE */
#define	DTRACE_PROBE(a)

#ifdef DTRACE_PROBE1
#undef	DTRACE_PROBE1
#endif	/* DTRACE_PROBE1 */
#define	DTRACE_PROBE1(a, b, c)

#ifdef DTRACE_PROBE2
#undef	DTRACE_PROBE2
#endif	/* DTRACE_PROBE2 */
#define	DTRACE_PROBE2(a, b, c, d, e)

#ifdef DTRACE_PROBE3
#undef	DTRACE_PROBE3
#endif	/* DTRACE_PROBE3 */
#define	DTRACE_PROBE3(a, b, c, d, e, f, g)

#ifdef DTRACE_PROBE4
#undef	DTRACE_PROBE4
#endif	/* DTRACE_PROBE4 */
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)

/*
 * Tunables.
 */
typedef struct zfs_kernel_param {
	const char *name;	/* unused stub */
} zfs_kernel_param_t;

#define	ZFS_MODULE_PARAM(scope_prefix, name_prefix, name, type, perm, desc)
#define	ZFS_MODULE_PARAM_ARGS void
#define	ZFS_MODULE_PARAM_CALL(scope_prefix, name_prefix, name, setfunc, \
	getfunc, perm, desc)

/*
 * Thread-specific data
 */
#define	tsd_get(k) pthread_getspecific(k)
#define	tsd_set(k, v) pthread_setspecific(k, v)
#define	tsd_create(kp, d) pthread_key_create((pthread_key_t *)kp, d)
#define	tsd_destroy(kp) /* nothing */
#ifdef __FreeBSD__
typedef off_t loff_t;
#endif

/*
 * kstat creation, installation and deletion
 */
extern kstat_t *kstat_create(const char *, int,
    const char *, const char *, uchar_t, ulong_t, uchar_t);
extern void kstat_install(kstat_t *);
extern void kstat_delete(kstat_t *);
extern void kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t index));

/*
 * procfs list manipulation
 */

typedef struct procfs_list {
	void		*pl_private;
	kmutex_t	pl_lock;
	list_t		pl_list;
	uint64_t	pl_next_id;
	size_t		pl_node_offset;
} procfs_list_t;

#ifndef __cplusplus
struct seq_file { };
void seq_printf(struct seq_file *m, const char *fmt, ...);

typedef struct procfs_list_node {
	list_node_t	pln_link;
	uint64_t	pln_id;
} procfs_list_node_t;

void procfs_list_install(const char *module,
    const char *submodule,
    const char *name,
    mode_t mode,
    procfs_list_t *procfs_list,
    int (*show)(struct seq_file *f, void *p),
    int (*show_header)(struct seq_file *f),
    int (*clear)(procfs_list_t *procfs_list),
    size_t procfs_list_node_off);
void procfs_list_uninstall(procfs_list_t *procfs_list);
void procfs_list_destroy(procfs_list_t *procfs_list);
void procfs_list_add(procfs_list_t *procfs_list, void *p);
#endif

#define	XVA_MAPSIZE	3
#define	XVA_MAGIC	0x78766174

extern char *vn_dumpdir;
#define	AV_SCANSTAMP_SZ	32		/* length of anti-virus scanstamp */

#if 0
typedef struct xoptattr {
	inode_timespec_t xoa_createtime;	/* Create time of file */
	uint8_t		xoa_archive;
	uint8_t		xoa_system;
	uint8_t		xoa_readonly;
	uint8_t		xoa_hidden;
	uint8_t		xoa_nounlink;
	uint8_t		xoa_immutable;
	uint8_t		xoa_appendonly;
	uint8_t		xoa_nodump;
	uint8_t		xoa_settable;
	uint8_t		xoa_opaque;
	uint8_t		xoa_av_quarantined;
	uint8_t		xoa_av_modified;
	uint8_t		xoa_av_scanstamp[AV_SCANSTAMP_SZ];
	uint8_t		xoa_reparse;
	uint8_t		xoa_offline;
	uint8_t		xoa_sparse;
} xoptattr_t;

typedef struct vattr {
	uint32_t	va_mask;	/* attribute bit-mask */
	ushort_t	va_mode;	/* acc mode */
	uid_t		va_uid;		/* owner uid */
	gid_t		va_gid;		/* owner gid */
	long		va_fsid;	/* fs id */
	long		va_nodeid;	/* node # */
	uint32_t	va_nlink;	/* # links */
	uint64_t	va_size;	/* file size */
	timespec_t	va_atime;	/* last acc */
	timespec_t	va_mtime;	/* last mod */
	timespec_t	va_ctime;	/* last chg */
	dev_t		va_rdev;	/* dev */
	uint64_t	va_nblocks;	/* space used */
	uint32_t	va_blksize;	/* block size */
} vattr_t;

typedef struct xvattr {
	vattr_t		xva_vattr;	/* Embedded vattr structure */
	uint32_t	xva_magic;	/* Magic Number */
	uint32_t	xva_mapsize;	/* Size of attr bitmap (32-bit words) */
	uint32_t	*xva_rtnattrmapp;	/* Ptr to xva_rtnattrmap[] */
	uint32_t	xva_reqattrmap[XVA_MAPSIZE];	/* Requested attrs */
	uint32_t	xva_rtnattrmap[XVA_MAPSIZE];	/* Returned attrs */
	xoptattr_t	xva_xoptattrs;	/* Optional attributes */
} xvattr_t;

typedef struct vsecattr {
	uint_t		vsa_mask;	/* See below */
	int		vsa_aclcnt;	/* ACL entry count */
	void		*vsa_aclentp;	/* pointer to ACL entries */
	int		vsa_dfaclcnt;	/* default ACL entry count */
	void		*vsa_dfaclentp;	/* pointer to default ACL entries */
	size_t		vsa_aclentsz;	/* ACE size in bytes of vsa_aclentp */
} vsecattr_t;
#endif

#define	AT_MODE		0x00002
#define	AT_UID		0x00004
#define	AT_GID		0x00008
#define	AT_FSID		0x00010
#define	AT_NODEID	0x00020
#define	AT_NLINK	0x00040
#define	AT_SIZE		0x00080
#define	AT_ATIME	0x00100
#define	AT_MTIME	0x00200
#define	AT_CTIME	0x00400
#define	AT_RDEV		0x00800
#define	AT_BLKSIZE	0x01000
#define	AT_NBLOCKS	0x02000
#define	AT_SEQ		0x08000
#define	AT_XVATTR	0x10000

#define	CRCREAT		0

#define	F_FREESP	11
#define	FIGNORECASE	0x80000 /* request case-insensitive lookups */

/*
 * Random stuff
 */
#define	ddi_get_lbolt()		(gethrtime() >> 23)
#define	ddi_get_lbolt64()	(gethrtime() >> 23)
#define	hz	119	/* frequency when using gethrtime() >> 23 for lbolt */

#define	ddi_time_before(a, b)		(a < b)
#define	ddi_time_after(a, b)		ddi_time_before(b, a)
#define	ddi_time_before_eq(a, b)	(!ddi_time_after(a, b))
#define	ddi_time_after_eq(a, b)		ddi_time_before_eq(b, a)

#define	ddi_time_before64(a, b)		(a < b)
#define	ddi_time_after64(a, b)		ddi_time_before64(b, a)
#define	ddi_time_before_eq64(a, b)	(!ddi_time_after64(a, b))
#define	ddi_time_after_eq64(a, b)	ddi_time_before_eq64(b, a)

extern void delay(clock_t ticks);

#define	SEC_TO_TICK(sec)	((sec) * hz)
#define	MSEC_TO_TICK(msec)	(howmany((hrtime_t)(msec) * hz, MILLISEC))
#define	USEC_TO_TICK(usec)	(howmany((hrtime_t)(usec) * hz, MICROSEC))
#define	NSEC_TO_TICK(nsec)	(howmany((hrtime_t)(nsec) * hz, NANOSEC))

#define	max_ncpus	64
#define	boot_ncpus	(sysconf(_SC_NPROCESSORS_ONLN))

/*
 * Process priorities as defined by setpriority(2) and getpriority(2).
 */
#define	minclsyspri	19
#define	maxclsyspri	-20
#define	defclsyspri	0

#define	CPU_SEQID	((uintptr_t)pthread_self() & (max_ncpus - 1))
#define	CPU_SEQID_UNSTABLE	CPU_SEQID

#define	ptob(x)		((x) * PAGESIZE)

#define	NN_DIVISOR_1000	(1U << 0)
#define	NN_NUMBUF_SZ	(6)

extern uint64_t physmem;
extern const char *random_path;
extern const char *urandom_path;

extern int highbit64(uint64_t i);
extern int lowbit64(uint64_t i);
extern int random_get_bytes(uint8_t *ptr, size_t len);
extern int random_get_pseudo_bytes(uint8_t *ptr, size_t len);

static __inline__ uint32_t
random_in_range(uint32_t range)
{
	uint32_t r;

	ASSERT(range != 0);

	if (range == 1)
		return (0);

	(void) random_get_pseudo_bytes((uint8_t *)&r, sizeof (r));

	return (r % range);
}

extern void kernel_init(int mode);
extern void kernel_fini(void);
extern void random_init(void);
extern void random_fini(void);

struct spa;
extern void show_pool_stats(struct spa *);
extern int set_global_var(char const *arg);

typedef struct callb_cpr {
	kmutex_t	*cc_lockp;
} callb_cpr_t;

#define	CALLB_CPR_INIT(cp, lockp, func, name)	{		\
	(cp)->cc_lockp = lockp;					\
}

#define	CALLB_CPR_SAFE_BEGIN(cp) {				\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
}

#define	CALLB_CPR_SAFE_END(cp, lockp) {				\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
}

#define	CALLB_CPR_EXIT(cp) {					\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
	mutex_exit((cp)->cc_lockp);				\
}

#define	INGLOBALZONE(z)			(1)
extern uint32_t zone_get_hostid(void *zonep);
extern int zone_dataset_visible(const char *dataset, int *write);

extern char *kmem_vasprintf(const char *fmt, va_list adx);
extern char *kmem_asprintf(const char *fmt, ...);
#define	kmem_strfree(str) kmem_free((str), strlen(str) + 1)
#define	kmem_strdup(s)  strdup(s)

#ifndef __cplusplus
extern int kmem_scnprintf(char *restrict str, size_t size,
    const char *restrict fmt, ...);
#endif

/*
 * Hostname information
 */
extern int ddi_strtoull(const char *str, char **nptr, int base,
    u_longlong_t *result);

typedef struct utsname	utsname_t;
extern utsname_t *utsname(void);

/* ZFS Boot Related stuff. */

struct _buf {
	intptr_t	_fd;
};

struct bootstat {
	uint64_t st_size;
};

typedef struct ace_object {
	uid_t		a_who;
	uint32_t	a_access_mask;
	uint16_t	a_flags;
	uint16_t	a_type;
	uint8_t		a_obj_type[16];
	uint8_t		a_inherit_obj_type[16];
} ace_object_t;


#define	ACE_ACCESS_ALLOWED_OBJECT_ACE_TYPE	0x05
#define	ACE_ACCESS_DENIED_OBJECT_ACE_TYPE	0x06
#define	ACE_SYSTEM_AUDIT_OBJECT_ACE_TYPE	0x07
#define	ACE_SYSTEM_ALARM_OBJECT_ACE_TYPE	0x08

extern int zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr);
extern int zfs_secpolicy_rename_perms(const char *from, const char *to,
    cred_t *cr);
extern int zfs_secpolicy_destroy_perms(const char *name, cred_t *cr);
extern zoneid_t getzoneid(void);

#define	DDI_SLEEP	KM_SLEEP
#define	ddi_log_sysevent(_a, _b, _c, _d, _e, _f, _g) \
	sysevent_post_event(_c, _d, _b, "libzpool", _e, _f)

#define	zfs_sleep_until(wakeup)						\
	do {								\
		hrtime_t delta = wakeup - gethrtime();			\
		struct timespec ts;					\
		ts.tv_sec = delta / NANOSEC;				\
		ts.tv_nsec = delta % NANOSEC;				\
		(void) nanosleep(&ts, NULL);				\
	} while (0)

typedef int fstrans_cookie_t;

extern fstrans_cookie_t spl_fstrans_mark(void);
extern void spl_fstrans_unmark(fstrans_cookie_t);
extern int __spl_pf_fstrans_check(void);


/*
 * Kernel modules
 */
#define	__init
#define	__exit

/*
 * Userland ioctl handling
 */

extern void zfs_user_ioctl_init(void);
extern int copyinstr(const char *, char *, size_t, size_t *);
extern int ddi_copyin(const void *, void *, size_t, int);
extern int ddi_copyout(const void *, void *, size_t, int);

#define	is_system_labeled()		(0)
#define	groupmember(gid, cr)		(0)

#endif  /* _KERNEL || _STANDALONE */

#ifdef __cplusplus
};
#endif

#endif	/* _SYS_ZFS_CONTEXT_H */

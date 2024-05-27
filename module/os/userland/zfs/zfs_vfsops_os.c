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
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/acl.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/cmn_err.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zil.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_deleg.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/policy.h>
#include <sys/atomic.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/sunddi.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/jail.h>
#include <sys/osd.h>
#include <ufs/ufs/quota.h>
#include <sys/zfs_quota.h>

#include "zfs_comutil.h"

#ifndef	MNTK_VMSETSIZE_BUG
#define	MNTK_VMSETSIZE_BUG	0
#endif
#ifndef	MNTK_NOMSYNC
#define	MNTK_NOMSYNC	8
#endif

struct mtx zfs_debug_mtx;
MTX_SYSINIT(zfs_debug_mtx, &zfs_debug_mtx, "zfs_debug", MTX_DEF);
int zfs_super_owner;
int zfs_debug_level;

struct zfs_jailparam {
	int mount_snapshot;
};

static struct zfs_jailparam zfs_jailparam0 = {
	.mount_snapshot = 0,
};

static int zfs_jailparam_slot;

static int zfs_version_acl = ZFS_ACL_VERSION;
static int zfs_version_spa = SPA_VERSION;
static int zfs_version_zpl = ZPL_VERSION;

#if __FreeBSD_version >= 1400018
static int zfs_quotactl(vfs_t *vfsp, int cmds, uid_t id, void *arg,
    bool *mp_busy);
#else
static int zfs_quotactl(vfs_t *vfsp, int cmds, uid_t id, void *arg);
#endif
static int zfs_mount(vfs_t *vfsp);
static int zfs_umount(vfs_t *vfsp, int fflag);
static int zfs_root(vfs_t *vfsp, int flags, vnode_t **vpp);
static int zfs_statfs(vfs_t *vfsp, struct statfs *statp);
static int zfs_vget(vfs_t *vfsp, ino_t ino, int flags, vnode_t **vpp);
static int zfs_sync(vfs_t *vfsp, int waitfor);
#if __FreeBSD_version >= 1300098
static int zfs_checkexp(vfs_t *vfsp, struct sockaddr *nam, uint64_t *extflagsp,
    struct ucred **credanonp, int *numsecflavors, int *secflavors);
#else
static int zfs_checkexp(vfs_t *vfsp, struct sockaddr *nam, int *extflagsp,
    struct ucred **credanonp, int *numsecflavors, int **secflavors);
#endif
static int zfs_fhtovp(vfs_t *vfsp, fid_t *fidp, int flags, vnode_t **vpp);
static void zfs_freevfs(vfs_t *vfsp);

int
zfs_get_temporary_prop(dsl_dataset_t *ds, zfs_prop_t zfs_prop __unused,
    uint64_t *val __unused, char *setpoint __unused)
{
	zfsvfs_t *zfvp;
	objset_t *os;
	int error;

	error = dmu_objset_from_ds(ds, &os);
	if (error != 0)
		return (error);

	error = getzfsvfs_impl(os, &zfvp);
	if (error != 0)
		return (error);
	if (zfvp == NULL)
		return (ENOENT);
	return (0);
}

boolean_t
zfs_is_readonly(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_readonly);
}

static void
atime_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_atime = (newval != 0);
}

static void
xattr_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == ZFS_XATTR_OFF) {
		zfsvfs->z_flags &= ~ZSB_XATTR;
	} else {
		zfsvfs->z_flags |= ZSB_XATTR;

		if (newval == ZFS_XATTR_SA)
			zfsvfs->z_xattr_sa = B_TRUE;
		else
			zfsvfs->z_xattr_sa = B_FALSE;
	}
}

static void
blksz_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	ASSERT3U(newval, <=, spa_maxblocksize(dmu_objset_spa(zfsvfs->z_os)));
	ASSERT3U(newval, >=, SPA_MINBLOCKSIZE);
	ASSERT(ISP2(newval));

	zfsvfs->z_max_blksz = newval;
}

static void
readonly_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_readonly = (newval != 0);
}

static void
setuid_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_setuid = (newval != 0);
}

static void
exec_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_exec = (newval != 0);
}

/*
 * The nbmand mount option can be changed at mount time.
 * We can't allow it to be toggled on live file systems or incorrect
 * behavior may be seen from cifs clients
 *
 * This property isn't registered via dsl_prop_register(), but this callback
 * will be called when a file system is first mounted
 */
static void
nbmand_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_nbmand = (newval != 0);
}

static void
snapdir_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_show_ctldir = (newval != 0);
}

static void
acl_mode_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_mode = newval;
}

static void
acl_inherit_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_inherit = newval;
}

static void
acl_type_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_type = newval;
}

int
zfs_register_callbacks(zfsvfs_t *zfsvfs)
{
	struct dsl_dataset *ds = NULL;
	objset_t *os = NULL;
	uint64_t nbmand;
	boolean_t readonly = B_FALSE;
	boolean_t do_readonly = B_FALSE;
	int error = 0;

	ASSERT3P(zfsvfs, !=, NULL);
	os = zfsvfs->z_os;

	/*
	 * This function can be called for a snapshot when we update snapshot's
	 * mount point, which isn't really supported.
	 */
	if (dmu_objset_is_snapshot(os))
		return (EOPNOTSUPP);

	/*
	 * The act of registering our callbacks will destroy any mount
	 * options we may have.  In order to enable temporary overrides
	 * of mount options, we stash away the current values and
	 * restore them after we register the callbacks.
	 */
	if (!spa_writeable(dmu_objset_spa(os))) {
		readonly = B_TRUE;
		do_readonly = B_TRUE;
	}

	/*
	 * We need to enter pool configuration here, so that we can use
	 * dsl_prop_get_int_ds() to handle the special nbmand property below.
	 * dsl_prop_get_integer() can not be used, because it has to acquire
	 * spa_namespace_lock and we can not do that because we already hold
	 * z_teardown_lock.  The problem is that spa_write_cachefile() is called
	 * with spa_namespace_lock held and the function calls ZFS vnode
	 * operations to write the cache file and thus z_teardown_lock is
	 * acquired after spa_namespace_lock.
	 */
	ds = dmu_objset_ds(os);
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);

	/*
	 * nbmand is a special property.  It can only be changed at
	 * mount time.
	 *
	 * This is weird, but it is documented to only be changeable
	 * at mount time.
	 */
	if ((error = dsl_prop_get_int_ds(ds, "nbmand", &nbmand)) != 0) {
		dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
		return (error);
	}

	/*
	 * Register property callbacks.
	 *
	 * It would probably be fine to just check for i/o error from
	 * the first prop_register(), but I guess I like to go
	 * overboard...
	 */
	error = dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ATIME), atime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_XATTR), xattr_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_RECORDSIZE), blksz_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_READONLY), readonly_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SETUID), setuid_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_EXEC), exec_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SNAPDIR), snapdir_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLTYPE), acl_type_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLMODE), acl_mode_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLINHERIT), acl_inherit_changed_cb,
	    zfsvfs);
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
	if (error)
		goto unregister;

	/*
	 * Invoke our callbacks to restore temporary mount options.
	 */
	if (do_readonly)
		readonly_changed_cb(zfsvfs, readonly);
	nbmand_changed_cb(zfsvfs, nbmand);

	return (0);

unregister:
	dsl_prop_unregister_all(ds, zfsvfs);
	return (error);
}

taskq_t *zfsvfs_taskq;

static void
zfsvfs_task_unlinked_drain(void *context, int pending __unused)
{

	zfs_unlinked_drain((zfsvfs_t *)context);
}

int
zfsvfs_create_impl(zfsvfs_t **zfvp, zfsvfs_t *zfsvfs, objset_t *os)
{
	int error;

	zfsvfs->z_vfs = NULL;
	zfsvfs->z_parent = zfsvfs;

	mutex_init(&zfsvfs->z_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&zfsvfs->z_fuid_lock, NULL, RW_DEFAULT, NULL);

	error = zfsvfs_init(zfsvfs, os);
	if (error != 0) {
		dmu_objset_disown(os, B_TRUE, zfsvfs);
		*zfvp = NULL;
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	*zfvp = zfsvfs;
	return (0);
}

void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	int i;

	zfs_fuid_destroy(zfsvfs);

	mutex_destroy(&zfsvfs->z_lock);
	rw_destroy(&zfsvfs->z_fuid_lock);
	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);
	dataset_kstats_destroy(&zfsvfs->z_kstat);
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}

static int
getpoolname(const char *osname, char *poolname)
{
	char *p;

	p = strchr(osname, '/');
	if (p == NULL) {
		if (strlen(osname) >= MAXNAMELEN)
			return (ENAMETOOLONG);
		(void) strcpy(poolname, osname);
	} else {
		if (p - osname >= MAXNAMELEN)
			return (ENAMETOOLONG);
		(void) strlcpy(poolname, osname, p - osname + 1);
	}
	return (0);
}

static void
fetch_osname_options(char *name, bool *checkpointrewind)
{

	if (name[0] == '!') {
		*checkpointrewind = true;
		memmove(name, name + 1, strlen(name));
	} else {
		*checkpointrewind = false;
	}
}

/*
 * Teardown the zfsvfs::z_os.
 *
 * Note, if 'unmounting' is FALSE, we return with the 'z_teardown_lock'
 * and 'z_teardown_inactive_lock' held.
 */
static int
zfsvfs_teardown(zfsvfs_t *zfsvfs, boolean_t unmounting)
{
	znode_t	*zp;
	dsl_dir_t *dd;

	/*
	 * Close the zil. NB: Can't close the zil while zfs_inactive
	 * threads are blocked as zil_close can call zfs_inactive.
	 */
	if (zfsvfs->z_log) {
		zil_close(zfsvfs->z_log);
		zfsvfs->z_log = NULL;
	}

	/*
	 * If we are not unmounting (ie: online recv) and someone already
	 * unmounted this file system while we were doing the switcheroo,
	 * or a reopen of z_os failed then just bail out now.
	 */
	if (!unmounting && (zfsvfs->z_unmounted || zfsvfs->z_os == NULL)) {
		return (SET_ERROR(EIO));
	}

	/*
	 * If we are unmounting, set the unmounted flag and let new vops
	 * unblock.  zfs_inactive will have the unmounted behavior, and all
	 * other vops will fail with EIO.
	 */
	if (unmounting) {
		zfsvfs->z_unmounted = B_TRUE;
	}

	/*
	 * z_os will be NULL if there was an error in attempting to reopen
	 * zfsvfs, so just return as the properties had already been
	 * unregistered and cached data had been evicted before.
	 */
	if (zfsvfs->z_os == NULL)
		return (0);

	/*
	 * Unregister properties.
	 */
	zfs_unregister_callbacks(zfsvfs);

	/*
	 * Evict cached data
	 */
	if (!zfs_is_readonly(zfsvfs))
		txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	dmu_objset_evict_dbufs(zfsvfs->z_os);
	dd = zfsvfs->z_os->os_dsl_dataset->ds_dir;
	dsl_dir_cancel_waiters(dd);

	return (0);
}

/*
 * Rebuild SA and release VOPs.  Note that ownership of the underlying dataset
 * is an invariant across any of the operations that can be performed while the
 * filesystem was suspended.  Whether it succeeded or failed, the preconditions
 * are the same: the relevant objset and associated dataset are owned by
 * zfsvfs, held, and long held on entry.
 */
int
zfs_resume_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	int err;
	znode_t *zp;

	/*
	 * We already own this, so just update the objset_t, as the one we
	 * had before may have been evicted.
	 */
	objset_t *os;
	VERIFY3P(ds->ds_owner, ==, zfsvfs);
	VERIFY(dsl_dataset_long_held(ds));
	dsl_pool_t *dp = spa_get_dsl(dsl_dataset_get_spa(ds));
	dsl_pool_config_enter(dp, FTAG);
	VERIFY0(dmu_objset_from_ds(ds, &os));
	dsl_pool_config_exit(dp, FTAG);

	err = zfsvfs_init(zfsvfs, os);
	if (err != 0)
		goto bail;

	ds->ds_dir->dd_activity_cancelled = B_FALSE;
	VERIFY0(zfsvfs_setup(zfsvfs, B_FALSE));

	zfs_set_fuid_feature(zfsvfs);
bail:
	return (err);
}

void
zfs_init(void)
{

	printf("ZFS filesystem version: " ZPL_VERSION_STRING "\n");

	/*
	 * Initialize .zfs directory structures
	 */
	zfsctl_init();

	/*
	 * Initialize znode cache, vnode ops, etc...
	 */
	zfs_znode_init();

	dmu_objset_register_type(DMU_OST_ZFS, zpl_get_file_info);
}

void
zfs_fini(void)
{
	zfsctl_fini();
	zfs_znode_fini();
}

/*
 * Release VOPs and unmount a suspended filesystem.
 */
int
zfs_end_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	/*
	 * We already own this, so just hold and rele it to update the
	 * objset_t, as the one we had before may have been evicted.
	 */
	objset_t *os;
	VERIFY3P(ds->ds_owner, ==, zfsvfs);
	VERIFY(dsl_dataset_long_held(ds));
	dsl_pool_t *dp = spa_get_dsl(dsl_dataset_get_spa(ds));
	dsl_pool_config_enter(dp, FTAG);
	VERIFY0(dmu_objset_from_ds(ds, &os));
	dsl_pool_config_exit(dp, FTAG);
	zfsvfs->z_os = os;

	zfsvfs->z_unmounted = B_TRUE;
	return (0);
}

boolean_t
zfs_get_vfs_flag_unmounted(objset_t *os)
{

	ASSERT3U(dmu_objset_type(os), ==, DMU_OST_ZFS);

	return (B_FALSE);
}

void
zfsvfs_update_fromname(const char *oldname __unused,
    const char *newname __unused)
{
	/* Do nothing. */
}

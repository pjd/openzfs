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
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/dsl_prop.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>

#include "zfs_comutil.h"

void
zfs_set_fuid_feature(zfsvfs_t *zfsvfs)
{
	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);
}

void
zfs_unregister_callbacks(zfsvfs_t *zfsvfs)
{
	objset_t *os = zfsvfs->z_os;

	if (!dmu_objset_is_snapshot(os))
		dsl_prop_unregister_all(dmu_objset_ds(os), zfsvfs);
}

/*
 * Block out VOPs and close zfsvfs_t::z_os
 *
 * Note, if successful, then we return with the 'z_teardown_lock' and
 * 'z_teardown_inactive_lock' write held.  We leave ownership of the underlying
 * dataset and objset intact so that they can be atomically handed off during
 * a subsequent rollback or recv operation and the resume thereafter.
 */
int
zfs_suspend_fs(zfsvfs_t *zfsvfs)
{
	int error;

	if ((error = zfsvfs_teardown(zfsvfs, B_FALSE)) != 0)
		return (error);

	return (0);
}

/*
 * Associate this zfsvfs with the given objset, which must be owned.
 * This will cache a bunch of on-disk state from the objset in the
 * zfsvfs.
 */
int
zfsvfs_init(zfsvfs_t *zfsvfs, objset_t *os)
{
	int error;
	uint64_t val;

	zfsvfs->z_max_blksz = SPA_OLD_MAXBLOCKSIZE;
	zfsvfs->z_show_ctldir = ZFS_SNAPDIR_VISIBLE;
	zfsvfs->z_os = os;

	error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &zfsvfs->z_version);
	if (error != 0)
		return (error);
	if (zfsvfs->z_version >
	    zfs_zpl_version_map(spa_version(dmu_objset_spa(os)))) {
		dprintf("Can't mount a version %lld file system on a version "
		    "%lld pool\n. Pool must be upgraded to mount this "
		    "file system.\n", (u_longlong_t)zfsvfs->z_version,
		    (u_longlong_t)spa_version(dmu_objset_spa(os)));
		return (SET_ERROR(ENOTSUP));
	}
	error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_norm = (int)val;

	error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_utf8 = (val != 0);

	error = zfs_get_zplprop(os, ZFS_PROP_CASE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_case = (uint_t)val;

	error = zfs_get_zplprop(os, ZFS_PROP_ACLTYPE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_acl_type = (uint_t)val;

	/*
	 * Fold case on file systems that are always or sometimes case
	 * insensitive.
	 */
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
	    zfsvfs->z_case == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	zfs_set_fuid_feature(zfsvfs);

	uint64_t sa_obj = 0;
	if (zfsvfs->z_use_sa) {
		/* should either have both of these objects or none */
		error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1,
		    &sa_obj);
		if (error != 0)
			return (error);

		error = zfs_get_zplprop(os, ZFS_PROP_XATTR, &val);
		if (error == 0 && val == ZFS_XATTR_SA)
			zfsvfs->z_xattr_sa = B_TRUE;
	}

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1,
	    &zfsvfs->z_root);
	if (error != 0)
		return (error);
	ASSERT3U(zfsvfs->z_root, !=, 0);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET, 8, 1,
	    &zfsvfs->z_unlinkedobj);
	if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA],
	    8, 1, &zfsvfs->z_userquota_obj);
	if (error == ENOENT)
		zfsvfs->z_userquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA],
	    8, 1, &zfsvfs->z_groupquota_obj);
	if (error == ENOENT)
		zfsvfs->z_groupquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_PROJECTQUOTA],
	    8, 1, &zfsvfs->z_projectquota_obj);
	if (error == ENOENT)
		zfsvfs->z_projectquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USEROBJQUOTA],
	    8, 1, &zfsvfs->z_userobjquota_obj);
	if (error == ENOENT)
		zfsvfs->z_userobjquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPOBJQUOTA],
	    8, 1, &zfsvfs->z_groupobjquota_obj);
	if (error == ENOENT)
		zfsvfs->z_groupobjquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_PROJECTOBJQUOTA],
	    8, 1, &zfsvfs->z_projectobjquota_obj);
	if (error == ENOENT)
		zfsvfs->z_projectobjquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES, 8, 1,
	    &zfsvfs->z_fuid_obj);
	if (error == ENOENT)
		zfsvfs->z_fuid_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SHARES_DIR, 8, 1,
	    &zfsvfs->z_shares_dir);
	if (error == ENOENT)
		zfsvfs->z_shares_dir = 0;
	else if (error != 0)
		return (error);

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);
	if (error != 0)
		return (error);

	if (zfsvfs->z_version >= ZPL_VERSION_SA)
		sa_register_update_callback(os, zfs_sa_upgrade);

#ifdef __FreeBSD__
	/*
	 * Only use the name cache if we are looking for a
	 * name on a file system that does not require normalization
	 * or case folding.  We can also look there if we happen to be
	 * on a non-normalizing, mixed sensitivity file system IF we
	 * are looking for the exact name (which is always the case on
	 * FreeBSD).
	 */
	zfsvfs->z_use_namecache = !zfsvfs->z_norm ||
	    ((zfsvfs->z_case == ZFS_CASE_MIXED) &&
	    !(zfsvfs->z_norm & ~U8_TEXTPREP_TOUPPER));
#endif

	return (0);
}

int
zfsvfs_create(const char *osname, boolean_t readonly, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	int error;
	boolean_t ro = (readonly || (strchr(osname, '@') != NULL));

#ifdef __FreeBSD__
	/*
	 * The 'osname' is used as the filesystem's special node, which means
	 * it must fit in statfs.f_mntfromname, or else it can't be
	 * enumerated, so libzfs_mnttab_find() returns NULL, which causes
	 * 'zfs unmount' to think it's not mounted when it is.
	 */
	if (strlen(osname) >= MNAMELEN)
		return (SET_ERROR(ENAMETOOLONG));
#endif

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	error = dmu_objset_own(osname, DMU_OST_ZFS, ro, B_TRUE, zfsvfs, &os);
	if (error != 0) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	error = zfsvfs_create_impl(zfvp, zfsvfs, os);

	return (error);
}

int
zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting)
{
	int error;
	boolean_t readonly = zfs_is_readonly(zfsvfs);

#ifdef __FreeBSD__
	/*
	 * Check for a bad on-disk format version now since we
	 * lied about owning the dataset readonly before.
	 */
	if (!readonly &&
	    dmu_objset_incompatible_encryption_version(zfsvfs->z_os))
		return (SET_ERROR(EROFS));
#endif

	error = zfs_register_callbacks(zfsvfs);
	if (error)
		return (error);

	/*
	 * If we are not mounting (ie: online recv), then we don't
	 * have to worry about replaying the log as we blocked all
	 * operations out since we closed the ZIL.
	 */
	if (mounting) {
		ASSERT3P(zfsvfs->z_kstat.dk_kstats, ==, NULL);
		error = dataset_kstats_create(&zfsvfs->z_kstat, zfsvfs->z_os);
		if (error)
			return (error);
		zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data,
		    &zfsvfs->z_kstat.dk_zil_sums);

		/*
		 * During replay we remove the read only flag to
		 * allow replays to succeed.
		 */
		if (readonly != 0) {
			zfs_change_readonly(zfsvfs, B_FALSE);
		} else {
			zap_stats_t zs;
			dsl_dir_t *dd;

			if (zap_get_stats(zfsvfs->z_os, zfsvfs->z_unlinkedobj,
			    &zs) == 0) {
				dataset_kstats_update_nunlinks_kstat(
				    &zfsvfs->z_kstat, zs.zs_num_entries);
				dprintf_ds(zfsvfs->z_os->os_dsl_dataset,
				    "num_entries in unlinked set: %llu",
				    (u_longlong_t)zs.zs_num_entries);
			}
			zfs_unlinked_drain(zfsvfs);
			dd = zfsvfs->z_os->os_dsl_dataset->ds_dir;
			dd->dd_activity_cancelled = B_FALSE;
		}

		/*
		 * Parse and replay the intent log.
		 *
		 * Because of ziltest, this must be done after
		 * zfs_unlinked_drain().  (Further note: ziltest
		 * doesn't use readonly mounts, where
		 * zfs_unlinked_drain() isn't called.)  This is because
		 * ziltest causes spa_sync() to think it's committed,
		 * but actually it is not, so the intent log contains
		 * many txg's worth of changes.
		 *
		 * In particular, if object N is in the unlinked set in
		 * the last txg to actually sync, then it could be
		 * actually freed in a later txg and then reallocated
		 * in a yet later txg.  This would write a "create
		 * object N" record to the intent log.  Normally, this
		 * would be fine because the spa_sync() would have
		 * written out the fact that object N is free, before
		 * we could write the "create object N" intent log
		 * record.
		 *
		 * But when we are in ziltest mode, we advance the "open
		 * txg" without actually spa_sync()-ing the changes to
		 * disk.  So we would see that object N is still
		 * allocated and in the unlinked set, and there is an
		 * intent log record saying to allocate it.
		 */
		if (spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			if (zil_replay_disable) {
				zil_destroy(zfsvfs->z_log, B_FALSE);
			} else {
#ifdef __FreeBSD__
				boolean_t use_nc = zfsvfs->z_use_namecache;
				zfsvfs->z_use_namecache = B_FALSE;
#endif
				zfsvfs->z_replay = B_TRUE;
				zil_replay(zfsvfs->z_os, zfsvfs,
				    zfs_replay_vector);
				zfsvfs->z_replay = B_FALSE;
#ifdef __FreeBSD__
				zfsvfs->z_use_namecache = use_nc;
#endif
			}
		}

		/* restore readonly bit */
		if (readonly != 0)
			zfs_change_readonly(zfsvfs, B_TRUE);
	} else {
		ASSERT3P(zfsvfs->z_kstat.dk_kstats, !=, NULL);
		zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data,
		    &zfsvfs->z_kstat.dk_zil_sums);
	}

	/*
	 * Set the objset user_ptr to track its zfsvfs.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	return (0);
}

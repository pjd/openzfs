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
 * Portions Copyright 2020 iXsystems, Inc.
 */

#ifndef _SYS_ZFS_VFSOPS_H
#define	_SYS_ZFS_VFSOPS_H

#ifdef _KERNEL
#include <sys/zfs_vfsops_os.h>

extern void zfs_set_fuid_feature(struct zfsvfs *zfsvfs);
extern int zfs_suspend_fs(zfsvfs_t *zfsvfs);
extern int zfsvfs_init(zfsvfs_t *zfsvfs, objset_t *os);
extern int zfsvfs_create(const char *osname, boolean_t readonly,
    zfsvfs_t **zfvp);
extern int zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting);
extern void zfs_unregister_callbacks(zfsvfs_t *zfsvfs);

extern boolean_t zfs_is_readonly(zfsvfs_t *zfsvfs);
extern void zfs_change_readonly(zfsvfs_t *zfsvfs, boolean_t on);
extern int zfs_register_callbacks(zfsvfs_t *zfsvfs);
extern int zfsvfs_teardown(zfsvfs_t *zfsvfs, boolean_t unmounting);
#endif

extern void zfsvfs_update_fromname(const char *, const char *);

#endif /* _SYS_ZFS_VFSOPS_H */

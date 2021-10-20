/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 * Copyright (c) 2020, Pawel Jakub Dawidek <pawel@dawidek.net>. All rights reserved.
 */

#ifndef _SYS_BRT_H
#define	_SYS_BRT_H

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int brt_object_count(brt_t *brt, uint64_t *count);
extern int brt_object_info(brt_t *brt, dmu_object_info_t *);
extern boolean_t brt_object_exists(brt_t *brt);

extern boolean_t brt_entry_decref(brt_t *brt, const blkptr_t *bp);

extern uint64_t brt_get_dspace(spa_t *spa);
extern uint64_t brt_get_pool_ratio(spa_t *spa);

extern brt_t *brt_select(spa_t *spa);
extern boolean_t brt_may_exists(spa_t *spa, const blkptr_t *bp);
extern void brt_enter(brt_t *brt);
extern void brt_exit(brt_t *brt);
extern void brt_init(void);
extern void brt_fini(void);
extern brt_entry_t *brt_lookup(brt_t *brt, const blkptr_t *bp);

extern boolean_t brt_contains(spa_t *spa, const blkptr_t *bp);

extern void brt_pending_add(brt_t *brt, const blkptr_t *bp, dmu_tx_t *tx);
extern void brt_pending_apply(brt_t *brt, uint64_t txg);

extern void brt_create(spa_t *spa);
extern int brt_load(spa_t *spa);
extern void brt_unload(spa_t *spa);
extern void brt_sync(spa_t *spa, uint64_t txg);
extern int brt_object_update(brt_t *brt, brt_entry_t *bre, dmu_tx_t *tx);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BRT_H */

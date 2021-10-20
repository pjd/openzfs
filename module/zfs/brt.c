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
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2020, Pawel Jakub Dawidek <pawel@dawidek.net>. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/brt.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/arc.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>

/*
 * BRT - Block Reference Table.
 */
#define	BRT_OBJECT_ENTRIES_NAME	"net.dawidek:brt:entries"

/*
 * In-core brt
 */
struct brt {
	kmutex_t	brt_lock;
	avl_tree_t	brt_tree;
	spa_t		*brt_spa;
	objset_t	*brt_os;
	uint64_t	brt_object;
	list_t		brt_pending;
};

/*
 * On-disk brt entry:  key (name) and physical storage (value).
 */
typedef struct brt_key {
	uint64_t	brk_vdev;
	uint64_t	brk_offset;
} brt_key_t;

#define	BRT_KEY_WORDS	(sizeof (brt_key_t) / sizeof (uint64_t))

typedef struct brt_phys {
	uint64_t	brp_refcnt;
} brt_phys_t;

/*
 * In-core brt entry
 */
struct brt_entry {
	brt_key_t	bre_key;
	brt_phys_t	bre_phys;
	avl_node_t	bre_node;
};

typedef struct brt_pending_entry {
	brt_key_t	bpe_key;
	uint64_t	bpe_dsize;
	uint64_t	bpe_txg;
	list_node_t	bpe_node;
} brt_pending_entry_t;

static kmem_cache_t *brt_entry_cache;
static kmem_cache_t *brt_pending_entry_cache;

/*
 * Enable/disable prefetching of BRT entries that we are going to modify.
 */
int zfs_brt_prefetch = 0;

int brt_zap_leaf_blockshift = 12;
int brt_zap_indirect_blockshift = 12;

kstat_t	*brt_ksp = NULL;

typedef struct brt_stats {
	kstat_named_t brt_addref_entry_in_memory;
	kstat_named_t brt_addref_entry_not_on_disk;
	kstat_named_t brt_addref_entry_on_disk;
	kstat_named_t brt_addref_entry_read_lost_race;
	kstat_named_t brt_decref_entry_in_memory;
	kstat_named_t brt_decref_entry_loaded_from_disk;
	kstat_named_t brt_decref_entry_not_in_memory;
	kstat_named_t brt_decref_entry_not_on_disk;
	kstat_named_t brt_decref_entry_read_lost_race;
	kstat_named_t brt_decref_entry_still_referenced;
	kstat_named_t brt_decref_free_data_later;
	kstat_named_t brt_decref_free_data_now;
	kstat_named_t brt_decref_no_entry;
} brt_stats_t;

static brt_stats_t brt_stats = {
	{ "addref_entry_in_memory",		KSTAT_DATA_UINT64 },
	{ "addref_entry_not_on_disk",		KSTAT_DATA_UINT64 },
	{ "addref_entry_on_disk",		KSTAT_DATA_UINT64 },
	{ "addref_entry_read_lost_race",	KSTAT_DATA_UINT64 },
	{ "decref_entry_in_memory",		KSTAT_DATA_UINT64 },
	{ "decref_entry_loaded_from_disk",	KSTAT_DATA_UINT64 },
	{ "decref_entry_not_in_memory",		KSTAT_DATA_UINT64 },
	{ "decref_entry_not_on_disk",		KSTAT_DATA_UINT64 },
	{ "decref_entry_read_lost_race",	KSTAT_DATA_UINT64 },
	{ "decref_entry_still_referenced",	KSTAT_DATA_UINT64 },
	{ "decref_free_data_later",		KSTAT_DATA_UINT64 },
	{ "decref_free_data_now",		KSTAT_DATA_UINT64 },
	{ "decref_no_entry",			KSTAT_DATA_UINT64 }
};

#define	BRTSTAT_BUMP(stat)	atomic_inc_64(&brt_stats.stat.value.ui64)

static int
brt_zap_create(objset_t *os, uint64_t *objectp, dmu_tx_t *tx)
{
	zap_flags_t flags = ZAP_FLAG_HASH64 | ZAP_FLAG_UINT64_KEY;

	*objectp = zap_create_flags(os, 0, flags, DMU_OTN_ZAP_METADATA,
	    brt_zap_leaf_blockshift, brt_zap_indirect_blockshift,
	    DMU_OT_NONE, 0, tx);

	return (*objectp == 0 ? SET_ERROR(ENOTSUP) : 0);
}

static int
brt_zap_destroy(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	return (zap_destroy(os, object, tx));
}

static int
brt_zap_lookup(objset_t *os, uint64_t object, brt_entry_t *bre)
{
	uint64_t one, physsize;
	int error;

	/* XXXPJD: Byte order of bre_key? */
	error = zap_length_uint64(os, object, (uint64_t *)&bre->bre_key,
	    BRT_KEY_WORDS, &one, &physsize);
	if (error)
		return (error);

	ASSERT(one == 1);
	ASSERT(physsize == sizeof(bre->bre_phys));

	return (zap_lookup_uint64(os, object, (uint64_t *)&bre->bre_key,
	    BRT_KEY_WORDS, 1, sizeof(bre->bre_phys), &bre->bre_phys));
}

static void
brt_zap_prefetch(objset_t *os, uint64_t object, brt_entry_t *bre)
{
	(void) zap_prefetch_uint64(os, object, (uint64_t *)&bre->bre_key,
	    BRT_KEY_WORDS);
}

static int
brt_zap_update(objset_t *os, uint64_t object, brt_entry_t *bre, dmu_tx_t *tx)
{

	/* XXXPJD: Byte order of bre_key? */
	return (zap_update_uint64(os, object, (uint64_t *)&bre->bre_key,
	    BRT_KEY_WORDS, 1, sizeof(bre->bre_phys), &bre->bre_phys, tx));
}

static int
brt_zap_remove(objset_t *os, uint64_t object, brt_entry_t *bre, dmu_tx_t *tx)
{

	/* XXXPJD: Byte order of bre_key? */
	return (zap_remove_uint64(os, object, (uint64_t *)&bre->bre_key,
	    BRT_KEY_WORDS, tx));
}

static int
brt_zap_count(objset_t *os, uint64_t object, uint64_t *count)
{
	return (zap_count(os, object, count));
}

static void
brt_key_fill(brt_key_t *brk, const blkptr_t *bp)
{

	brk->brk_vdev = DVA_GET_VDEV(&bp->blk_dva[0]);
	brk->brk_offset = DVA_GET_OFFSET(&bp->blk_dva[0]);
}

static void
brt_phys_addref(brt_phys_t *brp)
{
	brp->brp_refcnt++;
}

static boolean_t
brt_phys_decref(brt_phys_t *brp)
{

	ASSERT(brp->brp_refcnt > 0);
	brp->brp_refcnt--;

	return (brp->brp_refcnt == 0);
}

static uint64_t
brt_phys_total_refcnt(const brt_entry_t *bre)
{
	return (bre->bre_phys.brp_refcnt);
}

static void
brt_object_create(brt_t *brt, dmu_tx_t *tx)
{
	spa_t *spa = brt->brt_spa;
	objset_t *os = brt->brt_os;
	uint64_t *objectp = &brt->brt_object;

	ASSERT(*objectp == 0);
	VERIFY(brt_zap_create(os, objectp, tx) == 0);
	ASSERT(*objectp != 0);

	VERIFY(zap_add(os, DMU_POOL_DIRECTORY_OBJECT, BRT_OBJECT_ENTRIES_NAME,
	    sizeof (uint64_t), 1, objectp, tx) == 0);

	spa_feature_incr(spa, SPA_FEATURE_BLOCK_CLONING, tx);
}

static void
brt_object_destroy(brt_t *brt, dmu_tx_t *tx)
{
	spa_t *spa = brt->brt_spa;
	objset_t *os = brt->brt_os;
	uint64_t *objectp = &brt->brt_object;
	uint64_t count;

	ASSERT(*objectp != 0);
	VERIFY(brt_object_count(brt, &count) == 0 && count == 0);
	VERIFY0(zap_remove(os, DMU_POOL_DIRECTORY_OBJECT,
	    BRT_OBJECT_ENTRIES_NAME, tx));
	VERIFY0(brt_zap_destroy(os, *objectp, tx));

	spa_feature_decr(spa, SPA_FEATURE_BLOCK_CLONING, tx);

	*objectp = 0;
}

static int
brt_object_load(brt_t *brt)
{
	int error;

	error = zap_lookup(brt->brt_os, DMU_POOL_DIRECTORY_OBJECT,
	    BRT_OBJECT_ENTRIES_NAME, sizeof (uint64_t), 1, &brt->brt_object);
	if (error != 0)
		return (error);

	return (0);
}

static int
brt_object_lookup(brt_t *brt, brt_entry_t *bre)
{

	if (!brt_object_exists(brt))
		return (SET_ERROR(ENOENT));

	return (brt_zap_lookup(brt->brt_os, brt->brt_object, bre));
}

static void
brt_object_prefetch(brt_t *brt, brt_entry_t *bre)
{
	if (!brt_object_exists(brt))
		return;

	brt_zap_prefetch(brt->brt_os, brt->brt_object, bre);
}

int
brt_object_update(brt_t *brt, brt_entry_t *bre, dmu_tx_t *tx)
{
	ASSERT(brt_object_exists(brt));

	return (brt_zap_update(brt->brt_os, brt->brt_object, bre, tx));
}

static int
brt_object_remove(brt_t *brt, brt_entry_t *bre, dmu_tx_t *tx)
{

	if (!brt_object_exists(brt))
		return (ENOENT);

	return (brt_zap_remove(brt->brt_os, brt->brt_object, bre, tx));
}

int
brt_object_count(brt_t *brt, uint64_t *count)
{
	ASSERT(brt_object_exists(brt));

	return (brt_zap_count(brt->brt_os, brt->brt_object, count));
}

int
brt_object_info(brt_t *brt, dmu_object_info_t *doi)
{
	if (!brt_object_exists(brt))
		return (SET_ERROR(ENOENT));

	return (dmu_object_info(brt->brt_os, brt->brt_object, doi));
}

boolean_t
brt_object_exists(brt_t *brt)
{
	return (!!brt->brt_object);
}

/*
 * Return TRUE if we _can_ have BRT entry for this bp. It might be false
 * positive, but gives as quick answer if we should look into BRT, which
 * may require reads and thus will be more expensive.
 */
boolean_t
brt_may_exists(spa_t *spa, const blkptr_t *bp)
{
	brt_t *brt = spa->spa_brt;

	if (!avl_is_empty(&brt->brt_tree))
		return (TRUE);

	if (!brt_object_exists(brt))
		return (FALSE);

	return (TRUE);
}

uint64_t
brt_get_dspace(spa_t *spa)
{

	return (spa->spa_brt->brt_dsize);
}

uint64_t
brt_get_pool_ratio(spa_t *spa)
{
	brt_t *brt = spa->spa_brt;

	if (brt->brt_dsize == 0)
		return (100);

	return ((brt->brt_drefsize + brt->brt_dsize) * 100 / brt->brt_drefsize);
}

brt_t *
brt_select(spa_t *spa)
{
	return (spa->spa_brt);
}

void
brt_enter(brt_t *brt)
{
	mutex_enter(&brt->brt_lock);
}

void
brt_exit(brt_t *brt)
{
	mutex_exit(&brt->brt_lock);
}

static void
brt_stat_init(void)
{
	brt_ksp = kstat_create("zfs", 0, "brt", "misc", KSTAT_TYPE_NAMED,
	    sizeof (brt_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (brt_ksp != NULL) {
		brt_ksp->ks_data = &brt_stats;
		kstat_install(brt_ksp);
	}
}

static void
brt_stat_fini(void)
{
	if (brt_ksp != NULL) {
		kstat_delete(brt_ksp);
		brt_ksp = NULL;
	}
}

void
brt_init(void)
{
	brt_entry_cache = kmem_cache_create("brt_entry_cache",
	    sizeof (brt_entry_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	brt_pending_entry_cache = kmem_cache_create("brt_pending_entry_cache",
	    sizeof (brt_pending_entry_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	brt_stat_init();
}

void
brt_fini(void)
{
	brt_stat_fini();

	kmem_cache_destroy(brt_entry_cache);
}

static brt_entry_t *
brt_alloc(const brt_key_t *brk)
{
	brt_entry_t *bre;

	bre = kmem_cache_alloc(brt_entry_cache, KM_SLEEP);
	bzero(bre, sizeof (brt_entry_t));

	bre->bre_key = *brk;

	return (bre);
}

static void
brt_free(brt_entry_t *bre)
{

	kmem_cache_free(brt_entry_cache, bre);
}

static void
brt_entry_addref(brt_t *brt, const brt_key_t *brk, uint64_t dsize)
{
	brt_entry_t *bre, *racebre;
	avl_index_t where;
	int error;

	ASSERT(MUTEX_HELD(&brt->brt_lock));

	bre = avl_find(&brt->brt_tree, brk, NULL);
	if (bre != NULL) {
		BRTSTAT_BUMP(brt_addref_entry_in_memory);
	} else {
		bre = brt_alloc(brk);

		brt_exit(brt);

		error = brt_object_lookup(brt, bre);
		ASSERT(error == 0 || error == ENOENT);
		if (error == 0) {
			BRTSTAT_BUMP(brt_addref_entry_on_disk);
		} else {
			BRTSTAT_BUMP(brt_addref_entry_not_on_disk);
		}

		brt_enter(brt);

		racebre = avl_find(&brt->brt_tree, brk, &where);
		if (racebre != NULL) {
			BRTSTAT_BUMP(brt_addref_entry_read_lost_race);
			brt_free(bre);
			bre = racebre;
		}
		if (racebre == NULL) {
			avl_insert(&brt->brt_tree, bre, where);
		}
	}
	brt_phys_addref(&bre->bre_phys);
}

/* Return TRUE if block should be freed immediately. */
boolean_t
brt_entry_decref(brt_t *brt, const blkptr_t *bp)
{
	brt_entry_t *bre, *racebre, bre_search;
	avl_index_t where;
	int error;

	brt_key_fill(&bre_search.bre_key, bp);

	brt_enter(brt);

	bre = avl_find(&brt->brt_tree, &bre_search, NULL);
	if (bre != NULL) {
		BRTSTAT_BUMP(brt_decref_entry_in_memory);
		goto out;
	} else {
		BRTSTAT_BUMP(brt_decref_entry_not_in_memory);
	}

	bre = brt_alloc(&bre_search.bre_key);

	brt_exit(brt);

	error = brt_object_lookup(brt, bre);
	ASSERT(error == 0 || error == ENOENT);

	brt_enter(brt);

	if (error == ENOENT) {
		BRTSTAT_BUMP(brt_decref_entry_not_on_disk);
		brt_free(bre);
		bre = NULL;
		goto out;
	}

	racebre = avl_find(&brt->brt_tree, &bre_search, &where);
	if (racebre != NULL) {
		BRTSTAT_BUMP(brt_decref_entry_read_lost_race);
		brt_free(bre);
		bre = racebre;
		goto out;
	}

	BRTSTAT_BUMP(brt_decref_entry_loaded_from_disk);
	avl_insert(&brt->brt_tree, bre, where);

out:
	if (bre == NULL) {
		/*
		 * This is a free of a regular (not cloned) block.
		 */
		brt_exit(brt);
		BRTSTAT_BUMP(brt_decref_no_entry);
		return (B_TRUE);
	}
	if (brt_phys_total_refcnt(bre) == 0) {
		brt_exit(brt);
		BRTSTAT_BUMP(brt_decref_free_data_now);
		return (B_TRUE);
	}

	if (brt_phys_decref(&bre->bre_phys)) {
		BRTSTAT_BUMP(brt_decref_free_data_later);
	} else {
		BRTSTAT_BUMP(brt_decref_entry_still_referenced);
	}

	brt_exit(brt);

	return (B_FALSE);
}

static void
brt_prefetch(brt_t *brt, const blkptr_t *bp)
{
	brt_entry_t bre;

	if (!zfs_brt_prefetch || bp == NULL)
		return;

	brt_key_fill(&bre.bre_key, bp);

	brt_object_prefetch(brt, &bre);
}

void
brt_pending_add(brt_t *brt, const blkptr_t *bp, dmu_tx_t *tx)
{
	brt_pending_entry_t *bpe;

	bpe = kmem_cache_alloc(brt_pending_entry_cache, KM_SLEEP);
	bpe->bpe_txg = dmu_tx_get_txg(tx);
	ASSERT3U(bpe->bpe_txg, !=, 0);
	bpe->bpe_dsize = bp_get_dsize(brt->brt_spa, bp);

	brt_key_fill(&bpe->bpe_key, bp);

	brt_enter(brt);
	list_insert_tail(&brt->brt_pending, bpe);
	brt_exit(brt);

	/* Prefetch BRT entry, as we will need it in the syncing context. */
	brt_prefetch(brt, bp);
}

void
brt_pending_apply(brt_t *brt, uint64_t txg)
{
	brt_pending_entry_t *bpe;

	ASSERT3U(txg, !=, 0);

	brt_enter(brt);
	while ((bpe = list_head(&brt->brt_pending)) != NULL) {
		ASSERT3U(txg, <=, bpe->bpe_txg);

		if (txg < bpe->bpe_txg) {
			break;
		}

		list_remove(&brt->brt_pending, bpe);
		/*
		 * brt_entry_addref() may temporairly drop the BRT lock,
		 * but that's ok.
		 */
		brt_entry_addref(brt, &bpe->bpe_key, bpe->bpe_dsize);

		kmem_cache_free(brt_pending_entry_cache, bpe);
	}
	brt_exit(brt);
}

static int
brt_entry_compare(const void *x1, const void *x2)
{
	const brt_entry_t *bre1 = x1;
	const brt_entry_t *bre2 = x2;
	const brt_key_t *brk1 = &bre1->bre_key;
	const brt_key_t *brk2 = &bre2->bre_key;

	if (brk1->brk_vdev < brk2->brk_vdev) {
		return (-1);
	} else if (brk1->brk_vdev > brk2->brk_vdev) {
		return (1);
	}
	if (brk1->brk_offset < brk2->brk_offset) {
		return (-1);
	} else if (brk1->brk_offset > brk2->brk_offset) {
		return (1);
	}

	return (0);
}

static void
brt_table_alloc(brt_t *brt)
{
	list_create(&brt->brt_pending, sizeof (brt_pending_entry_t),
	    offsetof(brt_pending_entry_t, bpe_node));
	avl_create(&brt->brt_tree, brt_entry_compare,
	    sizeof (brt_entry_t), offsetof(brt_entry_t, bre_node));
}

static void
brt_table_free(brt_t *brt)
{
	ASSERT(avl_numnodes(&brt->brt_tree) == 0);
	avl_destroy(&brt->brt_tree);
}

void
brt_create(spa_t *spa)
{
	brt_t *brt;

	ASSERT(spa->spa_brt == NULL);

	brt = kmem_zalloc(sizeof(*brt), KM_SLEEP);
	mutex_init(&brt->brt_lock, NULL, MUTEX_DEFAULT, NULL);
	brt->brt_spa = spa;
	brt->brt_os = spa->spa_meta_objset;
	brt_table_alloc(brt);

	spa->spa_brt = brt;
}

int
brt_load(spa_t *spa)
{
	int error;

	brt_create(spa);

	error = brt_object_load(spa->spa_brt);
	if (error != 0 && error != ENOENT)
		return (error);

	return (0);
}

void
brt_unload(spa_t *spa)
{
	brt_t *brt = spa->spa_brt;

	ASSERT(brt != NULL);

	brt_table_free(brt);
	mutex_destroy(&brt->brt_lock);
	kmem_free(brt, sizeof(*brt));
	spa->spa_brt = NULL;
}

#ifdef TODO
boolean_t
brt_contains(spa_t *spa, const blkptr_t *bp)
{
	brt_t *brt;
	brt_entry_t bre;

	brt = spa->spa_brt;

	brt_key_fill(&bre.bre_key, bp);

	return (brt_object_lookup(brt, &bre));
}
#endif

static void
brt_sync_entry(brt_t *brt, brt_entry_t *bre, dmu_tx_t *tx, uint64_t txg)
{
	brt_phys_t *brp = &bre->bre_phys;

	if (brp->brp_refcnt == 0) {
		int error;

		error = brt_object_remove(brt, bre, tx);
		ASSERT(error == 0 || error == ENOENT);
		/*
		 * If error == ENOENT then clonefile(2) was done from a removed
		 * (but opened) file (open(), unlink()).
		 */
		ASSERT(brt_object_lookup(brt, bre) == ENOENT);
	} else {
		if (!brt_object_exists(brt)) {
			brt_object_create(brt, tx);
		}
		VERIFY(brt_object_update(brt, bre, tx) == 0);
	}
}

static void
brt_sync_table(brt_t *brt, dmu_tx_t *tx, uint64_t txg)
{
	brt_entry_t *bre;
	void *cookie = NULL;

	if (avl_numnodes(&brt->brt_tree) == 0) {
		return;
	}

	while ((bre = avl_destroy_nodes(&brt->brt_tree, &cookie)) != NULL) {
		brt_sync_entry(brt, bre, tx, txg);
		brt_free(bre);
	}

	if (brt_object_exists(brt)) {
		uint64_t count;

		VERIFY(brt_object_count(brt, &count) == 0);
		if (count == 0) {
			brt_object_destroy(brt, tx);
		}
	}
}

void
brt_sync(spa_t *spa, uint64_t txg)
{
	dmu_tx_t *tx;
	brt_t *brt;

	ASSERT(spa_syncing_txg(spa) == txg);

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);

	brt = spa->spa_brt;
	brt_enter(brt);
	brt_sync_table(brt, tx, txg);
	brt_exit(brt);

	dmu_tx_commit(tx);
}

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_brt, zfs_brt_, prefetch, INT, ZMOD_RW,
	"Enable prefetching of BRT entries");
/* END CSTYLED */

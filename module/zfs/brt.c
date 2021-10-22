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
#include <sys/vdev_impl.h>
#include <icp/include/sys/bitmap.h>	/* XXX */

/*
 * BRT - Block Reference Table.
 */
#define	BRT_OBJECT_ENTRIES_NAME	"net.dawidek:brt:entries"
#define	BRT_OBJECT_VDEV_PREFIX	"net.dawidek:brt:vdev-"

/*
 * We divide each VDEV into 1GB chunks. Each chunk is represented in memory
 * by a 64bit counter, thus 1TB VDEV requires 8kB of memory.
 */
#define	BRT_RANGE_CHUNK	(1024 * 1024 * 1024)
/*
 * We don't want to update the whole structure every time. Maintain bitmap
 * of dirty blocks within the regions, so that single bit represents a
 * block size of refcounts. For example if we have a 64TB vdev then all
 * refcounts take 512kB of memory. If the block size if 4kB then we want to
 * have 128 separate bits dirty bits. Each bit will represent change in one
 * of 512 refcounts (4kB / sizeof(uint64_t)).
 */
#define	BRT_RANGE_SIZE_TO_NBLOCKS(size, blocksize)			\
	(((size) - 1) / (blocksize) / sizeof(uint64_t) + 1)

typedef struct brt_vdev_phys {
	uint64_t	bvp_size;
	uint64_t	bvp_count;
	uint64_t	bvp_drefsize;
	uint64_t	bvp_dsize;
} brt_vdev_phys_t;

typedef struct brt_vdev {
	uint64_t	bv_vdev;
	/*
	 * Sum of all bv_refcount[]s.
	 */
	uint64_t	bv_count;
	/*
	 * Remember VDEV size, so we can reallocate if its size changes.
	 */
	uint64_t	bv_size;
	/*
	 * This is the array with all the refcounts (one refcount per 1GB).
	 */
	uint64_t	*bv_refcount;
	/*
	 * Object number in the MOS.
	 */
	uint64_t	bv_object;
	/*
	 * Disk space savings thanks to BRT.
	 */
	uint64_t	bv_drefsize;
	uint64_t	bv_dsize;
	/*
	 * bv_refcount[] potentially can be a bit to big to sychronize it all
	 * when we just changed few refcounts. The fields below allow us to
	 * track updates to bv_refcount[] array since the last sync.
	 * A single bit in the bv_bitmap represents as many refcounts as can
	 * fit into a single brt_blocksize, where brt_blocksize is
	 * (1 << spa->spa_min_ashift).
	 * For example we have 65536 refcounts in the bv_refcount array
	 * (so the whole array is 512kB). We updated bv_refcount[2] and
	 * bv_refcount[5]. In that case only first bit in the bv_bitmap will
	 * be set and we will write only first 4kB out of 512kB (assuming
	 * spa_min_ashift is 12).
	 */
	boolean_t	bv_dirty;
	ulong_t		*bv_bitmap;
	uint64_t	bv_nblocks;
} brt_vdev_t;

/*
 * In-core brt
 */
typedef struct brt {
	kmutex_t	brt_lock;
	avl_tree_t	brt_tree;
	spa_t		*brt_spa;
	objset_t	*brt_os;
	uint64_t	brt_object;
	uint64_t	brt_blocksize;
	uint64_t	brt_drefsize;
	uint64_t	brt_dsize;
	list_t		brt_pending;
	brt_vdev_t	*brt_vdevs;
	uint64_t	brt_nvdevs;
} brt_t;

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
typedef struct brt_entry {
	brt_key_t	bre_key;
	brt_phys_t	bre_phys;
	avl_node_t	bre_node;
} brt_entry_t;

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

static void
brt_enter(brt_t *brt)
{
	mutex_enter(&brt->brt_lock);
}

static void
brt_exit(brt_t *brt)
{
	mutex_exit(&brt->brt_lock);
}

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
brt_vdev_dump(brt_t *brt)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid;

	if (brt->brt_nvdevs == 0) {
		printf("BRT empty\n");
		return;
	}

	printf("BRT vdev dump:\n");
	for (vdevid = 0; vdevid < brt->brt_nvdevs; vdevid++) {
		uint64_t idx;

		brtvd = &brt->brt_vdevs[vdevid];
		printf("[vdevid=%ju/%ju] dirty=%d size=%ju count=%ju nblocks=%ju bitmapsize=%ju\n", (uintmax_t)vdevid, (uintmax_t)brtvd->bv_vdev, brtvd->bv_dirty, (uintmax_t)brtvd->bv_size, (uintmax_t)brtvd->bv_count, (uintmax_t)brtvd->bv_nblocks, (uintmax_t)BT_SIZEOFMAP(brtvd->bv_nblocks));
		if (!brtvd->bv_dirty) {
			continue;
		}
		printf("refcounts:\n");
		for (idx = 0; idx < brtvd->bv_size; idx++) {
			if (brtvd->bv_refcount[idx] > 0) {
				printf("  [%04ju] %ju\n", (uintmax_t)idx, (uintmax_t)brtvd->bv_refcount[idx]);
			}
		}
		printf("bitmap: ");
		for (idx = 0; idx < brtvd->bv_nblocks; idx++) {
			printf("%d", BT_TEST(brtvd->bv_bitmap, idx));
		}
		printf("\n");
	}
}

static void
brt_vdev_create(brt_t *brt, brt_vdev_t *brtvd, dmu_tx_t *tx)
{
	char name[64];

	ASSERT(MUTEX_HELD(&brt->brt_lock));
	ASSERT0(brtvd->bv_object);
	ASSERT(brtvd->bv_refcount != NULL);
	ASSERT(brtvd->bv_size > 0);
	ASSERT(brtvd->bv_bitmap != NULL);
	ASSERT(brtvd->bv_nblocks > 0);

	/*
	 * We allocate DMU buffer to store the bv_refcount[] array.
	 * We will keep array size (bv_size) and cummulative count for all
	 * bv_refcount[]s (bv_count) in the bonus buffer.
	 */
	brtvd->bv_object = dmu_object_alloc(brt->brt_os,
	    DMU_OTN_UINT64_METADATA, brt->brt_blocksize,
	    DMU_OTN_UINT64_METADATA, sizeof (brt_vdev_phys_t), tx);

	snprintf(name, sizeof(name), "%s%ju", BRT_OBJECT_VDEV_PREFIX,
	    (uintmax_t)brtvd->bv_vdev);
	VERIFY0(zap_add(brt->brt_os, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), 1, &brtvd->bv_object, tx));
}

static void
brt_vdev_load(brt_t *brt, brt_vdev_t *brtvd)
{
	char name[64];
	dmu_buf_t *db;
	brt_vdev_phys_t *bvphys;
	int error;

	snprintf(name, sizeof(name), "%s%ju", BRT_OBJECT_VDEV_PREFIX,
	    (uintmax_t)brtvd->bv_vdev);
	error = zap_lookup(brt->brt_os, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), 1, &brtvd->bv_object);
	ASSERT(error == 0 || error == ENOENT);
	if (error != 0)
		return;

	error = dmu_bonus_hold(brt->brt_os, brtvd->bv_object, FTAG, &db);
	ASSERT0(error);
	if (error != 0)
		return;

	bvphys = db->db_data;
	/* TODO: We don't support VDEV shrinking. */
	ASSERT3U(bvphys->bvp_size, <=, brtvd->bv_size);

	/*
	 * If VDEV grew, we will leave new bv_refcount[] entries untouched.
	 */
	error = dmu_read(brt->brt_os, brtvd->bv_object, 0,
	    bvphys->bvp_size * sizeof (uint64_t), brtvd->bv_refcount,
	    DMU_READ_NO_PREFETCH);
	ASSERT0(error);

	brtvd->bv_count = bvphys->bvp_count;
	brtvd->bv_drefsize = bvphys->bvp_drefsize;
	brtvd->bv_dsize = bvphys->bvp_dsize;
	brt->brt_drefsize += brtvd->bv_drefsize;
	brt->brt_dsize += brtvd->bv_dsize;

	dmu_buf_rele(db, FTAG);
}

static void
brt_vdev_destroy(brt_t *brt, brt_vdev_t *brtvd, dmu_tx_t *tx)
{
	char name[64];
	dmu_buf_t *db;
	brt_vdev_phys_t *bvphys;

	VERIFY0(dmu_bonus_hold(brt->brt_os, brtvd->bv_object, FTAG, &db));
	bvphys = db->db_data;
	ASSERT0(bvphys->bvp_count);
	ASSERT0(bvphys->bvp_drefsize);
	ASSERT0(bvphys->bvp_dsize);
	dmu_buf_rele(db, FTAG);

	ASSERT0(dmu_object_free(brt->brt_os, brtvd->bv_object, tx));

	snprintf(name, sizeof(name), "%s%ju", BRT_OBJECT_VDEV_PREFIX,
	    (uintmax_t)brtvd->bv_vdev);
	VERIFY0(zap_remove(brt->brt_os, DMU_POOL_DIRECTORY_OBJECT, name, tx));

	brtvd->bv_object = 0;
}

static void
brt_vdev_sync_one(brt_t *brt, brt_vdev_t *brtvd, dmu_tx_t *tx)
{
	dmu_buf_t *db;
	brt_vdev_phys_t *bvphys;

	ASSERT(brtvd->bv_dirty);

	if (brtvd->bv_object == 0) {
		/*
		 * No BRT VDEV object for this VDEV yet, allocate one.
		 */
		brt_vdev_create(brt, brtvd, tx);
	}

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY0(dmu_bonus_hold(brt->brt_os, brtvd->bv_object, FTAG, &db));

	/*
	 * TODO: Walk through brtvd->bv_bitmap and write only dirty parts.
	 */
	dmu_write(brt->brt_os, brtvd->bv_object, 0,
	    brtvd->bv_size * sizeof (uint64_t), brtvd->bv_refcount, tx);

	dmu_buf_will_dirty(db, tx);
	bvphys = db->db_data;
	bvphys->bvp_size = brtvd->bv_size;
	bvphys->bvp_count = brtvd->bv_count;
	bvphys->bvp_drefsize = brtvd->bv_drefsize;
	bvphys->bvp_dsize = brtvd->bv_dsize;
	dmu_buf_rele(db, FTAG);

	bzero(brtvd->bv_bitmap, BT_SIZEOFMAP(brtvd->bv_nblocks));
	brtvd->bv_dirty = FALSE;
}

static void
brt_vdev_realloc(brt_t *brt, brt_vdev_t *brtvd)
{
	vdev_t *vd;
	uint64_t *refcount;
	ulong_t *bitmap;
	uint64_t nblocks, size;

	ASSERT(MUTEX_HELD(&brt->brt_lock));

	spa_config_enter(brt->brt_spa, SCL_VDEV, FTAG, RW_READER);
	vd = vdev_lookup_top(brt->brt_spa, brtvd->bv_vdev);
	size = vdev_get_min_asize(vd) / BRT_RANGE_CHUNK;
	spa_config_exit(brt->brt_spa, SCL_VDEV, FTAG);

	refcount = kmem_zalloc(sizeof(uint64_t) * size, KM_SLEEP);
	nblocks = BRT_RANGE_SIZE_TO_NBLOCKS(size, brt->brt_blocksize);
	bitmap = kmem_zalloc(BT_SIZEOFMAP(nblocks), KM_SLEEP);

	if (brtvd->bv_size == 0) {
		ASSERT(brtvd->bv_refcount == NULL);
		ASSERT(brtvd->bv_bitmap == NULL);
		ASSERT0(brtvd->bv_nblocks);
	} else {
		ASSERT(brtvd->bv_refcount != NULL);
		ASSERT(brtvd->bv_bitmap != NULL);
		ASSERT(brtvd->bv_nblocks > 0);
		/*
		 * TODO: Allow vdev shrinking. We only need to implement
		 * shrinking the on-disk BRT VDEV object.
		 * dmu_free_range(brt->brt_os, brtvd->bv_object, offset, size, tx);
		 */
		ASSERT3U(brtvd->bv_size, <=, size);

		bcopy(brtvd->bv_refcount, refcount,
		    sizeof(uint64_t) * MIN(size, brtvd->bv_size));
		bcopy(brtvd->bv_bitmap, bitmap,
		    MIN(BT_SIZEOFMAP(nblocks), BT_SIZEOFMAP(brtvd->bv_nblocks)));
		kmem_free(brtvd->bv_refcount,
		    sizeof(uint64_t) * brtvd->bv_size);
		kmem_free(brtvd->bv_bitmap, BT_SIZEOFMAP(brtvd->bv_nblocks));
	}

	brtvd->bv_size = size;
	brtvd->bv_refcount = refcount;
	brtvd->bv_bitmap = bitmap;
	brtvd->bv_nblocks = nblocks;
}

static void
brt_expand_vdevs(brt_t *brt, uint64_t nvdevs)
{
	brt_vdev_t *vdevs;
	uint64_t vdevid;

	ASSERT(MUTEX_HELD(&brt->brt_lock));
	ASSERT3U(nvdevs, >, brt->brt_nvdevs);

	vdevs = kmem_zalloc(sizeof(brt_vdev_t) * nvdevs, KM_SLEEP);
	if (brt->brt_nvdevs > 0) {
		ASSERT(brt->brt_vdevs != NULL);

		bcopy(brt->brt_vdevs, vdevs,
		    sizeof(brt_vdev_t) * brt->brt_nvdevs);
		kmem_free(brt->brt_vdevs,
		    sizeof(brt_vdev_t) * brt->brt_nvdevs);
	}
	for (vdevid = brt->brt_nvdevs; vdevid < nvdevs; vdevid++) {
		vdevs[vdevid].bv_vdev = vdevid;
	}

	brt->brt_vdevs = vdevs;
	brt->brt_nvdevs = nvdevs;
}

static boolean_t
brt_vdev_lookup(brt_t *brt, const brt_entry_t *bre)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid, idx;
	boolean_t found, unlock;

	if (!MUTEX_HELD(&brt->brt_lock)) {
		unlock = TRUE;
		brt_enter(brt);
	} else {
		unlock = FALSE;
	}

	found = FALSE;

	vdevid = bre->bre_key.brk_vdev;
	idx = bre->bre_key.brk_offset / BRT_RANGE_CHUNK;

	if (vdevid >= brt->brt_nvdevs)
		goto out;

	brtvd = &brt->brt_vdevs[vdevid];
	if (brtvd->bv_refcount == NULL || idx >= brtvd->bv_size) {
		/* VDEV must have been expanded. */
		goto out;
	}

	found = brtvd->bv_refcount[idx] > 0;
out:
	if (unlock)
		brt_exit(brt);

	return (found);
}

static void
brt_vdev_addref(brt_t *brt, const brt_entry_t *bre, uint64_t dsize)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid, idx;

	ASSERT(MUTEX_HELD(&brt->brt_lock));

	vdevid = bre->bre_key.brk_vdev;
	idx = bre->bre_key.brk_offset / BRT_RANGE_CHUNK;

	if (vdevid >= brt->brt_nvdevs) {
		/* New VDEV was added. */
		brt_expand_vdevs(brt, vdevid + 1);
	}
	ASSERT3U(vdevid, <, brt->brt_nvdevs);

	brtvd = &brt->brt_vdevs[vdevid];
	if (brtvd->bv_refcount == NULL || idx >= brtvd->bv_size) {
		/* VDEV not allocated/loaded yet or was been expanded. */
		brt_vdev_realloc(brt, brtvd);
	}

	ASSERT3U(idx, <, brtvd->bv_size);

	brtvd->bv_count++;
	brtvd->bv_refcount[idx]++;
	brtvd->bv_dirty = TRUE;
	idx = idx / brt->brt_blocksize / 8;
	BT_SET(brtvd->bv_bitmap, idx);

	brtvd->bv_dsize += dsize;
	brt->brt_dsize += dsize;
	if (brt_phys_total_refcnt(bre) == 1) {
		brtvd->bv_drefsize += dsize;
		brt->brt_drefsize += dsize;
	}
brt_vdev_dump(brt);
}

static void
brt_vdev_decref(brt_t *brt, const brt_entry_t *bre, uint64_t dsize)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid, idx;

	ASSERT(MUTEX_HELD(&brt->brt_lock));

	vdevid = bre->bre_key.brk_vdev;
	idx = bre->bre_key.brk_offset / BRT_RANGE_CHUNK;

	ASSERT3U(vdevid, <, brt->brt_nvdevs);

	brtvd = &brt->brt_vdevs[vdevid];
	ASSERT(brtvd->bv_refcount != NULL);
	ASSERT3U(idx, <, brtvd->bv_size);

	ASSERT(brtvd->bv_count > 0);
	brtvd->bv_count--;
	ASSERT(brtvd->bv_refcount[idx] > 0);
	brtvd->bv_refcount[idx]--;
	brtvd->bv_dirty = TRUE;
	idx = idx / brt->brt_blocksize / 8;
	BT_SET(brtvd->bv_bitmap, idx);

	brtvd->bv_dsize -= dsize;
	brt->brt_dsize -= dsize;
	if (brt_phys_total_refcnt(bre) == 0) {
		brtvd->bv_drefsize -= dsize;
		brt->brt_drefsize -= dsize;
	}
brt_vdev_dump(brt);
}

static void
brt_vdevs_sync(brt_t *brt, dmu_tx_t *tx)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid;

	ASSERT(MUTEX_HELD(&brt->brt_lock));

	for (vdevid = 0; vdevid < brt->brt_nvdevs; vdevid++) {
		brtvd = &brt->brt_vdevs[vdevid];
		if (brtvd->bv_dirty)
			brt_vdev_sync_one(brt, brtvd, tx);
		if (brtvd->bv_count == 0)
			brt_vdev_destroy(brt, brtvd, tx);
	}
}

static void
brt_vdevs_load(brt_t *brt)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid;

	brt_enter(brt);

	brt_expand_vdevs(brt, brt->brt_spa->spa_root_vdev->vdev_children);
	for (vdevid = 0; vdevid < brt->brt_nvdevs; vdevid++) {
		brtvd = &brt->brt_vdevs[vdevid];
		ASSERT(brtvd->bv_refcount == NULL);

		brt_vdev_realloc(brt, brtvd);
		brt_vdev_load(brt, brtvd);
	}

	brt_exit(brt);
}

static void
brt_vdevs_free(brt_t *brt)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid;

	brt_enter(brt);

	for (vdevid = 0; vdevid < brt->brt_nvdevs; vdevid++) {
		brtvd = &brt->brt_vdevs[vdevid];
		kmem_free(brtvd->bv_refcount,
		    sizeof (uint64_t) * brtvd->bv_size);
	}
	kmem_free(brt->brt_vdevs, sizeof (brt_vdev_t) * brt->brt_nvdevs);

	brt_exit(brt);
}

static boolean_t
brt_object_exists(brt_t *brt)
{
	return (!!brt->brt_object);
}

static int
brt_object_count(brt_t *brt, uint64_t *count)
{
	ASSERT(brt_object_exists(brt));

	return (brt_zap_count(brt->brt_os, brt->brt_object, count));
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

	if (!brt_vdev_lookup(brt, bre))
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

static int
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

/*
 * Return TRUE if we _can_ have BRT entry for this bp. It might be false
 * positive, but gives as quick answer if we should look into BRT, which
 * may require reads and thus will be more expensive.
 */
boolean_t
brt_may_exists(spa_t *spa, const blkptr_t *bp)
{
	brt_t *brt = spa->spa_brt;
	brt_entry_t bre_search;

	if (!avl_is_empty(&brt->brt_tree))
		return (TRUE);

	if (!brt_object_exists(brt))
		return (FALSE);

	brt_key_fill(&bre_search.bre_key, bp);

	return (brt_vdev_lookup(brt, &bre_search));
}

uint64_t
brt_get_dspace(spa_t *spa)
{

	if (spa->spa_brt == NULL)
		return (0);

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
		if (error == 0)
			BRTSTAT_BUMP(brt_addref_entry_on_disk);
		else
			BRTSTAT_BUMP(brt_addref_entry_not_on_disk);

		brt_enter(brt);

		racebre = avl_find(&brt->brt_tree, brk, &where);
		if (racebre != NULL) {
			BRTSTAT_BUMP(brt_addref_entry_read_lost_race);
			brt_free(bre);
			bre = racebre;
		}
		if (racebre == NULL)
			avl_insert(&brt->brt_tree, bre, where);
	}
	brt_phys_addref(&bre->bre_phys);
	brt_vdev_addref(brt, bre, dsize);
}

/* Return TRUE if block should be freed immediately. */
boolean_t
brt_entry_decref(spa_t *spa, const blkptr_t *bp)
{
	brt_t *brt;
	brt_entry_t *bre, *racebre, bre_search;
	avl_index_t where;
	int error;

	brt_key_fill(&bre_search.bre_key, bp);

	brt = spa->spa_brt;
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

	if (brt_phys_decref(&bre->bre_phys))
		BRTSTAT_BUMP(brt_decref_free_data_later);
	else
		BRTSTAT_BUMP(brt_decref_entry_still_referenced);
	brt_vdev_decref(brt, bre, bp_get_dsize(brt->brt_spa, bp));

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
brt_pending_add(spa_t *spa, const blkptr_t *bp, dmu_tx_t *tx)
{
	brt_t *brt;
	brt_pending_entry_t *bpe;

	brt = spa->spa_brt;

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
brt_pending_apply(spa_t *spa, uint64_t txg)
{
	brt_t *brt;
	brt_pending_entry_t *bpe;

	ASSERT3U(txg, !=, 0);

	brt = spa->spa_brt;

	brt_enter(brt);
	while ((bpe = list_head(&brt->brt_pending)) != NULL) {
		ASSERT3U(txg, <=, bpe->bpe_txg);

		if (txg < bpe->bpe_txg)
			break;

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

	if (brk1->brk_vdev < brk2->brk_vdev)
		return (-1);
	else if (brk1->brk_vdev > brk2->brk_vdev)
		return (1);

	if (brk1->brk_offset < brk2->brk_offset)
		return (-1);
	else if (brk1->brk_offset > brk2->brk_offset)
		return (1);

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
	brt->brt_blocksize = (1 << spa->spa_min_ashift);
	brt->brt_vdevs = NULL;
	brt->brt_nvdevs = 0;
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

	if (error == 0)
		brt_vdevs_load(spa->spa_brt);

	return (0);
}

void
brt_unload(spa_t *spa)
{
	brt_t *brt = spa->spa_brt;

	ASSERT(brt != NULL);

	brt_vdevs_free(brt);
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
		if (!brt_object_exists(brt))
			brt_object_create(brt, tx);
		VERIFY(brt_object_update(brt, bre, tx) == 0);
	}
}

static void
brt_sync_table(brt_t *brt, dmu_tx_t *tx, uint64_t txg)
{
	brt_entry_t *bre;
	void *cookie = NULL;

	if (avl_numnodes(&brt->brt_tree) == 0)
		return;

	while ((bre = avl_destroy_nodes(&brt->brt_tree, &cookie)) != NULL) {
		brt_sync_entry(brt, bre, tx, txg);
		brt_free(bre);
	}

	if (brt_object_exists(brt)) {
		uint64_t count;

		VERIFY(brt_object_count(brt, &count) == 0);
		if (count == 0)
			brt_object_destroy(brt, tx);
	}

	brt_vdevs_sync(brt, tx);
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

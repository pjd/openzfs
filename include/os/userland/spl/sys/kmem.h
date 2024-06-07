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

#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

struct kmem_cache;
typedef struct kmem_cache kmem_cache_t;
/*
 * Kernel memory
 */
#define	KMC_NODEBUG		UMC_NODEBUG
#define	KMC_KVMEM		0x0

#define	KM_SLEEP	0x00000000
#define	KM_PUSHPAGE	KM_SLEEP
#define	KM_NOSLEEP	0x00000001

#define	kmem_alloc(size, flags)		((void) sizeof (flags), malloc(size))
#define	kmem_zalloc(size, flags)	((void) sizeof (flags), calloc(1, size))
#define	kmem_free(ptr, size)		((void) sizeof (size), free(ptr))

#define	vmem_alloc(_s, _f)	kmem_alloc(_s, _f)
#define	vmem_zalloc(_s, _f)	kmem_zalloc(_s, _f)
#define	vmem_free(_b, _s)	kmem_free(_b, _s)

#define	kmem_cache_reap_now(_c)		do { (void) (_c); } while (0)
#define	kmem_cache_set_move(_c, _cb)	do { (void) (_c); (void) (_cb); } while (0)

#define	POINTER_INVALIDATE(pp)		(*(pp) = (void *)((uintptr_t)(*(pp)) | 0x1))
#define	POINTER_IS_VALID(p)		(!((uintptr_t)(p) & 0x3))
#if 0
#define	kmem_alloc(_s, _f)	umem_alloc(_s, _f)
#define	kmem_zalloc(_s, _f)	umem_zalloc(_s, _f)
#define	kmem_free(_b, _s)	umem_free(_b, _s)
#define	kmem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i) \
	umem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i)
#define	kmem_cache_destroy(_c)	umem_cache_destroy(_c)
#define	kmem_cache_alloc(_c, _f) umem_cache_alloc(_c, _f)
#define	kmem_cache_free(_c, _b)	umem_cache_free(_c, _b)
#define	kmem_debugging()	0
#define	POINTER_IS_VALID(_p)	0

typedef umem_cache_t kmem_cache_t;
#endif

typedef enum kmem_cbrc {
	KMEM_CBRC_YES,
	KMEM_CBRC_NO,
	KMEM_CBRC_LATER,
	KMEM_CBRC_DONT_NEED,
	KMEM_CBRC_DONT_KNOW
} kmem_cbrc_t;

extern kmem_cache_t *kmem_cache_create(const char *name, size_t bufsize,
    size_t align, int (*constructor)(void *, void *, int),
    void (*destructor)(void *, void *), void (*reclaim)(void *), void *private,
    void *vmp, int cflags);
extern void kmem_cache_destroy(kmem_cache_t *cache);
extern void *kmem_cache_alloc(kmem_cache_t *cache, int flags);
extern void kmem_cache_free(kmem_cache_t *cache, void *buf);

extern boolean_t kmem_cache_reap_active(void);
extern void kmem_cache_reap_soon(kmem_cache_t *cache);
extern void kmem_reap(void);
extern int kmem_debugging(void);
extern char *kmem_vasprintf(const char *fmt, va_list adx);
extern char *kmem_asprintf(const char *fmt, ...);

extern uint64_t spl_kmem_cache_inuse(kmem_cache_t *cache);
extern uint64_t spl_kmem_cache_entry_size(kmem_cache_t *cache);
extern void spl_kmem_cache_set_move(kmem_cache_t *skc,
    kmem_cbrc_t (move)(void *, void *, size_t, void *));

#endif	/* _SPL_KMEM_H */

/*
 * Copyright (c) 2006-2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <sys/kmem.h>
//#include <sys/kmem_cache.h>

struct kmem_cache {
	char		kc_name[32];
	size_t		kc_size;
	size_t		kc_items;
	int		(*kc_constructor)(void *, void *, int);
	void		(*kc_destructor)(void *, void *);
	void		*kc_private;
};

static int
kmem_std_constructor(void *mem, int size __unused, void *private, int flags)
{
	struct kmem_cache *cache = private;

	return (cache->kc_constructor(mem, cache->kc_private, flags));
}

static void
kmem_std_destructor(void *mem, int size __unused, void *private)
{
	struct kmem_cache *cache = private;

	cache->kc_destructor(mem, cache->kc_private);
}

kmem_cache_t *
kmem_cache_create(const char *name, size_t bufsize, size_t align __unused,
    int (*constructor)(void *, void *, int), void (*destructor)(void *, void *),
    void (*reclaim)(void *) __unused, void *private, void *vmp,
    int cflags __unused)
{
	kmem_cache_t *cache;

	ASSERT3P(vmp, ==, NULL);

	cache = kmem_alloc(sizeof (*cache), KM_SLEEP);
	strlcpy(cache->kc_name, name, sizeof (cache->kc_name));
	cache->kc_constructor = constructor;
	cache->kc_destructor = destructor;
	cache->kc_private = private;
	cache->kc_size = bufsize;
	cache->kc_items = 0;

	return (cache);
}

void
kmem_cache_destroy(kmem_cache_t *cache)
{
	assert(cache->kc_items == 0);

	kmem_free(cache, sizeof (*cache));
}

void *
kmem_cache_alloc(kmem_cache_t *cache, int flags)
{
	void *p;

	p = kmem_alloc(cache->kc_size, flags);
	if (p != NULL && cache->kc_constructor != NULL)
		kmem_std_constructor(p, cache->kc_size, cache, flags);
	cache->kc_items++;
	return (p);
}

void
kmem_cache_free(kmem_cache_t *cache, void *buf)
{
	if (cache->kc_destructor != NULL)
		kmem_std_destructor(buf, cache->kc_size, cache);
	kmem_free(buf, cache->kc_size);
	cache->kc_items--;
}

/*
 * Allow our caller to determine if there are running reaps.
 *
 * This call is very conservative and may return B_TRUE even when
 * reaping activity isn't active. If it returns B_FALSE, then reaping
 * activity is definitely inactive.
 */
boolean_t
kmem_cache_reap_active(void)
{

	return (B_FALSE);
}

/*
 * Reap (almost) everything soon.
 *
 * Note: this does not wait for the reap-tasks to complete. Caller
 * should use kmem_cache_reap_active() (above) and/or moderation to
 * avoid scheduling too many reap-tasks.
 */
void
kmem_cache_reap_soon(kmem_cache_t *cache __unused)
{
}

void
kmem_reap(void)
{
}

int
kmem_debugging(void)
{
	return (0);
}

char *
kmem_vasprintf(const char *fmt, va_list adx)
{
	char *msg;
	va_list adx2;

	va_copy(adx2, adx);
	msg = kmem_alloc(vsnprintf(NULL, 0, fmt, adx) + 1, KM_SLEEP);
	(void) vsprintf(msg, fmt, adx2);
	va_end(adx2);

	return (msg);
}

/*
 * Do not change the length of the returned string; it must be freed
 * with strfree().
 */
char *
kmem_asprintf(const char *fmt, ...)
{
	va_list adx;
	char *buf;

	va_start(adx, fmt);
	buf = kmem_vasprintf(fmt, adx);
	va_end(adx);

	return (buf);
}

uint64_t
spl_kmem_cache_inuse(kmem_cache_t *cache)
{
	return (cache->kc_items);
}

uint64_t
spl_kmem_cache_entry_size(kmem_cache_t *cache)
{
	return (cache->kc_size);
}

/*
 * Register a move callback for cache defragmentation.
 * XXX: Unimplemented but harmless to stub out for now.
 */
void
spl_kmem_cache_set_move(kmem_cache_t *skc __unused,
    kmem_cbrc_t (move)(void *, void *, size_t, void *))
{
	ASSERT3P(move, !=, NULL);
}

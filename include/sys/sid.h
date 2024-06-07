/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#ifndef _SYS_SID_H
#define	_SYS_SID_H

typedef struct ksiddomain {
	char	kd_name[0];
} ksiddomain_t;

typedef enum ksid_index {
	KSID_USER,
	KSID_GROUP,
	KSID_OWNER,
	KSID_COUNT
} ksid_index_t;

typedef int ksid_t;

static inline ksiddomain_t *
ksid_lookupdomain(const char *dom)
{
	ksiddomain_t *kd;
	size_t len = strlen(dom);

	kd = kmem_zalloc(sizeof (ksiddomain_t) + len + 1, KM_SLEEP);
	memcpy(kd->kd_name, dom, len + 1);

	return (kd);
}

static inline void
ksiddomain_rele(ksiddomain_t *ksid)
{
	size_t len = strlen(ksid->kd_name);

	kmem_free(ksid, sizeof (ksiddomain_t) + len + 1);
}

#endif /* _SYS_SID_H */

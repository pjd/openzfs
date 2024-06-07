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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#ifndef _SPL_SYS_CRED_H
#define	_SPL_SYS_CRED_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The credential is an opaque kernel private data structure defined in
 * <sys/cred_impl.h>.
 */

typedef void cred_t;

#define	CRED()	(NULL)
#define	kcred	(NULL)

#define	KUID_TO_SUID(x)		(x)
#define	KGID_TO_SGID(x)		(x)

static inline int
crgetuid(cred_t *cr __unused)
{
	return (0);
}
static inline int
crgetruid(cred_t *cr __unused)
{
	return (0);
}
static inline int
crgetgid(cred_t *cr __unused)
{
	return (0);
}
static inline void *
crgetgroups(cred_t *cr __unused)
{
	return (NULL);
}
static inline int
crgetngroups(cred_t *cr __unused)
{
	return (0);
}
static inline int
crgetzoneid(cred_t *cr __unused)
{
	return (0);
}

#define	zfs_init_idmap		(NULL)
#define	zfs_i_user_ns(vp)	(NULL)

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_SYS_CRED_H */

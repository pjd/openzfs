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
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@compeng.uni-frankfurt.de>.
 */

#ifndef _SYS_VDEV_RAIDY_H
#define	_SYS_VDEV_RAIDY_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct zio;
struct raidy_row;
struct raidy_map;
#ifdef TODO
#if !defined(_KERNEL)
struct kernel_param {};
#endif
#endif

/*
 * vdev_raidy interface
 */
void vdev_raidy_generate_parity_row(struct raidy_map *, struct raidy_row *);
void vdev_raidy_generate_parity(struct raidy_map *);
void vdev_raidy_reconstruct(struct raidy_map *, const int *, int);

extern const zio_vsd_ops_t vdev_raidy_vsd_ops;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_RAIDY_H */

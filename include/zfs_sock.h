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

#ifndef	_ZFS_SOCK_H
#define	_ZFS_SOCK_H

#include <sys/types.h>

/* This structure is passed over the socket to the daemon. */
typedef struct zfs_ioctl_arg {
	uint64_t zia_pid; /* pid of client process */
	uint64_t zia_ioctl;
	uint64_t zia_cmd; /* address of zfs_cmd_t in client's address space */
} zfs_ioctl_arg_t;

/* daemon's response to the client */
typedef struct zfs_ioctl_response {
	int zir_retval;
	int zir_errno;
} zfs_ioctl_response_t;

enum zfs_ioctl_msgtype {
	ZIM_IOCTL,
	ZIM_IOCTL_RESPONSE,
	ZIM_COPYIN,
	ZIM_COPYIN_RESPONSE,
	ZIM_COPYINSTR,
	ZIM_COPYINSTR_RESPONSE,
	ZIM_COPYOUT,
	ZIM_COPYOUT_RESPONSE,
	ZIM_GET_FD,
	ZIM_GET_FD_RESPONSE,
	ZIM_MAX
};

struct zfs_cmd;

typedef struct zfs_ioctl_msg {
	enum zfs_ioctl_msgtype zim_type;
	int zim_pad;
	union {
		struct {
			uint64_t zim_ioctl;
			uint64_t zim_cmd; /* address of zfs_cmd_t in client's address space */
		} zim_ioctl;
		struct {
			int zim_retval;
			int zim_errno;
		} zim_ioctl_response;
		struct {
			uint64_t zim_address;
			uint64_t zim_len;
		} zim_copyin;
		struct {
			int zim_errno;
			/* data follows */
		} zim_copyin_response;
		struct {
			uint64_t zim_address;
			uint64_t zim_length;
		} zim_copyinstr;
		struct {
			int zim_errno;
			int zim_length;
			/* data follows */
		} zim_copyinstr_response;
		struct {
			uint64_t zim_address;
			uint64_t zim_len;
			/* data follows */
		} zim_copyout;
		struct {
			int zim_errno;
		} zim_copyout_response;
		struct {
			int zim_fd;
			int zim_pad;
		} zim_get_fd;
		struct {
			int zim_errno;
		} zim_get_fd_response;
	} zim_u;

} zfs_ioctl_msg_t;

#endif	/* _ZFS_SOCK_H */

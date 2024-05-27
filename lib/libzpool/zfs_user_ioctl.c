/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/spa.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/zfs_ioctl.h>
#include <zfs_sock.h>
#include <libzfs.h>
#include <signal.h>

#undef SET_ERROR
#define SET_ERROR(x) (x)

static pthread_key_t pid_key;
static pthread_key_t ioctl_key;
libzfs_handle_t *g_zfs;

struct file {
	int	f_remotefd;
	int	f_localfd;
	off_t	f_offset;
	TAILQ_ENTRY(file) f_next;
};

typedef struct file file_t;

typedef struct conn_arg {
	int conn_fd;
} conn_arg_t;

struct cdev;
struct thread;
extern int zfsdev_ioctl(struct cdev *, u_long, caddr_t, int, struct thread *);
extern void zfs_ioctl_init(void);

static int
ioctl_recv(int size, void *dst)
{
	conn_arg_t *ca = pthread_getspecific(ioctl_key);
	ssize_t done;

	while (size > 0) {
		done = recv(ca->conn_fd, dst, size, 0);
		switch (done) {
		case 0:
			return (ENOTCONN);
		case -1:
			if (errno == EINTR) {
				continue;
			}
			return (errno);
		default:
			size -= done;
			dst = (char *)dst + done;
			break;
		}
	}

	return (0);
}

static int
ioctl_send(int size, const void *data)
{
	conn_arg_t *ca = pthread_getspecific(ioctl_key);
	ssize_t done;

	while (size > 0) {
		done = send(ca->conn_fd, data, size, 0);
		switch (done) {
		case 0:
			return (ENOTCONN);
		case -1:
			if (errno == EINTR) {
				continue;
			}
			return (errno);
		default:
			size -= done;
			data = (const char *)data + done;
			break;
		}
	}

	return (0);
}

static int
ioctl_sendmsg(const zfs_ioctl_msg_t *msg, int payload_len, const void *payload)
{
	int error;

	error = ioctl_send(sizeof (*msg), msg);
	if (error != 0)
		return (error);

	if (payload_len != 0)
		error = ioctl_send(payload_len, payload);
	return (error);
}

static int
ioctl_recvmsg(zfs_ioctl_msg_t *msg)
{

	return (ioctl_recv(sizeof (*msg), msg));
}

int
ddi_copyin(const void *src, void *dst, size_t size, int flag __unused)
{
	int error;
	zfs_ioctl_msg_t msg = { 0 };

	msg.zim_type = ZIM_COPYIN;
	msg.zim_u.zim_copyin.zim_address = (uintptr_t)src;
	msg.zim_u.zim_copyin.zim_len = size;
	error = ioctl_sendmsg(&msg, 0, NULL);
	if (error != 0)
		return (error);

	error = ioctl_recvmsg(&msg);
	if (error != 0)
		return (error);

	ASSERT3U(msg.zim_type, ==, ZIM_COPYIN_RESPONSE);
	if (msg.zim_type != ZIM_COPYIN_RESPONSE)
		return (EINVAL);
	if (msg.zim_u.zim_copyin_response.zim_errno != 0)
		return (msg.zim_u.zim_copyin_response.zim_errno);

	error = ioctl_recv(size, dst);
	if (error != 0) {
		printf("%s: errno=%u\n", __func__, error);
	}
	return (error);
}

int
copyinstr(const char *src, char *dst, size_t size, size_t *copied)
{
	int error;
	zfs_ioctl_msg_t msg = { 0 };

	assert(copied == NULL);

	msg.zim_type = ZIM_COPYINSTR;
	msg.zim_u.zim_copyinstr.zim_address = (uintptr_t)src;
	msg.zim_u.zim_copyinstr.zim_length = size;
	error = ioctl_sendmsg(&msg, 0, NULL);
	if (error != 0)
		return (error);

	error = ioctl_recvmsg(&msg);
	if (error != 0)
		return (error);

	ASSERT3U(msg.zim_type, ==, ZIM_COPYINSTR_RESPONSE);
	if (msg.zim_type != ZIM_COPYINSTR_RESPONSE)
		return (EINVAL);
	if (msg.zim_u.zim_copyinstr_response.zim_errno != 0)
		return (msg.zim_u.zim_copyinstr_response.zim_errno);

	error = ioctl_recv(msg.zim_u.zim_copyinstr_response.zim_length, dst);
	if (error != 0) {
		printf("%s: errno=%u\n", __func__, error);
	}
	return (error);
}

int
ddi_copyout(const void *src, void *dst, size_t size, int flag __unused)
{
	int error;
	zfs_ioctl_msg_t msg = { 0 };

	msg.zim_type = ZIM_COPYOUT;
	msg.zim_u.zim_copyout.zim_address = (uintptr_t)dst;
	msg.zim_u.zim_copyout.zim_len = size;
	error = ioctl_sendmsg(&msg, size, src);
	if (error != 0)
		return (error);

	error = ioctl_recvmsg(&msg);
	if (error != 0)
		return (error);

	ASSERT3U(msg.zim_type, ==, ZIM_COPYOUT_RESPONSE);
	if (msg.zim_type != ZIM_COPYOUT_RESPONSE)
		return (EINVAL);
	error = msg.zim_u.zim_copyin_response.zim_errno;
	if (error != 0) {
		printf("%s: errno=%u\n", __func__, error);
	}
	return (error);
}

#if 0
#ifndef MSG_CMSG_CLOEXEC
#define	MSG_CMSG_CLOEXEC	(0)
#endif

static int
fd_recv(int *fdp)
{
	conn_arg_t *ca = pthread_getspecific(ioctl_key);
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	uint8_t dummy;
	int error;

	bzero(&msg, sizeof(msg));
	bzero(&iov, sizeof(iov));

	/*
	 * XXX: We recveive one byte along with the control message, because
	 *      setting msg_iov to NULL only works if this is the first
	 *      packet received over the socket. Once we receive some data we
	 *      won't be able to receive control messages anymore. This is most
	 *      likely a kernel bug.
	 */
	iov.iov_base = &dummy;
	iov.iov_len = sizeof(dummy);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_controllen = CMSG_SPACE(sizeof(int));
	msg.msg_control = calloc(1, msg.msg_controllen);
	if (msg.msg_control == NULL) {
		return (ENOMEM);
	}

	for (;;) {
		if (recvmsg(ca->conn_fd, &msg, MSG_CMSG_CLOEXEC) == -1) {
			if (errno == EINTR) {
				continue;
			}
			error = errno;
			goto out;
		}
		break;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET ||
	    cmsg->cmsg_type != SCM_RIGHTS ||
	    cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
		error = EINVAL;
		goto out;
	}

	bcopy(CMSG_DATA(cmsg), fdp, sizeof(*fdp));
#if (MSG_CMSG_CLOEXEC == 0)
	/*
	 * If the MSG_CMSG_CLOEXEC flag is not available we cannot set the
	 * close-on-exec flag atomically, but we still want to set it for
	 * consistency.
	 */
	(void) fcntl(*fdp, F_SETFD, FD_CLOEXEC);
#endif

	error = 0;
out:
	free(msg.msg_control);

	return (error);
}

static TAILQ_HEAD(, file) filestructs = TAILQ_HEAD_INITIALIZER(filestructs);

struct file *
getf(int remotefd)
{
	zfs_ioctl_msg_t msg = { 0 };
	struct file *fp;
	int localfd;

	msg.zim_type = ZIM_GET_FD;
	msg.zim_u.zim_get_fd.zim_fd = remotefd;
	if (ioctl_sendmsg(&msg, 0, NULL) != 0)
		return (NULL);

	if (ioctl_recvmsg(&msg) != 0)
		return (NULL);

	ASSERT3U(msg.zim_type, ==, ZIM_GET_FD_RESPONSE);
	if (msg.zim_u.zim_get_fd_response.zim_errno != 0)
		return (NULL);

	if (fd_recv(&localfd) != 0)
		return (NULL);

	fp = malloc(sizeof(*fp));
	if (fp == NULL) {
		close(localfd);
		return (NULL);
	}

	fp->f_remotefd = remotefd;
	fp->f_localfd = localfd;
	fp->f_offset = 0;

	TAILQ_INSERT_TAIL(&filestructs, fp, f_next);

	return (fp);
}

void
releasef(int fd)
{
	struct file *fp;

	TAILQ_FOREACH(fp, &filestructs, f_next) {
		if (fp->f_remotefd == fd) {
			break;
		}
	}
	ASSERT(fp != NULL);

	TAILQ_REMOVE(&filestructs, fp, f_next);
	close(fp->f_localfd);
	fp->f_localfd = -666;
	free(fp);
}
#endif

/*
 * Read a property stored within the master node.
 * XXX copied
 */
#include <sys/zap.h>
int zfs_get_zplprop(objset_t *os, zfs_prop_t prop, uint64_t *value);
int
zfs_get_zplprop(objset_t *os, zfs_prop_t prop, uint64_t *value)
{
	const char *pname;
	int error = ENOENT;

	/*
	 * Look up the file system's value for the property.  For the
	 * version property, we look up a slightly different string.
	 */
	if (prop == ZFS_PROP_VERSION)
		//pname = ZPL_VERSION_STR;
		pname = "VERSION";
	else
		pname = zfs_prop_to_name(prop);

	if (os != NULL)
		//error = zap_lookup(os, MASTER_NODE_OBJ, pname, 8, 1, value);
		error = zap_lookup(os, 1, pname, 8, 1, value);

	if (error == ENOENT) {
		/* No value set, use the default value */
		switch (prop) {
		case ZFS_PROP_VERSION:
			*value = ZPL_VERSION;
			break;
		case ZFS_PROP_NORMALIZE:
		case ZFS_PROP_UTF8ONLY:
			*value = 0;
			break;
		case ZFS_PROP_CASE:
			*value = ZFS_CASE_SENSITIVE;
			break;
		default:
			return (error);
		}
		error = 0;
	}
	return (error);
}

/* XXX copied; need copyrights */
const struct ioc {
	uint_t	code;
	const char *name;
	const char *datastruct;
} iocnames[] = {
	/* ZFS ioctls */
	{ (uint_t)ZFS_IOC_POOL_CREATE,		"ZFS_IOC_POOL_CREATE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_DESTROY,		"ZFS_IOC_POOL_DESTROY",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_IMPORT,		"ZFS_IOC_POOL_IMPORT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_EXPORT,		"ZFS_IOC_POOL_EXPORT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_CONFIGS,		"ZFS_IOC_POOL_CONFIGS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_STATS,		"ZFS_IOC_POOL_STATS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_TRYIMPORT,	"ZFS_IOC_POOL_TRYIMPORT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_SCAN,		"ZFS_IOC_POOL_SCAN",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_FREEZE,		"ZFS_IOC_POOL_FREEZE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_UPGRADE,		"ZFS_IOC_POOL_UPGRADE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_GET_HISTORY,	"ZFS_IOC_POOL_GET_HISTORY",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_VDEV_ADD,		"ZFS_IOC_VDEV_ADD",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_VDEV_REMOVE,		"ZFS_IOC_VDEV_REMOVE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_VDEV_SET_STATE,	"ZFS_IOC_VDEV_SET_STATE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_VDEV_ATTACH,		"ZFS_IOC_VDEV_ATTACH",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_VDEV_DETACH,		"ZFS_IOC_VDEV_DETACH",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_VDEV_SETPATH,		"ZFS_IOC_VDEV_SETPATH",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_VDEV_SETFRU,		"ZFS_IOC_VDEV_SETFRU",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_OBJSET_STATS,		"ZFS_IOC_OBJSET_STATS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_OBJSET_ZPLPROPS,	"ZFS_IOC_OBJSET_ZPLPROPS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_DATASET_LIST_NEXT,	"ZFS_IOC_DATASET_LIST_NEXT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SNAPSHOT_LIST_NEXT,	"ZFS_IOC_SNAPSHOT_LIST_NEXT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SET_PROP,		"ZFS_IOC_SET_PROP",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_CREATE,		"ZFS_IOC_CREATE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_DESTROY,		"ZFS_IOC_DESTROY",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_ROLLBACK,		"ZFS_IOC_ROLLBACK",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_RENAME,		"ZFS_IOC_RENAME",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_RECV,			"ZFS_IOC_RECV",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SEND,			"ZFS_IOC_SEND",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_INJECT_FAULT,		"ZFS_IOC_INJECT_FAULT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_CLEAR_FAULT,		"ZFS_IOC_CLEAR_FAULT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_INJECT_LIST_NEXT,	"ZFS_IOC_INJECT_LIST_NEXT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_ERROR_LOG,		"ZFS_IOC_ERROR_LOG",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_CLEAR,		"ZFS_IOC_CLEAR",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_PROMOTE,		"ZFS_IOC_PROMOTE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SNAPSHOT,		"ZFS_IOC_SNAPSHOT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_DSOBJ_TO_DSNAME,	"ZFS_IOC_DSOBJ_TO_DSNAME",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_OBJ_TO_PATH,		"ZFS_IOC_OBJ_TO_PATH",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_SET_PROPS,	"ZFS_IOC_POOL_SET_PROPS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_GET_PROPS,	"ZFS_IOC_POOL_GET_PROPS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SET_FSACL,		"ZFS_IOC_SET_FSACL",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_GET_FSACL,		"ZFS_IOC_GET_FSACL",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SHARE,		"ZFS_IOC_SHARE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_INHERIT_PROP,		"ZFS_IOC_INHERIT_PROP",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SMB_ACL,		"ZFS_IOC_SMB_ACL",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_USERSPACE_ONE,	"ZFS_IOC_USERSPACE_ONE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_USERSPACE_MANY,	"ZFS_IOC_USERSPACE_MANY",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_USERSPACE_UPGRADE,	"ZFS_IOC_USERSPACE_UPGRADE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_HOLD,			"ZFS_IOC_HOLD",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_RELEASE,		"ZFS_IOC_RELEASE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_GET_HOLDS,		"ZFS_IOC_GET_HOLDS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_OBJSET_RECVD_PROPS,	"ZFS_IOC_OBJSET_RECVD_PROPS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_VDEV_SPLIT,		"ZFS_IOC_VDEV_SPLIT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_NEXT_OBJ,		"ZFS_IOC_NEXT_OBJ",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_DIFF,			"ZFS_IOC_DIFF",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_TMP_SNAPSHOT,		"ZFS_IOC_TMP_SNAPSHOT",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_OBJ_TO_STATS,		"ZFS_IOC_OBJ_TO_STATS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SPACE_WRITTEN,	"ZFS_IOC_SPACE_WRITTEN",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_DESTROY_SNAPS,	"ZFS_IOC_DESTROY_SNAPS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_REGUID,		"ZFS_IOC_POOL_REGUID",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_POOL_REOPEN,		"ZFS_IOC_POOL_REOPEN",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SEND_PROGRESS,	"ZFS_IOC_SEND_PROGRESS",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_LOG_HISTORY,		"ZFS_IOC_LOG_HISTORY",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SEND_NEW,		"ZFS_IOC_SEND_NEW",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_SEND_SPACE,		"ZFS_IOC_SEND_SPACE",
		"zfs_cmd_t" },
	{ (uint_t)ZFS_IOC_CLONE,		"ZFS_IOC_CLONE",
		"zfs_cmd_t" },
};

/*
 * Utility function to print a packed nvlist by unpacking
 * and calling the libnvpair pretty printer.  Frees all
 * allocated memory internally.
 */
static void
show_packed_nvlist(uintptr_t offset, size_t size)
{
	nvlist_t *nvl = NULL;
	char *buf;

	if ((offset == 0) || (size == 0)) {
		return;
	}

	buf = malloc(size);
	if (ddi_copyin((void *)offset, buf, size, 0) != 0) {
		(void) printf("\t<?>");
	} else {
		int result;

		result = nvlist_unpack(buf, size, &nvl, 0);
		if (result == 0) {
			dump_nvlist(nvl, 8);
			nvlist_free(nvl);
		} else {
			(void) printf("\tunpack of nvlist failed: %d\n",
			    result);
		}
	}
	free(buf);
}

static void
show_zfs_ioc(long addr, int showdst)
{
	static const zfs_share_t zero_share = {0};
	static const dmu_objset_stats_t zero_objstats = {0};
	static const struct drr_begin zero_drrbegin = {0};
	static const zinject_record_t zero_injectrec = {0};
	static const zfs_stat_t zero_zstat = {0};
	zfs_cmd_t zc;

	if (ddi_copyin((void *)addr, &zc, sizeof (zc), 0) != 0) {
		(void) printf(" zfs_ioctl read failed\n");
		return;
	}

	if (zc.zc_name[0])
		(void) printf("    zc_name=%s\n", zc.zc_name);
	if (zc.zc_value[0])
		(void) printf("    zc_value=%s\n", zc.zc_value);
	if (zc.zc_string[0])
		(void) printf("    zc_string=%s\n", zc.zc_string);
	if (zc.zc_guid != 0) {
		(void) printf("    zc_guid=%llu\n",
		    (u_longlong_t)zc.zc_guid);
	}
	if (zc.zc_nvlist_conf_size) {
		(void) printf("    nvlist_conf:\n");
		show_packed_nvlist(zc.zc_nvlist_conf,
		    zc.zc_nvlist_conf_size);
	}
	if (zc.zc_nvlist_src_size) {
		(void) printf("    nvlist_src:\n");
		show_packed_nvlist(zc.zc_nvlist_src,
		    zc.zc_nvlist_src_size);
	}
	if (showdst && zc.zc_nvlist_dst_size) {
		(void) printf("    nvlist_dst:\n");
		show_packed_nvlist(zc.zc_nvlist_dst,
		    zc.zc_nvlist_dst_size);
	}
	if (zc.zc_cookie != 0) {
		(void) printf("    zc_cookie=%llu\n",
		    (u_longlong_t)zc.zc_cookie);
	}
	if (zc.zc_objset_type != 0) {
		(void) printf("    zc_objset_type=%llu\n",
		    (u_longlong_t)zc.zc_objset_type);
	}
	if (zc.zc_perm_action != 0) {
		(void) printf("    zc_perm_action=%llu\n",
		    (u_longlong_t)zc.zc_perm_action);
	}
	if (zc.zc_history != 0) {
		(void) printf("    zc_history=%llu\n",
		    (u_longlong_t)zc.zc_history);
	}
	if (zc.zc_obj != 0) {
		(void) printf("    zc_obj=%llu\n",
		    (u_longlong_t)zc.zc_obj);
	}
	if (zc.zc_iflags != 0) {
		(void) printf("    zc_obj=0x%llx\n",
		    (u_longlong_t)zc.zc_iflags);
	}

	if (memcmp(&zc.zc_share, &zero_share, sizeof (zc.zc_share))) {
		zfs_share_t *z = &zc.zc_share;
		(void) printf("    zc_share:\n");
		if (z->z_exportdata) {
			(void) printf("\tz_exportdata=0x%llx\n",
			    (u_longlong_t)z->z_exportdata);
		}
		if (z->z_sharedata) {
			(void) printf("\tz_sharedata=0x%llx\n",
			    (u_longlong_t)z->z_sharedata);
		}
		if (z->z_sharetype) {
			(void) printf("\tz_sharetype=%llu\n",
			    (u_longlong_t)z->z_sharetype);
		}
		if (z->z_sharemax) {
			(void) printf("\tz_sharemax=%llu\n",
			    (u_longlong_t)z->z_sharemax);
		}
	}

	if (memcmp(&zc.zc_objset_stats, &zero_objstats,
	    sizeof (zc.zc_objset_stats))) {
		dmu_objset_stats_t *dds = &zc.zc_objset_stats;
		(void) printf("    zc_objset_stats:\n");
		if (dds->dds_num_clones) {
			(void) printf("\tdds_num_clones=%llu\n",
			    (u_longlong_t)dds->dds_num_clones);
		}
		if (dds->dds_creation_txg) {
			(void) printf("\tdds_creation_txg=%llu\n",
			    (u_longlong_t)dds->dds_creation_txg);
		}
		if (dds->dds_guid) {
			(void) printf("\tdds_guid=%llu\n",
			    (u_longlong_t)dds->dds_guid);
		}
		if (dds->dds_type)
			(void) printf("\tdds_type=%u\n", dds->dds_type);
		if (dds->dds_is_snapshot) {
			(void) printf("\tdds_is_snapshot=%u\n",
			    dds->dds_is_snapshot);
		}
		if (dds->dds_inconsistent) {
			(void) printf("\tdds_inconsitent=%u\n",
			    dds->dds_inconsistent);
		}
		if (dds->dds_origin[0]) {
			(void) printf("\tdds_origin=%s\n", dds->dds_origin);
		}
	}

	if (memcmp(&zc.zc_begin_record, &zero_drrbegin,
	    sizeof (zc.zc_begin_record))) {
		struct drr_begin *drr = &zc.zc_begin_record;
		(void) printf("    zc_begin_record:\n");
		if (drr->drr_magic) {
			(void) printf("\tdrr_magic=%llu\n",
			    (u_longlong_t)drr->drr_magic);
		}
		if (drr->drr_versioninfo) {
			(void) printf("\tdrr_versioninfo=%llu\n",
			    (u_longlong_t)drr->drr_versioninfo);
		}
		if (drr->drr_creation_time) {
			(void) printf("\tdrr_creation_time=%llu\n",
			    (u_longlong_t)drr->drr_creation_time);
		}
		if (drr->drr_type)
			(void) printf("\tdrr_type=%u\n", drr->drr_type);
		if (drr->drr_flags)
			(void) printf("\tdrr_flags=0x%x\n", drr->drr_flags);
		if (drr->drr_toguid) {
			(void) printf("\tdrr_toguid=%llu\n",
			    (u_longlong_t)drr->drr_toguid);
		}
		if (drr->drr_fromguid) {
			(void) printf("\tdrr_fromguid=%llu\n",
			    (u_longlong_t)drr->drr_fromguid);
		}
		if (drr->drr_toname[0]) {
			(void) printf("\tdrr_toname=%s\n", drr->drr_toname);
		}
	}

	if (memcmp(&zc.zc_inject_record, &zero_injectrec,
	    sizeof (zc.zc_inject_record))) {
		zinject_record_t *zi = &zc.zc_inject_record;
		(void) printf("    zc_inject_record:\n");
		if (zi->zi_objset) {
			(void) printf("\tzi_objset=%llu\n",
			    (u_longlong_t)zi->zi_objset);
		}
		if (zi->zi_object) {
			(void) printf("\tzi_object=%llu\n",
			    (u_longlong_t)zi->zi_object);
		}
		if (zi->zi_start) {
			(void) printf("\tzi_start=%llu\n",
			    (u_longlong_t)zi->zi_start);
		}
		if (zi->zi_end) {
			(void) printf("\tzi_end=%llu\n",
			    (u_longlong_t)zi->zi_end);
		}
		if (zi->zi_guid) {
			(void) printf("\tzi_guid=%llu\n",
			    (u_longlong_t)zi->zi_guid);
		}
		if (zi->zi_level) {
			(void) printf("\tzi_level=%lu\n",
			    (ulong_t)zi->zi_level);
		}
		if (zi->zi_error) {
			(void) printf("\tzi_error=%lu\n",
			    (ulong_t)zi->zi_error);
		}
		if (zi->zi_type) {
			(void) printf("\tzi_type=%llu\n",
			    (u_longlong_t)zi->zi_type);
		}
		if (zi->zi_freq) {
			(void) printf("\tzi_freq=%lu\n",
			    (ulong_t)zi->zi_freq);
		}
		if (zi->zi_failfast) {
			(void) printf("\tzi_failfast=%lu\n",
			    (ulong_t)zi->zi_failfast);
		}
		if (zi->zi_func[0])
			(void) printf("\tzi_func=%s\n", zi->zi_func);
		if (zi->zi_iotype) {
			(void) printf("\tzi_iotype=%lu\n",
			    (ulong_t)zi->zi_iotype);
		}
		if (zi->zi_duration) {
			(void) printf("\tzi_duration=%ld\n",
			    (long)zi->zi_duration);
		}
		if (zi->zi_timer) {
			(void) printf("\tzi_timer=%llu\n",
			    (u_longlong_t)zi->zi_timer);
		}
	}

	if (zc.zc_defer_destroy) {
		(void) printf("    zc_defer_destroy=%d\n",
		    (int)zc.zc_defer_destroy);
	}
	if (zc.zc_flags) {
		(void) printf("    zc_flags=0x%x\n",
		    zc.zc_flags);
	}
	if (zc.zc_action_handle) {
		(void) printf("    zc_action_handle=%llu\n",
		    (u_longlong_t)zc.zc_action_handle);
	}
	if (zc.zc_cleanup_fd >= 0)
		(void) printf("    zc_cleanup_fd=%d\n", zc.zc_cleanup_fd);
	if (zc.zc_sendobj) {
		(void) printf("    zc_sendobj=%llu\n",
		    (u_longlong_t)zc.zc_sendobj);
	}
	if (zc.zc_fromobj) {
		(void) printf("    zc_fromobj=%llu\n",
		    (u_longlong_t)zc.zc_fromobj);
	}
	if (zc.zc_createtxg) {
		(void) printf("    zc_createtxg=%llu\n",
		    (u_longlong_t)zc.zc_createtxg);
	}

	if (memcmp(&zc.zc_stat, &zero_zstat, sizeof (zc.zc_stat))) {
		zfs_stat_t *zs = &zc.zc_stat;
		(void) printf("    zc_stat:\n");
		if (zs->zs_gen) {
			(void) printf("\tzs_gen=%llu\n",
			    (u_longlong_t)zs->zs_gen);
		}
		if (zs->zs_mode) {
			(void) printf("\tzs_mode=%llu\n",
			    (u_longlong_t)zs->zs_mode);
		}
		if (zs->zs_links) {
			(void) printf("\tzs_links=%llu\n",
			    (u_longlong_t)zs->zs_links);
		}
		if (zs->zs_ctime[0]) {
			(void) printf("\tzs_ctime[0]=%llu\n",
			    (u_longlong_t)zs->zs_ctime[0]);
		}
		if (zs->zs_ctime[1]) {
			(void) printf("\tzs_ctime[1]=%llu\n",
			    (u_longlong_t)zs->zs_ctime[1]);
		}
	}
}

static const char *
ioc2name(int ioc)
{
	for (int i = 0; i < sizeof (iocnames) / sizeof (iocnames[0]); i++)
		if (iocnames[i].code == ioc)
			return (iocnames[i].name);
	return ("unknown");
}

#pragma GCC diagnostic ignored "-Wframe-larger-than="
static void *
handle_connection(void *argp)
{
	conn_arg_t *arg = argp;
	int conn_fd = arg->conn_fd;
	int error;

	VERIFY0(pthread_setspecific(ioctl_key, arg));

	while (B_TRUE) {
		zfs_ioctl_msg_t msg;
		int ioctl;
		uint64_t arg;

		error = ioctl_recvmsg(&msg);
		if (error != 0) {
			if (error == ENOTCONN) {
				fprintf(stderr, "connection closed\n");
			} else {
				perror("ioctl_recvmsg failed");
fprintf(stderr, "%s:%u: ioctl_recvmsg() failed error=%d\n", __func__, __LINE__, error);
			}
			break;
		}

		if (msg.zim_type != ZIM_IOCTL) {
			fprintf(stderr, "unexpected message received (type %u)\n",
			    msg.zim_type);
abort();
			break;
		}

		ioctl = msg.zim_u.zim_ioctl.zim_ioctl;
		arg = msg.zim_u.zim_ioctl.zim_cmd;

		if (getenv("ZFSD_DEBUG") != NULL) {
			printf("zfsdev_ioctl(%s %jx)\n", ioc2name(ioctl),
			    (uintmax_t)arg);
		}

abort();
		msg.zim_type = ZIM_IOCTL_RESPONSE;
//		msg.zim_u.zim_ioctl_response.zim_errno = zfsdev_ioctl(NULL,
//		    ioctl, (caddr_t)arg, 0, NULL);
//		zfsdev_ioctl_common(uint_t vecnum, zfs_cmd_t *zc)
		msg.zim_u.zim_ioctl_response.zim_retval =
		    (msg.zim_u.zim_ioctl_response.zim_errno == 0) ? 0 : -1;

		if (getenv("ZFSD_DEBUG") != NULL) {
			show_zfs_ioc(arg,
			    msg.zim_u.zim_ioctl_response.zim_retval == 0);
			printf("errno = %u\n",
			    msg.zim_u.zim_ioctl_response.zim_errno);
		}

		error = ioctl_sendmsg(&msg, 0, NULL);
		if (error != 0) {
			if (error == ENOTCONN) {
				fprintf(stderr, "connection closed\n");
			} else {
				perror("ioctl_sendmsg failed");
fprintf(stderr, "%s:%u: ioctl_sendmsg() failed error=%d\n", __func__, __LINE__, error);
			}
			break;
		}
	}
	(void) close(conn_fd);
	free(arg);
	return (NULL);
}
#pragma GCC diagnostic pop

static void *
accept_connection(void *argp)
{
	int sock_fd = (uintptr_t)argp;
	int conn_fd;
	struct sockaddr_un address;
	socklen_t socklen = sizeof(address);

	/* XXX kick off a thread for this */
	while ((conn_fd = accept(sock_fd, (struct sockaddr *)&address,
	    &socklen)) >= 0) {
		pthread_t tid;
		conn_arg_t *ca = malloc(sizeof (conn_arg_t));
		ca->conn_fd = conn_fd;
		pthread_create(&tid, NULL, handle_connection, ca);
	}

	if (conn_fd < 0)
		perror("accept failed");

	return (NULL);
}

#if 0
/* ARGSUSED */
static int
space_delta_cb(dmu_object_type_t bonustype, void *data,
    uint64_t *userp, uint64_t *groupp)
{
	/*
	 * Is it a valid type of object to track?
	 */
	if (bonustype != DMU_OT_ZNODE && bonustype != DMU_OT_SA)
		return (ENOENT);
	(void) fprintf(stderr, "modifying object that needs user accounting");
	abort();
	/* NOTREACHED */
}
#endif

void
zfs_user_ioctl_init(void)
{
	pthread_t td;
	int sock_fd;
	struct sockaddr_un address = { 0 };
	char *socket_name;
	int error;

	zfs_ioctl_init();

	socket_name = getenv(ZFS_SOCKET_ENVVAR);
	if (socket_name == NULL)
		return;

	error = pthread_key_create(&pid_key, NULL);
	if (error != 0) {
		perror("pthread_key_create");
		abort();
	}

	error = pthread_key_create(&ioctl_key, NULL);
	if (error != 0) {
		perror("pthread_key_create");
		abort();
        }

	sigignore(SIGPIPE);

	if ((sock_fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
		perror("socket failed");
		exit(1);
	}

	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s",
	    socket_name);

	(void) unlink(socket_name);
	if (bind(sock_fd, (struct sockaddr *)&address,
	    sizeof(struct sockaddr_un)) != 0) {
		perror("bind failed");
		exit(1);
	}

	if (listen(sock_fd, 2) != 0) {
		perror("listen failed");
		exit(1);
	}

	pthread_create(&td, NULL, accept_connection,
	    (void*)(uintptr_t)sock_fd);
}

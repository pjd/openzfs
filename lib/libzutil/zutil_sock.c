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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/zfs_ioctl.h>

#include <zfs_sock.h>
#include <libzutil.h>

static int
ioctl_recv(int sock, int size, void *dst)
{
	while (size > 0) {
		int recvd = recv(sock, dst, size, 0);
		if (recvd == -1)
			return (errno);
		if (recvd == 0)
			return (EINVAL);
		size -= recvd;
		dst = (char *)dst + recvd;
	}
	return (0);
}

static int
ioctl_send(int sock, int size, const void *data)
{
	while (size > 0) {
		int sent = send(sock, data, size, 0);
		if (sent == -1)
			return (errno);
		if (sent == 0)
			return (EINVAL);
		size -= sent;
		data = (const char *)data + sent;
	}
	return (0);
}

static int
ioctl_sendmsg(int sock, const zfs_ioctl_msg_t *msg, int payload_len,
    const void *payload)
{
	int error;

	error = ioctl_send(sock, sizeof (*msg), msg);
	if (error != 0)
		return (error);

	if (payload_len != 0)
		error = ioctl_send(sock, payload_len, payload);
	return (error);
}

#define	fd_is_valid(fd)	(fcntl((fd), F_GETFL) != -1 || errno != EBADF)

static int
ioctl_sendmsg_fd(int sock, const zfs_ioctl_msg_t *msgp, int fd)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	uint8_t dummy;
	int error;

	error = ioctl_send(sock, sizeof (*msgp), msgp);
	if (error != 0)
		return (error);

	if (!fd_is_valid(fd)) {
		return (EBADF);
	}

	bzero(&msg, sizeof(msg));

	/*
	 * XXX: We send one byte along with the control message, because
	 *      setting msg_iov to NULL only works if this is the first
	 *      packet send over the socket. Once we send some data we
	 *      won't be able to send control messages anymore. This is most
	 *      likely a kernel bug.
	 */
	dummy = 0;
	iov.iov_base = &dummy;
	iov.iov_len = sizeof(dummy);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_controllen = CMSG_SPACE(sizeof(int));
	msg.msg_control = calloc(1, msg.msg_controllen);
	if (msg.msg_control == NULL) {
		return (ENOMEM);
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	assert(cmsg != NULL);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
	bcopy(&fd, CMSG_DATA(cmsg), sizeof(fd));

	for (;;) {
		if (sendmsg(sock, &msg, 0) == -1) {
			if (errno == EINTR)
				continue;
			error = errno;
			goto out;
		}
		break;
	}

	error = 0;
out:
	free(msg.msg_control);

	return (error);
}

static int
ioctl_recvmsg(int sock, zfs_ioctl_msg_t *msg)
{
	return (ioctl_recv(sock, sizeof (*msg), msg));
}

static int
ioctl_process_message(int sock, zfs_ioctl_msg_t *msg)
{
	int error;

	switch (msg->zim_type) {
	case ZIM_COPYIN: {
		uint64_t len = msg->zim_u.zim_copyin.zim_len;
		void *addr = (void *)(uintptr_t)msg->zim_u.zim_copyin.zim_address;

printf("%s:%u: ZIM_COPYIN\n", __func__, __LINE__);
		/* XXX handle EFAULT */
		msg->zim_type = ZIM_COPYIN_RESPONSE;
		msg->zim_u.zim_copyin_response.zim_errno = 0;
		error = ioctl_sendmsg(sock, msg, len, addr);
		if (error != 0) {
			perror("sendmsg(copyin_response) failed");
			return (-1);
		}
		break;
	}
	case ZIM_COPYINSTR: {
		uint64_t len = msg->zim_u.zim_copyinstr.zim_length;
		void *addr = (void *)(uintptr_t)msg->zim_u.zim_copyin.zim_address;

printf("%s:%u: ZIM_COPYINSTR\n", __func__, __LINE__);
		/* XXX handle EFAULT */
		msg->zim_type = ZIM_COPYINSTR_RESPONSE;
		msg->zim_u.zim_copyinstr_response.zim_errno = 0;
		msg->zim_u.zim_copyinstr_response.zim_length =
		    strnlen(addr, len - 1) + 1;
		error = ioctl_sendmsg(sock, msg,
		    msg->zim_u.zim_copyinstr_response.zim_length, addr);
		if (error != 0) {
			perror("sendmsg(copyinstr_response) failed");
			return (-1);
		}
		break;
	}
	case ZIM_COPYOUT: {
		uint64_t len = msg->zim_u.zim_copyout.zim_len;
		void *addr = (void *)(uintptr_t)msg->zim_u.zim_copyout.zim_address;

printf("%s:%u: ZIM_COPYOUT\n", __func__, __LINE__);
		error = ioctl_recv(sock, len, addr);
		if (error != 0) {
			perror("recv for copyout failed");
			return (-1);
		}

		/* XXX handle EFAULT */
		msg->zim_type = ZIM_COPYOUT_RESPONSE;
		msg->zim_u.zim_copyout_response.zim_errno = 0;
		error = ioctl_sendmsg(sock, msg, 0, NULL);
		if (error != 0) {
			perror("sendmsg(copyout_response) failed");
			return (-1);
		}

		break;
	}
	case ZIM_GET_FD: {
		int fd = msg->zim_u.zim_get_fd.zim_fd;

printf("%s:%u: ZIM_GET_FD\n", __func__, __LINE__);
#if 0
		fprintf(stderr, "handling get_fd request fd %d\n", fd);
#endif
		msg->zim_type = ZIM_GET_FD_RESPONSE;
		msg->zim_u.zim_get_fd_response.zim_errno = 0;
		error = ioctl_sendmsg_fd(sock, msg, fd);
		if (error != 0) {
			perror("sendmsg(get_fd_response) failed");
			return (-1);
		}
		break;
	}
	default:
		printf("invalid message type %u\n",
		    msg->zim_type);
		return (-1);

	}
	return (0);
}

int
zsock_open(const char *path)
{
	struct sockaddr_un sun;
	int sock;

	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path)) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	sun.sun_len = SUN_LEN(&sun);
	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		return (-1);
	}
	if (connect(sock, (struct sockaddr *)&sun, sun.sun_len) < 0) {
		(void) close(sock);
		return (-1);
	}

	return (sock);
}

boolean_t
zsock_is_sock(int fd)
{
	struct stat sb;

	if (fstat(fd, &sb) < 0) {
		return (B_FALSE);
	}
	return (S_ISSOCK(sb.st_mode));
}

int
zsock_ioctl(int sock, zfs_ioc_t ioc, zfs_cmd_t *cmd)
{
	int error;
	zfs_ioctl_msg_t msg = { 0 };

	/* XXX need one connection per thread */
	msg.zim_type = ZIM_IOCTL;
	msg.zim_u.zim_ioctl.zim_ioctl = ioc;
	msg.zim_u.zim_ioctl.zim_cmd = (uintptr_t)cmd;
	error = ioctl_sendmsg(sock, &msg, 0, NULL);
	if (error != 0) {
		errno = error;
		return (-1);
	}

	for (;;) {
		error = ioctl_recvmsg(sock, &msg);
		if (error != 0) {
			errno = error;
			return (-1);
		}
		if (msg.zim_type == ZIM_IOCTL_RESPONSE) {
printf("%s:%u: ZIM_IOCTL_RESPONSE\n", __func__, __LINE__);
			errno = msg.zim_u.zim_ioctl_response.zim_errno;
			return (msg.zim_u.zim_ioctl_response.zim_retval);
		}
		error = ioctl_process_message(sock, &msg);
		if (error != 0) {
			errno = error;
			return (-1);
		}
	}

	return (0);
}

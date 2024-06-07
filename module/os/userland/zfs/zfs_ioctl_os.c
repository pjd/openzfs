/*
 * Copyright (c) 2020 iXsystems, Inc.
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
 *
 */

#include <sys/zfs_context.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/nvpair.h>
#include <sys/spa_impl.h>
#include <sys/vdev_os.h>
#include <sys/zfs_vfsops.h>
#include <sys/zone.h>

#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>

int
zfs_vfs_ref(zfsvfs_t **zfvp)
{

	if (*zfvp == NULL)
		return (SET_ERROR(ESRCH));

	*zfvp = NULL;
	return (SET_ERROR(ESRCH));
}

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs __unused)
{
	return (B_FALSE);
}

void
zfs_vfs_rele(zfsvfs_t *zfsvfs __unused)
{
	abort();
}

/* Update the VFS's cache of mountpoint properties */
void
zfs_ioctl_update_mount_cache(const char *dsname __unused)
{
}

uint64_t
zfs_max_nvlist_src_size_os(void)
{
	return (4 * 1024 * 1024);
}

void
zfs_ioctl_init_os(void)
{
}

int
zfsdev_attach(void)
{
	return (0);
}

void
zfsdev_detach(void)
{
}

void
zfsdev_private_set_state(void *priv __unused, zfsdev_state_t *zs __unused)
{
}

zfsdev_state_t *
zfsdev_private_get_state(void *priv)
{
	return (priv);
}

extern long zfsdev_ioctl(unsigned int cmd, unsigned long arg);

long
zfsdev_ioctl(unsigned int cmd, unsigned long arg)
{
	uint_t vecnum;
	zfs_cmd_t *zc;
	int error, rc;

	vecnum = cmd - ZFS_IOC_FIRST;

	zc = vmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	if (ddi_copyin((void *)(uintptr_t)arg, zc, sizeof (zfs_cmd_t), 0)) {
		error = SET_ERROR(EFAULT);
		goto out;
	}
	error = zfsdev_ioctl_common(vecnum, zc, 0);
	rc = ddi_copyout(zc, (void *)(uintptr_t)arg, sizeof (zfs_cmd_t), 0);
	if (error == 0 && rc != 0)
		error = SET_ERROR(EFAULT);
out:
	vmem_free(zc, sizeof (zfs_cmd_t));
	return (error);
}

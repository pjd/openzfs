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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/policy.h>
#include <sys/zfs_znode.h>

int
secpolicy_nfs(const struct cred *cr __unused)
{
	return (0);
}

int
secpolicy_zfs(const struct cred *cr __unused)
{
	return (0);
}

int
secpolicy_zfs_proc(const struct cred *cr __unused,
    const struct proc *proc __unused)
{
	return (0);
}

int
secpolicy_sys_config(const struct cred *cr __unused, int checkonly __unused)
{
	return (0);
}

int
secpolicy_zinject(const struct cred *cr __unused)
{
	return (0);
}

int
secpolicy_fs_unmount(const struct cred *cr __unused,
    const struct mount *vfsp __unused)
{
	return (0);
}

int
secpolicy_fs_owner(const struct mount *mp __unused,
    const struct cred *cr __unused)
{
	return (0);
}

int
secpolicy_basic_link(const struct vnode *vp __unused,
    const struct cred *cr __unused)
{
	return (0);
}

int
secpolicy_vnode_stky_modify(const struct cred *cr __unused)
{
	return (EPERM);
}

int
secpolicy_vnode_remove(const struct vnode *vp __unused,
    const struct cred *cr __unused)
{
	return (0);
}

int
secpolicy_vnode_access(const struct cred *cr __unused,
    const struct vnode *vp __unused, int owner __unused, int accmode __unused)
{
	return (0);
}

int
secpolicy_vnode_access2(const struct cred *cr __unused,
    const struct vnode *vp __unused, int owner __unused, int curmode __unused,
    int wantmode __unused)
{
	return (0);
}

int
secpolicy_vnode_any_access(const struct cred *cr __unused,
    const struct vnode *vp __unused, int owner __unused)
{
	return (0);
}

int
secpolicy_vnode_setdac(const struct vnode *vp __unused,
    const struct cred *cr __unused, int owner __unused)
{
	return (0);
}

int
secpolicy_vnode_setattr(const struct cred *cr __unused,
    const struct vnode *vp __unused, struct vattr *vap,
    const struct vattr *ovap, int flags __unused,
    int unlocked_access(void *, int, struct cred *) __unused,
    const void *node __unused)
{
	if ((vap->va_mask & AT_MODE) == 0) {
		vap->va_mode = ovap->va_mode;
	}
	return (0);
}

int
secpolicy_vnode_create_gid(const struct cred *cr __unused)
{
	return (EPERM);
}

int
secpolicy_vnode_setids_setgids(const struct vnode *vp __unused,
    const struct cred *cr __unused, int gid __unused,
    const void *idmap __unused, const void *mnt_ns __unused)
{
	return (0);
}

int
secpolicy_vnode_setid_retain(struct znode *zp __unused,
    const struct cred *cr __unused, int issuidroot __unused)
{
	return (0);
}

void
secpolicy_setid_clear(const struct vattr *vap __unused,
    const struct vnode *vp __unused, const struct cred *cr __unused)
{
}

int
secpolicy_setid_setsticky_clear(const struct vnode *vp __unused,
    const struct vattr *vap __unused, const struct vattr *ovap __unused,
    const struct cred *cr __unused)
{
	return (0);
}

int
secpolicy_fs_mount(const struct cred *cr __unused,
    const struct vnode *mvp __unused, const struct mount *vfsp __unused)
{
	return (0);
}

int
secpolicy_vnode_owner(const struct vnode *vp __unused,
    const struct cred *cr __unused, int owner __unused)
{
	return (0);
}

int
secpolicy_vnode_chown(const struct vnode *vp __unused,
    const struct cred *cr __unused, int owner __unused)
{
	return (0);
}

void
secpolicy_fs_mount_clearopts(const struct cred *cr __unused,
    const struct mount *vfsp __unused)
{
}

int
secpolicy_xvattr(const struct vnode *vp __unused,
    const struct xvattr *xvap __unused, int owner __unused,
    const struct cred *cr __unused, int vtype __unused)
{
	return (0);
}

int
secpolicy_smb(const struct cred *cr __unused)
{
	return (0);
}

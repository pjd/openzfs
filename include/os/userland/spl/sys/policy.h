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
 *
 * $ $FreeBSD$
 */

#ifndef _SPL_SYS_POLICY_H_
#define	_SPL_SYS_POLICY_H_

struct cred;
struct mount;
struct proc;
struct vattr;
struct vnode;
struct xvattr;
struct znode;

int secpolicy_nfs(const struct cred *cr);
int secpolicy_zfs(const struct cred *cr);
int secpolicy_zfs_proc(const struct cred *cr, const struct proc *proc);
int secpolicy_sys_config(const struct cred *cr, int checkonly);
int secpolicy_zinject(const struct cred *cr);
int secpolicy_fs_unmount(const struct cred *cr, const struct mount *vfsp);
int secpolicy_fs_owner(const struct mount *mp, const struct cred *cr);
int secpolicy_basic_link(const struct vnode *vp, const struct cred *cr);
int secpolicy_vnode_stky_modify(const struct cred *cr);
int secpolicy_vnode_remove(const struct vnode *vp, const struct cred *cr);
int secpolicy_vnode_access(const struct cred *cr, const struct vnode *vp,
    int owner, int accmode);
int secpolicy_vnode_access2(const struct cred *cr, const struct vnode *vp,
    int owner, int curmode, int wantmode);
int secpolicy_vnode_any_access(const struct cred *cr, const struct vnode *vp,
    int owner);
int secpolicy_vnode_setdac(const struct vnode *vp, const struct cred *cr,
    int owner);
int secpolicy_vnode_setattr(const struct cred *cr, const struct vnode *vp,
    struct vattr *vap, const struct vattr *ovap, int flags,
    int unlocked_access(void *, int, struct cred *), const void *node);
int secpolicy_vnode_create_gid(const struct cred *cr);
int secpolicy_vnode_setids_setgids(const struct vnode *vp,
    const struct cred *cr, int gid, const void *idmap, const void *mnt_ns);
int secpolicy_vnode_setid_retain(struct znode *zp, const struct cred *cr,
    int issuidroot);
void secpolicy_setid_clear(const struct vattr *vap, const struct vnode *vp,
    const struct cred *cr);
int secpolicy_setid_setsticky_clear(const struct vnode *vp,
    const struct vattr *vap, const struct vattr *ovap, const struct cred *cr);
int secpolicy_fs_mount(const struct cred *cr, const struct vnode *mvp,
    const struct mount *vfsp);
int secpolicy_vnode_owner(const struct vnode *vp, const struct cred *cr,
    int owner);
int secpolicy_vnode_chown(const struct vnode *vp, const struct cred *cr,
    int owner);
void secpolicy_fs_mount_clearopts(const struct cred *cr,
    const struct mount *vfsp);
int secpolicy_xvattr(const struct vnode *vp, const struct xvattr *xvap,
    int owner, const struct cred *cr, int vtype);
int secpolicy_smb(const struct cred *cr);

#endif	/* _SPL_SYS_POLICY_H_ */

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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/trace_zfs.h>

typedef struct zfs_dbgmsg {
	procfs_list_node_t	zdm_node;
	uint64_t		zdm_timestamp;
	uint_t			zdm_size;
	char			zdm_msg[]; /* variable length allocation */
} zfs_dbgmsg_t;

static procfs_list_t zfs_dbgmsgs;
static uint_t zfs_dbgmsg_size = 0;
static uint_t zfs_dbgmsg_maxsize = 4<<20; /* 4MB */

/*
 * Internal ZFS debug messages are enabled by default.
 *
 * # Print debug messages
 * cat /proc/spl/kstat/zfs/dbgmsg
 *
 * # Disable the kernel debug message log.
 * echo 0 > /sys/module/zfs/parameters/zfs_dbgmsg_enable
 *
 * # Clear the kernel debug message log.
 * echo 0 >/proc/spl/kstat/zfs/dbgmsg
 */
int zfs_dbgmsg_enable = B_TRUE;

static int
zfs_dbgmsg_show_header(struct seq_file *f)
{
	seq_printf(f, "%-12s %-8s\n", "timestamp", "message");
	return (0);
}

static int
zfs_dbgmsg_show(struct seq_file *f, void *p)
{
	zfs_dbgmsg_t *zdm = (zfs_dbgmsg_t *)p;
	seq_printf(f, "%-12llu %-s\n",
	    (u_longlong_t)zdm->zdm_timestamp, zdm->zdm_msg);
	return (0);
}

static void
zfs_dbgmsg_purge(uint_t max_size)
{
	while (zfs_dbgmsg_size > max_size) {
		zfs_dbgmsg_t *zdm = list_remove_head(&zfs_dbgmsgs.pl_list);
		if (zdm == NULL)
			return;

		uint_t size = zdm->zdm_size;
		free(zdm);
		zfs_dbgmsg_size -= size;
	}
}

static int
zfs_dbgmsg_clear(procfs_list_t *procfs_list)
{
	(void) procfs_list;
	mutex_enter(&zfs_dbgmsgs.pl_lock);
	zfs_dbgmsg_purge(0);
	mutex_exit(&zfs_dbgmsgs.pl_lock);
	return (0);
}

void
zfs_dbgmsg_init(void)
{
	procfs_list_install("zfs",
	    NULL,
	    "dbgmsg",
	    0600,
	    &zfs_dbgmsgs,
	    zfs_dbgmsg_show,
	    zfs_dbgmsg_show_header,
	    zfs_dbgmsg_clear,
	    offsetof(zfs_dbgmsg_t, zdm_node));
}

void
zfs_dbgmsg_fini(void)
{
	procfs_list_uninstall(&zfs_dbgmsgs);
	zfs_dbgmsg_purge(0);

	/*
	 * TODO - decide how to make this permanent
	 */
#ifdef _KERNEL
	procfs_list_destroy(&zfs_dbgmsgs);
#endif
}

void
__set_error(const char *file, const char *func, int line, int err)
{
	/*
	 * To enable this:
	 *
	 * $ echo 512 >/sys/module/zfs/parameters/zfs_flags
	 */
	if (zfs_flags & ZFS_DEBUG_SET_ERROR)
		__dprintf(B_FALSE, file, func, line, "error %lu",
		    (ulong_t)err);
}

void
__zfs_dbgmsg(char *buf)
{
	uint_t size = sizeof (zfs_dbgmsg_t) + strlen(buf) + 1;
	zfs_dbgmsg_t *zdm = calloc(1, size);
	assert(zdm != NULL);
	zdm->zdm_size = size;
	zdm->zdm_timestamp = gethrestime_sec();
	strcpy(zdm->zdm_msg, buf);

	mutex_enter(&zfs_dbgmsgs.pl_lock);
	procfs_list_add(&zfs_dbgmsgs, zdm);
	zfs_dbgmsg_size += size;
	zfs_dbgmsg_purge(zfs_dbgmsg_maxsize);
	mutex_exit(&zfs_dbgmsgs.pl_lock);
}

void
zfs_dbgmsg_print(int fd, const char *tag)
{
	ssize_t ret __attribute__((unused));

	mutex_enter(&zfs_dbgmsgs.pl_lock);

	/*
	 * We use write() in this function instead of printf()
	 * so it is safe to call from a signal handler.
	 */
	ret = write(fd, "ZFS_DBGMSG(", 11);
	ret = write(fd, tag, strlen(tag));
	ret = write(fd, ") START:\n", 9);

	for (zfs_dbgmsg_t *zdm = list_head(&zfs_dbgmsgs.pl_list); zdm != NULL;
	    zdm = list_next(&zfs_dbgmsgs.pl_list, zdm)) {
		ret = write(fd, zdm->zdm_msg, strlen(zdm->zdm_msg));
		ret = write(fd, "\n", 1);
	}

	ret = write(fd, "ZFS_DBGMSG(", 11);
	ret = write(fd, tag, strlen(tag));
	ret = write(fd, ") END\n", 6);

	mutex_exit(&zfs_dbgmsgs.pl_lock);
}

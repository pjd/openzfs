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

#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>

extern int zfs_kmod_init(void);

static bool stop_requested;

static void
stop_handler(int sig __unused)
{

	stop_requested = true;
}

int
main(int argc, char **argv)
{
	extern const char *spa_config_path;
	bool foreground;
	char c;

	if (getenv("UZPOOLD_SOCK") == NULL) {
		fprintf(stderr, "%s not set in env\n", "UZPOOLD_SOCK");
		exit(1);
	}

	foreground = false;
	spa_config_path = "/var/tmp/uzpoold.cache";

	while ((c = getopt(argc, argv, "c:F")) != -1) {
		switch (c) {
		case 'c':
			spa_config_path = optarg;
			break;
		case 'F':
			foreground = true;
			break;
		default:
			abort();
			break;
		}
	}
	argc -= optind;
	argv += optind;

fprintf(stderr, "%s:%u\n", __func__, __LINE__);
	if (!foreground) {
		if (daemon(0, 0) == -1) {
			err(1, "Unable to daemonize");
		}
	}

	sigignore(SIGPIPE);

	(void)signal(SIGINT, stop_handler);
	(void)signal(SIGTERM, stop_handler);

fprintf(stderr, "%s:%u\n", __func__, __LINE__);
	kernel_init(FREAD | FWRITE);
//	zfs_kmod_init();

	while (!stop_requested) {
		/* Just wait for the signal. */
		select(0, NULL, NULL, NULL, NULL);
	}

	kernel_fini();

	return (0);
}

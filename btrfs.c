/* Copyright (c) 2013, Robert Clausecker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. */

#include <errno.h>

#include "btrfs.h"

#ifdef __linux__

# include <btrfs/ioctl.h>
# include <fcntl.h>
# include <linux/magic.h>
# include <sys/ioctl.h>
# include <sys/stat.h>
# include <sys/statfs.h>
# include <sys/types.h>
# include <unistd.h>

# define BAIL { retval = -1; goto cleanup; }

int btrfs_clone(const char *old, const char *new) {
	struct stat old_stat, new_stat;
	struct statfs fs_stat;
	int old_fd = -1, new_fd = -1;
	int retval = 0;

	new_fd = open(new,O_WRONLY|O_CREAT|O_EXCL,0664);
	if (new_fd == -1) BAIL

	/* figure out whether both files are on the same file system and whether
	 * the file system is actually a btrfs */
	if (stat(old,&old_stat) == -1) BAIL
	if (stat(new,&new_stat) == -1) BAIL

	if (old_stat.st_dev != new_stat.st_dev) {
		errno = EXDEV;
		BAIL
	}

	if (statfs(old,&fs_stat) == -1) BAIL
	if (fs_stat.f_type != BTRFS_SUPER_MAGIC) {
		errno = EPERM;
		BAIL
	}

	old_fd = open(old,O_RDONLY);
	if (old_fd == -1) BAIL

	retval = ioctl(new_fd,BTRFS_IOC_CLONE,old_fd);

	cleanup:

	if (old_fd != -1) close(old_fd);
	if (new_fd != -1) close(new_fd);

	return retval;
}

# undef BAIL

#else

int btrfs_clone(const char *old, const char *new) {
	(void)old;
	(void)new;
	errno = ENOTSUP;
	return -1;
}

#endif
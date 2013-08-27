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

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _FILE_OFFSET_BITS 64

#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "match.h"
#include "action.h"

static int perform_link(
	link_func do_link,
	const char *lf_name,
	const char *old,
	const char *new,
	int preserve) {

	char *tmp, *new_dup;
	int len;
	struct stat newstat;
	struct timespec timespecs[2];

	new_dup = strdup(new);
	if (new_dup == NULL) {
		perror("malloc failed");
		return 1;
	}

	/* /fdup.##########.tmp where ########## refers to the pid */
	len = strlen(new_dup) + 21;

	tmp = malloc(len);
	if (tmp == NULL) {
		perror("malloc failed");
		return 1;
	}

	snprintf(tmp,len,"%s/fdup.%010d.tmp",dirname(new_dup),getpid());
	free(new_dup);

	if (do_link(old,tmp) == -1) {
		fprintf(stderr,"Cannot %s %s to %s: ",lf_name,old,tmp);
		perror(NULL);
		free(tmp);
		return -1;
	}

	if (stat(new,&newstat) == -1) {
		fprintf(stderr,"Cannot call stat on %s: ",new);
		perror(NULL);
		free(tmp);
		return -1;
	}

	/* call utimes first as chmod and chown may disallow this */
	timespecs[0].tv_sec = newstat.st_atime;
	timespecs[0].tv_nsec = 0;
	timespecs[1].tv_sec = newstat.st_mtime;
	timespecs[1].tv_nsec = 0;

	if (utimensat(AT_FDCWD,tmp,timespecs,AT_SYMLINK_NOFOLLOW) == -1) {
		fprintf(stderr,
			"Cannot set modification and access times of file %s.\n"
			"Times on file %s will be clobbered: ",tmp,new);
		perror(NULL);
		if (preserve) {
			free(tmp);
			return -1;
		}
	}

	/* permissions and ownership do not make sense on symbolic links */
	if (do_link == symlink) goto skip_preservation;

	if (chmod(tmp,newstat.st_mode) == -1) {
		fprintf(stderr,
			"Cannot set permission of file %s.\n"
			"Permission of file %s will be clobbered: ",tmp,new);
		perror(NULL);
		if (preserve) {
			free(tmp);
			return -1;
		}
	}

	if (chown(tmp,newstat.st_uid,newstat.st_gid) == -1) {
		fprintf(stderr,
			"Cannot set ownership of file %s.\n"
			"Ownership of file %s will be clobbered: ",tmp,new);
		perror(NULL);
		if (preserve) {
			free(tmp);
			return -1;
		}
	}

	skip_preservation:

	if (rename(tmp,new) == -1) {
		fprintf(stderr,"Cannot rename %s to %s: ",tmp,new);
		perror(NULL);
		free(tmp);
		return -1;
	}

	free(tmp);
	return 0;
}

int make_links(struct matcher *m, link_flags f, link_func lf, const char *lf_name) {
	const char *orig, *dup;
	int link_count = 0, pair_count = 0, preserve = f & LINKS_PRESERVE;

	while ((orig = next_group(m))) {
		pair_count++;
		while ((dup = next_file(m))) {
			link_count++;
			if (perform_link(lf,lf_name,orig,dup,preserve)) return 1;
			if (f & LINKS_PRESERVE) fprintf(stderr,
				"\rMade %9d links for %9d groups",link_count,pair_count);
		}
	}

	return 0;
}

int print_dups(struct matcher *m) {
	int first = 1;
	const char *file;

	while ((file = next_group(m))) {
		if (first) first = 0;
		else printf("\n");

		puts(file);

		while ((file = next_file(m))) puts(file);
	}

	return 0;
}

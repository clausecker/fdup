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

#define _XOPEN_SOURCE 500
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>

#include <openssl/sha.h>

#include "match.h"

static enum {
	LIST_DUPS_MODE,
	HARD_LINK_MODE,
	SOFT_LINK_MODE
} operation_mode = LIST_DUPS_MODE;

static struct matcher *m;

static int print_walker(const char *fpath,const struct stat *sb,int tf,struct FTW *ftwbuf) {

	/* silence warnings */
	(void)tf;
	(void)ftwbuf;

	if (!S_ISREG(sb->st_mode)) return 0;

	if (register_file(m,fpath,sb)) return 1;

	fprintf(stderr,"\r%9d files",get_file_count(m));

	return 0;
}

static int print_dups(struct matcher *m) {
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

static int perform_hardlink(const char *old, const char *new) {
	char *tmp, *new_dup;
	int rval = 0, len;

	new_dup = strdup(new);
	if (new_dup == NULL) {
		perror("malloc failed");
		return 1;
	}

	/* /fdup.##########.tmp */
	len = strlen(new_dup) + 21;

	tmp = malloc(len);
	if (tmp == NULL) {
		perror("malloc failed");
		return 1;
	}

	snprintf(tmp,len,"%s/fdup.%010d.tmp",dirname(new_dup),getpid());
	free(new_dup);

	if (link(old,tmp) == -1) {
		fprintf(stderr,"Cannot hardlink %s to %s",old,tmp);
		perror(NULL);
		rval = 1;
	} else if (rename(tmp,new) == -1) {
		fprintf(stderr,"Cannot rename %s to %s",tmp,new);
		perror(NULL);
		rval = 1;
	}

	free(tmp);

	return rval;
}

static int make_hard_links(struct matcher *m) {
	const char *orig, *dup;
	int link_count = 0, pair_count = 0;

	while ((orig = next_group(m))) {
		pair_count++;
		while ((dup = next_file(m))) {
			link_count++;
			if (perform_hardlink(orig,dup)) return 1;
			fprintf(stderr,"\rMade %9d hardlinks for %9d groups",link_count,pair_count);
		}
	}

	return 0;
}

static void help(const char *program) {
	printf("Usage: %s [-H | -L | -S] [-hx] directory...\n",program);
}

int main(int argc, char *argv[]) {
	int ok=1,i,opt;
	rlim_t maxfiles;
	struct rlimit limit;
	matcher_flags flags = M_MODE|M_UID|M_GID;

	while ((opt = getopt(argc,argv,"SHLhx")) != -1) {
		switch(opt) {
		case 'L':
			operation_mode = LIST_DUPS_MODE;
			break;
		case 'S':
			operation_mode = SOFT_LINK_MODE;
			break;
		case 'H':
			operation_mode = HARD_LINK_MODE;
			flags |= M_LINK; /* avoid a quirk in rename */
			/* intentional fallthrough */
		case 'x':
			flags |= M_DEV;
			break;
		case 'h':
		default:
			help(argv[0]);
			return 2;
		}
	}

	if (optind >= argc) {
		help(argv[0]);
		return 2;
	}

	m = new_matcher(flags);

	/* attempt to use as many files as possible */
	getrlimit(RLIMIT_NOFILE,&limit);
	maxfiles = limit.rlim_cur - 8; /* spare some file descriptors */

	fputs("Scanning file system\n",stderr);

	for (i = optind; i < argc; i++) {
		ok = nftw(argv[i],print_walker,maxfiles,flags&M_DEV?FTW_PHYS:0);
		if (ok == -1) {
			fprintf(stderr,"\nError processing argument %s: ",argv[i]);
			perror(NULL);
		}
	}

	fputs("\nLooking for duplicates...\n",stderr);
	if (finalize_matcher(m)) return 1;

	switch (operation_mode) {
	case LIST_DUPS_MODE: ok = print_dups(m); break;
	case HARD_LINK_MODE: ok = make_hard_links(m); break;
	case SOFT_LINK_MODE: fputs("Not implemented yet",stderr); break;
	}

	if (!ok) return 1;

	free_matcher(m);

	return 0;
}

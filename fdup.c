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

#include <ftw.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "match.h"
#include "action.h"
#include "btrfs.h"

/* these variables have to be global as it is not possible to supply an extra
 * argument to the function passed to nftw. The only way to supply extra data
 * to walker are in fact global variables. */
static struct matcher *matcher;
static off_t lower_boundary = 0;
static off_t upper_boundary = 0;
static int has_upper_boundary = 0;
static int verbose = 0;

static off_t adjust_suffix(off_t,char);
static void help(const char *);
static int walker(const char*,const struct stat*,int,struct FTW*);

static int walker(const char *fpath,const struct stat *sb,int tf,struct FTW *ftwbuf) {
	/* silence warnings */
	(void)tf;
	(void)ftwbuf;

	if (!S_ISREG(sb->st_mode)) return 0;
	if (sb->st_size < lower_boundary) return 0;
	if (has_upper_boundary && sb->st_size > upper_boundary) return 0;

	if (register_file(matcher,fpath,sb)) return 1;

	if (verbose) fprintf(stderr,"\r%9d files",get_file_count(matcher));

	return 0;
}

static void help(const char *program) {
	printf("Usage: %s [-B | -H | -L | -S] [-hpvx] [-b cdglmpu] [-s n[,m]] directory...\n",program);
}

/* apply kilo, mega, giga etc. suffix */
static off_t adjust_suffix(off_t n, char suffix) {
	switch (suffix) {
	case 'K': return n << 10;
	case 'M': return n << 20;
	case 'G': return n << 30;
	case 'T': return n << 40;
	case 'P': return n << 50;
	case 'E': return n << 60;
	default: return n;
	}
}

int main(int argc, char *argv[]) {
	int ok = 1, i, opt, xdev = 0;
	rlim_t maxfiles;
	struct rlimit limit;
	matcher_flags flags = 0;
	link_flags lf = 0;
	char *argrmdr;
	enum {
		LIST_DUPS_MODE,
		HARD_LINK_MODE,
		SOFT_LINK_MODE,
		BTRFS_COPY_MODE
	} mode = LIST_DUPS_MODE;

	while ((opt = getopt(argc,argv,"BHLSb:hps:vx")) != -1) {
		switch(opt) {
		case 'B':
			mode = BTRFS_COPY_MODE;
			break;
		case 'H':
			mode = HARD_LINK_MODE;
			flags |= M_DEV|M_LINK; /* avoid a quirk in rename */
			break;
		case 'L':
			mode = LIST_DUPS_MODE;
			break;
		case 'S':
			mode = SOFT_LINK_MODE;
			break;
		case 'b':
			optarg--;
			while (*++optarg != '\0') switch (*optarg) {
			case 'c': flags |= M_CTIME; break;
			case 'd': flags |= M_DEV; break;
			case 'g': flags |= M_GID; break;
			case 'l': flags |= M_LINK; break;
			case 'm': flags |= M_MTIME; break;
			case 'p': flags |= M_MODE; break;
			case 'u': flags |= M_UID; break;
			default:
				fprintf(stderr,"Unknown specifier %c to -b\n",*optarg);
				return 2;
			}
			break;
		case 'p':
			lf &= LINKS_PRESERVE;
			break;
		case 's':
			if (*optarg == '\0') {
				help(argv[0]);
				return 2;
			}
			lower_boundary = strtoll(optarg,&argrmdr,10);
			if (strchr(",KMGTPE",*argrmdr) == NULL) {
				fprintf(stderr,"Unexpected character %c in string to -s\n",*argrmdr);
				help(argv[0]);
				return 2;
			}
			if (*argrmdr != '\0' && *argrmdr != ',')
				lower_boundary = adjust_suffix(lower_boundary,*argrmdr++);
			if (*argrmdr == ',') argrmdr++;
			if (*argrmdr == '\0') break;

			has_upper_boundary = 1;
			optarg = argrmdr;
			upper_boundary = strtoll(optarg,&argrmdr,10);

			if (strchr("KMGTPE",*argrmdr) == NULL) {
				fprintf(stderr,"Unexpected character %c in string to -s\n",*argrmdr);
				help(argv[0]);
				return 2;
			}

			if (*argrmdr != '\0') upper_boundary = adjust_suffix(upper_boundary,*argrmdr);
			break;
		case 'v':
			verbose = 1;
			lf &= LINKS_VERBOSE;
			break;
		case 'x':
			xdev = 1;
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

	matcher = new_matcher(flags);

	/* attempt to use as many files as possible */
	getrlimit(RLIMIT_NOFILE,&limit);
	maxfiles = limit.rlim_cur - 8; /* spare some file descriptors */

	if (verbose) fputs("Scanning file system...\n",stderr);

	for (i = optind; i < argc; i++) {
		ok = nftw(argv[i],walker,maxfiles,FTW_PHYS|(xdev?FTW_MOUNT:0));
		if (ok == -1) {
			fprintf(stderr,"\nError processing argument %s: ",argv[i]);
			perror(NULL);
		}
	}

	if (verbose) fputs("\nLooking for duplicates...\n",stderr);
	if (finalize_matcher(matcher)) return 1;

	switch (mode) {
	case LIST_DUPS_MODE:  ok = print_dups(matcher); break;
	case HARD_LINK_MODE:  ok = make_links(matcher,lf,link,"hardlink"); break;
	case SOFT_LINK_MODE:  ok = make_links(matcher,lf,symlink,"symlink"); break;
	case BTRFS_COPY_MODE: ok = make_links(matcher,lf,btrfs_clone,"clone"); break;
	}

	if (!ok) return 1;

	free_matcher(matcher);

	return 0;
}

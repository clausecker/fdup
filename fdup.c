/* Copyright (c) 2013, Robert Clausecker
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

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

#define BUFSIZE (16*1024)

#ifndef __has_attribute
# define __has_attribute(x) 0
#endif

/* this macro surpresses warnings that something is unused */
#if defined(__GNUC__) || __has_attribute(unused)
# define UNUSED __attribute__((unused))
#else
# define UNUSED
#endif

struct fileinfo {
	long path; /* pointer into filename file */
	ino_t inode; /* tell apart hardlinks to the same file */
	unsigned char hash[SHA_DIGEST_LENGTH];
};

static const char hextab[16] = "0123456789abcdef";

static enum {
	NO_XDEV_FLAG = 0x1
} operation_flags = 0;
static enum {
	LIST_DUPS_MODE,
	LIST_HASH_MODE,
	HARD_LINK_MODE
} operation_mode = LIST_DUPS_MODE;
static FILE *names;
static FILE *hashes;
static int filecount = 0;
static size_t bytecount = 0;

static void sha1_to_string(
	char out[2*SHA_DIGEST_LENGTH+1],
	const unsigned char hash[SHA_DIGEST_LENGTH]) {

	int i;
	unsigned int n;
	for (i = 0; i < SHA_DIGEST_LENGTH; i++) {
		n = hash[i];
		out[2*i] = hextab[n>>4];
		out[2*i+1] = hextab[n&15];
	}

	out[2*i] = '\0';
}

/* returns 1 on success, 0 on failure */
static int file_sha1(unsigned char hash[20], const char *filepath) {
	unsigned char buf[BUFSIZE];
	SHA_CTX sha;
	ssize_t count;
	int fd = open(filepath, O_RDONLY);

	/* we probably don't have the right permissions */
	if (fd < 0) return 0;

	SHA1_Init(&sha);

	while ((count = read(fd,buf,BUFSIZE)) > 0) {
		bytecount += count;
		SHA1_Update(&sha,buf,count);
	}

	if (count < 0) {
		perror("Error reading file in file_sha");
		close(fd);
		return 0;
	}

	SHA1_Final(hash,&sha);
	close(fd);

	return 1;
}

static int print_walker(
	const char *fpath,
	const struct stat *sb,
	int typeflag UNUSED,
	struct FTW *ftwbuf UNUSED) {

	struct fileinfo finfo;

	if (!S_ISREG(sb->st_mode) || !file_sha1(finfo.hash,fpath)) return 0;

	finfo.inode = sb->st_ino;
	finfo.path = ftell(names);
	fwrite(fpath,sizeof(char),strlen(fpath)+1,names);
	fwrite(&finfo,sizeof finfo,1,hashes);
	filecount++;
	fprintf(stderr,"\r%15zu bytes, %9d files",bytecount,filecount);

	return 0;
}

static int cmp_fileinfo(const struct fileinfo *a, const struct fileinfo *b) {
	return memcmp(a->hash,b->hash,sizeof(a->hash));
}

/* returns -1 on error, fd of hashes otherwise */
static struct fileinfo *sort_hashes(void) {
	int fd;
	struct fileinfo *infos;

	fd = fileno(hashes);

	infos = mmap(NULL,filecount*sizeof*infos,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	if (infos == MAP_FAILED) {
		perror("Cannot map temporary file in sort_hashes");
		return NULL;
	}

	qsort(infos,filecount,sizeof*infos,(int(*)(const void*,const void*))cmp_fileinfo);

	return infos;
}

static const char *map_filenames(void) {
	int fd;
	const char *filenames;

	fd = fileno(names);
	filenames = mmap(NULL,ftell(names),PROT_READ,MAP_SHARED,fd,0);
        if (filenames == MAP_FAILED) {
                perror("Cannot map file names");
                return NULL;
        }

	return filenames;
}

static int print_files(const struct fileinfo *infos) {
	int i;
	const char *filenames;
	char digest[2*SHA_DIGEST_LENGTH+1];

	if ((filenames = map_filenames()) == NULL) return 0;

	for (i = 0; i < filecount; i++) {
		sha1_to_string(digest,infos[i].hash);
		printf("%40s %s\n",digest,filenames+infos[i].path);
	}

	return 1;
}

static int print_dups(const struct fileinfo *infos) {
	int i;
	bool odup = false, ndup, first = true;
	const char *filenames;

	if (filecount < 2) return 1;

	if ((filenames = map_filenames()) == NULL) return 0;

	for (i = 1; i < filecount; i++) {
		ndup = cmp_fileinfo(&infos[i-1],&infos[i]) == 0;

		if (ndup && !odup && !first) {
			puts("");
		}

		if (ndup || odup) {
			first = false;
			puts(filenames+infos[i-1].path);
		}

		odup = ndup;
	}

	if (odup) {
		printf("%s\n",filenames+infos[filecount-1].path);
	}

	return 1;
}

static int perform_hardlink(const char *old, const char *new) {
	char *tmp, *new_dup;
	int rval = 1, len;

	new_dup = strdup(new);
	if (new_dup == NULL) {
		perror("malloc failed");
		exit(1);
	}

	/* /fdup.##########.tmp */
	len = strlen(new_dup) + 21;

	tmp = malloc(len);
	if (tmp == NULL) {
		perror("malloc failed");
		exit(1);
	}

	snprintf(tmp,len,"%s/fdup.%010d.tmp",dirname(new_dup),getpid());
	free(new_dup);

	if (link(old,tmp) == -1) {
		fprintf(stderr,"Cannot hardlink %s to %s",old,tmp);
		perror(NULL);
		rval = 0;
	} else if (rename(tmp,new) == -1) {
		fprintf(stderr,"Cannot rename %s to %s",tmp,new);
		perror(NULL);
		rval = 0;
	}

	free(tmp);

	return rval;
}

static int make_hard_links(const struct fileinfo *infos) {
	const char *filenames, *reference;
	ino_t ref_inode;
	int i, link_count = 0, pair_count = 0, rval = 1;
	bool odup = false, ndup;

	if (filecount <2) return 1;

	if ((filenames = map_filenames()) == NULL) return 0;

	for (i = 1; i < filecount; i++) {
		ndup = cmp_fileinfo(&infos[i-1],&infos[i]) == 0;

		/* add a new file to an existing group */
		if (odup) {
			if (infos[i-1].inode == ref_inode
			|| perform_hardlink(reference,filenames + infos[i-1].path))
				link_count++;
			else rval = 0;

			goto stats;
		}

		/* encountered a new group of duplicate files */
		if (ndup) {
			reference = filenames + infos[i-1].path;
			ref_inode = infos[i-1].inode;

			pair_count++;
			goto stats;
		}

		continue;

		stats:
		odup = ndup;
		fprintf(stderr,"\rMade %9d hardlinks for %9d groups of duplicates",link_count,pair_count);
	}

	/* If there is a file left over finish. */
	if (odup) {
		if (infos[i-1].inode == ref_inode
		|| perform_hardlink(reference,filenames + infos[i-1].path))
			link_count++;
		else rval = 0;

		fprintf(stderr,"\rMade %9d hardlinks for %9d groups of duplicates",link_count,pair_count);
	}

	if (link_count > 0 || pair_count > 0) fputs("\n",stderr);
	return rval;
}

static void help(const char *program) {
	printf("Usage: %s [-H | -L | -S] [-hx] directory...\n",program);
}

int main(int argc, char *argv[]) {
	int ok=1,i,opt;
	rlim_t maxfiles;
	struct rlimit limit;
	struct fileinfo *infos;

	while ((opt = getopt(argc,argv,"Hhlx")) != -1) {
		switch(opt) {
		case 'l':
			operation_mode = LIST_HASH_MODE;
			break;
		case 'H':
			operation_mode = HARD_LINK_MODE;
			/* intentional fallthrough */
		case 'x':
			operation_flags |= NO_XDEV_FLAG;
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

	names = tmpfile();
	if (names == NULL) {
		perror("Cannot create temporary file");
		return 1;
	}

	hashes = tmpfile();
	if (hashes == NULL) {
		perror("Cannot create temporary file");
		return 1;
	}

	/* attempt to use as many files as possible */
	getrlimit(RLIMIT_NOFILE,&limit);
	maxfiles = limit.rlim_cur - 8; /* spare some file descriptors */

	for (i = optind; i < argc; i++) {
		ok = nftw(argv[i],print_walker,maxfiles,operation_flags&NO_XDEV_FLAG?FTW_PHYS:0);
		if (ok == -1) {
			fprintf(stderr,"Error processing argument %s: ",argv[i]);
			perror(NULL);
		}
	}

	fflush(names);
	fflush(hashes);

	/* mmap doesn't like empty files. We won't generate output anyway. */
	if (filecount == 0) return 0;

	fprintf(stderr,"\nSorting %d files...\n",filecount);

	infos = sort_hashes();
	if (infos == NULL) return 1;

	switch (operation_mode) {
	case LIST_HASH_MODE: ok = print_files(infos); break;
	case LIST_DUPS_MODE: ok = print_dups(infos); break;
	case HARD_LINK_MODE: ok = make_hard_links(infos); break;
	}

	if (!ok) return 1;

	fclose(names);
	fclose(hashes);

	return 0;
}

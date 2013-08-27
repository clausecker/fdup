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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "match.h"

typedef unsigned char sha_hash[SHA_DIGEST_LENGTH];

struct fileinfo {
	struct stat stat;
	off_t path; /* pointer into filename file */
	int hashed; /* 0: no hash; 1: short hash; 2: full hash */
	sha_hash hash;
	sha_hash short_hash;
};

struct matcher {
	FILE *name_file;
	FILE *info_file;
	char *name_map;
	struct fileinfo *info_map;
	int file_count;
	int file_index;
	matcher_flags flags;
	bool finalized;
};

enum {
	HAS_SHORT_HASH = 1,
	HAS_FULL_HASH = 2,
	SHORT_HASH_SIZE = 16*1024*1024,
	BUFSIZE = 16*1024
};

/* hack: qsort does not allow an extra parameter so we instead store the
 * paremeter in this thread-local variable. */
static struct matcher *cmp_matcher;
static int cmp_fileinfo(struct fileinfo*,struct fileinfo*);
static int file_sha1(sha_hash,const char*,off_t);

struct matcher *new_matcher(matcher_flags f) {
	FILE *names, *infos;
	struct matcher *m = calloc(1,sizeof*m);

	if (m == NULL) {
		perror("Cannot allocate memory");
		return NULL;
	}
	m->flags = f;

	names = tmpfile();
	if (names == NULL) {
		perror("Cannot open temporary file");
		free(m);
		return NULL;
	}

	infos = tmpfile();
	if (infos == NULL) {
		perror("Cannot open temporary file");
		free(m);
		return NULL;
	}

	m->name_file = names;
	m->info_file = infos;

	return m;
}

int register_file(struct matcher *m, const char *path, const struct stat *stat) {
	struct fileinfo info;
	off_t offset;
	size_t len;

	if (m->finalized) {
		errno = EINVAL;
		return 1;
	}

	/* avoid leaking stack contents into temporary file */
	memset(&info,0,sizeof info);

	offset = ftello(m->name_file);
	info.path = offset;
	info.hashed = false;
	memcpy(&info.stat,stat,sizeof*stat);

	if (fwrite(&info,sizeof info,1,m->info_file) != 1) {
		perror("Error writing to temporary file");
		return 1;
	}

	len = strlen(path) + 1;
	if (fwrite(path,sizeof*path,len,m->name_file) != len) {
		perror("Error writing to temporary file");
		return 1;
	}

	m->file_count++;
	return 0;
}

int get_file_count(struct matcher *m) {
	return m->file_count;
}

int finalize_matcher(struct matcher *m) {
	int name_fd, info_fd, old_errno;
	size_t name_size, info_size;
	void *info_mapping, *name_mapping;

	if (m->finalized) {
		errno = EINVAL;
		return 1;
	}

	if (m->file_count == 0) {
		m->finalized = true;
		return 0;
	}

	if (fflush(m->info_file)) {
		perror("Failed to flush temporary file");
		return 1;
	}

	if (fflush(m->name_file)) {
		perror("Failed to flush temporary file");
		return 1;
	}

	info_fd = fileno(m->info_file);
	name_fd = fileno(m->name_file);
	info_size = ftello(m->info_file);
	name_size = ftello(m->name_file);

	name_mapping = mmap(NULL,name_size,PROT_READ,MAP_SHARED,name_fd,0);
	if (name_mapping == MAP_FAILED) {
		perror("Cannot map temporary file");
		return 1;
	}

	info_mapping = mmap(NULL,info_size,PROT_READ|PROT_WRITE,MAP_SHARED,info_fd,0);
	if (info_mapping == MAP_FAILED) {
		perror("Cannot map temporary file");
		return 1;
	}

	m->name_map = name_mapping;
	m->info_map = info_mapping;


	old_errno = errno;
	errno = 0;

	cmp_matcher = m;
	qsort(
		m->info_map,
		m->file_count,
		sizeof(struct fileinfo),
		(int(*)(const void*,const void*))cmp_fileinfo
	);

	if (errno != 0) {
		perror("Cannot sort file information");
		return 1;
	}

	errno = old_errno;

	m->finalized = true;

	return 0;
}

/* cmp_fileinfo orders files according to the following criteria, listed in
 * decreasing order of importance:
 *  - size
 *  - device id (only if M_DEV)
 *  - inode number (result depending on M_LINK)
 *  - permissions (except if M_MODE)
 *  - modification time (only if M_MTIME)
 *  - creation time (only if M_CTIME)
 *  - hash
 * cmp_fileinfo may fail. In this case it sets errno to a nonzero value.
 */

#define CMP_BY(x) if (a->stat.x != b->stat.x) return a->stat.x - b->stat.x

static int cmp_fileinfo(struct fileinfo *a,struct fileinfo *b) {
	matcher_flags f = cmp_matcher->flags;
	const char *names = cmp_matcher->name_map;
	int cmp;

	if (a == b) return 0;

	CMP_BY(st_size);

	if (f & M_DEV) CMP_BY(st_dev);

	if (a->stat.st_dev == b->stat.st_dev && a->stat.st_ino == b->stat.st_ino)
		return (f & M_LINK) ? strcmp(names+a->path, names+b->path) : 0;

	if (f & M_MODE) CMP_BY(st_mode);
	if (f & M_UID) CMP_BY(st_uid);
	if (f & M_GID) CMP_BY(st_gid);
	if (f & M_MTIME) CMP_BY(st_mtime);
	if (f & M_CTIME) CMP_BY(st_ctime);

	if (a->hashed & HAS_SHORT_HASH) {
		file_sha1(a->short_hash,names+a->path,SHORT_HASH_SIZE);
		a->hashed |= HAS_SHORT_HASH;
	}

	if (b->hashed & HAS_SHORT_HASH) {
		file_sha1(b->short_hash,names+b->path,SHORT_HASH_SIZE);
		b->hashed |= HAS_SHORT_HASH;
	}

	cmp = memcmp(a->short_hash,b->short_hash,SHA_DIGEST_LENGTH);

	if (cmp != 0) return cmp;

	if (a->hashed & HAS_FULL_HASH) {
		file_sha1(a->hash,names+a->path,a->stat.st_size);
		a->hashed |= HAS_FULL_HASH;
	}

	if (b->hashed & HAS_FULL_HASH) {
		file_sha1(b->hash,names+b->path,b->stat.st_size);
		b->hashed |= HAS_FULL_HASH;
	}

	return memcmp(a->hash,b->hash,SHA_DIGEST_LENGTH);
}

#undef CMP_BY

/* returns 1 on success, 0 on failure. Hashes first length bytes */
static int file_sha1(sha_hash hash, const char *filepath, off_t length) {
	unsigned char buf[BUFSIZE];
	SHA_CTX sha;
	ssize_t count;
	int fd = open(filepath,O_RDONLY);

	/* we probably don't have the right permissions */
	if (fd < 0) return 0;

	SHA1_Init(&sha);

	while (length > 0 && (count = read(fd,buf,BUFSIZE)) > 0) {
		SHA1_Update(&sha,buf,count<length?count:length);
		length -= count < length ? count : length;
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

/* after a successful next_group file_index points to the first file in the
 * current duplication group. */
const char *next_group(struct matcher *m) {
	int cmp, old_errno;

	if (!m->finalized) {
		errno = EINVAL;
		return NULL;
	}

	while (m->file_index + 1 < m->file_count) {
		old_errno = errno;
		errno = 0;
		cmp_matcher = m;
		cmp = cmp_fileinfo(m->info_map+m->file_index,m->info_map+m->file_index+1);
		if (errno != 0) return NULL;
		errno = old_errno;

		if (cmp == 0) return m->name_map + m->info_map[m->file_index].path;

		m->file_index++;
	}

	return NULL;
}

/* next_file yields the file immediately after the file pointed to by file_index,
 * iff it compares equal to the file pointed to by file_index */
const char *next_file(struct matcher *m) {
	int cmp, old_errno;

	if (!m->finalized) {
		errno = EINVAL;
		return NULL;
	}

	if (m->file_index + 1 >= m->file_count) return NULL;

	old_errno = errno;
	errno = 0;
	cmp_matcher = m;
	cmp = cmp_fileinfo(m->info_map+m->file_index,m->info_map+m->file_index+1);
	if (errno != 0) return NULL;
	errno = old_errno;

	m->file_index++;

	return cmp == 0 ? m->name_map + m->info_map[m->file_index].path : NULL;
}

void free_matcher(struct matcher *m) {
	off_t name_size = ftello(m->name_file), info_size = ftello(m->info_file);
	long pagesize;

	if (!m->finalized) goto skip_munmap;

	pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize > 0) goto got_pagesize;

	pagesize = sysconf(_SC_PAGE_SIZE);
	if (pagesize > 0) goto got_pagesize;

	fputs("Could not get page size\n",stderr);
	goto skip_munmap;

	got_pagesize:

	/* align name_size and info_size to page_size */
	name_size = name_size + (pagesize - 1 - (name_size - 1) % pagesize);
	info_size = info_size + (pagesize - 1 - (info_size - 1) % pagesize);

	munmap(m->name_map,name_size);
	munmap(m->info_map,info_size);

	skip_munmap:

	fclose(m->name_file);
	fclose(m->info_file);

	free(m);
}

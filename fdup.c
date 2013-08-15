#define _XOPEN_SOURCE 500
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
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
	unsigned char hash[SHA_DIGEST_LENGTH];
	long path; /* pointer into filename file */
};

static const char hextab[16] = "0123456789abcdef";

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

static int UNUSED print_files(struct fileinfo *infos) {
	int fd, i;
	const char *filenames;
	char digest[2*SHA_DIGEST_LENGTH+1];

	fd = fileno(names);

	filenames = mmap(NULL,ftell(names),PROT_READ,MAP_SHARED,fd,0);
	if (filenames == MAP_FAILED) {
		perror("Cannot map temporary file in print_files");
		return 0;
	}

	for(i = 0; i < filecount; i++) {
		sha1_to_string(digest,infos[i].hash);
		printf("%40s %s\n",digest,filenames+infos[i].path);
	}

	return 1;
}

static int UNUSED print_dups(const struct fileinfo *infos) {
	int fd, i;
	bool odup = false, ndup, first = true;
	const char *filenames;

	fd = fileno(names);

	filenames = mmap(NULL,ftell(names),PROT_READ,MAP_SHARED,fd,0);
	if (filenames == MAP_FAILED) {
		perror("Cannot map temporary file in print_dups");
		return 0;
	}

	if (filecount < 2) return 1;

	for(i = 1; i < filecount; i++) {
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

static void help(const char *program) {
	printf("Usage: %s file...\n",program);
}

int main(int argc, char *argv[]) {
	int ok,i;
	rlim_t maxfiles;
	struct rlimit limit;
	struct fileinfo *infos;

	if (argc < 2) {
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

	for (i = 1; i < argc; i++) {
		ok = nftw(argv[i],print_walker,maxfiles,FTW_PHYS);
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

	/* ok = print_files(infos); */
	ok = print_dups(infos);

	if (!ok) return 1;

	fclose(names);
	fclose(hashes);

	return 0;
}

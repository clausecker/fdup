#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>

#include <openssl/sha.h>

#define BUFSIZE (16*1024)

#ifndef __has_attribute
# define __has_attribute(x) 0
#endif

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

	return 0;
}

static int cmp_fileinfo(const void *a, const void *b) {
	struct fileinfo *aa = (struct fileinfo*)a;
	struct fileinfo *bb = (struct fileinfo*)b;

	return memcmp(aa->hash,bb->hash,sizeof(aa->hash));
}

/* returns -1 on error, fd of hashes otherwise */
static struct fileinfo *sort_hashes(void) {
	int fd;
	struct fileinfo *infos;

	fd = fileno(hashes);

	infos = mmap(NULL,filecount*sizeof*infos,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	if (infos == MAP_FAILED) {
		perror("Cannot map fdup.hashes");
		return NULL;
	}

	qsort(infos,filecount,sizeof*infos,cmp_fileinfo);

	return infos;
}

static int print_files(struct fileinfo *infos) {
	int fd, i;
	const char *filenames;
	char digest[2*SHA_DIGEST_LENGTH+1];

	fd = fileno(names);

	if (fd < 0) {
		perror("Cannot open fdup.names");
		return 0;
	}

	filenames = mmap(NULL,ftell(names),PROT_READ,MAP_SHARED,fd,0);
	if (filenames == MAP_FAILED) {
		perror("Cannot map fdup.names");
		return 0;
	}

	for(i = 0; i < filecount; i++) {
		sha1_to_string(digest,infos[i].hash);
		printf("%40s %s\n",digest,filenames+infos[i].path);
	}

	return 1;
}

int main(int argc, char *argv[]) {
	struct fileinfo *infos;

	if (argc != 2) {
		printf("Usage: %s <directory>\n",argv[0]);
		return 2;
	}

	names = fopen("fdup.names","w+b");

	if (names == NULL) {
		perror("Cannot open fdup.names");
		return 1;
	}

	hashes = fopen("fdup.hashes","w+b");

	if (hashes == NULL) {
		perror("Cannot open fdup.hashes");
		return 1;
	}

	nftw(argv[1],print_walker,256,0);

	fflush(names);
	fflush(hashes);

	fprintf(stderr,"Sorting %d files...\n",filecount);

	infos = sort_hashes();
	if (infos == NULL) return 1;

	if (!print_files(infos)) return 1;

	fclose(names);
	fclose(hashes);

	return 0;
}

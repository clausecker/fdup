#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <ftw.h>

#include <openssl/sha.h>

#define BUFSIZE (16*1024)

#ifdef __GNUC__
# define UNUSED __attribute__((unused))
#else
# define UNUSED
#endif

static const char hextab[16] = "0123456789abcdef";

void sha1_to_string(
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
int file_sha1(unsigned char hash[20], const char *filepath) {
	unsigned char buf[BUFSIZE];
	SHA_CTX sha;
	ssize_t count;
	int fd = open(filepath, O_RDONLY);

	/* we probably don't have the right permissions */
	if (fd < 0) {
		return 0;
	}

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

static const char types[] = {
	[FTW_F]		= '-',
	[FTW_D]		= 'd',
	[FTW_DNR]	= 'D',
	[FTW_NS]	= '!',
	[FTW_SL]	= 'l',
	[FTW_SLN]	= 'L'
};

static int print_walker(
	const char *fpath,
	const struct stat *sb UNUSED,
	int typeflag,
	struct FTW *ftwbuf UNUSED) {

	unsigned char hash[SHA_DIGEST_LENGTH];
	char digest[2*SHA_DIGEST_LENGTH+1];
	const char *dptr = "";

	if (typeflag == FTW_F && file_sha1(hash,fpath)) {
		sha1_to_string(digest,hash);
		dptr = digest;
	}

	printf("%c %40s %s\n",types[typeflag],dptr,fpath);
	return 0;
}

int main(int argc, char *argv[]) {

	if (argc != 2) {
		printf("Usage: %s <directory>\n",argv[0]);
		return 2;
	}

	nftw(argv[1],print_walker,256,FTW_MOUNT);

	return 0;
}

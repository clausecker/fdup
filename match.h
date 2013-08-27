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

#ifndef MATCH_H
#define MATCH_H

typedef enum {
	M_LINK  = 0x01, /* Are hardlinks to the same file distinct? */
	M_CTIME = 0x02, /* Are files with distinct creation time distinct? */
	M_MTIME = 0x04, /* Are files with distinct modification time distinct? */
	M_DEV   = 0x08, /* Are equal files on different devices distinct? */
	M_MODE  = 0x10, /* Are files with different access modes distint? */
	M_UID   = 0x20, /* Are files owned by different users distinct? */
	M_GID   = 0x40  /* Are files owned by differed groups distinct? */
} matcher_flags;

/* returns NULL on error with errno set appropriately */
struct matcher *new_matcher(matcher_flags);
/* these function return 0 on success */
int register_file(struct matcher*,const char*,const struct stat*);
int get_file_count(struct matcher*);
int finalize_matcher(struct matcher*);
/* return NULL if there is no next file in this group or no next group or 
 * on error. next_group returns the first file in said group. */
const char *next_group(struct matcher*);
const char *next_file(struct matcher*);
void free_matcher(struct matcher*);

#endif /* MATCH_H */

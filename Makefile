# Copyright (c) 2013, Robert Clausecker
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

LDLIBS=-lcrypto
CFLAGS=-O3 -Wall -Wextra -pedantic -std=c1x -g

LFS_CFLAGS=$(shell getconf LFS_CFLAGS)
LFS_LDFLAGS=$(shell getconf LFS_LDFLAGS)
LFS_LIBS=$(shell getconf LFS_LIBS)

REVISION=$(shell git rev-parse --short HEAD)

LDLIBS=$(LFS_LIBS) -lcrypto
LDFLAGS=$(LFS_LDFLAGS)
CFLAGS=$(LFS_CFLAGS) -O3 -Wall -Wextra -pedantic -std=c1x -g
NROFF=nroff
GIT=git

OBJ=fdup.o match.o

all: fdup

fdup: $(OBJ)

.PHONY: all clean

clean:
	$(RM) *.o *.s *~ fdup fdup.tar

README: fdup.1
	$(NROFF) -man fdup.1 | col -bx >$@

# HEAD gets updated whenever git revision changes
fdup.tar: .git/HEAD
	$(GIT) archive --prefix=fdup-$(REVISION)/ --format=tar -o $@ HEAD

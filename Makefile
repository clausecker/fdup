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

COL=col
CP=cp
GIT=git
INSTALL=install
MKDIR=mkdir -p
NROFF=nroff

REVISION=$(shell git describe --always)
SRCDIR=fdup-$(REVISION)

PREFIX?=/usr/local

all: build

tarball: $(SRCDIR).tar.gz

clean: src/clean
	$(RM) -r proto
	$(RM) $(SRCDIR).tar $(SRCDIR).tar.gz

build: src/fdup fdup.1
	$(MKDIR) proto/bin
	$(CP) src/fdup proto/bin
	$(MKDIR) proto/share/man/man1
	$(CP) fdup.1 proto/share/man/man1

install: build
	$(INSTALL) -d $(PREFIX)/bin
	$(INSTALL) proto/bin/fdup $(PREFIX)/bin
	$(INSTALL) -d $(PREFIX)/share/man/man1
	$(INSTALL) -m 0644 proto/share/man/man1/fdup.1 $(PREFIX)/share/man/man1/fdup.1

src/%:
	$(MAKE) -C $(dir $@) $(notdir $@)

README: fdup.1
	$(NROFF) -man fdup.1 | $(COL) -bx >$@

# HEAD gets updated whenever git revision changes
$(SRCDIR).tar: .git/HEAD
	$(GIT) archive --prefix=$(SRCDIR)/ -o $@ HEAD

$(SRCDIR).tar.gz: .git/HEAD
	$(GIT) archive --prefix=$(SRCDIR)/ -o $@ HEAD

.PHONY: all clean build install

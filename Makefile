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

BZIP2=bzip2 -9 -f
COL=col
CP=cp
GIT=git
GZIP=gzip -9 -f
INSTALL=install
MKDIR=mkdir -p
NROFF=nroff
TAR=tar
XZ=xz -9 -e -f

PREFIX?=/usr/local

all: build

git.mk:
	@echo "  GIT  " $@
	@if [ -d .git ] ; then \
		echo REVISION=`$(GIT) describe --always` >$@ ; \
	else \
		echo "Repository not available. Will not be able to make tarballs." ; \
		echo REVISION= >$@ ; \
	fi

include git.mk

SRCDIR=fdup-$(REVISION)

tarball: $(SRCDIR).tar.gz

clean: src/clean
	@echo "   RM  " proto && $(RM) -r proto
	@echo "   RM  " git.mk && $(RM) git.mk
	@echo "   RM  " $(SRCDIR).tar $(SRCDIR).tar.* && $(RM) $(SRCDIR).tar $(SRCDIR).tar.*
	@echo "   RM  " fdup.tar fdup.tar.* && $(RM) fdup.tar fdup.tar.*

build: src/build fdup.1
	@echo Populating proto directory...
	@echo " MKDIR " proto/bin && $(MKDIR) proto/bin
	@echo "   CP  " proto/bin/fdup && $(CP) src/fdup proto/bin
	@echo " MKDIR " proto/share/man/man1 && $(MKDIR) proto/share/man/man1
	@echo "   CP  " proto/share/man/man1/fdup.1 && $(CP) fdup.1 proto/share/man/man1

install: build
	@echo Installing...
	@echo " MKDIR " $(PREFIX)/bin && $(MKDIR) $(PREFIX)/bin
	@echo "INSTALL" $(PREFIX)/bin/fdup && $(INSTALL) proto/bin/fdup $(PREFIX)/bin
	@echo " MKDIR " $(PREFIX)/share/man/man1 && $(MKDIR) $(PREFIX)/share/man/man1
	@echo "INSTALL" $(PREFIX)/share/man/man1/fdup.1 && $(INSTALL) -m 0644 proto/share/man/man1/fdup.1 $(PREFIX)/share/man/man1/fdup.1

src/%:
	@echo Making `basename $@` in `dirname $@`...
	@cd `dirname $@` && $(MAKE) `basename $@`

$(SRCDIR).tar.gz: $(SRCDIR).tar
	@echo "  GZIP " $@ && $(GZIP) $^

%.gz: %
	@echo "  GZIP " $@ && $(GZIP) $<

%.bz2: %
	@echo " BZIP2 " $@ && $(BZIP2) $<

%.xz: %
	@echo "   XZ  " $@ && $(XZ) $<

README: fdup.1
	$(NROFF) -man fdup.1 | $(COL) -bx >$@

$(SRCDIR).tar:
	@if [ ! $(REVISION) ] ; then \
		echo "Cannot make tarball without git repository." ; \
		exit 1 ; \
	fi
	@echo "  GIT  " $@ && $(GIT) archive --prefix=$(SRCDIR)/ HEAD >$@


.PHONY: all clean build install src/*

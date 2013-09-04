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
GZIP=gzip -9
INSTALL=install
MKDIR=mkdir -p
NROFF=nroff

PREFIX?=/usr/local

all: build

git.mk:
	@echo "GIT " $@
	@if [ -d .git ] ; then \
		echo REVISION=`git describe --always` >$@ ; \
	else \
		echo "Repository not available. Will not be able to make tarballs." ; \
		echo REVISION= >$@ ; \
	fi

include git.mk

SRCDIR=fdup-$(REVISION)

tarball: $(SRCDIR).tar.gz

clean: src/clean
	@echo " RM " proto && $(RM) -r proto
	@echo " RM " git.mk && $(RM) git.mk
	@echo " RM " $(SRCDIR).tar && $(RM) $(SRCDIR).tar
	@echo " RM " $(SRCDIR).tar.gz && $(RM) $(SRCDIR).tar.gz

build: src/build fdup.1
	@echo Populating proto directory...
	@$(MKDIR) proto/bin
	@$(CP) src/fdup proto/bin
	@$(MKDIR) proto/share/man/man1
	@$(CP) fdup.1 proto/share/man/man1

install: build
	@echo Installing...
	@$(INSTALL) -d $(PREFIX)/bin
	@$(INSTALL) proto/bin/fdup $(PREFIX)/bin
	@$(INSTALL) -d $(PREFIX)/share/man/man1
	@$(INSTALL) -m 0644 proto/share/man/man1/fdup.1 $(PREFIX)/share/man/man1/fdup.1

src/%:
	@echo Making `basename $@` in `dirname $@`
	@cd `dirname $@` && $(MAKE) `basename $@`

README: fdup.1
	$(NROFF) -man fdup.1 | $(COL) -bx >$@

# HEAD gets updated whenever git revision changes
$(SRCDIR).tar:
	@if [ ! $(REVISION) ] ; then \
		echo "Cannot make tarball without git repository." ; \
		exit 1 ; \
	fi
	@echo "TAR " $@ && $(GIT) archive --prefix=$(SRCDIR)/ HEAD >$@

$(SRCDIR).tar.gz: $(SRCDIR).tar
	@echo "GZIP" $@ && $(GZIP) $^

.PHONY: all clean build install src/*

# /******************************************************************************
#  * Copyright (c) USTC(Suzhou) & Huawei Technologies Co., Ltd. 2022. All rights reserved.
#  * re2-rust licensed under the Mulan PSL v2.
#  * You can use this software according to the terms and conditions of the Mulan PSL v2.
#  * You may obtain a copy of Mulan PSL v2 at:
#  *     http://license.coscl.org.cn/MulanPSL2
#  * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
#  * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
#  * PURPOSE.
#  * See the Mulan PSL v2 for more details.
#  * Author: mengning<mengning@ustc.edu.cn>, liuzhitao<freekeeper@mail.ustc.edu.cn>, yangwentong<ywt0821@163.com>
#  * Create: 2022-06-21
#  * Description: Makefile of RE2-Rust.
#  ******************************************************************************/

CXX?=g++
# can override
CXXFLAGS?=-O3 -g
LDFLAGS?=-ldl
# required
RE2_CXXFLAGS?=-std=c++11 -pthread -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I. $(CCICU) $(CCPCRE)
RE2_LDFLAGS?=-pthread $(LDICU) $(LDPCRE)
AR?=ar
ARFLAGS?=rsc
NM?=nm
NMFLAGS?=-p

# Variables mandated by GNU, the arbiter of all good taste on the internet.
# http://www.gnu.org/prep/standards/standards.html
prefix=/usr/local
exec_prefix=$(prefix)
includedir=$(prefix)/include
libdir=$(exec_prefix)/lib
INSTALL=install
INSTALL_DATA=$(INSTALL) -m 644

# Work around the weirdness of sed(1) on Darwin. :/
ifeq ($(shell uname),Darwin)
SED_INPLACE=sed -i ''
else ifeq ($(shell uname),SunOS)
SED_INPLACE=sed -i
else
SED_INPLACE=sed -i
endif

# ABI version
# http://tldp.org/HOWTO/Program-Library-HOWTO/shared-libraries.html
SONAME=9

# To rebuild the Tables generated by Perl and Python scripts (requires Internet
# access for Unicode data), uncomment the following line:
# REBUILD_TABLES=1

# The SunOS linker does not support wildcards. :(
ifeq ($(shell uname),Darwin)
SOEXT=dylib
SOEXTVER=$(SONAME).$(SOEXT)
SOEXTVER00=$(SONAME).0.0.$(SOEXT)
MAKE_SHARED_LIBRARY=$(CXX) -dynamiclib -Wl,-compatibility_version,$(SONAME),-current_version,$(SONAME).0.0,-install_name,$(libdir)/libre2.$(SOEXTVER),-exported_symbols_list,libre2.symbols.darwin $(RE2_LDFLAGS) $(LDFLAGS)
else ifeq ($(shell uname),SunOS)
SOEXT=so
SOEXTVER=$(SOEXT).$(SONAME)
SOEXTVER00=$(SOEXT).$(SONAME).0.0
MAKE_SHARED_LIBRARY=$(CXX) -shared -Wl,-soname,libre2.$(SOEXTVER) $(RE2_LDFLAGS) $(LDFLAGS)
else
SOEXT=so
SOEXTVER=$(SOEXT).$(SONAME)
SOEXTVER00=$(SOEXT).$(SONAME).0.0
MAKE_SHARED_LIBRARY=$(CXX) -shared -Wl,-soname,libre2.$(SOEXTVER),--version-script,libre2.symbols $(RE2_LDFLAGS) $(LDFLAGS)
endif

.PHONY: all
all: libcapi.a obj/libre2.a obj/so/libre2.$(SOEXT)

INSTALL_HFILES=\
	re2/filtered_re2.h\
	re2/re2.h\
	re2/set.h\
	re2/stringpiece.h\
	regex-capi/include/regex_capi.h\

HFILES=\
	re2/testing/util/benchmark.h\
	re2/testing/util/logging.h\
	re2/testing/util/test.h\
	re2/testing/util/strutil.h\
	re2/testing/util/util.h\
	re2/filtered_re2.h\
	re2/re2.h\
	re2/set.h\
	re2/stringpiece.h\
	regex-capi/include/regex_capi.h\

# 仅保留接口stub
OFILES=obj/re2/re2.o\
	obj/re2/stringpiece.o\
	obj/re2/set.o\
	obj/re2/filtered_re2.o\

TESTOFILES=\
	obj/re2/testing/util/strutil.o\

TESTS=\
	obj/test/set_test\
	obj/test/re2_test\
	obj/test/re2_arg_test\
	obj/test/filtered_re2_test\

BIGTESTS=\
	obj/test/dfa_test\
	obj/test/exhaustive1_test\
	obj/test/exhaustive2_test\
	obj/test/exhaustive3_test\
	obj/test/exhaustive_test\
	obj/test/random_test\

SOFILES=$(patsubst obj/%,obj/so/%,$(OFILES))
# We use TESTOFILES for testing the shared lib, only it is built differently.
STESTS=$(patsubst obj/%,obj/so/%,$(TESTS))
SBIGTESTS=$(patsubst obj/%,obj/so/%,$(BIGTESTS))

DOFILES=$(patsubst obj/%,obj/dbg/%,$(OFILES))
DTESTOFILES=$(patsubst obj/%,obj/dbg/%,$(TESTOFILES))
DTESTS=$(patsubst obj/%,obj/dbg/%,$(TESTS))
DBIGTESTS=$(patsubst obj/%,obj/dbg/%,$(BIGTESTS))

.PRECIOUS: obj/%.o
obj/%.o: %.cc $(HFILES)
	@mkdir -p $$(dirname $@)
	$(CXX) -c -o $@ $(CPPFLAGS) $(RE2_CXXFLAGS) $(CXXFLAGS) -DNDEBUG $*.cc

.PRECIOUS: obj/dbg/%.o
obj/dbg/%.o: %.cc $(HFILES)
	@mkdir -p $$(dirname $@)
	$(CXX) -c -o $@ $(CPPFLAGS) $(RE2_CXXFLAGS) $(CXXFLAGS) $*.cc

.PRECIOUS: obj/so/%.o
obj/so/%.o: %.cc $(HFILES)
	@mkdir -p $$(dirname $@)
	$(CXX) -c -o $@ -fPIC $(CPPFLAGS) $(RE2_CXXFLAGS) $(CXXFLAGS) -DNDEBUG $*.cc

.PRECIOUS: libcapi.a
libcapi.a: 
	cargo build --release

.PRECIOUS: obj/libre2.a
obj/libre2.a: $(OFILES)
	@mkdir -p obj 
	$(AR) $(ARFLAGS) obj/libre2.a $(OFILES)

.PRECIOUS: obj/dbg/libre2.a
obj/dbg/libre2.a: $(DOFILES)
	@mkdir -p obj/dbg
	$(AR) $(ARFLAGS) obj/dbg/libre2.a $(DOFILES)

.PRECIOUS: obj/so/libre2.$(SOEXT)
obj/so/libre2.$(SOEXT): $(SOFILES) libre2.symbols libre2.symbols.darwin
	@mkdir -p obj/so
	$(MAKE_SHARED_LIBRARY) -o obj/so/libre2.$(SOEXTVER) $(SOFILES) target/release/libcapi.a $(LDFLAGS)
	ln -sf libre2.$(SOEXTVER) $@

.PRECIOUS: obj/dbg/test/%
obj/dbg/test/%: libcapi.a obj/dbg/libre2.a obj/dbg/re2/testing/%.o $(DTESTOFILES) obj/dbg/re2/testing/util/test.o
	@mkdir -p obj/dbg/test
	$(CXX) -o $@ obj/dbg/re2/testing/$*.o $(DTESTOFILES) obj/dbg/re2/testing/util/test.o obj/dbg/libre2.a target/release/libcapi.a $(RE2_LDFLAGS) $(LDFLAGS)

.PRECIOUS: obj/test/%
obj/test/%: obj/libre2.a obj/re2/testing/%.o $(TESTOFILES) obj/re2/testing/util/test.o
	@mkdir -p obj/test
	$(CXX) -o $@ obj/re2/testing/$*.o $(TESTOFILES) obj/re2/testing/util/test.o obj/libre2.a target/release/libcapi.a $(RE2_LDFLAGS) $(LDFLAGS)

# Test the shared lib, falling back to the static lib for private symbols
.PRECIOUS: obj/so/test/%
obj/so/test/%: obj/so/libre2.$(SOEXT) obj/libre2.a obj/re2/testing/%.o $(TESTOFILES) obj/re2/testing/util/test.o
	@mkdir -p obj/so/test
	$(CXX) -o $@ obj/re2/testing/$*.o $(TESTOFILES) obj/re2/testing/util/test.o -Lobj/so -lre2 obj/libre2.a target/release/libcapi.a $(RE2_LDFLAGS) $(LDFLAGS)

# Filter out dump.o because testing::TempDir() isn't available for it.
obj/test/regexp_benchmark: obj/libre2.a obj/re2/testing/regexp_benchmark.o $(TESTOFILES) obj/re2/testing/util/benchmark.o
	@mkdir -p obj/test
	$(CXX) -o $@ obj/re2/testing/regexp_benchmark.o $(filter-out obj/re2/testing/dump.o, $(TESTOFILES)) obj/re2/testing/util/benchmark.o obj/libre2.a target/release/libcapi.a $(RE2_LDFLAGS) $(LDFLAGS)

ifdef REBUILD_TABLES
.PRECIOUS: re2/perl_groups.cc
re2/perl_groups.cc: re2/make_perl_groups.pl
	perl $< > $@

.PRECIOUS: re2/unicode_%.cc
re2/unicode_%.cc: re2/make_unicode_%.py re2/unicode.py
	python3 $< > $@
endif

.PHONY: distclean
distclean: clean
	rm -f re2/perl_groups.cc re2/unicode_casefold.cc re2/unicode_groups.cc

.PHONY: clean
clean:
	rm -rf obj target
	rm -f re2/*.pyc testinstall

.PHONY: testofiles
testofiles: $(TESTOFILES)

.PHONY: test
test: $(DTESTS) $(TESTS) $(STESTS) debug-test static-test shared-test

.PHONY: debug-test
debug-test: $(DTESTS)
	@./runtests $(DTESTS)

.PHONY: static-test
static-test: $(TESTS)
	@./runtests $(TESTS)

.PHONY: shared-test
shared-test: $(STESTS)
	@./runtests -shared-library-path obj/so $(STESTS)

.PHONY: debug-bigtest
debug-bigtest: $(DTESTS) $(DBIGTESTS)
	@./runtests $(DTESTS) $(DBIGTESTS)

.PHONY: static-bigtest
static-bigtest: $(TESTS) $(BIGTESTS)
	@./runtests $(TESTS) $(BIGTESTS)

.PHONY: shared-bigtest
shared-bigtest: $(STESTS) $(SBIGTESTS)
	@./runtests -shared-library-path obj/so $(STESTS) $(SBIGTESTS)

.PHONY: benchmark
benchmark: obj/test/regexp_benchmark

.PHONY: install
install: static-install shared-install

.PHONY: static
static: obj/libre2.a

.PHONY: static-install
static-install: obj/libre2.a common-install 
	$(INSTALL) target/release/libcapi.a $(DESTDIR)$(libdir)/libcapi.a
	$(INSTALL) obj/libre2.a $(DESTDIR)$(libdir)/libre2.a
	

.PHONY: shared
shared: libcapi.a obj/so/libre2.$(SOEXT)

.PHONY: shared-install
shared-install: obj/so/libre2.$(SOEXT) common-install
	$(INSTALL) obj/so/libre2.$(SOEXT) $(DESTDIR)$(libdir)/libre2.$(SOEXTVER00)
	ln -sf libre2.$(SOEXTVER00) $(DESTDIR)$(libdir)/libre2.$(SOEXTVER)
	ln -sf libre2.$(SOEXTVER00) $(DESTDIR)$(libdir)/libre2.$(SOEXT)
	@ldconfig

.PHONY: common-install
common-install:
	@mkdir -p $(DESTDIR)$(includedir)/re2 # $(DESTDIR)$(libdir)/pkgconfig
	$(INSTALL_DATA) $(INSTALL_HFILES) $(DESTDIR)$(includedir)/re2

.PHONY: testinstall
testinstall: static-testinstall shared-testinstall
	@echo
	@echo Install tests passed.
	@echo

.PHONY: static-testinstall
static-testinstall: CXXFLAGS:=-std=c++11 -pthread -I$(DESTDIR)$(includedir) $(CXXFLAGS)
static-testinstall: LDFLAGS:=-pthread -L$(DESTDIR)$(libdir) -l:libre2.a $(LDICU) $(LDFLAGS)
static-testinstall:
	@mkdir -p obj
	@cp testinstall.cc obj
ifeq ($(shell uname),Darwin)
	@echo Skipping test for libre2.a on Darwin.
else ifeq ($(shell uname),SunOS)
	@echo Skipping test for libre2.a on SunOS.
else
	(cd obj && $(CXX) testinstall.cc -o testinstall $(CXXFLAGS) $(LDFLAGS))
	obj/testinstall
endif

.PHONY: shared-testinstall
shared-testinstall: CXXFLAGS:=-std=c++11 -pthread -I$(DESTDIR)$(includedir) $(CXXFLAGS)
shared-testinstall: LDFLAGS:=-pthread -L$(DESTDIR)$(libdir) -lre2 $(LDICU) 
shared-testinstall:
	@mkdir -p obj
	@cp testinstall.cc obj
	(cd obj && $(CXX) testinstall.cc -o testinstall $(CXXFLAGS) $(LDFLAGS))
ifeq ($(shell uname),Darwin)
	DYLD_LIBRARY_PATH="$(DESTDIR)$(libdir):$(DYLD_LIBRARY_PATH)" obj/testinstall
else
	LD_LIBRARY_PATH="$(DESTDIR)$(libdir):$(LD_LIBRARY_PATH)" obj/testinstall
endif

.PHONY: benchlog
benchlog: obj/test/regexp_benchmark
	(echo '==BENCHMARK==' `hostname` `date`; \
	  (uname -a; $(CXX) --version; git rev-parse --short HEAD; file obj/test/regexp_benchmark) | sed 's/^/# /'; \
	  echo; \
	  ./obj/test/regexp_benchmark 'PCRE|RE2') | tee -a benchlog.$$(hostname | sed 's/\..*//')

.PHONY: log
log:
	$(MAKE) clean
	$(MAKE) CXXFLAGS="$(CXXFLAGS) -DLOGGING=1" \
		$(filter obj/test/exhaustive%_test,$(BIGTESTS))
	echo '#' RE2 exhaustive tests built by make log >re2-exhaustive.txt
	echo '#' $$(date) >>re2-exhaustive.txt
	obj/test/exhaustive_test |grep -v '^PASS$$' >>re2-exhaustive.txt
	obj/test/exhaustive1_test |grep -v '^PASS$$' >>re2-exhaustive.txt
	obj/test/exhaustive2_test |grep -v '^PASS$$' >>re2-exhaustive.txt
	obj/test/exhaustive3_test |grep -v '^PASS$$' >>re2-exhaustive.txt

	$(MAKE) CXXFLAGS="$(CXXFLAGS) -DLOGGING=1" obj/test/search_test
	echo '#' RE2 basic search tests built by make $@ >re2-search.txt
	echo '#' $$(date) >>re2-search.txt
	obj/test/search_test |grep -v '^PASS$$' >>re2-search.txt

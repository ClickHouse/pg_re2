EXTENSION    = $(shell grep -m 1 '"name":' META.json | \
               sed -e 's/[[:space:]]*"name":[[:space:]]*"\([^"]*\)",/\1/')
EXTVERSION   = $(shell grep -m 1 'default_version' re2.control | \
               sed -e "s/[[:space:]]*default_version[[:space:]]*=[[:space:]]*'\([^']*\)',\{0,1\}/\1/")
DISTVERSION  = $(shell grep -m 1 '^[[:space:]]\{2\}"version":' META.json | \
               sed -e 's/[[:space:]]*"version":[[:space:]]*"\([^"]*\)",\{0,1\}/\1/')

DATA         = sql/$(EXTENSION)--$(EXTVERSION).sql
MODULE_big   = $(EXTENSION)
OBJS         = src/pg_re2.o src/re2_cache.o src/re2_wrapper.o

PG_CONFIG   ?= pg_config
PG_CXXFLAGS  = -std=c++17
SHLIB_LINK   = -lre2 -lstdc++

TESTS        ?= $(wildcard test/sql/*.sql)
REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test --load-extension=$(EXTENSION)
EXTRA_CLEAN = src/version.h $(EXTENSION)-$(DISTVERSION).zip

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)


# Require the version header.
$(OBJS): src/version.h

# Versioned source file.
src/version.h: src/version.h.in
	sed -e 's,__VERSION__,$(DISTVERSION),g' $< > $@

.PHONY: format
format: src/*.c src/*.h src/*.cpp
	clang-format -i $^

.PHONY: lint
lint: src/*.c src/*.h src/*.cpp
	clang-format --dry-run --Werror $^

.PHONY: lint22
lint22: src/*.c src/*.h src/*.cpp
	clang-format-22 --dry-run --Werror $^

# Build a PGXN distribution bundle.
dist: $(EXTENSION)-$(DISTVERSION).zip

$(EXTENSION)-$(DISTVERSION).zip:
	git archive-all -v --prefix "$(EXTENSION)-$(DISTVERSION)/" --force-submodules $(EXTENSION)-$(DISTVERSION).zip

# Test the PGXN distribution.
dist-test: $(EXTENSION)-$(DISTVERSION).zip
	unzip $(EXTENSION)-$(DISTVERSION).zip
	cd $(EXTENSION)-$(DISTVERSION)
	make && make install && make installcheck

.PHONY: apt-install-tools
apt-install-tools:
	curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg
	echo 'deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/noble/ llvm-toolchain-noble-22 main' | sudo tee /etc/apt/sources.list.d/llvm-22.list
	apt-get update
	apt-get install -y --no-install-recommends clang-format-22

.PHONY: release-notes # Show release notes for current version (must have `mknotes` in PATH).
release-notes: CHANGELOG.md
	mknotes -v v$(DISTVERSION) -f $< -r https://github.com/$(or $(GITHUB_REPOSITORY),ClickHouse/pg_re2)

EXTENSION    = re2
EXTVERSION   = 0.1

DATA         = sql/$(EXTENSION)--$(EXTVERSION).sql
MODULE_big   = $(EXTENSION)
OBJS         = src/pg_re2.o src/re2_cache.o src/re2_wrapper.o

PG_CONFIG   ?= pg_config
PG_CXXFLAGS  = -std=c++17
SHLIB_LINK   = -lre2 -lstdc++

TESTS        ?= $(wildcard test/sql/*.sql)
REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test --load-extension=$(EXTENSION)

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

.PHONY: format
format: src/*.c src/*.h src/*.cpp
	clang-format -i $^

.PHONY: lint
lint: src/*.c src/*.h src/*.cpp
	clang-format --dry-run --Werror $^

.PHONY: lint22
lint22: src/*.c src/*.h src/*.cpp
	clang-format-22 --dry-run --Werror $^

.PHONY: apt-install-tools
apt-install-tools:
	curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg
	echo 'deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/noble/ llvm-toolchain-noble-22 main' | sudo tee /etc/apt/sources.list.d/llvm-22.list
	apt-get update
	apt-get install -y --no-install-recommends clang-format-22

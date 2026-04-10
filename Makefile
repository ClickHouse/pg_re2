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
format:
	clang-format -i src/*.c src/*.h src/*.cpp

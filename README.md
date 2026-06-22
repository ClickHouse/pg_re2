[![PGXN version](https://badge.fury.io/pg/re2.svg)](https://badge.fury.io/pg/re2)
[![Build Status](https://github.com/ClickHouse/pg_re2/actions/workflows/ci.yml/badge.svg)](https://github.com/ClickHouse/pg_re2/actions/workflows/ci.yml)

`re2` provides [ClickHouse] [regular expression functions] with [re2]

``` psql
try=# create extension re2;
CREATE EXTENSION

try=# SELECT re2match('hello world', 'h.*o');
 re2match 
----------
 t
(1 row)
```

[![Benchmark](benchmark/graph.png)](benchmark/README.md)

Building
--------

Install [re2], then:

``` sh
make
make installcheck
make install
```

If you encounter an error such as:

```
"Makefile", line 8: Need an operator
```

You need to use GNU make, which may well be installed on your system as
`gmake`:

``` sh
gmake
gmake install
gmake installcheck
```

If you encounter an error such as:

```
make: pg_config: Command not found
```

Be sure that you have `pg_config` installed and in your path. If you used a
package management system such as RPM to install PostgreSQL, be sure that the
`-devel` package is also installed. If necessary tell the build process where
to find it:

``` sh
env PG_CONFIG=/path/to/pg_config make && make installcheck && make install
```

If you encounter an error such as:

```
src/re2_wrapper.cpp:14:10: fatal error: 're2/re2.h' file not found
```

You either need to install [re2] or tell the compiler where to find it.
If, for example, you installed the [Homebrew re2], try:

``` sh
make CPPFLAGS=-I/opt/homebrew/include \
     CFLAGS=-I/opt/homebrew/include \
     LDFLAGS=-L/opt/homebrew/lib
```

If you encounter an error such as:

```
ERROR:  must be owner of database regression
```

You need to run the test suite using a super user, such as the default
"postgres" super user:

``` sh
make installcheck PGUSER=postgres
```

To install the extension in a custom prefix on PostgreSQL 18 or later, pass
the `prefix` argument to `install` (but no other `make` targets):

```sh
make install prefix=/usr/local/extras
```

Then ensure that the prefix is included in the following [`postgresql.conf`
parameters]:

```ini
extension_control_path = '/usr/local/extras/postgresql/share:$system'
dynamic_library_path   = '/usr/local/extras/postgresql/lib:$libdir'
```

Usage
-----

Once re2 is installed, you can add it to a database by connecting to a
database as a super user and running:

``` sql
CREATE EXTENSION re2;
```

To install into a specific schema, use the `SCHEMA` clause:

``` sql
CREATE SCHEMA env;
CREATE EXTENSION re2 SCHEMA env;
```

Dependencies
------------

`re2` extension requires PostgreSQL 13 or higher, & [re2].

  [ClickHouse]: https://clickhouse.com/clickhouse "ClickHouse: The fastest open-source analytical database"
  [re2]: https://github.com/google/re2 "RE2, a regular expression library"
  [regular expression functions]: https://clickhouse.com/docs/sql-reference/functions/string-search-functions
    "ClickHouse Docs: Functions for Searching in Strings"
  [Homebrew re2]: https://formulae.brew.sh/formula/re2
  [`postgresql.conf` parameters]: https://www.postgresql.org/docs/devel/runtime-config-client.html#RUNTIME-CONFIG-CLIENT-OTHER

re2 0.4.0
=========

## Synopsis

```sql
CREATE EXTENSION re2;
```

## Description

This library contains a single PostgreSQL extension, `re2`, which provides
[ClickHouse]-compatible [regular expression functions] powered by [re2]. It
supports PostgreSQL 13 and higher.

It also provides the `@~` operator for RE2 text matches that can use GIN
indexes.

To align with ClickHouse regular expression function behavior, this extension
provides functions in which:

*   Unlike re2's default behavior, `.` matches line breaks. To disable this,
    prepend the pattern with `(?-s)`.

*   Patterns may be passed as either `TEXT` or `BYTEA` values, the latter of
    which supports `\0` bytes.

## Functions

### `re2match` ###

Checks if a provided string matches the provided regular expression pattern.

**Syntax**

```sql
SELECT re2match( :haystack, :pattern );
```

**Parameters**

`:haystack`
: String in which the search is performed. `TEXT` or `BYTEA`

`:pattern`
: Regular expression pattern. `TEXT`

**Returns `BOOL`**

Returns `true` if the pattern matches, `false` otherwise.

**ClickHouse equivalent: [match](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#match)**

### `re2extract()` ###

Extracts the first match of a regular expression in a string. Returns an empty
string if `:haystack` doesn't match `:pattern`.

**Syntax**

```sql
SELECT re2extract( :haystack, :pattern );
```

**Parameters**

`:haystack`
: String in which the search is performed. `TEXT` or `BYTEA`

`:pattern`
: Regular expression, typically containing a capturing group. `TEXT`

**Returns `TEXT`**

Returns extracted fragment as a string.

**ClickHouse equivalent: [extract](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#extract)**

### `re2extractall()` ###

Like [`re2extract`](#re2extract), but returns an array of all matches of a
regular expression in a string. Returns an empty array if `:haystack:` doesn't
match the `:pattern`.

**Syntax**

```sql
SELECT re2extractall( :haystack, :pattern );
```

**Parameters**

`:haystack`
: String from which to extract fragments. `TEXT` or `BYTEA`

`:pattern`
: Regular expression, optionally containing capturing groups. `TEXT`

**Returns `TEXT[]`**

Returns array of extracted fragments.

**ClickHouse equivalent: [extractAll](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#extractAll)**

### `re2regexpextract()` ###

Extracts the first string in `:haystack` that matches the regexp `:pattern`
and corresponds to the regex group index.

**Syntax**

```sql
SELECT re2regexpextract( :haystack, :pattern, :index DEFAULT 1 );
```

**Parameters**

`:haystack`
: String in which regexp pattern will be matched. `TEXT` or `BYTEA`

`:pattern`
: Regular expression. Pattern may contain multiple regexp groups, `:index`
  indicates which group to extract. An index of `0` means matching the entire
  regular expression. `TEXT`

`:index`
: Optional. An integer greater or equal `0` with default `1`. It represents
  which regex group to extract. `INT`

**Returns `TEXT`**

Returns a string match.

**ClickHouse equivalent: [regexpExtract](https://clickhouse.com/docs/sql-reference/functions/string-functions#regexpExtract)**

### `re2extractgroups()` ###

Extracts all groups from non-overlapping substrings matched by a regular
expression.

**Syntax**

```sql
SELECT re2extractgroups( :hastack, :pattern );
```

**Parameters**

`:haystack`
: Input string to extract from. `TEXT` or `BYTEA`

`:pattern`
: Regular expression. `TEXT`

**Returns `text[][]`**

If the function finds at least one matching group, it returns
`ARRAY[ARRAY[TEXT]]` column, clustered by group_id (`1` to `N`, where `N` is
number of capturing groups in regexp). If there is no matching group, it
returns an empty array.

**ClickHouse equivalent: [extractGroups](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#extractGroups)**

### `re2extractallgroupsvertical()` ###

Matches all non-overlapping occurrences of `:pattern` and returns a 2D array
where each inner array contains the capturing groups for one match.

**Syntax**

```sql
SELECT re2extractallgroupsvertical( :haystack, :pattern );
```

**Parameters**

`:haystack`
: Input string to extract from. `TEXT` or `BYTEA`

`:pattern`
: Regular expression with at least one capturing group. `TEXT`

**Returns `text[][]` or `bytea[][]`**

Two-dimensional array of capturing groups, one row per match. If no matches
are found, returns an empty array.

**ClickHouse equivalent: [extractAllGroupsVertical](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#extractAllGroupsVertical)**

### `re2extractallgroupshorizontal()` ###

Matches all non-overlapping occurrences of `:pattern` and returns a 2D array
where each inner array contains all matches for one capturing group.

**Syntax**

```sql
SELECT re2extractallgroupshorizontal( :haystack, :pattern );
```

**Parameters**

`:haystack`
: Input string to extract from. `TEXT` or `BYTEA`

`:pattern`
: Regular expression with at least one capturing group. `TEXT`

**Returns `text[][]` or `bytea[][]`**

Two-dimensional array of matches, one row per capturing group. If no matches
are found, returns an empty array (ClickHouse returns an array of empty
arrays, one per group; PostgreSQL cannot represent that shape, so empty
collapses to a flat empty array).

**ClickHouse equivalent: [extractAllGroupsHorizontal](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#extractAllGroupsHorizontal)**

### `re2regexpquotemeta()` ###

Escapes regex metacharacters with a backslash. Escaped characters: `\0`, `\\`,
`|`, `(`, `)`, `^`, `$`, `.`, `[`, `]`, `?`, `*`, `+`, `{`, `:`, `-`.

**Syntax**

```sql
SELECT re2regexpquotemeta( :input );
```

**Parameters**

`:input`
: String to escape. `TEXT` or `BYTEA`

**Returns `TEXT` or `BYTEA`** matching input type.

**ClickHouse equivalent: [regexpQuoteMeta](https://clickhouse.com/docs/sql-reference/functions/string-functions#regexpquotemeta)**

### `re2splitbyregexp()` ###

Splits `:haystack` into substrings using `:pattern` as a separator. If
`:pattern` is empty, the haystack is split into individual characters. If
`:max_substrings > 0`, returns at most that many substrings (extras are
dropped).

**Syntax**

```sql
SELECT re2splitbyregexp( :pattern, :haystack, :max_substrings DEFAULT 0 );
```

**Parameters**

`:pattern`
: Regular expression separator. `TEXT`

`:haystack`
: Input string to split. `TEXT` or `BYTEA`

`:max_substrings`
: Optional cap on the number of returned substrings. `0` means unlimited.
  `INTEGER`

**Returns `text[]` or `bytea[]`** matching haystack type. Zero-length matches
are treated as no-match (matching ClickHouse behavior).

**ClickHouse equivalent: [splitByRegexp](https://clickhouse.com/docs/sql-reference/functions/splitting-merging-functions#splitByRegexp)**

### `re2replaceregexpone()` ###

Replaces the first occurrence of the substring matching the regular expression
`:pattern` (in re2 syntax) in `:haystack` by the `:replacement` string.
`:replacement` can contain substitutions `\0-\9`. Substitutions `\1-\9`
correspond to the 1st to 9th capturing group (submatch), substitution `\0`
corresponds to the entire match. To use a verbatim `\` character in the
pattern or replacement strings, escape it using `\`. Also keep in mind that
string literals require extra escaping.

**Syntax**

```sql
SELECT re2replaceregexpone( :hastack, :pattern, :replacement );
```

**Parameters**

`:haystack`
: Input string to search. `TEXT` or `BYTEA`

`:pattern`
: The regular expression pattern to find. `TEXT`

`:replacement`
: The string to replace the pattern with, may contain substitutions. `TEXT`

**Returns `TEXT`**

Returns a string with the first regex match replaced.

**ClickHouse equivalent: [replaceRegexpOne](https://clickhouse.com/docs/sql-reference/functions/string-replace-functions#replaceRegexpOne)**

### `re2replaceregexpall()` ###

Like [`re2replaceregexpone`](#re2replaceregexpone) but replaces all
occurrences of the pattern. As an exception, if a regular expression worked on
an empty substring, the replacement is not made more than once.

**Syntax**

```sql
SELECT re2replaceregexpall( :hastack, :pattern, :replacement );
```

**Parameters**

`:haystack`
: Input string to search. `TEXT` or `BYTEA`

`:pattern`
: The regular expression pattern to find. `TEXT`

`:replacement`
: The string to replace the pattern with, may contain substitutions. `TEXT`

**Returns `TEXT`**

Returns a string with all regex matches replaced.

**ClickHouse equivalent: [replaceRegexpAll](https://clickhouse.com/docs/sql-reference/functions/string-replace-functions#replaceRegexpAll)**

### `re2countmatches()` ###

Returns number of matches of a regular expression in a string.

```sql
SELECT re2countmatches( :hastack, :pattern );
```

**Parameters**

`:haystack`
: The string to search. `TEXT` or `BYTEA`

`:pattern`
: Regular expression pattern. `TEXT`

**Returns `INT`**

Returns the number of matches found.

**ClickHouse equivalent: [countMatches](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#countMatches)**

### `re2countmatchescaseinsensitive()` ###

Like [`re2countmatches`](#re2countmatches) but performs case-insensitive matching.

**Syntax**

```sql
SELECT re2countmatchescaseinsensitive( :hastack, :pattern );
```

**Parameters**

`:haystack`
: The string to search. `TEXT` or `BYTEA`

`:pattern`
: Regular expression pattern. `TEXT`

**Returns `INT`**

Returns the number of matches found.

**ClickHouse equivalent: [countMatchesCaseInsensitive](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#countMatchesCaseInsensitive)**

### `re2multimatchany()` ###

Check if at least one of multiple regular expression patterns matches a
haystack. Pass one or more patterns or an array of patterns prepended with
`VARIADIC`.

If you only want to search multiple substrings in a string, you can use
function [`re2multisearchany`](#re2multisearchany), which it works much faster
than this function.

**Syntax**

```sql
SELECT re2multimatchany( :haystack, :pattern1, :pattern2, ... );
SELECT re2multimatchany( :haystack, VARIADIC ARRAY[:pattern1, :pattern2, ...] );
```

**Parameters**

`:haystack`
: String in which patterns are searched. `TEXT` or `BYTEA`

`:pattern`
: A list or variadic array of one or more regular expression patterns. `TEXT`

**Returns `BOOL`**

Returns `true` if the pattern matches, `false` otherwise.

**ClickHouse equivalent: [multiMatchAny](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#multiMatchAny)**

### `re2multimatchanyindex()` ###

Like [`re2multimatchany`](#re2multimatchany) but returns any index that
matches the haystack. Pass one or more patterns or an array of patterns
prepended with `VARIADIC`.

```sql
SELECT re2multimatchanyindex( :haystack, :pattern1, :pattern2, ... );
SELECT re2multimatchanyindex( :haystack, VARIADIC ARRAY[:pattern1, :pattern2, ...] );
```

**Parameters**

`:haystack`
: String in which patterns are searched. `TEXT`

`:pattern`
: A list or variadic array of one or more regular expression patterns. `TEXT`

**Returns `INT`**

Returns the index (starting from 1) of the first pattern that matches, or 0 if
no match is found.

**ClickHouse equivalent: [multiMatchAnyIndex](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#multiMatchAnyIndex)**

### `re2multimatchallindices()` ###

Like multiMatchAny but returns the array of all indices that match the
haystack in any order. Pass one or more patterns or an array of patterns
prepended with `VARIADIC`.

**Syntax**

```sql
SELECT re2multimatchallindices( :haystack, :pattern1, :pattern2, ... );
SELECT re2multimatchallindices( :haystack, VARIADIC ARRAY[:pattern1, :pattern2, ...] );
```

**Parameters**

`:haystack`
: String in which patterns are searched. `TEXT`

`:pattern`
: A list or variadic array of one or more regular expression patterns. `TEXT`

**Returns `INT[]`**

Array of all indices (starting from 1) that match the haystack in any order.
Returns an empty array if no matches are found.

**ClickHouse equivalent: [multiMatchAllIndices](https://clickhouse.com/docs/sql-reference/functions/string-search-functions#multiMatchAllIndices)**

### `re2_version()` ###

Returns the semantic version of the re2 extension library.

**Syntax**

```sql
SELECT re2_version();
```

**Returns `TEXT`**

The re2 library semantic version string.

## Operators

### `@~` ###

Checks whether the left `TEXT` value matches the right `TEXT` RE2 pattern.

**Syntax**

```sql
SELECT :haystack @~ :pattern;
```

**Parameters**

`:haystack`
: String in which the search is performed. `TEXT`

`:pattern`
: Regular expression pattern. `TEXT`

**Returns `BOOL`**

Returns `true` if the pattern matches, `false` otherwise. For `TEXT` inputs,
`haystack @~ pattern` is equivalent to `re2match(haystack, pattern)`.

`@~` is GIN-indexable through the `gin_re2_ops` operator class:

```sql
CREATE INDEX ON logs USING gin (msg gin_re2_ops);
SELECT * FROM logs WHERE msg @~ 'connection (refused|timed out)';
```

`@~` and `re2match(text, text)` match identically, produce the same row
estimates, and both use the b-tree ranges [described
below](#b-tree-range-from-an-anchored-prefix). Only `@~` can use a GIN index,
since Postgres ties GIN operator classes to operators, not functions.

## Index Support

Without an index, every RE2 filter reads whole table. Two mechanisms below let
applicable patterns visit only rows that could match, then recheck the exact
match on each candidate: results never change, only how quickly they arrive,
with one known GIN exception noted below.

Matching, indexed or not, compares raw UTF-8 bytes without Unicode
normalization: a pattern with a composed character (`é` as one code point)
will not find a haystack holding its decomposed form (`e` plus combining
accent). Pattern and haystack must agree on normalization form.

### B-tree range from an anchored prefix

For a constant pattern starting with `^` and a fixed prefix, `re2match` and
`@~` read only the index range covering that prefix. This feature requires no
query or index change, provided the index orders bytewise, such as a
`text_pattern_ops` index or a default b-tree whose collation sorts like `C`
(`C`, `POSIX`, builtin `C` and `C.UTF8`).

```sql
CREATE INDEX ON logs (msg text_pattern_ops);
-- reads index range [request_, request`) then rechecks each row
SELECT * FROM logs WHERE re2match(msg, '^request_id=\d+');
```

The prefix survives grouping: `^us(?:er|ed)_` scans the range `[use, usf)`.
Patterns that are unanchored, alternate at top level (`^a|^b`), branch to
different first characters (`^(a|b)`), or set inline flags (`(?i)`) fall back
to the plan the query would otherwise get.

### GIN trigram prefilter (`gin_re2_ops`)

`gin_re2_ops` indexes three-character sequences (trigrams) of each value.
A `@~` query reduces its pattern to literal text every match needs.

This example finds `connection ` plus `refused` or `timed out` via the
`gin_re2_ops` GIN index and visits only rows containing necessary trigrams,
rechecking the full pattern on each:

```sql
CREATE INDEX ON logs USING gin (msg gin_re2_ops);
SELECT * FROM logs WHERE msg @~ 'connection (refused|timed out)';
```

False negative: RE2 case-insensitive matching uses Unicode simple folding,
which relates ASCII `s` and `k` to `ſ` (U+017F) and `K` (U+212A), while index
trigrams fold ASCII only. An all-ASCII `(?i)` pattern such as `(?i)secret`
matches `ſecret` on a full scan, but the index skips that row.

## Versioning Policy

The re2 extension adheres to [Semantic Versioning] for its public releases.

*   The major version increments for API changes
*   The minor version increments for backward compatible SQL changes
*   The patch version increments for binary-only changes

Once installed, PostgreSQL tracks two variations of the version:

*   The library version (defined by `PG_MODULE_MAGIC` on PostgreSQL 18 and
    higher) includes the full semantic version, visible in the output of
    [`re2_version()`](#re2_version) or the Postgres
    [`pg_get_loaded_modules()`] function.
*   The extension version (defined in the control file) includes only the
    major and minor versions, visible in the `pg_catalog.pg_extension` table,
    the output of the `pg_available_extension_versions()` function, and
    `\dx re2`.

In practice this means that a release that increments the patch version, e.g.
from `v0.1.0` to `v0.1.1`, benefits all databases that have loaded `v0.1` and
do not need to run `ALTER EXTENSION` to benefit from the upgrade.

A release that increments the minor or major versions, on the other hand, will
be accompanied by SQL upgrade scripts, and all existing database that contain
the extension must run `ALTER EXTENSION re2 UPDATE` to benefit from the
upgrade.

## Authors

*   [Philip Dubé](https://serprex.github.io/)
*   [David E. Wheeler](https://justatheory.com/)

## Copyright

Copyright (c) 2026, ClickHouse.

  [ClickHouse]: https://clickhouse.com/clickhouse "ClickHouse: The fastest open-source analytical database"
  [re2]: https://github.com/google/re2 "RE2, a regular expression library"
  [regular expression functions]: https://clickhouse.com/docs/sql-reference/functions/string-search-functions
    "ClickHouse Docs: Functions for Searching in Strings"
  [Semantic Versioning]: https://semver.org/spec/v2.0.0.html
    "Semantic Versioning 2.0.0"

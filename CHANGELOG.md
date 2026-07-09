# Changelog

All notable changes to this project will be documented in this file. It uses the
[Keep a Changelog] format, and this project adheres to [Semantic Versioning].

  [Keep a Changelog]: https://keepachangelog.com/en/1.1.0/
  [Semantic Versioning]: https://semver.org/spec/v2.0.0.html
    "Semantic Versioning 2.0.0"

## [v0.4.1] — Unreleased

### 🐛 Fixes

*   Fix use-after-free caused by reusing re2 pattern after cache invalidation.

## [v0.4.0] — 2026-07-03

### ⚡ Improvements

*   Added `re2_version()`, which returns the full semantic version. This is
    the same value visible in `pg_get_loaded_modules()`, but available in
    Postgres versions prior to 18, and without having to load re2 first.
*   Added Index support, avoiding table scans for patterns that can use an
    index:
    *   A pattern starting with `^` and a fixed prefix, such as `WHERE
        re2match(col, '^order_2025')`, now reads the matching range of an
        existing b-tree index. See [Index Support](doc/re2.md#index-support).
    *   Added `@~` match operator (same as `re2match`) and `gin_re2_ops`
        operator class. `CREATE INDEX ... USING gin (col gin_re2_ops)` lets
        `WHERE col @~ pattern` only visit rows containing required trigrams.
    *   Row-count estimates for `re2match` and `@~` now come from testing
        patterns against column statistics for patterns containing a constant
        pattern. In such cases this improves cost estimation over the default
        fixed guess, allowing the planner to pick better plans.

### 📔 Notes

*   Run `ALTER EXTENSION re2 UPDATE TO '0.4'` to expose the new operator and
    operator class on existing databases.

  [v0.4.0]: https://github.com/clickhouse/pg_re2/compare/v0.3.0...v0.4.0

## [v0.3.0] — 2026-05-26

### 🐛 Fixes

*   `re2splitbyregexp` argument order corrected to `(pattern, haystack[,
    max_substrings])` matching ClickHouse's `splitByRegexp`. Previously
    released as `(haystack, pattern)` in 0.2.0.

### 📔 Notes

*   Run `ALTER EXTENSION re2 UPDATE TO '0.3'` to apply the fix on existing
    databases.

  [v0.3.0]: https://github.com/clickhouse/pg_re2/compare/v0.2.0...v0.3.0

## [v0.2.0] — 2026-05-26

### ⚡ Improvements

*   Added `re2extractallgroupshorizontal`, `re2extractallgroupsvertical`,
    `re2regexpquotemeta`, and `re2splitbyregexp` (CH-compatible). Each gets a
    `bytea` overload alongside `text`.
*   Fixed memory safety in C++ wrapper.

### 📔 Notes

*   Run `ALTER EXTENSION re2 UPDATE TO '0.2'` to expose the new functions on
    existing databases.

  [v0.2.0]: https://github.com/clickhouse/pg_re2/compare/v0.1.1...v0.2.0

## [v0.1.1] — 2026-04-16

This release makes binary-only changes. Once installed, any existing use of
re2 v0.1 will get its benefits on reload without needing to
`ALTER EXTENSION UPDATE`.

### ⚡ Improvements

*   Added support for Postgres versions 13+ (previously 16+).

### 📚 Documentation

*   Documented the use of the variadic list of pattern arguments in the
    `re2multimatch*` functions and how an array can be passed via the
    `VARIADIC` keyword to mimic the array syntax for the corresponding
    ClickHouse functions.

  [v0.1.1]: https://github.com/clickhouse/pg_re2/compare/v0.1.0...v0.1.1

## [v0.1.0] — 2026-04-15

### ⚡ Improvements

*   First release, everything is new!

### 🏗️ Build Setup

*   PGXS build pipeline
*   PGXN and GitHub release workflows

### ⬆️ Dependencies

*   Requires the [re2] library

### 📚 Documentation

*   Reference documentation in [doc/re2.md](doc/re2.md)

### 📔 Notes

*   Set full version in `PG_MODULE_MAGIC_EXT`

  [v0.1.0]: https://github.com/clickhouse/pg_re2/compare/5b8df6a...v0.1.0
  [re2]: https://github.com/google/re2 "RE2, a regular expression library"

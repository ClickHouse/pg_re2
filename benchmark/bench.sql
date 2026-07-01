-- Benchmark: re2 extension vs PostgreSQL builtin POSIX regex
-- PG 15+ functions: regexp_like, regexp_count, regexp_substr, regexp_replace
-- Outputs CSV rows: category,test_name,engine,rows,time_ms

\timing off
\pset format unaligned
\pset tuples_only on
\pset fieldsep ','

-- Compare single-thread engine throughput, exclude JIT compile variance
SET jit = off;
SET max_parallel_workers_per_gather = 0;

-- Data built once by setup.sql; this file measures regex only

-- ============================================================
-- 1. MATCH: re2match() vs regexp_like()
-- ============================================================

-- 1a. Simple literal
SELECT 'match', 'simple_literal', 're2',
  (SELECT count(*) FROM bench_data WHERE re2match(email, 'example42')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'match', 'simple_literal', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_like(email, 'example42')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 1b. Character class
SELECT 'match', 'char_class', 're2',
  (SELECT count(*) FROM bench_data WHERE re2match(logline, 'error_code=[0-9]{3}')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'match', 'char_class', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_like(logline, 'error_code=[0-9]{3}')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 1c. Alternation
SELECT 'match', 'alternation', 're2',
  (SELECT count(*) FROM bench_data WHERE re2match(email, '@example(1|22|333|4444)\.')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'match', 'alternation', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_like(email, '@example(1|22|333|4444)\.')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 1d. Nested quantifier on longtext
SELECT 'match', 'nested_quantifier', 're2',
  (SELECT count(*) FROM bench_data WHERE re2match(longtext, '(quick\s+brown\s+){2,}')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'match', 'nested_quantifier', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_like(longtext, '(quick\s+brown\s+){2,}')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 1e. Email validation
SELECT 'match', 'email_validation', 're2',
  (SELECT count(*) FROM bench_data WHERE re2match(email, '^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'match', 'email_validation', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_like(email, '^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 1f. IP address pattern
SELECT 'match', 'ip_address', 're2',
  (SELECT count(*) FROM bench_data WHERE re2match(logline, '\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'match', 'ip_address', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_like(logline, '\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 1g. Classic ReDoS-shaped pattern (e?){10}e{10}: catastrophic for naive
-- backtrackers (PCRE, Python re, Java), but both RE2 (automaton) and PG ARE
-- (hybrid DFA/NFA) stay linear. Measures throughput parity, not ReDoS rescue
SELECT 'match', 'ambiguous_quantifier', 're2',
  (SELECT count(*) FROM bench_data WHERE id <= 100000 AND re2match(email, '(e?){10}e{10}')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'match', 'ambiguous_quantifier', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE id <= 100000 AND regexp_like(email, '(e?){10}e{10}')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 1h. Deeply nested alternation
SELECT 'match', 'deep_alternation', 're2',
  (SELECT count(*) FROM bench_data WHERE re2match(logline, '(((error|warn|info)_code)|((path|route)_id))=\d+')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'match', 'deep_alternation', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_like(logline, '(((error|warn|info)_code)|((path|route)_id))=\d+')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- ============================================================
-- 2. EXTRACT: re2extract() vs regexp_substr()
-- ============================================================

-- 2a. Domain from email
SELECT 'extract', 'domain_from_email', 're2',
  (SELECT count(*) FROM bench_data WHERE re2extract(email, '@([a-z0-9]+)\.') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'extract', 'domain_from_email', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_substr(email, '@([a-z0-9]+)\.') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 2b. Error code from logline
SELECT 'extract', 'error_code', 're2',
  (SELECT count(*) FROM bench_data WHERE re2extract(logline, 'error_code=(\d+)') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'extract', 'error_code', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_substr(logline, 'error_code=(\d+)') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 2c. IP from logline
SELECT 'extract', 'ip_from_log', 're2',
  (SELECT count(*) FROM bench_data WHERE re2extract(logline, '(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'extract', 'ip_from_log', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_substr(logline, '(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- ============================================================
-- 3. EXTRACT ALL: re2extractall() vs regexp_matches(..., 'g')
-- 3a/3b build one array per row (array_agg, fair vs re2extractall)
-- 3c/3d expand matches to rows (regexp_matches set-returning paradigm)
-- ============================================================

-- 3a. All words from longtext (2.5k rows, ~1M matches)
SELECT 'extract_all', 'all_words', 're2',
  (SELECT sum(array_length(re2extractall(longtext, '\w+'), 1)) FROM bench_data WHERE id <= 2500),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'extract_all', 'all_words', 'pg_builtin',
  (SELECT sum(array_length(arr, 1)) FROM bench_data
     CROSS JOIN LATERAL (SELECT array_agg(m[1]) AS arr FROM regexp_matches(longtext, '\w+', 'g') m) s
     WHERE id <= 2500),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 3b. All numbers from logline
SELECT 'extract_all', 'all_numbers', 're2',
  (SELECT sum(array_length(re2extractall(logline, '\d+'), 1)) FROM bench_data),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'extract_all', 'all_numbers', 'pg_builtin',
  (SELECT sum(array_length(arr, 1)) FROM bench_data
     CROSS JOIN LATERAL (SELECT array_agg(m[1]) AS arr FROM regexp_matches(logline, '\d+', 'g') m) s),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 3c. All words via unnest (apples-to-apples with regexp_matches set-returning)
SELECT 'extract_all_unnest', 'all_words', 're2',
  (SELECT count(*) FROM bench_data, unnest(re2extractall(longtext, '\w+')) WHERE id <= 2500),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'extract_all_unnest', 'all_words', 'pg_builtin',
  (SELECT count(*) FROM bench_data, regexp_matches(longtext, '\w+', 'g') WHERE id <= 2500),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 3d. All numbers via unnest
SELECT 'extract_all_unnest', 'all_numbers', 're2',
  (SELECT count(*) FROM bench_data, unnest(re2extractall(logline, '\d+'))),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'extract_all_unnest', 'all_numbers', 'pg_builtin',
  (SELECT count(*) FROM bench_data, regexp_matches(logline, '\d+', 'g')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- ============================================================
-- 4. REPLACE ONE: re2replaceregexpone() vs regexp_replace()
-- ============================================================

SELECT 'replace_one', 'first_digits', 're2',
  (SELECT count(*) FROM bench_data WHERE re2replaceregexpone(logline, '\d+', 'NUM') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'replace_one', 'first_digits', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_replace(logline, '\d+', 'NUM') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- ============================================================
-- 5. REPLACE ALL: re2replaceregexpall() vs regexp_replace(..., 'g')
-- ============================================================

-- 5a. All digits in logline
SELECT 'replace_all', 'all_digits', 're2',
  (SELECT count(*) FROM bench_data WHERE re2replaceregexpall(logline, '\d+', 'NUM') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'replace_all', 'all_digits', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE regexp_replace(logline, '\d+', 'NUM', 'g') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 5b. All whitespace in longtext (2.5k rows)
SELECT 'replace_all', 'all_whitespace', 're2',
  (SELECT count(*) FROM bench_data WHERE id <= 2500 AND re2replaceregexpall(longtext, '\s+', ' ') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'replace_all', 'all_whitespace', 'pg_builtin',
  (SELECT count(*) FROM bench_data WHERE id <= 2500 AND regexp_replace(longtext, '\s+', ' ', 'g') IS NOT NULL),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- ============================================================
-- 6. COUNT MATCHES: re2countmatches() vs regexp_count()
-- ============================================================

-- 6a. Digit sequences in logline
SELECT 'count_matches', 'digit_sequences', 're2',
  (SELECT sum(re2countmatches(logline, '\d+')) FROM bench_data),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'count_matches', 'digit_sequences', 'pg_builtin',
  (SELECT sum(regexp_count(logline, '\d+')) FROM bench_data),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 6b. Words in longtext (2.5k rows, ~1M matches)
SELECT 'count_matches', 'words_longtext', 're2',
  (SELECT sum(re2countmatches(longtext, '\w+')) FROM bench_data WHERE id <= 2500),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'count_matches', 'words_longtext', 'pg_builtin',
  (SELECT sum(regexp_count(longtext, '\w+')) FROM bench_data WHERE id <= 2500),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- ============================================================
-- 7. INDEX SCAN: re2 index support vs postgres index scan
--   idx_btree  re2match(col,'^lit') planner support -> text_pattern_ops range,
--              vs postgres' own '~' prefix support on the same index
--   idx_gin    col @~ pat (gin_re2_ops, RE2 FilteredRE2 atoms)
--              vs col ~ pat (pg_trgm gin_trgm_ops)
-- enable_seqscan off so both engines are measured on their index, not a scan
-- ============================================================
SET enable_seqscan = off;

-- 7a. b-tree literal prefix (~11k rows in range, recheck cost visible)
SELECT 'idx_btree', 'prefix_literal', 're2',
  (SELECT count(*) FROM bench_index_data WHERE re2match(email, '^user5')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'idx_btree', 'prefix_literal', 'pg_builtin',
  (SELECT count(*) FROM bench_index_data WHERE email ~ '^user5'),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 7b. b-tree literal prefix + bounded char-class tail (prefix still extractable)
SELECT 'idx_btree', 'prefix_charclass', 're2',
  (SELECT count(*) FROM bench_index_data WHERE re2match(email, '^user12[0-9]')),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'idx_btree', 'prefix_charclass', 'pg_builtin',
  (SELECT count(*) FROM bench_index_data WHERE email ~ '^user12[0-9]'),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 7c. GIN required literal atom (re2 keeps a tight candidate set; pg_trgm wins
-- here because its per-candidate consistent check is far cheaper)
SELECT 'idx_gin', 'literal', 're2',
  (SELECT count(*) FROM bench_index_data WHERE logline @~ 'error_code=123'),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'idx_gin', 'literal', 'pg_builtin',
  (SELECT count(*) FROM bench_index_data WHERE logline ~ 'error_code=123'),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

-- 7d. GIN alternation (atoms on each branch)
SELECT 'idx_gin', 'alternation', 're2',
  (SELECT count(*) FROM bench_index_data WHERE logline @~ 'error_code=(100|200|300)'),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

SELECT 'idx_gin', 'alternation', 'pg_builtin',
  (SELECT count(*) FROM bench_index_data WHERE logline ~ 'error_code=(100|200|300)'),
  (extract(epoch FROM clock_timestamp() - ts) * 1000)::numeric(12,2)
FROM (SELECT clock_timestamp() AS ts) t;

RESET enable_seqscan;

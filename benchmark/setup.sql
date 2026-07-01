-- Benchmark setup: build data once, outside iteration loop
-- bench.sql measures regex throughput against this table per iteration

CREATE EXTENSION IF NOT EXISTS re2;
ALTER EXTENSION re2 UPDATE; -- index support needs >= 0.4
-- pg_trgm supplies gin_trgm_ops, the postgres side of the GIN index comparison
CREATE EXTENSION IF NOT EXISTS pg_trgm;

DROP TABLE IF EXISTS bench_data;
CREATE TABLE bench_data AS
SELECT
  id,
  -- emails (~40 chars)
  'user' || (id % 10000)::text || '@example' || (id % 100)::text || '.com' AS email,
  -- log lines (~200 chars)
  repeat(chr(65 + (id % 26)), 20) || ' error_code=' || (id % 999)::text
    || ' path=/api/v' || (id % 5)::text || '/users/' || id::text
    || ' ip=192.168.' || (id % 256)::text || '.' || (id % 256)::text
    || ' ' || repeat(chr(97 + (id % 26)), 100) AS logline,
  -- long text (~2000 chars, per-row distinct via id-derived number; 400 words/row)
  repeat('the quick brown fox jumps over the lazy dog ' || lpad(id::text, 6, '0') || ' ', 40) AS longtext
FROM generate_series(1, 10000) AS id;

ANALYZE bench_data;

SELECT count(*) FROM bench_data WHERE email IS NOT NULL;
SELECT count(*) FROM bench_data WHERE logline IS NOT NULL;
SELECT count(*) FROM bench_data WHERE longtext IS NOT NULL;

-- Index-scan comparison table (bench.sql section 7). Larger than bench_data so
-- index descent, not per-row overhead, dominates. Distinct ids keep prefixes
-- and literals selective.
DROP TABLE IF EXISTS bench_index_data;
CREATE TABLE bench_index_data AS
SELECT
  id,
  'user' || id::text || '@example' || (id % 100)::text || '.com' AS email,
  repeat(chr(65 + (id % 26)), 20) || ' error_code=' || (id % 999)::text
    || ' path=/api/v' || (id % 5)::text || '/users/' || id::text
    || ' ip=192.168.' || (id % 256)::text || '.' || (id % 256)::text AS logline
FROM generate_series(1, 100000) AS id;

-- b-tree prefix range needs a bytewise-comparing opclass (text_pattern_ops)
CREATE INDEX bench_index_email_btree ON bench_index_data (email text_pattern_ops);
-- competing GIN trigram indexes on the same column: re2 FilteredRE2 vs pg_trgm
CREATE INDEX bench_index_logline_re2 ON bench_index_data USING gin (logline gin_re2_ops);
CREATE INDEX bench_index_logline_trgm ON bench_index_data USING gin (logline gin_trgm_ops);

ANALYZE bench_index_data;

SELECT count(*) FROM bench_index_data;

-- Benchmark setup: build data once, outside iteration loop
-- bench.sql measures regex throughput against this table per iteration

CREATE EXTENSION IF NOT EXISTS re2;

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

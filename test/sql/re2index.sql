-- Index support (extension 0.4).
-- Assert index paths return identical results to sequential rechecks. Plan
-- shape is asserted only under forced settings (seqscan/bitmapscan off, after
-- VACUUM so index-only scan wins deterministically); the unforced b-tree
-- choice is left to the planner (varies by PG version).

-- ==== Planner support: b-tree range from a fixed prefix ====

CREATE TABLE re2idx_btree (s text);
INSERT INTO re2idx_btree SELECT 'user_' || g FROM generate_series(1, 5000) g;
INSERT INTO re2idx_btree SELECT 'acct_' || g FROM generate_series(1, 5000) g;
CREATE INDEX ON re2idx_btree (s text_pattern_ops);
ANALYZE re2idx_btree;

-- anchored literal prefix: index range vs seqscan agree
SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^user_12');
SET enable_indexscan = off; SET enable_bitmapscan = off;
SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^user_12');
RESET enable_indexscan; RESET enable_bitmapscan;

-- anchored with char-class tail still yields a fixed prefix
SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^acct_9[0-9]');

-- unanchored / top-level alternation are not prefixable: still correct
SELECT count(*) FROM re2idx_btree WHERE re2match(s, 'ser_12');
SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^user_1|^acct_1');

-- EXPLAIN shows which patterns get a range (Index Cond) and which only
-- filter. Alternation inside a group and a POSIX class keep a fixed prefix.
-- '^h[éø]llo': é and ø share their first UTF-8 byte, but the derived prefix
-- stops at the whole character 'h', never mid-character, so the constant
-- stays printable in any client encoding. '^[éü]x' shares no complete
-- character, so no range. A non-capturing group keeps anchoring: er/ed share
-- 'use' hence a range, while user/acct share nothing hence none.
VACUUM ANALYZE re2idx_btree;
SET enable_seqscan = off; SET enable_bitmapscan = off;
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^user_1(2|3)');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^acct_9[[:digit:]]');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^h[éø]llo');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^[éü]x');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^us(?:er|ed)_1');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^(?:user|acct)_1');
-- operator form reaches the same support function through its function
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_btree WHERE s @~ '^user_1(2|3)';
SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^user_1(2|3)');
SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^us(?:er|ed)_1');
SELECT count(*) FROM re2idx_btree WHERE re2match(s, '^(?:user|acct)_1');
RESET enable_seqscan; RESET enable_bitmapscan;

DROP TABLE re2idx_btree;

-- ==== start_anchored soundness: constructs a byte scan can misread ====
-- Patterns a byte scan can misread into a wrong b-tree range that silently
-- drops rows: quantified caret, \Q..\E quoting, POSIX class member, leading
-- ] class member, and a [^] class whose leading ] member takes two skips,
-- folding them into one hides the top-level | behind a phantom class ('xab]'
-- matches yet a bogus [a,b) range skips it). EXPLAIN must show a filter with
-- no Index Cond; forced index counts must match seqscan (2, 5, 5, 2, 5).

CREATE TABLE re2idx_anchor (s text);
INSERT INTO re2idx_anchor VALUES ('abc'),('xabc'),('aa('),('xab'),('a5'),('x(y'),('axz'),('xab]');
CREATE INDEX ON re2idx_anchor (s text_pattern_ops);
VACUUM ANALYZE re2idx_anchor;
SET enable_seqscan = off; SET enable_bitmapscan = off;
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^?abc');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^aa\Q(\E|ab');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^a[[:digit:](]|ab');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^x[](]y|xz');
EXPLAIN (COSTS OFF) SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^a[^](]|ab]');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^?abc');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^aa\Q(\E|ab');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^a[[:digit:](]|ab');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^x[](]y|xz');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^a[^](]|ab]');
SET enable_seqscan = on; SET enable_indexscan = off;
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^?abc');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^aa\Q(\E|ab');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^a[[:digit:](]|ab');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^x[](]y|xz');
SELECT count(*) FROM re2idx_anchor WHERE re2match(s, '^a[^](]|ab]');
RESET enable_seqscan; RESET enable_indexscan; RESET enable_bitmapscan;

DROP TABLE re2idx_anchor;

-- ==== Selectivity: bare re2match() estimates from column stats ====
-- Support function answers SupportRequestSelectivity through the @~ operator:
-- a constant pattern is run over the column's most-common-value statistics,
-- replacing the flat 0.3333 boolean-function default (3333 rows here). Two
-- distinct values on a fully-sampled table make stored fractions exact, so
-- estimates are stable.

CREATE FUNCTION re2idx_est(q text) RETURNS bigint LANGUAGE plpgsql AS $$
DECLARE j json;
BEGIN
	EXECUTE 'EXPLAIN (FORMAT JSON) ' || q INTO j;
	RETURN (j->0->'Plan'->>'Plan Rows')::bigint;
END $$;

CREATE TABLE re2idx_sel (s text);
INSERT INTO re2idx_sel SELECT CASE WHEN g % 100 = 0 THEN 'needle' ELSE 'haystack' END
FROM generate_series(1, 10000) g;
ANALYZE re2idx_sel;

-- statistics-driven: 1% of rows match
SELECT re2idx_est($$SELECT * FROM re2idx_sel WHERE re2match(s, 'needle')$$);
-- matches every sampled value: full row count
SELECT re2idx_est($$SELECT * FROM re2idx_sel WHERE re2match(s, '.')$$);
-- operator form agrees with function form
SELECT re2idx_est($$SELECT * FROM re2idx_sel WHERE s @~ 'needle'$$);
-- join clause: flat matching default (0.010) over the cross product
SELECT re2idx_est($$SELECT * FROM re2idx_sel a, re2idx_sel b WHERE re2match(a.s, b.s)$$);

DROP TABLE re2idx_sel;
DROP FUNCTION re2idx_est;

-- ==== GIN trigram prefilter driven by RE2 FilteredRE2 ====

CREATE TABLE re2idx_gin (msg text);
INSERT INTO re2idx_gin SELECT 'error code ' || g FROM generate_series(1, 3000) g;
INSERT INTO re2idx_gin SELECT 'info message ' || g FROM generate_series(1, 3000) g;
-- non-ASCII (no digits, so the [0-9]+ counts above are undisturbed): RE2 folds
-- atoms with Unicode case folding while the index folds ASCII only.
INSERT INTO re2idx_gin SELECT 'diagnostic ÄBC' FROM generate_series(1, 100);
INSERT INTO re2idx_gin SELECT 'diagnostic äbc' FROM generate_series(1, 100);
CREATE INDEX ON re2idx_gin USING gin (msg gin_re2_ops);
ANALYZE re2idx_gin;

-- required literal atom
SELECT count(*) FROM re2idx_gin WHERE msg @~ 'error code';
-- alternation: atoms on both branches
SELECT count(*) FROM re2idx_gin WHERE msg @~ 'error|message';
-- lossy prefilter: recheck removes false positives (anchored tail)
SELECT count(*) FROM re2idx_gin WHERE msg @~ 'error code 999$';
-- pattern without a >=3-byte atom: full scan, still correct
SELECT count(*) FROM re2idx_gin WHERE msg @~ '[0-9]+';

-- index path (@~) must equal the sequential recheck (re2match) for each.
-- 'error code 1'/'message 25' pair a common atom with a rare tail, exercising
-- the tri-state consistent's fast-scan skip; a wrong skip would drop matches.
-- 'ÄBC'/'äbc' force the non-ASCII scan-all fallback: RE2's Unicode-folded atom
-- would miss the ASCII-folded index, so those must scan-all and recheck. The
-- '\xC4BC' escapes are ASCII pattern text that RE2 still compiles to the same
-- non-ASCII atom bytes, so the fallback must inspect the compiled atom, not just
-- the raw pattern.
SET enable_seqscan = off;
-- forced index: confirm @~ rides the GIN index, not a penalised seqscan, for
-- both a selective atom and the non-ASCII scan-all fallback.
EXPLAIN (COSTS OFF) SELECT * FROM re2idx_gin WHERE msg @~ 'error code';
EXPLAIN (COSTS OFF) SELECT * FROM re2idx_gin WHERE msg @~ 'ÄBC';
EXPLAIN (COSTS OFF) SELECT * FROM re2idx_gin WHERE msg @~ '\xC4BC';
SELECT bool_and(idx = seq) AS gin_matches_re2match
FROM (
	SELECT
		(SELECT count(*) FROM re2idx_gin WHERE msg @~ p)		 AS idx,
		(SELECT count(*) FROM re2idx_gin WHERE re2match(msg, p)) AS seq
	FROM unnest(ARRAY['error code', 'error|message', 'error code 999$', '[0-9]+',
					  'error code 1', 'message 25', 'code 2500$',
					  'ÄBC', 'äbc', '\xC4BC', '(?i)\xC4BC',
					  -- alternation-heavy / repeat-trigram atoms: exercise
					  -- extract_query's cross-atom and within-atom dedup
					  'error code 1|info message 2|error code 3',
					  'errorerror|message']) p
) q;
RESET enable_seqscan;

DROP TABLE re2idx_gin;

-- ==== huge GIN-indexed value: dedup must stay under MaxAllocSize ====
-- Entries were sized 8 bytes per trigram before dedup, overflowing palloc's
-- 1GB cap past ~134MB of text a plain text column accepts.

CREATE TABLE re2idx_big (msg text);
CREATE INDEX ON re2idx_big USING gin (msg gin_re2_ops);
INSERT INTO re2idx_big VALUES ('start.' || repeat('ab', 75000000));
SET enable_seqscan = off;
SELECT count(*) FROM re2idx_big WHERE msg @~ 'start';
RESET enable_seqscan;

DROP TABLE re2idx_big;

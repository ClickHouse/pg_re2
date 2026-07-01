\echo Use "ALTER EXTENSION re2 UPDATE TO '0.4'" to load this file. \quit

CREATE FUNCTION re2_version() RETURNS TEXT
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

-- re2match(col, '^literal...') on a text_pattern_ops (or bytewise-sorting
-- collation) b-tree becomes a range scan bounded by the pattern's fixed prefix.
CREATE FUNCTION pgre2_match_support(internal) RETURNS internal
AS 'MODULE_PATHNAME', 'pgre2_match_support'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

ALTER FUNCTION re2match(text, text) SUPPORT pgre2_match_support;

-- Regex match compiles and scans a pattern (cf. core ts_match at procost 100),
-- far above a comparison. Raises seqscan-filter and GIN-recheck costs so the
-- planner stops treating re2match as free.
ALTER FUNCTION re2match(text, text) COST 100;
ALTER FUNCTION re2match(bytea, text) COST 100;

-- GIN trigram prefilter driven by RE2 FilteredRE2 atoms.
-- matchingsel runs re2match over the column's most-common-value and histogram
-- statistics (exact on the sample, no pattern parsing) instead of the flat 0.5
-- a RESTRICT-less operator gets. Core regexeqsel is unusable here: it
-- Spencer-compiles the pattern and would error at plan time on RE2-only syntax.
CREATE OPERATOR @~ (
	LEFTARG = text, RIGHTARG = text, PROCEDURE = re2match,
	RESTRICT = matchingsel, JOIN = matchingjoinsel
);

CREATE FUNCTION gin_re2_extract_value(text, internal) RETURNS internal
AS 'MODULE_PATHNAME', 'gin_re2_extract_value'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION gin_re2_extract_query(text, internal, int2, internal, internal, internal, internal) RETURNS internal
AS 'MODULE_PATHNAME', 'gin_re2_extract_query'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION gin_re2_consistent(internal, int2, text, int4, internal, internal, internal, internal) RETURNS boolean
AS 'MODULE_PATHNAME', 'gin_re2_consistent'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- Tri-state consistent lets GIN fast-scan: a rare atom's trigrams can skip past
-- a common atom's huge posting list instead of rechecking every entry.
CREATE FUNCTION gin_re2_triconsistent(internal, int2, text, int4, internal, internal, internal) RETURNS "char"
AS 'MODULE_PATHNAME', 'gin_re2_triconsistent'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR CLASS gin_re2_ops
FOR TYPE text USING gin
AS
	OPERATOR	1	@~ (text, text),
	FUNCTION	1	btint4cmp (int4, int4),
	FUNCTION	2	gin_re2_extract_value (text, internal),
	FUNCTION	3	gin_re2_extract_query (text, internal, int2, internal, internal, internal, internal),
	FUNCTION	4	gin_re2_consistent (internal, int2, text, int4, internal, internal, internal, internal),
	FUNCTION	6	gin_re2_triconsistent (internal, int2, text, int4, internal, internal, internal),
	STORAGE		int4;

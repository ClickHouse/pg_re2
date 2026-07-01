\echo Use "CREATE EXTENSION re2" to load this file. \quit

CREATE FUNCTION re2match(text, text) RETURNS boolean
AS 'MODULE_PATHNAME', 'pgre2_match'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extract(text, text) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2_extract'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractall(text, text) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2_extractall'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2regexpextract(text, text, int DEFAULT 1) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2_regexpextract'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractgroups(text, text) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2_extractgroups'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractallgroupshorizontal(text, text) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2_extractallgroupshorizontal'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractallgroupsvertical(text, text) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2_extractallgroupsvertical'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2regexpquotemeta(text) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2_regexpquotemeta'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2splitbyregexp(pattern text, haystack text, max_substrings int DEFAULT 0) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2_splitbyregexp'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2replaceregexpone(text, text, text) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2_replaceregexpone'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2replaceregexpall(text, text, text) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2_replaceregexpall'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2countmatches(text, text) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2_countmatches'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2countmatchescaseinsensitive(text, text) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2_countmatchescaseinsensitive'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchany(text, VARIADIC text[]) RETURNS boolean
AS 'MODULE_PATHNAME', 'pgre2_multimatchany'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchanyindex(text, VARIADIC text[]) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2_multimatchanyindex'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchallindices(text, VARIADIC text[]) RETURNS integer[]
AS 'MODULE_PATHNAME', 'pgre2_multimatchallindices'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- bytea overloads (haystack can contain \0 bytes)

CREATE FUNCTION re2match(bytea, text) RETURNS boolean
AS 'MODULE_PATHNAME', 'pgre2_match_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extract(bytea, text) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2_extract_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractall(bytea, text) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2_extractall_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2regexpextract(bytea, text, int DEFAULT 1) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2_regexpextract_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractgroups(bytea, text) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2_extractgroups_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractallgroupshorizontal(bytea, text) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2_extractallgroupshorizontal_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractallgroupsvertical(bytea, text) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2_extractallgroupsvertical_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2regexpquotemeta(bytea) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2_regexpquotemeta_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2splitbyregexp(pattern text, haystack bytea, max_substrings int DEFAULT 0) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2_splitbyregexp_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2replaceregexpone(bytea, text, text) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2_replaceregexpone_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2replaceregexpall(bytea, text, text) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2_replaceregexpall_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2countmatches(bytea, text) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2_countmatches_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2countmatchescaseinsensitive(bytea, text) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2_countmatchescaseinsensitive_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchany(bytea, VARIADIC text[]) RETURNS boolean
AS 'MODULE_PATHNAME', 'pgre2_multimatchany_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchanyindex(bytea, VARIADIC text[]) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2_multimatchanyindex_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchallindices(bytea, VARIADIC text[]) RETURNS integer[]
AS 'MODULE_PATHNAME', 'pgre2_multimatchallindices_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

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

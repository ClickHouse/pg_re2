\echo Use "CREATE EXTENSION pg_re2ch" to load this file. \quit

CREATE FUNCTION re2match(text, text) RETURNS boolean
AS 'MODULE_PATHNAME', 'pgre2ch_match'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extract(text, text) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2ch_extract'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractall(text, text) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2ch_extractall'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2regexpextract(text, text, int DEFAULT 1) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2ch_regexpextract'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractgroups(text, text) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2ch_extractgroups'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2replaceregexpone(text, text, text) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2ch_replaceregexpone'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2replaceregexpall(text, text, text) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2ch_replaceregexpall'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2countmatches(text, text) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2ch_countmatches'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2countmatchescaseinsensitive(text, text) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2ch_countmatchescaseinsensitive'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchany(text, text[]) RETURNS boolean
AS 'MODULE_PATHNAME', 'pgre2ch_multimatchany'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchanyindex(text, text[]) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2ch_multimatchanyindex'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchallindices(text, text[]) RETURNS integer[]
AS 'MODULE_PATHNAME', 'pgre2ch_multimatchallindices'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- bytea overloads (haystack can contain \0 bytes)

CREATE FUNCTION re2match(bytea, text) RETURNS boolean
AS 'MODULE_PATHNAME', 'pgre2ch_match_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extract(bytea, text) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2ch_extract_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractall(bytea, text) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2ch_extractall_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2regexpextract(bytea, text, int DEFAULT 1) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2ch_regexpextract_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractgroups(bytea, text) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2ch_extractgroups_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2replaceregexpone(bytea, text, text) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2ch_replaceregexpone_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2replaceregexpall(bytea, text, text) RETURNS bytea
AS 'MODULE_PATHNAME', 'pgre2ch_replaceregexpall_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2countmatches(bytea, text) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2ch_countmatches_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2countmatchescaseinsensitive(bytea, text) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2ch_countmatchescaseinsensitive_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchany(bytea, text[]) RETURNS boolean
AS 'MODULE_PATHNAME', 'pgre2ch_multimatchany_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchanyindex(bytea, text[]) RETURNS integer
AS 'MODULE_PATHNAME', 'pgre2ch_multimatchanyindex_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2multimatchallindices(bytea, text[]) RETURNS integer[]
AS 'MODULE_PATHNAME', 'pgre2ch_multimatchallindices_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

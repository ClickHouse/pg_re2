\echo Use "ALTER EXTENSION re2 UPDATE TO '0.3'" to load this file. \quit

-- 0.2.0 shipped re2splitbyregexp with arguments swapped relative to ClickHouse.
-- text overload keeps the (text, text, int) SQL shape but the wrapper now reads
-- arg 0 as pattern; bytea overload changes from (bytea, text) to (text, bytea).

DROP FUNCTION re2splitbyregexp(bytea, text, int);

CREATE FUNCTION re2splitbyregexp(pattern text, haystack bytea, max_substrings int DEFAULT 0) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2_splitbyregexp_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

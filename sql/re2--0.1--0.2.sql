\echo Use "ALTER EXTENSION re2 UPDATE TO '0.2'" to load this file. \quit

CREATE FUNCTION re2extractallgroupshorizontal(text, text) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2_extractallgroupshorizontal'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2extractallgroupsvertical(text, text) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2_extractallgroupsvertical'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2regexpquotemeta(text) RETURNS text
AS 'MODULE_PATHNAME', 'pgre2_regexpquotemeta'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION re2splitbyregexp(text, text, int DEFAULT 0) RETURNS text[]
AS 'MODULE_PATHNAME', 'pgre2_splitbyregexp'
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

CREATE FUNCTION re2splitbyregexp(bytea, text, int DEFAULT 0) RETURNS bytea[]
AS 'MODULE_PATHNAME', 'pgre2_splitbyregexp_bytea'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

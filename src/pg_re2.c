#include "postgres.h"

#include "catalog/pg_type.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "re2_cache.h"
#include "version.h"

/* Extension metadata for the server. */
#ifdef PG_MODULE_MAGIC_EXT
PG_MODULE_MAGIC_EXT(.name = "re2", .version = RE2_EXT_VERSION);
#else
PG_MODULE_MAGIC;
#endif

/* build text datum from span (single palloc) */
static Datum
span_to_text(re2_span s)
{
	if (s.data)
		return PointerGetDatum(cstring_to_text_with_len(s.data, (int)s.len));
	return PointerGetDatum(cstring_to_text_with_len("", 0));
}

/* build bytea datum from span (single palloc) */
static Datum
span_to_bytea(re2_span s)
{
	size_t len = s.data ? s.len : 0;
	bytea *result = (bytea *)palloc(len + VARHDRSZ);

	SET_VARSIZE(result, len + VARHDRSZ);
	if (len > 0)
		memcpy(VARDATA(result), s.data, len);
	return PointerGetDatum(result);
}

static re2_pattern *
compile_arg(text *pattern)
{
	char		 errbuf[RE2_ERRBUF_SIZE];
	re2_pattern *pat;

	pat = re2_cache_lookup(VARDATA_ANY(pattern), VARSIZE_ANY_EXHDR(pattern), errbuf, sizeof(errbuf));
	if (!pat)
		ereport(ERROR, (errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("invalid RE2 pattern: %s", errbuf)));
	return pat;
}

static re2_pattern *
compile_arg_icase(text *pattern)
{
	char		 errbuf[RE2_ERRBUF_SIZE];
	size_t		 plen = VARSIZE_ANY_EXHDR(pattern);
	char		*ipat = (char *)palloc(plen + 4);
	re2_pattern *pat;

	memcpy(ipat, "(?i)", 4);
	memcpy(ipat + 4, VARDATA_ANY(pattern), plen);

	pat = re2_cache_lookup(ipat, plen + 4, errbuf, sizeof(errbuf));
	if (!pat)
		ereport(ERROR, (errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("invalid RE2 pattern: %s", errbuf)));
	return pat;
}

/* ---- text functions ---- */

PG_FUNCTION_INFO_V1(pgre2_match);
Datum
pgre2_match(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_BOOL(re2_match(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_extract);
Datum
pgre2_extract(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_DATUM(span_to_text(re2_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack))));
}

PG_FUNCTION_INFO_V1(pgre2_extractall);
Datum
pgre2_extractall(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	int			 count;
	re2_span	*spans;
	Datum		*elems;
	ArrayType	*arr;

	{
		char errbuf[RE2_ERRBUF_SIZE];

		errbuf[0] = '\0';
		spans
		= re2_extract_all(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), &count, errbuf, sizeof(errbuf));
		if (errbuf[0] != '\0')
			ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY), errmsg("%s", errbuf)));
	}

	elems = (Datum *)palloc(count * sizeof(Datum));
	for (int i = 0; i < count; i++)
		elems[i] = span_to_text(spans[i]);

	arr = construct_array(elems, count, TEXTOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

PG_FUNCTION_INFO_V1(pgre2_regexpextract);
Datum
pgre2_regexpextract(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	int			 group_idx = PG_GETARG_INT32(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	re2_span	 s;

	errbuf[0] = '\0';
	s = re2_regexp_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), group_idx, errbuf, sizeof(errbuf));
	if (errbuf[0] != '\0')
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf)));

	PG_RETURN_DATUM(span_to_text(s));
}

PG_FUNCTION_INFO_V1(pgre2_extractgroups);
Datum
pgre2_extractgroups(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	char		 errbuf[RE2_ERRBUF_SIZE];
	int			 count;
	re2_span	*spans;
	Datum		*elems;
	ArrayType	*arr;

	errbuf[0] = '\0';
	spans = re2_extract_groups(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), &count, errbuf, sizeof(errbuf));

	if (errbuf[0] != '\0')
		ereport(ERROR, (errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("%s", errbuf)));

	if (!spans)
	{
		arr = construct_array(NULL, 0, TEXTOID, -1, false, TYPALIGN_INT);
		PG_RETURN_ARRAYTYPE_P(arr);
	}

	elems = (Datum *)palloc(count * sizeof(Datum));
	for (int i = 0; i < count; i++)
		elems[i] = span_to_text(spans[i]);

	arr = construct_array(elems, count, TEXTOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

/*
 * Build a 2D ArrayType from element-builder fn applied to spans.
 * spans: row-major (match × group). vertical=true builds (match, group); else (group, match).
 * Empty result yields a 0-D empty array (postgres can't represent shape (k, 0)).
 */
typedef Datum (*span_to_datum_fn)(re2_span s);

static ArrayType *
build_groups_2d(re2_span *spans, int matches, int ngroups, bool vertical, span_to_datum_fn build, Oid elemoid)
{
	Datum *elems;
	int	   total = matches * ngroups;
	int	   dims[2];
	int	   lbs[2] = { 1, 1 };

	if (total == 0)
		return construct_empty_array(elemoid);

	elems = (Datum *)palloc(total * sizeof(Datum));

	if (vertical)
	{
		dims[0] = matches;
		dims[1] = ngroups;
		for (int i = 0; i < total; i++)
			elems[i] = build(spans[i]);
	}
	else
	{
		dims[0] = ngroups;
		dims[1] = matches;
		for (int g = 0; g < ngroups; g++)
			for (int m = 0; m < matches; m++)
				elems[g * matches + m] = build(spans[m * ngroups + g]);
	}

	return construct_md_array(elems, NULL, 2, dims, lbs, elemoid, -1, false, TYPALIGN_INT);
}

static ArrayType *
extractallgroups_common(text *haystack_va, text *pattern, bool vertical, bool as_bytea)
{
	re2_pattern *pat = compile_arg(pattern);
	const char	*hdata = VARDATA_ANY(haystack_va);
	size_t		 hlen = VARSIZE_ANY_EXHDR(haystack_va);
	char		 errbuf[RE2_ERRBUF_SIZE];
	int			 matches;
	int			 ngroups;
	re2_span	*spans;

	errbuf[0] = '\0';
	spans = re2_extract_all_groups(pat, hdata, hlen, &matches, &ngroups, errbuf, sizeof(errbuf));

	if (errbuf[0] != '\0')
		ereport(ERROR, (errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("%s", errbuf)));

	if (!spans || matches == 0)
		return construct_empty_array(as_bytea ? BYTEAOID : TEXTOID);

	return build_groups_2d(spans, matches, ngroups, vertical, as_bytea ? span_to_bytea : span_to_text,
						   as_bytea ? BYTEAOID : TEXTOID);
}

PG_FUNCTION_INFO_V1(pgre2_extractallgroupshorizontal);
Datum
pgre2_extractallgroupshorizontal(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(extractallgroups_common(PG_GETARG_TEXT_PP(0), PG_GETARG_TEXT_PP(1), false, false));
}

PG_FUNCTION_INFO_V1(pgre2_extractallgroupsvertical);
Datum
pgre2_extractallgroupsvertical(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(extractallgroups_common(PG_GETARG_TEXT_PP(0), PG_GETARG_TEXT_PP(1), true, false));
}

/*
 * Escape regex metacharacters per ClickHouse semantics:
 * \0 \\ | ( ) ^ $ . [ ] ? * + { : -
 * (Slightly differs from re2::RE2::QuoteMeta which uses \xNN for control bytes.)
 * Source: ClickHouse/src/Functions/regexpQuoteMeta.cpp
 */
static void *
quotemeta_impl(const char *src, size_t slen)
{
	void  *out = palloc(2 * slen + VARHDRSZ);
	char  *dst = VARDATA(out);
	size_t dlen = 0;

	for (size_t i = 0; i < slen; i++)
	{
		char c = src[i];

		switch (c)
		{
			case '\0':
			case '\\':
			case '|':
			case '(':
			case ')':
			case '^':
			case '$':
			case '.':
			case '[':
			case ']':
			case '?':
			case '*':
			case '+':
			case '{':
			case ':':
			case '-':
				dst[dlen++] = '\\';
				break;
			default:
				break;
		}
		dst[dlen++] = c;
	}
	SET_VARSIZE(out, dlen + VARHDRSZ);
	return out;
}

PG_FUNCTION_INFO_V1(pgre2_regexpquotemeta);
Datum
pgre2_regexpquotemeta(PG_FUNCTION_ARGS)
{
	text *input = PG_GETARG_TEXT_PP(0);

	PG_RETURN_TEXT_P(quotemeta_impl(VARDATA_ANY(input), VARSIZE_ANY_EXHDR(input)));
}

/* splitByRegexp empty-pattern path: each input byte becomes its own element. */
static ArrayType *
split_chars(const char *hdata, size_t hlen, int max_splits, bool as_bytea)
{
	int		   n = (max_splits > 0 && (size_t)max_splits < hlen) ? max_splits : (int)hlen;
	Datum	  *elems;
	ArrayType *arr;

	if (n == 0)
		return construct_empty_array(as_bytea ? BYTEAOID : TEXTOID);

	elems = (Datum *)palloc(n * sizeof(Datum));
	for (int i = 0; i < n; i++)
	{
		re2_span s = { hdata + i, 1 };

		elems[i] = as_bytea ? span_to_bytea(s) : span_to_text(s);
	}

	arr = construct_array(elems, n, as_bytea ? BYTEAOID : TEXTOID, -1, false, TYPALIGN_INT);
	return arr;
}

/*
 * splitByRegexp(haystack, pattern, max_splits=0). Empty pattern splits per byte;
 * otherwise re2_split emits substrings between matches. max_splits 0 = unlimited.
 */
static ArrayType *
splitbyregexp_common(text *haystack_va, text *pattern, int max_splits, bool as_bytea)
{
	const char *hdata = VARDATA_ANY(haystack_va);
	size_t		hlen = VARSIZE_ANY_EXHDR(haystack_va);
	size_t		plen = VARSIZE_ANY_EXHDR(pattern);

	if (plen == 0)
		return split_chars(hdata, hlen, max_splits, as_bytea);

	{
		re2_pattern *pat = compile_arg(pattern);
		char		 errbuf[RE2_ERRBUF_SIZE];
		int			 count;
		re2_span	*spans;
		Datum		*elems;
		ArrayType	*arr;

		errbuf[0] = '\0';
		spans = re2_split(pat, hdata, hlen, max_splits, &count, errbuf, sizeof(errbuf));
		if (errbuf[0] != '\0')
			ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY), errmsg("%s", errbuf)));

		if (!spans || count == 0)
			return construct_empty_array(as_bytea ? BYTEAOID : TEXTOID);

		elems = (Datum *)palloc(count * sizeof(Datum));
		for (int i = 0; i < count; i++)
			elems[i] = as_bytea ? span_to_bytea(spans[i]) : span_to_text(spans[i]);

		arr = construct_array(elems, count, as_bytea ? BYTEAOID : TEXTOID, -1, false, TYPALIGN_INT);
		return arr;
	}
}

PG_FUNCTION_INFO_V1(pgre2_splitbyregexp);
Datum
pgre2_splitbyregexp(PG_FUNCTION_ARGS)
{
	int max_splits = PG_NARGS() >= 3 && !PG_ARGISNULL(2) ? PG_GETARG_INT32(2) : 0;

	PG_RETURN_ARRAYTYPE_P(splitbyregexp_common(PG_GETARG_TEXT_PP(0), PG_GETARG_TEXT_PP(1), max_splits, false));
}

PG_FUNCTION_INFO_V1(pgre2_replaceregexpone);
Datum
pgre2_replaceregexpone(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	text		*replacement = PG_GETARG_TEXT_PP(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	void		*result;

	result = re2_replace_one(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), VARDATA_ANY(replacement),
							 VARSIZE_ANY_EXHDR(replacement), errbuf, sizeof(errbuf));
	if (!result)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf)));
	PG_RETURN_TEXT_P(result);
}

PG_FUNCTION_INFO_V1(pgre2_replaceregexpall);
Datum
pgre2_replaceregexpall(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	text		*replacement = PG_GETARG_TEXT_PP(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	void		*result;

	result = re2_replace_all(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), VARDATA_ANY(replacement),
							 VARSIZE_ANY_EXHDR(replacement), errbuf, sizeof(errbuf));
	if (!result)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf)));
	PG_RETURN_TEXT_P(result);
}

PG_FUNCTION_INFO_V1(pgre2_countmatches);
Datum
pgre2_countmatches(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_INT32(re2_count_matches(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_countmatchescaseinsensitive);
Datum
pgre2_countmatchescaseinsensitive(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg_icase(PG_GETARG_TEXT_PP(1));

	PG_RETURN_INT32(re2_count_matches(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

/* ---- multi-pattern helpers ---- */

static re2_pattern **
decon_patterns(ArrayType *arr, int *npatterns)
{
	Datum		 *elems;
	bool		 *nulls;
	int			  n;
	re2_pattern **pats;

	deconstruct_array(arr, TEXTOID, -1, false, TYPALIGN_INT, &elems, &nulls, &n);

	pats = (re2_pattern **)palloc(n * sizeof(re2_pattern *));
	for (int i = 0; i < n; i++)
	{
		text *t;
		char  errbuf[RE2_ERRBUF_SIZE];

		if (nulls[i])
			ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED), errmsg("pattern array must not contain NULLs")));

		t = DatumGetTextPP(elems[i]);
		pats[i] = re2_cache_lookup(VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t), errbuf, sizeof(errbuf));
		if (!pats[i])
			ereport(ERROR, (errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
							errmsg("invalid RE2 pattern at index %d: %s", i + 1, errbuf)));
	}
	*npatterns = n;
	return pats;
}

PG_FUNCTION_INFO_V1(pgre2_multimatchany);
Datum
pgre2_multimatchany(PG_FUNCTION_ARGS)
{
	text		 *haystack = PG_GETARG_TEXT_PP(0);
	ArrayType	 *patterns = PG_GETARG_ARRAYTYPE_P(1);
	const char	 *hdata = VARDATA_ANY(haystack);
	size_t		  hlen = VARSIZE_ANY_EXHDR(haystack);
	int			  n;
	re2_pattern **pats = decon_patterns(patterns, &n);

	for (int i = 0; i < n; i++)
	{
		if (re2_match(pats[i], hdata, hlen))
			PG_RETURN_BOOL(true);
	}
	PG_RETURN_BOOL(false);
}

PG_FUNCTION_INFO_V1(pgre2_multimatchanyindex);
Datum
pgre2_multimatchanyindex(PG_FUNCTION_ARGS)
{
	text		 *haystack = PG_GETARG_TEXT_PP(0);
	ArrayType	 *patterns = PG_GETARG_ARRAYTYPE_P(1);
	const char	 *hdata = VARDATA_ANY(haystack);
	size_t		  hlen = VARSIZE_ANY_EXHDR(haystack);
	int			  n;
	re2_pattern **pats = decon_patterns(patterns, &n);

	for (int i = 0; i < n; i++)
	{
		if (re2_match(pats[i], hdata, hlen))
			PG_RETURN_INT32(i + 1);
	}
	PG_RETURN_INT32(0);
}

PG_FUNCTION_INFO_V1(pgre2_multimatchallindices);
Datum
pgre2_multimatchallindices(PG_FUNCTION_ARGS)
{
	text		 *haystack = PG_GETARG_TEXT_PP(0);
	ArrayType	 *patterns = PG_GETARG_ARRAYTYPE_P(1);
	const char	 *hdata = VARDATA_ANY(haystack);
	size_t		  hlen = VARSIZE_ANY_EXHDR(haystack);
	int			  n;
	re2_pattern **pats = decon_patterns(patterns, &n);
	Datum		 *elems;
	int			  count = 0;
	ArrayType	 *arr;

	elems = (Datum *)palloc(n * sizeof(Datum));
	for (int i = 0; i < n; i++)
	{
		if (re2_match(pats[i], hdata, hlen))
			elems[count++] = Int32GetDatum(i + 1);
	}

	arr = construct_array(elems, count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

/* ==== bytea overloads ==== */

PG_FUNCTION_INFO_V1(pgre2_match_bytea);
Datum
pgre2_match_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_BOOL(re2_match(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_extract_bytea);
Datum
pgre2_extract_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_DATUM(span_to_bytea(re2_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack))));
}

PG_FUNCTION_INFO_V1(pgre2_extractall_bytea);
Datum
pgre2_extractall_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	int			 count;
	re2_span	*spans;
	Datum		*elems;
	ArrayType	*arr;

	{
		char errbuf[RE2_ERRBUF_SIZE];

		errbuf[0] = '\0';
		spans
		= re2_extract_all(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), &count, errbuf, sizeof(errbuf));
		if (errbuf[0] != '\0')
			ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY), errmsg("%s", errbuf)));
	}

	elems = (Datum *)palloc(count * sizeof(Datum));
	for (int i = 0; i < count; i++)
		elems[i] = span_to_bytea(spans[i]);

	arr = construct_array(elems, count, BYTEAOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

PG_FUNCTION_INFO_V1(pgre2_regexpextract_bytea);
Datum
pgre2_regexpextract_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	int			 group_idx = PG_GETARG_INT32(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	re2_span	 s;

	errbuf[0] = '\0';
	s = re2_regexp_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), group_idx, errbuf, sizeof(errbuf));
	if (errbuf[0] != '\0')
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf)));

	PG_RETURN_DATUM(span_to_bytea(s));
}

PG_FUNCTION_INFO_V1(pgre2_extractgroups_bytea);
Datum
pgre2_extractgroups_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	char		 errbuf[RE2_ERRBUF_SIZE];
	int			 count;
	re2_span	*spans;
	Datum		*elems;
	ArrayType	*arr;

	errbuf[0] = '\0';
	spans = re2_extract_groups(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), &count, errbuf, sizeof(errbuf));

	if (errbuf[0] != '\0')
		ereport(ERROR, (errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("%s", errbuf)));

	if (!spans)
	{
		arr = construct_array(NULL, 0, BYTEAOID, -1, false, TYPALIGN_INT);
		PG_RETURN_ARRAYTYPE_P(arr);
	}

	elems = (Datum *)palloc(count * sizeof(Datum));
	for (int i = 0; i < count; i++)
		elems[i] = span_to_bytea(spans[i]);

	arr = construct_array(elems, count, BYTEAOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

PG_FUNCTION_INFO_V1(pgre2_extractallgroupshorizontal_bytea);
Datum
pgre2_extractallgroupshorizontal_bytea(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(extractallgroups_common((text *)PG_GETARG_BYTEA_PP(0), PG_GETARG_TEXT_PP(1), false, true));
}

PG_FUNCTION_INFO_V1(pgre2_extractallgroupsvertical_bytea);
Datum
pgre2_extractallgroupsvertical_bytea(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(extractallgroups_common((text *)PG_GETARG_BYTEA_PP(0), PG_GETARG_TEXT_PP(1), true, true));
}

PG_FUNCTION_INFO_V1(pgre2_regexpquotemeta_bytea);
Datum
pgre2_regexpquotemeta_bytea(PG_FUNCTION_ARGS)
{
	bytea *input = PG_GETARG_BYTEA_PP(0);

	PG_RETURN_BYTEA_P(quotemeta_impl(VARDATA_ANY(input), VARSIZE_ANY_EXHDR(input)));
}

PG_FUNCTION_INFO_V1(pgre2_splitbyregexp_bytea);
Datum
pgre2_splitbyregexp_bytea(PG_FUNCTION_ARGS)
{
	int max_splits = PG_NARGS() >= 3 && !PG_ARGISNULL(2) ? PG_GETARG_INT32(2) : 0;

	PG_RETURN_ARRAYTYPE_P(splitbyregexp_common((text *)PG_GETARG_BYTEA_PP(0), PG_GETARG_TEXT_PP(1), max_splits, true));
}

PG_FUNCTION_INFO_V1(pgre2_replaceregexpone_bytea);
Datum
pgre2_replaceregexpone_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	text		*replacement = PG_GETARG_TEXT_PP(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	void		*result;

	result = re2_replace_one(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), VARDATA_ANY(replacement),
							 VARSIZE_ANY_EXHDR(replacement), errbuf, sizeof(errbuf));
	if (!result)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf)));
	PG_RETURN_BYTEA_P(result);
}

PG_FUNCTION_INFO_V1(pgre2_replaceregexpall_bytea);
Datum
pgre2_replaceregexpall_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));
	text		*replacement = PG_GETARG_TEXT_PP(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	void		*result;

	result = re2_replace_all(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), VARDATA_ANY(replacement),
							 VARSIZE_ANY_EXHDR(replacement), errbuf, sizeof(errbuf));
	if (!result)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf)));
	PG_RETURN_BYTEA_P(result);
}

PG_FUNCTION_INFO_V1(pgre2_countmatches_bytea);
Datum
pgre2_countmatches_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_INT32(re2_count_matches(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_countmatchescaseinsensitive_bytea);
Datum
pgre2_countmatchescaseinsensitive_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg_icase(PG_GETARG_TEXT_PP(1));

	PG_RETURN_INT32(re2_count_matches(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_multimatchany_bytea);
Datum
pgre2_multimatchany_bytea(PG_FUNCTION_ARGS)
{
	bytea		 *haystack = PG_GETARG_BYTEA_PP(0);
	ArrayType	 *patterns = PG_GETARG_ARRAYTYPE_P(1);
	const char	 *hdata = VARDATA_ANY(haystack);
	size_t		  hlen = VARSIZE_ANY_EXHDR(haystack);
	int			  n;
	re2_pattern **pats = decon_patterns(patterns, &n);

	for (int i = 0; i < n; i++)
	{
		if (re2_match(pats[i], hdata, hlen))
			PG_RETURN_BOOL(true);
	}
	PG_RETURN_BOOL(false);
}

PG_FUNCTION_INFO_V1(pgre2_multimatchanyindex_bytea);
Datum
pgre2_multimatchanyindex_bytea(PG_FUNCTION_ARGS)
{
	bytea		 *haystack = PG_GETARG_BYTEA_PP(0);
	ArrayType	 *patterns = PG_GETARG_ARRAYTYPE_P(1);
	const char	 *hdata = VARDATA_ANY(haystack);
	size_t		  hlen = VARSIZE_ANY_EXHDR(haystack);
	int			  n;
	re2_pattern **pats = decon_patterns(patterns, &n);

	for (int i = 0; i < n; i++)
	{
		if (re2_match(pats[i], hdata, hlen))
			PG_RETURN_INT32(i + 1);
	}
	PG_RETURN_INT32(0);
}

PG_FUNCTION_INFO_V1(pgre2_multimatchallindices_bytea);
Datum
pgre2_multimatchallindices_bytea(PG_FUNCTION_ARGS)
{
	bytea		 *haystack = PG_GETARG_BYTEA_PP(0);
	ArrayType	 *patterns = PG_GETARG_ARRAYTYPE_P(1);
	const char	 *hdata = VARDATA_ANY(haystack);
	size_t		  hlen = VARSIZE_ANY_EXHDR(haystack);
	int			  n;
	re2_pattern **pats = decon_patterns(patterns, &n);
	Datum		 *elems;
	int			  count = 0;
	ArrayType	 *arr;

	elems = (Datum *)palloc(n * sizeof(Datum));
	for (int i = 0; i < n; i++)
	{
		if (re2_match(pats[i], hdata, hlen))
			elems[count++] = Int32GetDatum(i + 1);
	}

	arr = construct_array(elems, count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

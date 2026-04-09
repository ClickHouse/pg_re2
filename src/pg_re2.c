#include "postgres.h"

#include "catalog/pg_type.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "re2_cache.h"

PG_MODULE_MAGIC;

/* build text datum from span (single palloc) */
static text *
span_to_text(re2_span s)
{
	if (s.data)
		return cstring_to_text_with_len(s.data, (int)s.len);
	return cstring_to_text_with_len("", 0);
}

/* build bytea datum from span (single palloc) */
static bytea *
span_to_bytea(re2_span s)
{
	size_t len = s.data ? s.len : 0;
	bytea *result = (bytea *)palloc(len + VARHDRSZ);

	SET_VARSIZE(result, len + VARHDRSZ);
	if (len > 0)
		memcpy(VARDATA(result), s.data, len);
	return result;
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

PG_FUNCTION_INFO_V1(pgre2ch_match);
Datum
pgre2ch_match(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_BOOL(re2_match(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2ch_extract);
Datum
pgre2ch_extract(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_TEXT_P(span_to_text(re2_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack))));
}

PG_FUNCTION_INFO_V1(pgre2ch_extractall);
Datum
pgre2ch_extractall(PG_FUNCTION_ARGS)
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
		elems[i] = PointerGetDatum(span_to_text(spans[i]));

	arr = construct_array(elems, count, TEXTOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

PG_FUNCTION_INFO_V1(pgre2ch_regexpextract);
Datum
pgre2ch_regexpextract(PG_FUNCTION_ARGS)
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

	PG_RETURN_TEXT_P(span_to_text(s));
}

PG_FUNCTION_INFO_V1(pgre2ch_extractgroups);
Datum
pgre2ch_extractgroups(PG_FUNCTION_ARGS)
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
		elems[i] = PointerGetDatum(span_to_text(spans[i]));

	arr = construct_array(elems, count, TEXTOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

PG_FUNCTION_INFO_V1(pgre2ch_replaceregexpone);
Datum
pgre2ch_replaceregexpone(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_replaceregexpall);
Datum
pgre2ch_replaceregexpall(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_countmatches);
Datum
pgre2ch_countmatches(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_INT32(re2_count_matches(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2ch_countmatchescaseinsensitive);
Datum
pgre2ch_countmatchescaseinsensitive(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_multimatchany);
Datum
pgre2ch_multimatchany(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_multimatchanyindex);
Datum
pgre2ch_multimatchanyindex(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_multimatchallindices);
Datum
pgre2ch_multimatchallindices(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_match_bytea);
Datum
pgre2ch_match_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_BOOL(re2_match(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2ch_extract_bytea);
Datum
pgre2ch_extract_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_BYTEA_P(span_to_bytea(re2_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack))));
}

PG_FUNCTION_INFO_V1(pgre2ch_extractall_bytea);
Datum
pgre2ch_extractall_bytea(PG_FUNCTION_ARGS)
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
		elems[i] = PointerGetDatum(span_to_bytea(spans[i]));

	arr = construct_array(elems, count, BYTEAOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

PG_FUNCTION_INFO_V1(pgre2ch_regexpextract_bytea);
Datum
pgre2ch_regexpextract_bytea(PG_FUNCTION_ARGS)
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

	PG_RETURN_BYTEA_P(span_to_bytea(s));
}

PG_FUNCTION_INFO_V1(pgre2ch_extractgroups_bytea);
Datum
pgre2ch_extractgroups_bytea(PG_FUNCTION_ARGS)
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
		elems[i] = PointerGetDatum(span_to_bytea(spans[i]));

	arr = construct_array(elems, count, BYTEAOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(arr);
}

PG_FUNCTION_INFO_V1(pgre2ch_replaceregexpone_bytea);
Datum
pgre2ch_replaceregexpone_bytea(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_replaceregexpall_bytea);
Datum
pgre2ch_replaceregexpall_bytea(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_countmatches_bytea);
Datum
pgre2ch_countmatches_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(PG_GETARG_TEXT_PP(1));

	PG_RETURN_INT32(re2_count_matches(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2ch_countmatchescaseinsensitive_bytea);
Datum
pgre2ch_countmatchescaseinsensitive_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg_icase(PG_GETARG_TEXT_PP(1));

	PG_RETURN_INT32(re2_count_matches(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2ch_multimatchany_bytea);
Datum
pgre2ch_multimatchany_bytea(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_multimatchanyindex_bytea);
Datum
pgre2ch_multimatchanyindex_bytea(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(pgre2ch_multimatchallindices_bytea);
Datum
pgre2ch_multimatchallindices_bytea(PG_FUNCTION_ARGS)
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

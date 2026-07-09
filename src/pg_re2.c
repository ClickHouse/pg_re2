#include "postgres.h"

#include "catalog/pg_type.h"
#include "fmgr.h"
#include "utils/array.h"
#include "utils/builtins.h"

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

/*
 * Compile of stable (Const / extern Param) pattern arg, kept in fn_extra
 * to skip per-row cache lookup. Owns re2_pattern, avoiding cache management.
 */
typedef struct
{
	re2_pattern			 *compiled;
	MemoryContextCallback cb;
} Re2FnExtra;

static void
fn_extra_release(void *arg)
{
	Re2FnExtra *fx = (Re2FnExtra *)arg;

	if (fx->compiled)
		re2_free(fx->compiled);
}

static re2_pattern *
compile_fn_arg(FunctionCallInfo fcinfo, int argnum, bool icase)
{
	FmgrInfo	*flinfo = fcinfo->flinfo;
	char		 errbuf[RE2_ERRBUF_SIZE];
	text		*pattern;
	const char	*data;
	size_t		 len;
	re2_pattern *pat;

	if (flinfo && flinfo->fn_extra)
		return ((Re2FnExtra *)flinfo->fn_extra)->compiled;

	pattern = PG_GETARG_TEXT_PP(argnum);
	len = VARSIZE_ANY_EXHDR(pattern);
	if (icase)
	{
		char *ipat = (char *)palloc(len + 4);

		memcpy(ipat, "(?i)", 4);
		memcpy(ipat + 4, VARDATA_ANY(pattern), len);
		data = ipat;
		len += 4;
	}
	else
		data = VARDATA_ANY(pattern);

	if (flinfo && get_fn_expr_arg_stable(flinfo, argnum))
	{
		Re2FnExtra *fx = (Re2FnExtra *)MemoryContextAllocZero(flinfo->fn_mcxt, sizeof(Re2FnExtra));

		/* register before compile: compiled == NULL keeps callback no-op */
		fx->cb.func = fn_extra_release;
		fx->cb.arg = fx;
		MemoryContextRegisterResetCallback(flinfo->fn_mcxt, &fx->cb);

		pat = re2_compile(data, len, errbuf, sizeof(errbuf));
		if (pat)
		{
			fx->compiled = pat;
			flinfo->fn_extra = fx;
		}
	}
	else
		pat = re2_cache_lookup(data, len, errbuf, sizeof(errbuf));

	if (!pat)
		ereport(ERROR, errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("invalid RE2 pattern: %s", errbuf));
	return pat;
}

static re2_pattern *
compile_arg(FunctionCallInfo fcinfo, int argnum)
{
	return compile_fn_arg(fcinfo, argnum, false);
}

static re2_pattern *
compile_arg_icase(FunctionCallInfo fcinfo, int argnum)
{
	return compile_fn_arg(fcinfo, argnum, true);
}

/* RE2 matchers allocate, promote OOM to error */
static bool
match_or_error(re2_pattern *pat, const char *hdata, size_t hlen)
{
	bool failed;
	bool matched = re2_match(pat, hdata, hlen, &failed);

	if (failed)
		ereport(ERROR, errcode(ERRCODE_OUT_OF_MEMORY), errmsg("out of memory"));
	return matched;
}

static int32
count_or_error(re2_pattern *pat, const char *hdata, size_t hlen)
{
	int32 n = re2_count_matches(pat, hdata, hlen);

	if (n < 0)
		ereport(ERROR, errcode(ERRCODE_OUT_OF_MEMORY), errmsg("out of memory"));
	return n;
}

/* ---- utility function ---- */
PG_FUNCTION_INFO_V1(re2_version);
Datum
re2_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(RE2_EXT_VERSION));
}

/* ---- text functions ---- */

PG_FUNCTION_INFO_V1(pgre2_match);
Datum
pgre2_match(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);

	PG_RETURN_BOOL(match_or_error(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_extract);
Datum
pgre2_extract(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	char		 errbuf[RE2_ERRBUF_SIZE];
	re2_span	 s;

	s = re2_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), errbuf, sizeof(errbuf));
	if (errbuf[0] != '\0')
		ereport(ERROR, errcode(ERRCODE_OUT_OF_MEMORY), errmsg("%s", errbuf));
	PG_RETURN_DATUM(span_to_text(s));
}

PG_FUNCTION_INFO_V1(pgre2_extractall);
Datum
pgre2_extractall(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
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
			ereport(ERROR, errcode(ERRCODE_OUT_OF_MEMORY), errmsg("%s", errbuf));
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
	re2_pattern *pat = compile_arg(fcinfo, 1);
	int			 group_idx = PG_GETARG_INT32(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	re2_span	 s;

	errbuf[0] = '\0';
	s = re2_regexp_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), group_idx, errbuf, sizeof(errbuf));
	if (errbuf[0] != '\0')
		ereport(ERROR, errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf));

	PG_RETURN_DATUM(span_to_text(s));
}

PG_FUNCTION_INFO_V1(pgre2_extractgroups);
Datum
pgre2_extractgroups(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	char		 errbuf[RE2_ERRBUF_SIZE];
	int			 count;
	re2_span	*spans;
	Datum		*elems;
	ArrayType	*arr;

	errbuf[0] = '\0';
	spans = re2_extract_groups(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), &count, errbuf, sizeof(errbuf));

	if (errbuf[0] != '\0')
		ereport(ERROR, errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("%s", errbuf));

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
extractallgroups_common(FunctionCallInfo fcinfo, bool vertical, bool as_bytea)
{
	text		*haystack_va = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	const char	*hdata = VARDATA_ANY(haystack_va);
	size_t		 hlen = VARSIZE_ANY_EXHDR(haystack_va);
	char		 errbuf[RE2_ERRBUF_SIZE];
	int			 matches;
	int			 ngroups;
	re2_span	*spans;

	errbuf[0] = '\0';
	spans = re2_extract_all_groups(pat, hdata, hlen, &matches, &ngroups, errbuf, sizeof(errbuf));

	if (errbuf[0] != '\0')
		ereport(ERROR, errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("%s", errbuf));

	if (!spans || matches == 0)
		return construct_empty_array(as_bytea ? BYTEAOID : TEXTOID);

	return build_groups_2d(spans, matches, ngroups, vertical, as_bytea ? span_to_bytea : span_to_text,
						   as_bytea ? BYTEAOID : TEXTOID);
}

PG_FUNCTION_INFO_V1(pgre2_extractallgroupshorizontal);
Datum
pgre2_extractallgroupshorizontal(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(extractallgroups_common(fcinfo, false, false));
}

PG_FUNCTION_INFO_V1(pgre2_extractallgroupsvertical);
Datum
pgre2_extractallgroupsvertical(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(extractallgroups_common(fcinfo, true, false));
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
 * splitByRegexp(pattern, haystack, max_splits=0). Empty pattern splits per byte;
 * otherwise re2_split emits substrings between matches. max_splits 0 = unlimited.
 */
static ArrayType *
splitbyregexp_common(FunctionCallInfo fcinfo, bool as_bytea)
{
	text	   *pattern = PG_GETARG_TEXT_PP(0);
	text	   *haystack_va = PG_GETARG_TEXT_PP(1);
	int			max_splits = PG_NARGS() >= 3 && !PG_ARGISNULL(2) ? PG_GETARG_INT32(2) : 0;
	const char *hdata = VARDATA_ANY(haystack_va);
	size_t		hlen = VARSIZE_ANY_EXHDR(haystack_va);
	size_t		plen = VARSIZE_ANY_EXHDR(pattern);

	if (plen == 0)
		return split_chars(hdata, hlen, max_splits, as_bytea);

	{
		re2_pattern *pat = compile_arg(fcinfo, 0);
		char		 errbuf[RE2_ERRBUF_SIZE];
		int			 count;
		re2_span	*spans;
		Datum		*elems;
		ArrayType	*arr;

		errbuf[0] = '\0';
		spans = re2_split(pat, hdata, hlen, max_splits, &count, errbuf, sizeof(errbuf));
		if (errbuf[0] != '\0')
			ereport(ERROR, errcode(ERRCODE_OUT_OF_MEMORY), errmsg("%s", errbuf));

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
	PG_RETURN_ARRAYTYPE_P(splitbyregexp_common(fcinfo, false));
}

PG_FUNCTION_INFO_V1(pgre2_replaceregexpone);
Datum
pgre2_replaceregexpone(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	text		*replacement = PG_GETARG_TEXT_PP(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	void		*result;

	result = re2_replace_one(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), VARDATA_ANY(replacement),
							 VARSIZE_ANY_EXHDR(replacement), errbuf, sizeof(errbuf));
	if (!result)
		ereport(ERROR, errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf));
	PG_RETURN_TEXT_P(result);
}

PG_FUNCTION_INFO_V1(pgre2_replaceregexpall);
Datum
pgre2_replaceregexpall(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	text		*replacement = PG_GETARG_TEXT_PP(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	void		*result;

	result = re2_replace_all(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), VARDATA_ANY(replacement),
							 VARSIZE_ANY_EXHDR(replacement), errbuf, sizeof(errbuf));
	if (!result)
		ereport(ERROR, errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf));
	PG_RETURN_TEXT_P(result);
}

PG_FUNCTION_INFO_V1(pgre2_countmatches);
Datum
pgre2_countmatches(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);

	PG_RETURN_INT32(count_or_error(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_countmatchescaseinsensitive);
Datum
pgre2_countmatchescaseinsensitive(PG_FUNCTION_ARGS)
{
	text		*haystack = PG_GETARG_TEXT_PP(0);
	re2_pattern *pat = compile_arg_icase(fcinfo, 1);

	PG_RETURN_INT32(count_or_error(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

/* ---- multi-pattern helpers ---- */

/* Lookup pattern element, ereport on invalid. Pointer valid only until next cache lookup */
static re2_pattern *
lookup_pattern(re2_span s, int i)
{
	char		 errbuf[RE2_ERRBUF_SIZE];
	re2_pattern *pat = re2_cache_lookup(s.data, s.len, errbuf, sizeof(errbuf));

	if (!pat)
		ereport(ERROR, errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
				errmsg("invalid RE2 pattern at index %d: %s", i + 1, errbuf));
	return pat;
}

/* Deconstruct pattern array into spans, reject NULLs */
static re2_span *
decon_patterns(ArrayType *arr, int *npatterns)
{
	Datum	 *elems;
	bool	 *nulls;
	int		  n;
	re2_span *spans;

	deconstruct_array(arr, TEXTOID, -1, false, TYPALIGN_INT, &elems, &nulls, &n);

	spans = (re2_span *)palloc(n * sizeof(re2_span));
	for (int i = 0; i < n; i++)
	{
		text *t;

		if (nulls[i])
			ereport(ERROR, errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED), errmsg("pattern array must not contain NULLs"));
		t = DatumGetTextPP(elems[i]);
		spans[i].data = VARDATA_ANY(t);
		spans[i].len = VARSIZE_ANY_EXHDR(t);
	}
	*npatterns = n;
	return spans;
}

/*
 * Compiled RE2::Set of stable pattern array arg, kept in fn_extra like
 * compile_fn_arg. set == NULL after first call marks loop fallback
 * (set-wide compile failure), skipping retry per row.
 */
typedef struct
{
	re2_set				 *set;
	int					  nset;
	MemoryContextCallback cb;
} Re2SetFnExtra;

static void
set_fn_extra_release(void *arg)
{
	Re2SetFnExtra *fx = (Re2SetFnExtra *)arg;

	if (fx->set)
		re2_set_free(fx->set);
}

/*
 * RE2::Set over stable pattern array arg: single pass per row instead of
 * per-pattern loop, compile kept in fn_extra. Returns NULL when loop path
 * applies: non-stable arg, single pattern, or set-wide compile failure.
 * Invalid pattern raises. *npatterns always set. *spans_out NULL on fn_extra
 * hit, caller deconstructs lazily when looping.
 */
static re2_set *
multimatch_set(FunctionCallInfo fcinfo, int argnum, int *npatterns, re2_span **spans_out)
{
	FmgrInfo *flinfo = fcinfo->flinfo;
	char	  errbuf[RE2_ERRBUF_SIZE];
	int		  err_index;
	re2_span *spans;
	int		  n;

	*spans_out = NULL;
	if (flinfo && flinfo->fn_extra)
	{
		Re2SetFnExtra *fx = (Re2SetFnExtra *)flinfo->fn_extra;

		*npatterns = fx->nset;
		return fx->set;
	}

	spans = decon_patterns(PG_GETARG_ARRAYTYPE_P(argnum), &n);
	*npatterns = n;
	*spans_out = spans;

	if (n > 1 && flinfo && get_fn_expr_arg_stable(flinfo, argnum))
	{
		Re2SetFnExtra *fx = (Re2SetFnExtra *)MemoryContextAllocZero(flinfo->fn_mcxt, sizeof(Re2SetFnExtra));

		/* register before compile: set == NULL keeps callback no-op */
		fx->cb.func = set_fn_extra_release;
		fx->cb.arg = fx;
		MemoryContextRegisterResetCallback(flinfo->fn_mcxt, &fx->cb);
		fx->nset = n;
		fx->set = re2_set_new(spans, n, &err_index, errbuf, sizeof(errbuf));
		if (!fx->set && err_index >= 0)
			ereport(ERROR, errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
					errmsg("invalid RE2 pattern at index %d: %s", err_index + 1, errbuf));
		flinfo->fn_extra = fx;
		return fx->set;
	}

	return NULL;
}

/*
 * Per-pattern loop path. Validates whole array upfront so early exit still
 * errors on invalid patterns regardless of haystack. Lookup per pattern when
 * matching because cache invalidation.
 */
static int32
multimatch_loop_anyindex(const re2_span *spans, int n, const char *hdata, size_t hlen)
{
	for (int i = 0; i < n; i++)
		(void)lookup_pattern(spans[i], i);
	for (int i = 0; i < n; i++)
	{
		if (match_or_error(lookup_pattern(spans[i], i), hdata, hlen))
			return i + 1;
	}
	return 0;
}

static bool
multimatch_any(FunctionCallInfo fcinfo)
{
	text	   *haystack = PG_GETARG_TEXT_PP(0);
	const char *hdata = VARDATA_ANY(haystack);
	size_t		hlen = VARSIZE_ANY_EXHDR(haystack);
	int			n;
	re2_span   *spans;
	re2_set	   *set = multimatch_set(fcinfo, 1, &n, &spans);

	if (set)
	{
		bool failed;
		bool matched = re2_set_match_any(set, hdata, hlen, &failed);

		/* failed: DFA exceeded memory budget, retry per pattern */
		if (!failed)
			return matched;
	}

	if (!spans)
		spans = decon_patterns(PG_GETARG_ARRAYTYPE_P(1), &n);
	return multimatch_loop_anyindex(spans, n, hdata, hlen) > 0;
}

static int32
multimatch_anyindex(FunctionCallInfo fcinfo)
{
	text	   *haystack = PG_GETARG_TEXT_PP(0);
	const char *hdata = VARDATA_ANY(haystack);
	size_t		hlen = VARSIZE_ANY_EXHDR(haystack);
	int			n;
	re2_span   *spans;
	re2_set	   *set = multimatch_set(fcinfo, 1, &n, &spans);

	if (set)
	{
		bool failed;
		int	 idx = re2_set_match_min(set, hdata, hlen, &failed);

		/* failed: DFA exceeded memory budget, retry per pattern */
		if (!failed)
			return idx + 1;
	}

	if (!spans)
		spans = decon_patterns(PG_GETARG_ARRAYTYPE_P(1), &n);
	return multimatch_loop_anyindex(spans, n, hdata, hlen);
}

static ArrayType *
multimatch_allindices(FunctionCallInfo fcinfo)
{
	text	   *haystack = PG_GETARG_TEXT_PP(0);
	const char *hdata = VARDATA_ANY(haystack);
	size_t		hlen = VARSIZE_ANY_EXHDR(haystack);
	int			n;
	re2_span   *spans;
	re2_set	   *set = multimatch_set(fcinfo, 1, &n, &spans);
	Datum	   *elems = (Datum *)palloc(n * sizeof(Datum));
	int			count = -1;

	if (set)
	{
		int *ids = (int *)palloc(n * sizeof(int));
		int	 nids = re2_set_match_indices(set, hdata, hlen, ids);

		/* nids < 0: DFA exceeded memory budget, retry per pattern */
		if (nids >= 0)
		{
			count = 0;
			for (int i = 0; i < nids; i++)
				elems[count++] = Int32GetDatum(ids[i] + 1);
		}
	}

	if (count < 0)
	{
		if (!spans)
			spans = decon_patterns(PG_GETARG_ARRAYTYPE_P(1), &n);
		count = 0;
		for (int i = 0; i < n; i++)
		{
			if (match_or_error(lookup_pattern(spans[i], i), hdata, hlen))
				elems[count++] = Int32GetDatum(i + 1);
		}
	}

	return construct_array(elems, count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
}

PG_FUNCTION_INFO_V1(pgre2_multimatchany);
Datum
pgre2_multimatchany(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(multimatch_any(fcinfo));
}

PG_FUNCTION_INFO_V1(pgre2_multimatchanyindex);
Datum
pgre2_multimatchanyindex(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(multimatch_anyindex(fcinfo));
}

PG_FUNCTION_INFO_V1(pgre2_multimatchallindices);
Datum
pgre2_multimatchallindices(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(multimatch_allindices(fcinfo));
}

/* ==== bytea overloads ==== */

PG_FUNCTION_INFO_V1(pgre2_match_bytea);
Datum
pgre2_match_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);

	PG_RETURN_BOOL(match_or_error(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_extract_bytea);
Datum
pgre2_extract_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	char		 errbuf[RE2_ERRBUF_SIZE];
	re2_span	 s;

	s = re2_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), errbuf, sizeof(errbuf));
	if (errbuf[0] != '\0')
		ereport(ERROR, errcode(ERRCODE_OUT_OF_MEMORY), errmsg("%s", errbuf));
	PG_RETURN_DATUM(span_to_bytea(s));
}

PG_FUNCTION_INFO_V1(pgre2_extractall_bytea);
Datum
pgre2_extractall_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
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
			ereport(ERROR, errcode(ERRCODE_OUT_OF_MEMORY), errmsg("%s", errbuf));
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
	re2_pattern *pat = compile_arg(fcinfo, 1);
	int			 group_idx = PG_GETARG_INT32(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	re2_span	 s;

	errbuf[0] = '\0';
	s = re2_regexp_extract(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), group_idx, errbuf, sizeof(errbuf));
	if (errbuf[0] != '\0')
		ereport(ERROR, errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf));

	PG_RETURN_DATUM(span_to_bytea(s));
}

PG_FUNCTION_INFO_V1(pgre2_extractgroups_bytea);
Datum
pgre2_extractgroups_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	char		 errbuf[RE2_ERRBUF_SIZE];
	int			 count;
	re2_span	*spans;
	Datum		*elems;
	ArrayType	*arr;

	errbuf[0] = '\0';
	spans = re2_extract_groups(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), &count, errbuf, sizeof(errbuf));

	if (errbuf[0] != '\0')
		ereport(ERROR, errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("%s", errbuf));

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
	PG_RETURN_ARRAYTYPE_P(extractallgroups_common(fcinfo, false, true));
}

PG_FUNCTION_INFO_V1(pgre2_extractallgroupsvertical_bytea);
Datum
pgre2_extractallgroupsvertical_bytea(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(extractallgroups_common(fcinfo, true, true));
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
	PG_RETURN_ARRAYTYPE_P(splitbyregexp_common(fcinfo, true));
}

PG_FUNCTION_INFO_V1(pgre2_replaceregexpone_bytea);
Datum
pgre2_replaceregexpone_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	text		*replacement = PG_GETARG_TEXT_PP(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	void		*result;

	result = re2_replace_one(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), VARDATA_ANY(replacement),
							 VARSIZE_ANY_EXHDR(replacement), errbuf, sizeof(errbuf));
	if (!result)
		ereport(ERROR, errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf));
	PG_RETURN_BYTEA_P(result);
}

PG_FUNCTION_INFO_V1(pgre2_replaceregexpall_bytea);
Datum
pgre2_replaceregexpall_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);
	text		*replacement = PG_GETARG_TEXT_PP(2);
	char		 errbuf[RE2_ERRBUF_SIZE];
	void		*result;

	result = re2_replace_all(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack), VARDATA_ANY(replacement),
							 VARSIZE_ANY_EXHDR(replacement), errbuf, sizeof(errbuf));
	if (!result)
		ereport(ERROR, errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("%s", errbuf));
	PG_RETURN_BYTEA_P(result);
}

PG_FUNCTION_INFO_V1(pgre2_countmatches_bytea);
Datum
pgre2_countmatches_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg(fcinfo, 1);

	PG_RETURN_INT32(count_or_error(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_countmatchescaseinsensitive_bytea);
Datum
pgre2_countmatchescaseinsensitive_bytea(PG_FUNCTION_ARGS)
{
	bytea		*haystack = PG_GETARG_BYTEA_PP(0);
	re2_pattern *pat = compile_arg_icase(fcinfo, 1);

	PG_RETURN_INT32(count_or_error(pat, VARDATA_ANY(haystack), VARSIZE_ANY_EXHDR(haystack)));
}

PG_FUNCTION_INFO_V1(pgre2_multimatchany_bytea);
Datum
pgre2_multimatchany_bytea(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(multimatch_any(fcinfo));
}

PG_FUNCTION_INFO_V1(pgre2_multimatchanyindex_bytea);
Datum
pgre2_multimatchanyindex_bytea(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(multimatch_anyindex(fcinfo));
}

PG_FUNCTION_INFO_V1(pgre2_multimatchallindices_bytea);
Datum
pgre2_multimatchallindices_bytea(PG_FUNCTION_ARGS)
{
	PG_RETURN_ARRAYTYPE_P(multimatch_allindices(fcinfo));
}

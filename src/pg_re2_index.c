/*
 * Index support for re2match:
 *  - planner support function turning re2match(col, '^literal...') into a
 *    b-tree range scan (via RE2::PossibleMatchRange), plus matchingsel-grade
 *    selectivity for bare re2match() calls.
 *  - GIN opclass gin_re2_ops driving a trigram prefilter from RE2::FilteredRE2.
 */
#include "postgres.h"

#if PG_VERSION_NUM >= 160000
#include "varatt.h" /* VARDATA_ANY/VARSIZE_ANY_EXHDR split from postgres.h in PG16 */
#endif

#include "access/gin.h"
#include "catalog/pg_operator_d.h"
#include "catalog/pg_opfamily_d.h"
#include "catalog/pg_type_d.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "nodes/makefuncs.h"
#include "nodes/supportnodes.h"
#include "port/pg_bitutils.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/palloc.h"
#include "utils/pg_locale.h"
#include "utils/selfuncs.h"
#include "utils/syscache.h"

#include "re2_cache.h"
#include "re2_wrapper.h"

/*
 * True only when every RE2 match must start at byte 0: a required leading '^'
 * with no top-level alternation, quantified anchor, inline flag group or
 * quoted-literal span. False merely skips the optimization, so anything not
 * modeled bails. Soundness only matters for RE2-valid patterns:
 * re2_possible_prefix rejects the rest right after. RE2 lacks API for this.
 */
static bool
start_anchored(const char *p, size_t len)
{
	int	 depth = 0;
	bool in_class = false;

	if (len == 0 || p[0] != '^')
		return false;
	/* quantifier makes the anchor optional/repeated: ^?abc matches "xabc" */
	if (len > 1 && (p[1] == '?' || p[1] == '*' || p[1] == '{'))
		return false;

	for (size_t i = 1; i < len; i++)
	{
		char c = p[i];

		if (c == '\\')
		{
			/* \Q..\E quotes metachars, hiding structure from this scan */
			if (i + 1 < len && p[i + 1] == 'Q')
				return false;
			/*
			 * Skip one escaped byte: longer escape tails (\x41, \x{7c},
			 * \123, \p{L}) contain no byte tracked below, and an escaped
			 * literal '|' or '(' never appears as its raw byte.
			 */
			i++;
			continue;
		}
		if (in_class)
		{
			if (c == '[' && i + 1 < len && p[i + 1] == ':')
			{
				/*
				 * POSIX class: RE2 scans for the first ":]" through end of
				 * pattern (parse.cc ParseCCName); when absent the '[' is a
				 * literal member and the class ends elsewhere, so bail.
				 */
				size_t j = i + 2;

				while (j + 1 < len && !(p[j] == ':' && p[j + 1] == ']'))
					j++;
				if (j + 1 >= len)
					return false;
				i = j + 1;
			}
			else if (c == ']')
				in_class = false;
			/* any other byte, including a bare '[', is a member */
			continue;
		}
		switch (c)
		{
			case '[':
				in_class = true;
				/* '^' negates, then a first ']' is a member, not the end */
				if (i + 1 < len && p[i + 1] == '^')
					i++;
				if (i + 1 < len && p[i + 1] == ']')
					i++;
				break;
			case '(':
				if (i + 1 < len && p[i + 1] == '?')
				{
					/* bare '(?:' is a plain group; flag forms ((?i), (?m:) bail:
					 * treating a flag-only '(?i)' as a group would miscount depth */
					if (i + 2 >= len || p[i + 2] != ':')
						return false;
					i += 2;
				}
				depth++;
				break;
			case ')':
				if (depth > 0)
					depth--;
				break;
			case '|':
				if (depth == 0)
					return false; /* top-level alternation de-anchors */
				break;
			default:
				break;
		}
	}
	return !in_class;
}

/*
 * Smallest string strictly greater than every string starting with prefix,
 * built by incrementing the rightmost ASCII byte and truncating. Restricting
 * to ASCII keeps the result valid in any server encoding. Returns length, or
 * 0 when no upper bound is representable (caller emits only the lower bound).
 */
static size_t
prefix_greater(const char *prefix, size_t len, char *out)
{
	for (size_t i = len; i > 0; i--)
	{
		unsigned char c = (unsigned char)prefix[i - 1];

		if (c >= 0x01 && c < 0x7f)
		{
			memcpy(out, prefix, i);
			out[i - 1] = (char)(c + 1);
			return i;
		}
	}
	return 0;
}

static Const *
text_const(const char *data, size_t len, Oid collation)
{
	text *t = cstring_to_text_with_len(data, (int)len);

	return makeConst(TEXTOID, -1, collation, -1, PointerGetDatum(t), false, false);
}

/* collation sorts bytewise (C, POSIX, builtin C/C.UTF8, C-locale default),
 * cf. like_support.c match_pattern_prefix; lc_collate_is_c removed in PG18 */
static bool
collation_is_c(Oid collation)
{
#if PG_VERSION_NUM >= 180000
	return pg_newlocale_from_collation(collation)->collate_is_c;
#else
	return lc_collate_is_c(collation);
#endif
}

static List *
match_re2_prefix(SupportRequestIndexCondition *req)
{
	Node		*node = req->node;
	List		*args;
	Node		*leftop;
	Node		*rightop;
	Const		*patt;
	text		*pt;
	re2_pattern *pat;
	Oid			 geopr;
	Oid			 ltopr;
	char		 errbuf[RE2_ERRBUF_SIZE];
	char		 prefix[256];
	char		 greater[256];
	size_t		 prefixlen;
	size_t		 greaterlen;
	List		*result;

	if (IsA(node, OpExpr))
		args = ((OpExpr *)node)->args;
	else if (IsA(node, FuncExpr))
		args = ((FuncExpr *)node)->args;
	else
		return NIL;

	if (list_length(args) != 2 || req->indexarg != 0)
		return NIL;

	leftop = linitial(args);
	rightop = lsecond(args);

	if (!IsA(rightop, Const))
		return NIL;
	patt = (Const *)rightop;
	if (patt->constisnull || patt->consttype != TEXTOID)
		return NIL;

	/* Byte-ordered range only sound with bytewise-comparing operators. */
	if (req->opfamily == TEXT_PATTERN_BTREE_FAM_OID)
	{
		geopr = TextPatternGreaterEqualOperator;
		ltopr = TextPatternLessOperator;
	}
	else if (req->opfamily == TEXT_BTREE_FAM_OID && collation_is_c(req->indexcollation))
	{
		geopr = TextGreaterEqualOperator;
		ltopr = TextLessOperator;
	}
	else
		return NIL;

	pt = DatumGetTextPP(patt->constvalue);
	if (!start_anchored(VARDATA_ANY(pt), VARSIZE_ANY_EXHDR(pt)))
		return NIL;

	/* backend cache spares a compile per candidate path per (re)plan;
	 * invalid pattern skips the rewrite and errors at execution instead */
	pat = re2_cache_lookup(VARDATA_ANY(pt), VARSIZE_ANY_EXHDR(pt), errbuf, sizeof(errbuf));
	if (!pat)
		return NIL;
	if (!re2_possible_prefix(pat, prefix, sizeof(prefix), &prefixlen))
		return NIL;

	/*
	 * Longest common prefix can split multibyte character, which is invalid.
	 * prefix_greater avoids splitting multibyte character.
	 */
	prefixlen = pg_mbcliplen(prefix, (int)prefixlen, (int)prefixlen);
	if (prefixlen == 0)
		return NIL;

	result = list_make1(make_opclause(geopr, BOOLOID, false, (Expr *)leftop,
									  (Expr *)text_const(prefix, prefixlen, req->indexcollation), InvalidOid,
									  req->indexcollation));

	greaterlen = prefix_greater(prefix, prefixlen, greater);
	if (greaterlen > 0)
		result = lappend(result, make_opclause(ltopr, BOOLOID, false, (Expr *)leftop,
											   (Expr *)text_const(greater, greaterlen, req->indexcollation), InvalidOid,
											   req->indexcollation));

	req->lossy = true; /* range only bounds matches; re2match still rechecked */
	return result;
}

/*
 * matchingsel-grade estimate for bare re2match() in a qual, replacing the flat
 * 0.3333 boolean-function default. Operator form (@~) never reaches here, its
 * RESTRICT estimator is consulted instead. generic_restriction_selectivity
 * runs the sampled column values through an operator's opcode; look up @~ in
 * re2match's own schema to hand it re2match itself.
 */
static SupportRequestSelectivity *
re2match_selectivity(SupportRequestSelectivity *req)
{
	if (req->is_join)
		req->selectivity = DEFAULT_MATCHING_SEL; /* cf. matchingjoinsel */
	else
	{
		Oid opr = GetSysCacheOid4(OPERNAMENSP, Anum_pg_operator_oid, CStringGetDatum("@~"), ObjectIdGetDatum(TEXTOID),
								  ObjectIdGetDatum(TEXTOID), ObjectIdGetDatum(get_func_namespace(req->funcid)));

		if (!OidIsValid(opr))
			return NULL;
		req->selectivity = generic_restriction_selectivity(req->root, opr, req->inputcollid, req->args, req->varRelid,
														   DEFAULT_MATCHING_SEL);
	}
	return req;
}

PG_FUNCTION_INFO_V1(pgre2_match_support);
Datum
pgre2_match_support(PG_FUNCTION_ARGS)
{
	Node *rawreq = (Node *)PG_GETARG_POINTER(0);

	if (IsA(rawreq, SupportRequestIndexCondition))
		PG_RETURN_POINTER(match_re2_prefix((SupportRequestIndexCondition *)rawreq));
	if (IsA(rawreq, SupportRequestSelectivity))
		PG_RETURN_POINTER(re2match_selectivity((SupportRequestSelectivity *)rawreq));

	PG_RETURN_POINTER(NULL);
}

/* Query-scope state shared from extract_query to consistent via extra_data. */
typedef struct
{
	re2_filter *filter;
	int			natoms;
	int		   *atom_off;  /* natoms+1 offsets into atom_keys */
	int		   *atom_keys; /* per-atom entry indices (into the entries array) */
	int		   *present;   /* consistent-check scratch, natoms entries */
} Re2GinQuery;

static inline unsigned char
lc(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

static inline int32
pack_trgm(const unsigned char *b)
{
	return (int32)(((uint32)lc(b[0]) << 16) | ((uint32)lc(b[1]) << 8) | lc(b[2]));
}

/* free extract_query filter when GIN scan's key context resets; arg NULL
 * until filter exists */
static void
re2_gin_cleanup(void *arg)
{
	if (arg)
		re2_filter_free((re2_filter *)arg);
}

/*
 * RE2 lowercases FilteredRE2 atoms with Unicode simple case folding, but
 * extract_value/pack_trgm fold ASCII only; a cased non-ASCII letter therefore
 * produces divergent trigrams on the two sides. Detect any non-ASCII byte so
 * the query path can fall back to a full scan instead of dropping matches.
 */
static bool
has_nonascii(text *t)
{
	const unsigned char *s = (const unsigned char *)VARDATA_ANY(t);
	int					 len = VARSIZE_ANY_EXHDR(t);

	for (int i = 0; i < len; i++)
		if (s[i] >= 0x80)
			return true;
	return false;
}

/*
 * Reverse of has_nonascii: an all-ASCII pattern can still compile to non-ASCII
 * atom bytes via escapes (\xC4, \x{...}) or a folded literal, and RE2 lowercased
 * those with Unicode folding. The ASCII-folded index trigrams would then miss
 * matching rows, so scan the compiled atoms and fall back on any high byte.
 */
static bool
atoms_have_nonascii(const re2_filter *f, int natoms)
{
	for (int a = 0; a < natoms; a++)
	{
		re2_span			 atom = re2_filter_atom(f, a);
		const unsigned char *s = (const unsigned char *)atom.data;

		for (size_t i = 0; i < atom.len; i++)
			if (s[i] >= 0x80)
				return true;
	}
	return false;
}

PG_FUNCTION_INFO_V1(gin_re2_extract_value);
Datum
gin_re2_extract_value(PG_FUNCTION_ARGS)
{
	text				*val = PG_GETARG_TEXT_PP(0);
	int32				*nentries = (int32 *)PG_GETARG_POINTER(1);
	const unsigned char *s = (const unsigned char *)VARDATA_ANY(val);
	int					 len = VARSIZE_ANY_EXHDR(val);
	Datum				*entries;
	int					 n;
	int					 m = 0;

	*nentries = 0;
	if (len < 3)
		PG_RETURN_POINTER(NULL);

	n = len - 2;
	if (n < 8192) /* tuning cutoff: below it qsort beats fixed bitmap cost */
	{
		int32 *trg = (int32 *)palloc(n * sizeof(int32));

		for (int i = 0; i < n; i++)
			trg[i] = pack_trgm(s + i);

		qsort(trg, n, sizeof(int32), cmp_int32);

		for (int i = 0; i < n; i++)
			if (i == 0 || trg[i] != trg[i - 1])
				trg[m++] = trg[i];

		entries = (Datum *)palloc(m * sizeof(Datum));
		for (int i = 0; i < m; i++)
			entries[i] = Int32GetDatum(trg[i]);
	}
	else
	{
		/*
		 * Bound memory on huge values: packed trigrams fit 24 bits, so a 2MB
		 * bitmap dedups without n-sized arrays (n * 8 for entries overflowed
		 * MaxAllocSize past ~134MB of text, n * 4 for trg past ~268MB) and an
		 * ascending walk emits entries sorted for free.
		 */
		uint64 *seen = (uint64 *)palloc0(((size_t)1 << 24) / 8);
		int		e = 0;

		for (int i = 0; i < n; i++)
		{
			int32 v = pack_trgm(s + i);

			seen[v >> 6] |= UINT64CONST(1) << (v & 63);
		}

		for (int w = 0; w < (1 << 24) / 64; w++)
			m += pg_popcount64(seen[w]);

		entries = (Datum *)palloc(m * sizeof(Datum));
		for (int w = 0; w < (1 << 24) / 64; w++)
		{
			uint64 bits = seen[w];

			while (bits)
			{
				entries[e++] = Int32GetDatum((int32)(w << 6 | pg_rightmost_one_pos64(bits)));
				bits &= bits - 1;
			}
		}
		pfree(seen);
	}

	*nentries = m;
	PG_RETURN_POINTER(entries);
}

PG_FUNCTION_INFO_V1(gin_re2_extract_query);
Datum
gin_re2_extract_query(PG_FUNCTION_ARGS)
{
	text				  *pattern = PG_GETARG_TEXT_PP(0);
	int32				  *nentries = (int32 *)PG_GETARG_POINTER(1);
	Pointer				 **extra_data = (Pointer **)PG_GETARG_POINTER(4);
	int32				  *searchMode = (int32 *)PG_GETARG_POINTER(6);
	char				   errbuf[RE2_ERRBUF_SIZE];
	re2_filter			  *f;
	int					   natoms;
	int32				  *all;
	int32				  *keys;
	int					  *flat;
	int					  *atom_off;
	int					   nkeys = 0;
	int					   flatn = 0;
	int					   total = 0;
	Datum				  *entries;
	Re2GinQuery			  *q;
	MemoryContextCallback *cb;

	*nentries = 0;

	/*
	 * Register cleanup before compiling so a longjmp from any palloc below
	 * frees the filter via context reset rather than leaking the C++ heap
	 * object. cb->arg stays NULL until the filter exists (cleanup no-ops).
	 */
	cb = (MemoryContextCallback *)palloc0(sizeof(MemoryContextCallback));
	cb->func = re2_gin_cleanup;
	MemoryContextRegisterResetCallback(CurrentMemoryContext, cb);

	f = re2_filter_new(VARDATA_ANY(pattern), VARSIZE_ANY_EXHDR(pattern), errbuf, sizeof(errbuf));
	if (!f)
		ereport(ERROR, errcode(ERRCODE_INVALID_REGULAR_EXPRESSION), errmsg("invalid RE2 pattern: %s", errbuf));
	cb->arg = f;

	natoms = re2_filter_num_atoms(f);

	/*
	 * Scan everything (recheck stays exact) when the pattern cannot be usefully
	 * prefiltered: no required atom, matchable with none present, or a non-ASCII
	 * byte on either side (pattern text or a compiled atom) whose Unicode-folded
	 * atom trigrams would miss the ASCII-folded index entries. The two byte
	 * scans cover disjoint cases; see has_nonascii, atoms_have_nonascii.
	 *
	 * Neither scan fires when folding matters only text-side. RE2's Unicode
	 * folding lets an all-ASCII (?i) pattern match long s (U+017F) or Kelvin sign
	 * (U+212A) text while compiling to all-ASCII atoms, so such rows are skipped.
	 * Fix would need extract_value to also emit trigrams with those code
	 * points folded to s/k.
	 */
	if (natoms == 0 || re2_filter_passes(f, NULL, 0) || has_nonascii(pattern) || atoms_have_nonascii(f, natoms))
	{
		re2_filter_free(f);
		cb->arg = NULL;
		*searchMode = GIN_SEARCH_MODE_ALL;
		PG_RETURN_POINTER(NULL);
	}

	for (int a = 0; a < natoms; a++)
	{
		re2_span atom = re2_filter_atom(f, a);

		if (atom.len >= 3)
			total += (int)atom.len - 2;
	}

	/* No usable trigram keys: default mode would wrongly return nothing. */
	if (total == 0)
	{
		re2_filter_free(f);
		cb->arg = NULL;
		*searchMode = GIN_SEARCH_MODE_ALL;
		PG_RETURN_POINTER(NULL);
	}

	/* every trigram in atom order, then sorted+uniqued into keys */
	all = (int32 *)palloc(total * sizeof(int32));
	for (int a = 0, pos = 0; a < natoms; a++)
	{
		re2_span			 atom = re2_filter_atom(f, a);
		const unsigned char *as = (const unsigned char *)atom.data;

		for (int i = 0; i + 3 <= (int)atom.len; i++)
			all[pos++] = pack_trgm(as + i);
	}

	keys = (int32 *)palloc(total * sizeof(int32));
	memcpy(keys, all, total * sizeof(int32));
	qsort(keys, total, sizeof(int32), cmp_int32);
	for (int i = 0; i < total; i++)
		if (i == 0 || keys[i] != keys[i - 1])
			keys[nkeys++] = keys[i];

	/* per atom: map trigrams to key indices, dedup within the atom's slice */
	flat = (int *)palloc(total * sizeof(int));
	atom_off = (int *)palloc((natoms + 1) * sizeof(int));
	flatn = 0;
	for (int a = 0, pos = 0; a < natoms; a++)
	{
		re2_span atom = re2_filter_atom(f, a);
		int		 cnt = atom.len >= 3 ? (int)atom.len - 2 : 0;
		int		 uniq;

		atom_off[a] = flatn;
		for (int i = 0; i < cnt; i++, pos++)
		{
			int32 *hit = (int32 *)bsearch(&all[pos], keys, nkeys, sizeof(int32), cmp_int32);

			flat[flatn++] = (int)(hit - keys);
		}
		qsort(flat + atom_off[a], flatn - atom_off[a], sizeof(int), cmp_int32);
		uniq = atom_off[a];
		for (int k = atom_off[a]; k < flatn; k++)
			if (k == atom_off[a] || flat[k] != flat[k - 1])
				flat[uniq++] = flat[k];
		flatn = uniq;
	}
	atom_off[natoms] = flatn;

	entries = (Datum *)palloc(nkeys * sizeof(Datum));
	for (int j = 0; j < nkeys; j++)
		entries[j] = Int32GetDatum(keys[j]);
	*nentries = nkeys;

	q = (Re2GinQuery *)palloc(sizeof(Re2GinQuery));
	q->filter = f;
	q->natoms = natoms;
	q->atom_off = atom_off;
	q->atom_keys = flat;
	q->present = (int *)palloc(natoms * sizeof(int));

	*extra_data = (Pointer *)palloc(nkeys * sizeof(Pointer));
	for (int j = 0; j < nkeys; j++)
		(*extra_data)[j] = (Pointer)q;

	PG_RETURN_POINTER(entries);
}

PG_FUNCTION_INFO_V1(gin_re2_consistent);
Datum
gin_re2_consistent(PG_FUNCTION_ARGS)
{
	bool	*check = (bool *)PG_GETARG_POINTER(0);
	int32	 nkeys = PG_GETARG_INT32(3);
	Pointer *extra_data = (Pointer *)PG_GETARG_POINTER(4);
	bool	*recheck = (bool *)PG_GETARG_POINTER(5);
	bool	 res;

	*recheck = true; /* trigram prefilter is always lossy */

	if (nkeys < 1)
		res = true;
	else
	{
		Re2GinQuery *q = (Re2GinQuery *)extra_data[0];
		int			 np = 0;

		for (int a = 0; a < q->natoms; a++)
		{
			bool all = true;

			for (int k = q->atom_off[a]; k < q->atom_off[a + 1]; k++)
				if (!check[q->atom_keys[k]])
				{
					all = false;
					break;
				}
			if (all)
				q->present[np++] = a;
		}
		res = re2_filter_passes(q->filter, q->present, np);
	}

	PG_RETURN_BOOL(res);
}

/*
 * Tri-state consistent enables GIN fast scan: without it GIN wraps the binary
 * consistent in a shim that bails to MAYBE past 4 unknown keys, defeating the
 * skip that lets a rare atom avoid scanning common atoms' huge posting lists.
 *
 * The prefilter is monotone in the atom set (an AND-OR tree of required
 * substrings), so treat every MAYBE key as possibly present: if the pattern
 * cannot match even under that maximal atom set the item is safely skipped.
 * A pass is always MAYBE, never TRUE: the trigram prefilter stays lossy and
 * the heap recheck (re2match) restores exactness.
 */
PG_FUNCTION_INFO_V1(gin_re2_triconsistent);
Datum
gin_re2_triconsistent(PG_FUNCTION_ARGS)
{
	GinTernaryValue *check = (GinTernaryValue *)PG_GETARG_POINTER(0);
	int32			 nkeys = PG_GETARG_INT32(3);
	Pointer			*extra_data = (Pointer *)PG_GETARG_POINTER(4);
	GinTernaryValue	 res;

	if (nkeys < 1)
		res = GIN_MAYBE; /* everything-scan: candidate, recheck */
	else
	{
		Re2GinQuery *q = (Re2GinQuery *)extra_data[0];
		int			 np = 0;

		for (int a = 0; a < q->natoms; a++)
		{
			bool possible = true;

			for (int k = q->atom_off[a]; k < q->atom_off[a + 1]; k++)
				if (check[q->atom_keys[k]] == GIN_FALSE)
				{
					possible = false;
					break;
				}
			if (possible)
				q->present[np++] = a;
		}
		res = re2_filter_passes(q->filter, q->present, np) ? GIN_MAYBE : GIN_FALSE;
	}

	PG_RETURN_GIN_TERNARY_VALUE(res);
}

#ifndef RE2_WRAPPER_H
#define RE2_WRAPPER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

#define RE2_ERRBUF_SIZE 128

	/* qsort comparator for int/int32 keys, shared across TUs */
	static inline int
	cmp_int32(const void *a, const void *b)
	{
		int x = *(const int *)a;
		int y = *(const int *)b;

		return (x > y) - (x < y);
	}

	/* span into haystack, no allocation */
	typedef struct
	{
		const char *data; /* NULL if no match */
		size_t		len;
	} re2_span;

	typedef struct re2_pattern re2_pattern;

	re2_pattern *re2_compile(const char *pattern, size_t pattern_len, char *errbuf, size_t errbuf_size);
	void		 re2_free(re2_pattern *pat);

	/* *failed set on OOM during match, NFA/BitState fallback matchers allocate */
	bool re2_match(const re2_pattern *pat, const char *text, size_t text_len, bool *failed);

	/* empty span when no match, OOM reported via errbuf */
	re2_span re2_extract(const re2_pattern *pat, const char *text, size_t text_len, char *errbuf, size_t errbuf_size);

	re2_span *re2_extract_all(const re2_pattern *pat, const char *text, size_t text_len, int *count, char *errbuf,
							  size_t errbuf_size);

	re2_span re2_regexp_extract(const re2_pattern *pat, const char *text, size_t text_len, int group_idx, char *errbuf,
								size_t errbuf_size);

	re2_span *re2_extract_groups(const re2_pattern *pat, const char *text, size_t text_len, int *count, char *errbuf,
								 size_t errbuf_size);

	/*
	 * Returns flat row-major array of match_count * ngroups spans (group #0 excluded).
	 * Sets *match_count and *ngroups_out. On error sets errbuf, returns NULL.
	 * If pattern has no capture groups, sets errbuf accordingly.
	 */
	re2_span *re2_extract_all_groups(const re2_pattern *pat, const char *text, size_t text_len, int *match_count,
									 int *ngroups_out, char *errbuf, size_t errbuf_size);

	/*
	 * Splits text by pattern. Returns spans of substrings between matches.
	 * If max_splits > 0, caps the number of returned tokens at max_splits.
	 */
	re2_span *re2_split(const re2_pattern *pat, const char *text, size_t text_len, int max_splits, int *count,
						char *errbuf, size_t errbuf_size);

	/* returns palloc'd varlena, caller casts to text* or bytea* */
	void *re2_replace_one(const re2_pattern *pat, const char *text, size_t text_len, const char *repl, size_t repl_len,
						  char *errbuf, size_t errbuf_size);

	void *re2_replace_all(const re2_pattern *pat, const char *text, size_t text_len, const char *repl, size_t repl_len,
						  char *errbuf, size_t errbuf_size);

	/* -1 on OOM */
	int re2_count_matches(const re2_pattern *pat, const char *text, size_t text_len);

	/* RE2::Set: patterns compiled into one automaton, single pass per row */
	typedef struct re2_set re2_set;

	/* NULL on failure; *err_index = 0-based failing pattern, or -1 for set-wide compile failure */
	re2_set *re2_set_new(const re2_span *patterns, int npatterns, int *err_index, char *errbuf, size_t errbuf_size);
	void	 re2_set_free(re2_set *set);

	/* *failed set when DFA exceeds memory budget mid-match, caller must fall back to per-pattern loop */
	bool re2_set_match_any(const re2_set *set, const char *text, size_t text_len, bool *failed);

	/* fills indices (0-based ascending, capacity >= npatterns), returns count, -1 on DFA failure or OOM */
	int re2_set_match_indices(const re2_set *set, const char *text, size_t text_len, int *indices);

	/* lowest matched index (0-based), -1 when none; *failed as in re2_set_match_any */
	int re2_set_match_min(const re2_set *set, const char *text, size_t text_len, bool *failed);

	/*
	 * Fixed byte prefix shared by every anchored match, derived from
	 * RE2::PossibleMatchRange (longest common prefix of its min/max bounds).
	 * Writes up to outcap bytes, sets *outlen. Returns false when no nonempty
	 * prefix exists. Caller is responsible for the anchoring precondition (see
	 * pg_re2_index.c): only sound for start-anchored patterns.
	 */
	bool re2_possible_prefix(const re2_pattern *pat, char *out, size_t outcap, size_t *outlen);

	/*
	 * FilteredRE2 wrapper: extracts required literal atoms (>= 3 bytes,
	 * lowercased) from a single pattern for use as an inverted-index prefilter.
	 */
	typedef struct re2_filter re2_filter;

	re2_filter *re2_filter_new(const char *pattern, size_t pattern_len, char *errbuf, size_t errbuf_size);
	void		re2_filter_free(re2_filter *f);
	int			re2_filter_num_atoms(const re2_filter *f);
	re2_span	re2_filter_atom(const re2_filter *f, int i);

	/*
	 * Given the sorted-ascending indices of atoms found present in a candidate,
	 * does the pattern's prefilter still permit a match? present may be NULL
	 * when n_present is 0 (probes whether the pattern can match with no atoms).
	 * Not const: reuses scratch buffers on f across calls (single-threaded).
	 */
	bool re2_filter_passes(re2_filter *f, const int *present, int n_present);

#ifdef __cplusplus
}
#endif

#endif /* RE2_WRAPPER_H */

#ifndef RE2_WRAPPER_H
#define RE2_WRAPPER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

#define RE2_ERRBUF_SIZE 64

	/* span into haystack, no allocation */
	typedef struct
	{
		const char *data; /* NULL if no match */
		size_t		len;
	} re2_span;

	typedef struct re2_pattern re2_pattern;

	re2_pattern *re2_compile(const char *pattern, size_t pattern_len, char *errbuf, size_t errbuf_size);
	void		 re2_free(re2_pattern *pat);
	int			 re2_num_captures(const re2_pattern *pat);

	bool re2_match(const re2_pattern *pat, const char *text, size_t text_len);

	re2_span re2_extract(const re2_pattern *pat, const char *text, size_t text_len);

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

	int re2_count_matches(const re2_pattern *pat, const char *text, size_t text_len);

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

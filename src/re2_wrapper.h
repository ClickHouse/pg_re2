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

#ifdef __cplusplus
}
#endif

#endif /* RE2_WRAPPER_H */

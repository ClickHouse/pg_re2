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
		int			len;
	} re2_span;

	typedef struct re2_pattern re2_pattern;

	re2_pattern *re2_compile(const char *pattern, int pattern_len, char *errbuf, size_t errbuf_size);
	void		 re2_free(re2_pattern *pat);
	int			 re2_num_captures(const re2_pattern *pat);

	bool re2_match(const re2_pattern *pat, const char *text, int text_len);

	re2_span re2_extract(const re2_pattern *pat, const char *text, int text_len);

	re2_span *re2_extract_all(const re2_pattern *pat, const char *text, int text_len, int *count, char *errbuf,
							  size_t errbuf_size);

	re2_span re2_regexp_extract(const re2_pattern *pat, const char *text, int text_len, int group_idx, char *errbuf,
								size_t errbuf_size);

	re2_span *re2_extract_groups(const re2_pattern *pat, const char *text, int text_len, int *count, char *errbuf,
								 size_t errbuf_size);

	/* returns palloc'd varlena, caller casts to text* or bytea* */
	void *re2_replace_one(const re2_pattern *pat, const char *text, int text_len, const char *repl, int repl_len,
						  char *errbuf, size_t errbuf_size);

	void *re2_replace_all(const re2_pattern *pat, const char *text, int text_len, const char *repl, int repl_len,
						  char *errbuf, size_t errbuf_size);

	int re2_count_matches(const re2_pattern *pat, const char *text, int text_len);

#ifdef __cplusplus
}
#endif

#endif /* RE2_WRAPPER_H */

#ifndef RE2_UTIL_H
#define RE2_UTIL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

	/* Growable varlena built in place: VARHDRSZ header followed by data region. */
	typedef struct
	{
		char  *base; /* palloc'd VARHDRSZ + cap */
		size_t cap;	 /* capacity of data region */
		size_t len;	 /* used data bytes */
	} varlena_out;

	/* returns false + fills errbuf on OOM */
	bool varlena_out_append(varlena_out *out, const char *s, size_t n, char *errbuf, size_t errbuf_size);

	/* palloc'd varlena on success, NULL on OOM (errbuf filled) */
	void *varlena_out_finish(varlena_out *out, char *errbuf, size_t errbuf_size);

	/* bytes to consume on empty match adjacent to prior match; mirrors RE2
	 * GlobalReplace UTF-8 rune skip (util/utf.h fullrune + chartorune). Caller
	 * guarantees p < ep so lead byte is readable. */
	size_t empty_match_advance(const char *p, const char *ep);

	/* reject \N backrefs in replacement that exceed ngroups; errbuf filled on failure */
	bool validate_rewrite(int ngroups, const char *repl, size_t repl_len, char *errbuf, size_t errbuf_size);

#ifdef __cplusplus
}
#endif

#endif /* RE2_UTIL_H */

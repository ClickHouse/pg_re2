#ifndef RE2_CACHE_H
#define RE2_CACHE_H

#include "re2_wrapper.h"

/*
 * Look up or compile a pattern. Returns cached re2_pattern.
 * On compile error, returns NULL, errbuf filled.
 */
re2_pattern *re2_cache_lookup(const char *pattern, size_t pattern_len, char *errbuf, size_t errbuf_size);

#endif /* RE2_CACHE_H */

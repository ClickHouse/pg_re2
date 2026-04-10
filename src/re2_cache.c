#include "postgres.h"

#include "common/hashfn.h"
#include "re2_cache.h"

#define RE2_CACHE_SIZE 1000

typedef struct
{
	char		*pattern; /* malloc'd copy */
	size_t		 pattern_len;
	re2_pattern *compiled;
} Re2CacheEntry;

static Re2CacheEntry cache[RE2_CACHE_SIZE];

re2_pattern *
re2_cache_lookup(const char *pattern, size_t pattern_len, char *errbuf, size_t errbuf_size)
{
	uint32		   h = hash_bytes((const unsigned char *)pattern, (int)pattern_len);
	int			   bucket = h % RE2_CACHE_SIZE;
	Re2CacheEntry *entry = &cache[bucket];

	/* cache hit: verify full pattern match */
	if (entry->compiled && entry->pattern_len == pattern_len && memcmp(entry->pattern, pattern, pattern_len) == 0)
	{
		return entry->compiled;
	}

	/* evict old entry */
	if (entry->compiled)
	{
		re2_free(entry->compiled);
		free(entry->pattern);
		entry->compiled = NULL;
		entry->pattern = NULL;
	}

	/* compile new */
	{
		re2_pattern *pat = re2_compile(pattern, pattern_len, errbuf, errbuf_size);

		if (!pat)
			return NULL;

		entry->pattern = (char *)malloc(pattern_len);
		if (!entry->pattern)
		{
			re2_free(pat);
			snprintf(errbuf, errbuf_size, "out of memory");
			return NULL;
		}
		memcpy(entry->pattern, pattern, pattern_len);
		entry->pattern_len = pattern_len;
		entry->compiled = pat;
		return pat;
	}
}

#include "postgres.h"
#if PG_VERSION_NUM >= 160000
#include "varatt.h"
#endif

#include "re2_util.h"

static bool
ensure(varlena_out *out, size_t need)
{
	size_t newcap = out->cap ? out->cap : 64;
	char  *p;

	while (newcap < out->len + need)
		newcap *= 2;
	if (newcap == out->cap)
		return true;
/* repalloc_extended: PG16+, backported to 15.18 & 14.23; absent on PG13 (EOL) */
#if PG_VERSION_NUM >= 160000 || (PG_VERSION_NUM >= 150018 && PG_VERSION_NUM < 160000)                                  \
|| (PG_VERSION_NUM >= 140023 && PG_VERSION_NUM < 150000)
	p = (char *)(out->base ? repalloc_extended(out->base, VARHDRSZ + newcap, MCXT_ALLOC_NO_OOM)
						   : palloc_extended(VARHDRSZ + newcap, MCXT_ALLOC_NO_OOM));
	if (!p)
		return false;
#else
	/* grow manually preserving NO_OOM */
	p = (char *)palloc_extended(VARHDRSZ + newcap, MCXT_ALLOC_NO_OOM);
	if (!p)
		return false;
	if (out->base)
	{
		memcpy(p, out->base, VARHDRSZ + out->len);
		pfree(out->base);
	}
#endif
	out->base = p;
	out->cap = newcap;
	return true;
}

bool
varlena_out_append(varlena_out *out, const char *s, size_t n, char *errbuf, size_t errbuf_size)
{
	if (n == 0)
		return true;
	if (!ensure(out, n))
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return false;
	}
	memcpy(out->base + VARHDRSZ + out->len, s, n);
	out->len += n;
	return true;
}

void *
varlena_out_finish(varlena_out *out, char *errbuf, size_t errbuf_size)
{
	/* ensure(0) allocates the header even when nothing was appended */
	if (!ensure(out, 0))
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
	SET_VARSIZE(out->base, VARHDRSZ + out->len);
	return out->base;
}

size_t
empty_match_advance(const char *p, const char *ep)
{
	unsigned char c = (unsigned char)p[0];
	int			  len = c < 0x80 ? 1 : c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : c < 0xF8 ? 4 : 1;
	long		  cp;
	long		  min_cp;

	if (len == 1 || (size_t)len > (size_t)(ep - p))
		return 1; /* ASCII, stray continuation, or truncated rune */
	for (int i = 1; i < len; i++)
		if (((unsigned char)p[i] & 0xC0) != 0x80)
			return 1; /* bad continuation -> RuneError */
	cp = len == 2	? ((c & 0x1F) << 6) | (p[1] & 0x3F)
		 : len == 3 ? ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F)
					: ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
	min_cp = len == 2 ? 0x80 : len == 3 ? 0x800 : 0x10000;
	return cp < min_cp ? 1 : (size_t)len; /* overlong -> RuneError */
}

bool
validate_rewrite(int ngroups, const char *repl, size_t repl_len, char *errbuf, size_t errbuf_size)
{
	for (size_t i = 0; i < repl_len; i++)
	{
		if (repl[i] == '\\' && i + 1 < repl_len)
		{
			int c = repl[i + 1];
			if (c >= '0' && c <= '9')
			{
				int ref = c - '0';
				if (ref > ngroups)
				{
					snprintf(errbuf, errbuf_size, "\\%d: backref beyond %d group(s)", ref, ngroups);
					return false;
				}
			}
			i++;
		}
	}
	return true;
}

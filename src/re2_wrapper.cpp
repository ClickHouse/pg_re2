extern "C"
{
#include "postgres.h"
#if PG_VERSION_NUM >= 160000
#include "varatt.h"
#endif
}

/* PG Min/Max macros conflict with abseil headers */
#undef Min
#undef Max

#include "re2_util.h"
#include "re2_wrapper.h"

#include <new>
#include <re2/re2.h>
#include <string>
#include <vector>

struct re2_pattern
{
	re2::RE2 re;
	re2_pattern(const re2::RE2::Options &opts, re2::StringPiece pat) : re(pat, opts) {}
};

static re2::RE2::Options
default_opts(void)
{
	re2::RE2::Options opts;
	opts.set_dot_nl(true);
	opts.set_log_errors(false);
	return opts;
}

re2_pattern *
re2_compile(const char *pattern, size_t pattern_len, char *errbuf, size_t errbuf_size)
{
	auto  opts = default_opts();
	auto *pat = new (std::nothrow) re2_pattern(opts, re2::StringPiece(pattern, pattern_len));
	if (!pat)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
	if (!pat->re.ok())
	{
		strlcpy(errbuf, pat->re.error().c_str(), errbuf_size);
		delete pat;
		return NULL;
	}
	return pat;
}

void
re2_free(re2_pattern *pat)
{
	delete pat;
}

int
re2_num_captures(const re2_pattern *pat)
{
	return pat->re.NumberOfCapturingGroups();
}

bool
re2_match(const re2_pattern *pat, const char *text, size_t text_len)
{
	return re2::RE2::PartialMatch(re2::StringPiece(text, text_len), pat->re);
}

/* run Match, caller must delete[] returned submatch array */
static bool
do_match(const re2_pattern *pat, re2::StringPiece input, int ngroups, re2::StringPiece **submatch_out)
{
	auto *sub = new (std::nothrow) re2::StringPiece[ngroups + 1];
	if (!sub)
		return false;

	if (!pat->re.Match(input, 0, input.size(), re2::RE2::UNANCHORED, sub, ngroups + 1))
	{
		delete[] sub;
		*submatch_out = NULL;
		return false;
	}
	*submatch_out = sub;
	return true;
}

static re2_span
sp_to_span(re2::StringPiece sp)
{
	re2_span s;
	s.data = sp.data();
	s.len = sp.size();
	return s;
}

re2_span
re2_extract(const re2_pattern *pat, const char *text, size_t text_len)
{
	re2::StringPiece input(text, text_len);
	int				 ngroups = pat->re.NumberOfCapturingGroups();
	int				 target = ngroups > 0 ? 1 : 0;
	re2_span		 empty = { NULL, 0 };

	re2::StringPiece *sub;
	if (!do_match(pat, input, target, &sub) || !sub)
		return empty;

	re2_span result = sp_to_span(sub[target]);
	delete[] sub;
	return result;
}

re2_span *
re2_extract_all(const re2_pattern *pat, const char *text, size_t text_len, int *count, char *errbuf, size_t errbuf_size)
{
	re2::StringPiece input(text, text_len);
	int				 ngroups = pat->re.NumberOfCapturingGroups();
	int				 target = ngroups > 0 ? 1 : 0;
	int				 needed = target + 1;
	size_t			 pos = 0;

	errbuf[0] = '\0';
	*count = 0;

	std::vector<re2_span> spans;
	try
	{
		while (pos <= text_len)
		{
			re2::StringPiece sub[2];
			if (!pat->re.Match(input, pos, input.size(), re2::RE2::UNANCHORED, sub, needed))
				break;

			spans.push_back(sp_to_span(sub[target]));

			size_t match_end = (sub[0].data() - text) + sub[0].size();
			pos = match_end > pos ? match_end : pos + 1;
		}
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}

	if (spans.empty())
		return NULL;

	re2_span *out = (re2_span *)palloc_extended(spans.size() * sizeof(re2_span), MCXT_ALLOC_NO_OOM);
	if (!out)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
	memcpy(out, spans.data(), spans.size() * sizeof(re2_span));
	*count = (int)spans.size();
	return out;
}

re2_span
re2_regexp_extract(const re2_pattern *pat, const char *text, size_t text_len, int group_idx, char *errbuf,
				   size_t errbuf_size)
{
	re2_span empty = { NULL, 0 };
	int		 ngroups = pat->re.NumberOfCapturingGroups();

	if (group_idx < 0 || group_idx > ngroups)
	{
		snprintf(errbuf, errbuf_size, "group index %d out of range [0, %d]", group_idx, ngroups);
		return empty;
	}

	errbuf[0] = '\0';

	re2::StringPiece  input(text, text_len);
	re2::StringPiece *sub;
	if (!do_match(pat, input, ngroups, &sub) || !sub)
		return empty;

	re2::StringPiece match = sub[group_idx];
	delete[] sub;

	if (match.data() == NULL)
		return empty;

	return sp_to_span(match);
}

re2_span *
re2_extract_groups(const re2_pattern *pat, const char *text, size_t text_len, int *count, char *errbuf,
				   size_t errbuf_size)
{
	int ngroups = pat->re.NumberOfCapturingGroups();
	if (ngroups == 0)
	{
		strlcpy(errbuf, "pattern has no capturing groups", errbuf_size);
		*count = 0;
		return NULL;
	}

	re2::StringPiece  input(text, text_len);
	re2::StringPiece *sub;
	if (!do_match(pat, input, ngroups, &sub) || !sub)
	{
		errbuf[0] = '\0';
		*count = 0;
		return NULL;
	}

	re2_span *out = (re2_span *)palloc_extended(ngroups * sizeof(re2_span), MCXT_ALLOC_NO_OOM);
	if (!out)
	{
		delete[] sub;
		strlcpy(errbuf, "out of memory", errbuf_size);
		*count = 0;
		return NULL;
	}
	*count = ngroups;
	for (int i = 0; i < ngroups; i++)
	{
		re2::StringPiece &g = sub[i + 1];
		out[i].data = g.data();
		out[i].len = g.data() ? g.size() : 0;
	}
	delete[] sub;
	return out;
}

re2_span *
re2_extract_all_groups(const re2_pattern *pat, const char *text, size_t text_len, int *match_count, int *ngroups_out,
					   char *errbuf, size_t errbuf_size)
{
	int ngroups = pat->re.NumberOfCapturingGroups();

	*match_count = 0;
	*ngroups_out = ngroups;

	if (ngroups == 0)
	{
		strlcpy(errbuf, "pattern has no capturing groups", errbuf_size);
		return NULL;
	}

	errbuf[0] = '\0';

	re2::StringPiece			  input(text, text_len);
	std::vector<re2_span>		  spans;
	std::vector<re2::StringPiece> sub(ngroups + 1);
	size_t						  pos = 0;

	try
	{
		while (pos <= text_len)
		{
			if (!pat->re.Match(input, pos, text_len, re2::RE2::UNANCHORED, sub.data(), ngroups + 1))
				break;

			for (int g = 1; g <= ngroups; g++)
			{
				re2::StringPiece &sp = sub[g];
				re2_span		  s;
				s.data = sp.data();
				s.len = sp.data() ? sp.size() : 0;
				spans.push_back(s);
			}

			size_t match_end = (sub[0].data() - text) + sub[0].size();
			pos = match_end > pos ? match_end : pos + 1;
		}
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}

	if (spans.empty())
		return NULL;

	re2_span *out = (re2_span *)palloc_extended(spans.size() * sizeof(re2_span), MCXT_ALLOC_NO_OOM);
	if (!out)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
	memcpy(out, spans.data(), spans.size() * sizeof(re2_span));
	*match_count = (int)(spans.size() / ngroups);
	return out;
}

re2_span *
re2_split(const re2_pattern *pat, const char *text, size_t text_len, int max_splits, int *count, char *errbuf,
		  size_t errbuf_size)
{
	std::vector<re2_span> spans;

	errbuf[0] = '\0';
	*count = 0;

	try
	{
		re2::StringPiece input(text, text_len);
		size_t			 pos = 0;
		int				 splits = 0;
		bool			 done = false;

		while (!done)
		{
			if (max_splits > 0 && splits >= max_splits)
				break;

			re2::StringPiece m;
			if (!pat->re.Match(input, pos, text_len, re2::RE2::UNANCHORED, &m, 1) || m.size() == 0)
			{
				re2_span s;
				s.data = text + pos;
				s.len = text_len - pos;
				spans.push_back(s);
				done = true;
			}
			else
			{
				size_t	 match_start = m.data() - text;
				re2_span s;
				s.data = text + pos;
				s.len = match_start - pos;
				spans.push_back(s);
				pos = match_start + m.size();
				splits++;
			}
		}
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}

	if (spans.empty())
		return NULL;

	re2_span *out = (re2_span *)palloc_extended(spans.size() * sizeof(re2_span), MCXT_ALLOC_NO_OOM);
	if (!out)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
	memcpy(out, spans.data(), spans.size() * sizeof(re2_span));
	*count = (int)spans.size();
	return out;
}

/* Global/first replace appending directly into palloc'd varlena, avoiding the
 * input copy and output copy that std::string + RE2::GlobalReplace incur. */
static void *
replace_impl(const re2_pattern *pat, const char *text, size_t text_len, const char *repl, size_t repl_len, bool global,
			 char *errbuf, size_t errbuf_size)
{
	if (!validate_rewrite(pat->re.NumberOfCapturingGroups(), repl, repl_len, errbuf, errbuf_size))
		return NULL;

	try
	{
		re2::StringPiece			  input(text, text_len);
		re2::StringPiece			  rewrite(repl, repl_len);
		int							  nvec = 1 + re2::RE2::MaxSubmatch(rewrite);
		std::vector<re2::StringPiece> vec(nvec);
		std::string					  rw; /* reused rewrite scratch */
		varlena_out					  out = {};
		const char					 *p = text;
		const char					 *ep = text + text_len;
		const char					 *lastend = nullptr;

		while (p <= ep)
		{
			if (!pat->re.Match(input, (size_t)(p - text), text_len, re2::RE2::UNANCHORED, vec.data(), nvec))
				break;

			const char *ms = vec[0].data();
			const char *me = ms + vec[0].size();

			/* empty match right after prior match: skip a rune, else infinite loop */
			if (ms == me && ms == lastend)
			{
				if (p < ep)
				{
					size_t adv = empty_match_advance(p, ep);
					if (!varlena_out_append(&out, p, adv, errbuf, errbuf_size))
						return NULL;
					p += adv;
				}
				else
					p++;
				continue;
			}

			if (!varlena_out_append(&out, p, ms - p, errbuf, errbuf_size))
				return NULL;
			rw.clear();
			pat->re.Rewrite(&rw, rewrite, vec.data(), nvec);
			if (!varlena_out_append(&out, rw.data(), rw.size(), errbuf, errbuf_size))
				return NULL;
			p = me;
			lastend = p;
			if (!global)
				break;
		}
		if (!varlena_out_append(&out, p, ep > p ? ep - p : 0, errbuf, errbuf_size))
			return NULL;
		return varlena_out_finish(&out, errbuf, errbuf_size);
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
}

void *
re2_replace_one(const re2_pattern *pat, const char *text, size_t text_len, const char *repl, size_t repl_len,
				char *errbuf, size_t errbuf_size)
{
	return replace_impl(pat, text, text_len, repl, repl_len, false, errbuf, errbuf_size);
}

void *
re2_replace_all(const re2_pattern *pat, const char *text, size_t text_len, const char *repl, size_t repl_len,
				char *errbuf, size_t errbuf_size)
{
	return replace_impl(pat, text, text_len, repl, repl_len, true, errbuf, errbuf_size);
}

int
re2_count_matches(const re2_pattern *pat, const char *text, size_t text_len)
{
	re2::StringPiece input(text, text_len);
	re2::StringPiece match;
	int				 n = 0;
	size_t			 pos = 0;

	while (pos <= text_len)
	{
		if (!pat->re.Match(input, pos, input.size(), re2::RE2::UNANCHORED, &match, 1))
			break;
		size_t match_end = (match.data() - text) + match.size();
		if (match.size() > 0)
			n++;
		pos = match_end > pos ? match_end : pos + 1;
	}
	return n;
}

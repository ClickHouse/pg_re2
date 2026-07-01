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

#include "re2_wrapper.h"

#include <algorithm>
#include <new>
#include <re2/filtered_re2.h>
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

static bool
validate_rewrite(const re2_pattern *pat, const char *repl, size_t repl_len, char *errbuf, size_t errbuf_size)
{
	int ngroups = pat->re.NumberOfCapturingGroups();

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

/* palloc varlena ready for PG_RETURN_TEXT_P/PG_RETURN_BYTEA_P, NULL on OOM */
static void *
make_varlena(const std::string &s)
{
	size_t len = s.size();
	char  *out = (char *)palloc_extended(len + VARHDRSZ, MCXT_ALLOC_NO_OOM);

	if (!out)
		return NULL;
	SET_VARSIZE(out, len + VARHDRSZ);
	memcpy(VARDATA(out), s.data(), len);
	return out;
}

void *
re2_replace_one(const re2_pattern *pat, const char *text, size_t text_len, const char *repl, size_t repl_len,
				char *errbuf, size_t errbuf_size)
{
	if (!validate_rewrite(pat, repl, repl_len, errbuf, errbuf_size))
		return NULL;

	try
	{
		std::string result(text, text_len);
		re2::RE2::Replace(&result, pat->re, re2::StringPiece(repl, repl_len));
		void *out = make_varlena(result);
		if (!out)
			strlcpy(errbuf, "out of memory", errbuf_size);
		return out;
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
}

void *
re2_replace_all(const re2_pattern *pat, const char *text, size_t text_len, const char *repl, size_t repl_len,
				char *errbuf, size_t errbuf_size)
{
	if (!validate_rewrite(pat, repl, repl_len, errbuf, errbuf_size))
		return NULL;

	try
	{
		std::string result(text, text_len);
		re2::RE2::GlobalReplace(&result, pat->re, re2::StringPiece(repl, repl_len));
		void *out = make_varlena(result);
		if (!out)
			strlcpy(errbuf, "out of memory", errbuf_size);
		return out;
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
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

bool
re2_possible_prefix(const re2_pattern *pat, char *out, size_t outcap, size_t *outlen)
{
	*outlen = 0;
	try
	{
		std::string mn, mx;

		if (!pat->re.PossibleMatchRange(&mn, &mx, 64))
			return false;

		/* Longest common prefix of [min, max] bounds every string in the range. */
		size_t lim = std::min(mn.size(), mx.size());
		size_t n = 0;
		while (n < lim && mn[n] == mx[n])
			n++;
		if (n == 0)
			return false;
		if (n > outcap)
			n = outcap;
		memcpy(out, mn.data(), n);
		*outlen = n;
		return true;
	}
	catch (std::bad_alloc &)
	{
		*outlen = 0;
		return false;
	}
}

struct re2_filter
{
	re2::FilteredRE2		 f;
	std::vector<std::string> atoms;
	std::vector<int>		 passes_in;	 /* re2_filter_passes scratch, reused */
	std::vector<int>		 passes_out; /* re2_filter_passes scratch, reused */
	re2_filter() : f(3) {}				 /* min_atom_len 3: every atom yields >= 1 trigram */
};

re2_filter *
re2_filter_new(const char *pattern, size_t pattern_len, char *errbuf, size_t errbuf_size)
{
	re2_filter *rf = NULL; /* NULL if the constructor throws; delete NULL is safe */

	try
	{
		int					id;
		re2::RE2::ErrorCode ec;

		rf = new re2_filter();
		ec = rf->f.Add(re2::StringPiece(pattern, pattern_len), default_opts(), &id);
		if (ec != re2::RE2::NoError)
		{
			strlcpy(errbuf, "invalid RE2 pattern", errbuf_size);
			delete rf;
			return NULL;
		}

		rf->f.Compile(&rf->atoms);
		return rf;
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		delete rf;
		return NULL;
	}
}

void
re2_filter_free(re2_filter *f)
{
	delete f;
}

int
re2_filter_num_atoms(const re2_filter *f)
{
	return (int)f->atoms.size();
}

re2_span
re2_filter_atom(const re2_filter *f, int i)
{
	re2_span s;
	s.data = f->atoms[i].data();
	s.len = f->atoms[i].size();
	return s;
}

bool
re2_filter_passes(re2_filter *f, const int *present, int n_present)
{
	try
	{
		/* assign/clear keep capacity, so steady state does no allocation */
		if (n_present == 0)
			f->passes_in.clear();
		else
			f->passes_in.assign(present, present + n_present);
		f->f.AllPotentials(f->passes_in, &f->passes_out);
		return !f->passes_out.empty();
	}
	catch (std::bad_alloc &)
	{
		return true; /* cannot prune safely: keep candidate for recheck */
	}
}

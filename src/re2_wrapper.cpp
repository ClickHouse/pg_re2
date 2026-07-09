extern "C"
{
#include "postgres.h"
#include "utils/memutils.h"
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
#include <re2/set.h>
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
	re2_pattern *pat = NULL; /* NULL if ctor throws; delete NULL is safe */

	try
	{
		pat = new re2_pattern(default_opts(), re2::StringPiece(pattern, pattern_len));
		if (!pat->re.ok())
		{
			strlcpy(errbuf, pat->re.error().c_str(), errbuf_size);
			delete pat;
			return NULL;
		}
		return pat;
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		delete pat;
		return NULL;
	}
}

void
re2_free(re2_pattern *pat)
{
	delete pat;
}

bool
re2_match(const re2_pattern *pat, const char *text, size_t text_len, bool *failed)
{
	*failed = false;
	try
	{
		return re2::RE2::PartialMatch(re2::StringPiece(text, text_len), pat->re);
	}
	catch (std::bad_alloc &)
	{
		/* NFA/BitState fallback matchers allocate */
		*failed = true;
		return false;
	}
}

/* run Match filling sub with ngroups+1 pieces, may throw bad_alloc */
static bool
do_match(const re2_pattern *pat, re2::StringPiece input, int ngroups, std::vector<re2::StringPiece> &sub)
{
	sub.resize(ngroups + 1);
	return pat->re.Match(input, 0, input.size(), re2::RE2::UNANCHORED, sub.data(), ngroups + 1);
}

static re2_span
sp_to_span(re2::StringPiece sp)
{
	re2_span s;
	s.data = sp.data();
	s.len = sp.size();
	return s;
}

/* NULL on OOM or over palloc cap, oversized request would elog longjmp past live C++ frames */
static re2_span *
spans_to_palloc(const std::vector<re2_span> &spans)
{
	size_t nbytes = spans.size() * sizeof(re2_span);

	if (nbytes > MaxAllocSize)
		return NULL;
	re2_span *out = (re2_span *)palloc_extended(nbytes, MCXT_ALLOC_NO_OOM);
	if (out)
		memcpy(out, spans.data(), nbytes);
	return out;
}

re2_span
re2_extract(const re2_pattern *pat, const char *text, size_t text_len, char *errbuf, size_t errbuf_size)
{
	int		 ngroups = pat->re.NumberOfCapturingGroups();
	int		 target = ngroups > 0 ? 1 : 0;
	re2_span empty = { NULL, 0 };

	errbuf[0] = '\0';
	try
	{
		std::vector<re2::StringPiece> sub;

		if (!do_match(pat, re2::StringPiece(text, text_len), target, sub))
			return empty;
		return sp_to_span(sub[target]);
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return empty;
	}
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

	re2_span *out = spans_to_palloc(spans);
	if (!out)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
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
	try
	{
		std::vector<re2::StringPiece> sub;

		if (!do_match(pat, re2::StringPiece(text, text_len), ngroups, sub))
			return empty;

		re2::StringPiece match = sub[group_idx];
		if (match.data() == NULL)
			return empty;
		return sp_to_span(match);
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return empty;
	}
}

re2_span *
re2_extract_groups(const re2_pattern *pat, const char *text, size_t text_len, int *count, char *errbuf,
				   size_t errbuf_size)
{
	int ngroups = pat->re.NumberOfCapturingGroups();

	*count = 0;
	if (ngroups == 0)
	{
		strlcpy(errbuf, "pattern has no capturing groups", errbuf_size);
		return NULL;
	}

	errbuf[0] = '\0';
	try
	{
		std::vector<re2::StringPiece> sub;

		if (!do_match(pat, re2::StringPiece(text, text_len), ngroups, sub))
			return NULL;

		size_t	  nbytes = (size_t)ngroups * sizeof(re2_span);
		re2_span *out = nbytes > MaxAllocSize ? NULL : (re2_span *)palloc_extended(nbytes, MCXT_ALLOC_NO_OOM);
		if (!out)
		{
			strlcpy(errbuf, "out of memory", errbuf_size);
			return NULL;
		}
		for (int i = 0; i < ngroups; i++)
		{
			re2::StringPiece &g = sub[i + 1];
			out[i].data = g.data();
			out[i].len = g.data() ? g.size() : 0;
		}
		*count = ngroups;
		return out;
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
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
	std::vector<re2::StringPiece> sub;
	size_t						  pos = 0;

	try
	{
		sub.resize(ngroups + 1);
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

	re2_span *out = spans_to_palloc(spans);
	if (!out)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
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

	re2_span *out = spans_to_palloc(spans);
	if (!out)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		return NULL;
	}
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

/* palloc varlena ready for PG_RETURN_TEXT_P/PG_RETURN_BYTEA_P, NULL on OOM or result over varlena cap */
static void *
make_varlena(const std::string &s)
{
	size_t len = s.size();

	if (len > MaxAllocSize - VARHDRSZ)
		return NULL;

	char *out = (char *)palloc_extended(len + VARHDRSZ, MCXT_ALLOC_NO_OOM);
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

/* ---- RE2::Set: one automaton over pattern array ---- */

struct re2_set
{
	re2::RE2::Set set;
	re2_set() : set(default_opts(), re2::RE2::UNANCHORED) {}
};

re2_set *
re2_set_new(const re2_span *patterns, int npatterns, int *err_index, char *errbuf, size_t errbuf_size)
{
	re2_set *s = NULL; /* NULL if ctor throws; delete NULL is safe */

	*err_index = -1;
	try
	{
		s = new re2_set();
		/* Add attaches match id in insertion order, Match ids follow array order */
		for (int i = 0; i < npatterns; i++)
		{
			std::string err;
			if (s->set.Add(re2::StringPiece(patterns[i].data, patterns[i].len), &err) < 0)
			{
				strlcpy(errbuf, err.c_str(), errbuf_size);
				*err_index = i;
				delete s;
				return NULL;
			}
		}
		/* fails when combined program exceeds max_mem, no per-pattern attribution; Match on uncompiled Set segfaults */
		if (!s->set.Compile())
		{
			strlcpy(errbuf, "pattern set too large - compile failed", errbuf_size);
			delete s;
			return NULL;
		}
		return s;
	}
	catch (std::bad_alloc &)
	{
		strlcpy(errbuf, "out of memory", errbuf_size);
		delete s;
		return NULL;
	}
}

void
re2_set_free(re2_set *set)
{
	delete set;
}

bool
re2_set_match_any(const re2_set *set, const char *text, size_t text_len, bool *failed)
{
	try
	{
		re2::RE2::Set::ErrorInfo ei;
		bool					 matched = set->set.Match(re2::StringPiece(text, text_len), NULL, &ei);

		/* DFA out of memory: Set has no NFA fallback, unlike RE2 proper */
		*failed = !matched && ei.kind != re2::RE2::Set::kNoError;
		return matched;
	}
	catch (std::bad_alloc &)
	{
		*failed = true;
		return false;
	}
}

int
re2_set_match_indices(const re2_set *set, const char *text, size_t text_len, int *indices)
{
	try
	{
		std::vector<int>		 ids;
		re2::RE2::Set::ErrorInfo ei;

		if (!set->set.Match(re2::StringPiece(text, text_len), &ids, &ei))
			return ei.kind == re2::RE2::Set::kNoError ? 0 : -1;

		/* Match output unordered, extension semantics want array order */
		qsort(ids.data(), ids.size(), sizeof(int), cmp_int32);
		std::copy(ids.begin(), ids.end(), indices);
		return (int)ids.size();
	}
	catch (std::bad_alloc &)
	{
		/* vector growth OOM, same fallback as DFA failure */
		return -1;
	}
}

int
re2_set_match_min(const re2_set *set, const char *text, size_t text_len, bool *failed)
{
	try
	{
		std::vector<int>		 ids;
		re2::RE2::Set::ErrorInfo ei;

		if (!set->set.Match(re2::StringPiece(text, text_len), &ids, &ei))
		{
			*failed = ei.kind != re2::RE2::Set::kNoError;
			return -1;
		}
		*failed = false;
		/* Match returning true with vector guarantees nonempty */
		return *std::min_element(ids.begin(), ids.end());
	}
	catch (std::bad_alloc &)
	{
		/* vector growth OOM, same fallback as DFA failure */
		*failed = true;
		return -1;
	}
}

int
re2_count_matches(const re2_pattern *pat, const char *text, size_t text_len)
{
	try
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
	catch (std::bad_alloc &)
	{
		return -1;
	}
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

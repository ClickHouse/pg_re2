extern "C"
{
#include "postgres.h"
#include "varatt.h"
}

/* PG Min/Max macros conflict with abseil headers */
#undef Min
#undef Max

#include "re2_wrapper.h"

#include <new>
#include <re2/re2.h>
#include <string>

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
		snprintf(errbuf, errbuf_size, "out of memory");
		return NULL;
	}
	if (!pat->re.ok())
	{
		snprintf(errbuf, errbuf_size, "%s", pat->re.error().c_str());
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
	int				 capacity = 16;
	int				 n = 0;
	re2_span		*out = (re2_span *)palloc(capacity * sizeof(re2_span));
	size_t			 pos = 0;

	while (pos <= text_len)
	{
		re2::StringPiece sub[2];
		if (!pat->re.Match(input, pos, input.size(), re2::RE2::UNANCHORED, sub, needed))
			break;

		if (n == capacity)
		{
			capacity *= 2;
			out = (re2_span *)repalloc(out, capacity * sizeof(re2_span));
		}
		out[n++] = sp_to_span(sub[target]);

		size_t match_end = (sub[0].data() - text) + sub[0].size();
		pos = match_end > pos ? match_end : pos + 1;
	}

	errbuf[0] = '\0';
	*count = n;
	if (n == 0)
	{
		pfree(out);
		return NULL;
	}
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
		snprintf(errbuf, errbuf_size, "pattern has no capturing groups");
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

	*count = ngroups;
	re2_span *out = (re2_span *)palloc(ngroups * sizeof(re2_span));
	for (int i = 0; i < ngroups; i++)
	{
		re2::StringPiece &g = sub[i + 1];
		out[i].data = g.data();
		out[i].len = g.data() ? g.size() : 0;
	}
	delete[] sub;
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

/* palloc varlena ready for PG_RETURN_TEXT_P/PG_RETURN_BYTEA_P */
static void *
make_varlena(const std::string &s)
{
	size_t len = s.size();
	char  *out = (char *)palloc(len + VARHDRSZ);

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
		return make_varlena(result);
	}
	catch (std::bad_alloc &)
	{
		snprintf(errbuf, errbuf_size, "out of memory");
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
		return make_varlena(result);
	}
	catch (std::bad_alloc &)
	{
		snprintf(errbuf, errbuf_size, "out of memory");
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

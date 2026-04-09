-- match: basic
SELECT re2match('hello world', 'h.*o');
SELECT re2match('hello world', '^world');
SELECT re2match('hello world', 'world$');
SELECT re2match('line1' || chr(10) || 'line2', 'line1.line2');  -- dot matches newline
SELECT re2match(NULL, 'x') IS NULL AS null_propagation;

-- match: CH edge cases
SELECT re2match('Hello', '');                        -- empty pattern matches
SELECT re2match('Too late', '(?i)too late');          -- inline case-insensitive
SELECT re2match('Too late', '(?i:too late)');
SELECT re2match('a key="v" ', 'key="(.*?)"');        -- lazy quantifier

-- extract
SELECT re2extract('foo bar baz', '(\w+)\s+(\w+)');  -- first capture group
SELECT re2extract('foo bar baz', '\w+');             -- no groups: whole match
SELECT re2extract('no match here', '\d+');           -- no match: empty string
SELECT re2extract(NULL, '\d+') IS NULL AS ex_null;

-- extractall
SELECT re2extractall('abc 123 def 456', '\d+');
SELECT re2extractall('aaa bbb ccc', '(\w)\w+');     -- first capture group
SELECT re2extractall('no match', '\d+');             -- empty array
SELECT re2extractall('{"a":"1","b":"2","c":"","d":"4"}', ':"([^"]*)"');  -- CH: empty values
SELECT re2extractall(NULL, '\d+') IS NULL AS ea_null;

-- regexpextract
SELECT re2regexpextract('100-200', '(\d+)-(\d+)', 1);  -- first group
SELECT re2regexpextract('100-200', '(\d+)-(\d+)', 2);  -- second group
SELECT re2regexpextract('100-200', '(\d+)-(\d+)', 0);  -- whole match
SELECT re2regexpextract('100-200', '(\d+).*', 1);      -- greedy
SELECT re2regexpextract('100-200', '([a-z])', 1);       -- no match: empty string
SELECT re2regexpextract('2024-01-15', '(\d{4})-(\d{2})-(\d{2})');  -- default index=1
-- CH: greedy competing groups
SELECT re2regexpextract('0123456789', '(\d+)(\d+)', 1);
SELECT re2regexpextract('0123456789', '(\d+)(\d+)', 2);
-- null propagation
SELECT re2regexpextract(NULL, '(\d+)', 1) IS NULL AS re_null1;
SELECT re2regexpextract('100', NULL, 1) IS NULL AS re_null2;

-- regexpextract errors
\set ON_ERROR_STOP off
SELECT re2regexpextract('100-200', '(\d+)-(\d+)', 3);   -- out of range
SELECT re2regexpextract('100-200', '(\d+)-(\d+)', -1);  -- negative
SELECT re2regexpextract('100-200', '\d+-\d+', 1);       -- no groups + index 1
\set ON_ERROR_STOP on

-- extractgroups
SELECT re2extractgroups('hello world', '(\w+) (\w+)');
SELECT re2extractgroups('2024-01-15', '(\d{4})-(\d{2})-(\d{2})');
SELECT re2extractgroups('no match', '(\d{4})-(\d{2})-(\d{2})');  -- no match: empty array
SELECT re2extractgroups(NULL, '(\d+)') IS NULL AS eg_null;

-- extractgroups errors
\set ON_ERROR_STOP off
SELECT re2extractgroups('hello', '\w+');  -- no capture groups
\set ON_ERROR_STOP on

-- replaceregexpone
SELECT re2replaceregexpone('Hello', 'l', 'x');                                -- first only
SELECT re2replaceregexpone('hello world', '(\w+)', 'REPLACED');
SELECT re2replaceregexpone('hello world', '(\w+)', '[\0]');                   -- backref \0
SELECT re2replaceregexpone('2024-01-15', '(\d+)-(\d+)-(\d+)', '\3/\2/\1');
-- CH: trim leading commas only (first match)
SELECT re2replaceregexpone(',,1,,', '^[,]*|[,]*$', '');
SELECT re2replaceregexpone('1,,', '^[,]*|[,]*$', '');
SELECT re2replaceregexpone(',,1', '^[,]*|[,]*$', '');
-- null propagation
SELECT re2replaceregexpone(NULL, '\d+', 'x') IS NULL AS rp1_null;

-- replaceregexpone error: invalid backref
\set ON_ERROR_STOP off
SELECT re2replaceregexpone('Hello', 'l', '\1');  -- \1: backref beyond 0 group(s)
\set ON_ERROR_STOP on

-- replaceregexpall
SELECT re2replaceregexpall('Hello', 'l', 'x');                       -- all occurrences
SELECT re2replaceregexpall('abc 123 def 456', '\d+', 'NUM');
SELECT re2replaceregexpall('hello', '.', '[\0]');
SELECT re2replaceregexpall('aaa', 'a', 'bb');
-- CH empty match edge cases
SELECT re2replaceregexpall(',,1,,', '^[,]*|[,]*$', '');              -- strip leading/trailing
SELECT re2replaceregexpall(',,1', '^[,]*|[,]*$', '');
SELECT re2replaceregexpall('1,,', '^[,]*|[,]*$', '');
SELECT re2replaceregexpall('a', 'z*', '');                           -- empty matches preserved
SELECT re2replaceregexpall('aazzq', 'z*', '');
-- CH: ^ anchor only
SELECT re2replaceregexpall('Hello, World!', '^', 'here: ');
-- domain extraction
SELECT re2replaceregexpall('https://www.clickhouse.com/', '^https?://(?:www\.)?([^/]+)/.*$', '\1');
-- null propagation
SELECT re2replaceregexpall(NULL, '\d+', 'x') IS NULL AS rpa_null;

-- countmatches
SELECT re2countmatches('', 'foo');                              -- empty haystack
SELECT re2countmatches('foo', '');                              -- empty pattern: 0 (CH compat)
SELECT re2countmatches('foobarfoo', 'foo');                     -- basic
SELECT re2countmatches('oooo', 'oo');                           -- non-overlapping
SELECT re2countmatches('abc 123 def 456 ghi 789', '\d+');
SELECT re2countmatches('aaa', 'a');
SELECT re2countmatches('foo', '[f]{0}');                        -- zero-width: 0 (CH compat)
SELECT re2countmatches('foo', '[f]{0}foo');                     -- zero-width prefix + content
-- CH: [a-zA-Z]* on mixed content, counts only non-empty matches
SELECT re2countmatches('  foo bar   ', '[a-zA-Z]*');
-- CH: capturing groups
SELECT re2countmatches('foobarbazfoobarbaz', 'foo(bar)(?:baz|)');
-- null propagation
SELECT re2countmatches(NULL, '\d+') IS NULL AS cm_null;

-- countmatchescaseinsensitive
SELECT re2countmatchescaseinsensitive('foobarFOO', 'foo');
SELECT re2countmatchescaseinsensitive('ooOO', 'oo');

-- multimatchany
SELECT re2multimatchany('hello world', ARRAY['\d+', 'world']);
SELECT re2multimatchany('hello world', ARRAY['\d+', '^\d+$']);
SELECT re2multimatchany('abc', ARRAY['']);                       -- empty pattern matches
SELECT re2multimatchany('', ARRAY['']);                          -- empty haystack + empty pattern
SELECT re2multimatchany('', ARRAY['some string']);               -- empty haystack, no match
SELECT re2multimatchany('abc', ARRAY[]::text[]);                -- empty array: no match
SELECT re2multimatchany(NULL, ARRAY['\d+']) IS NULL AS mm_null;

-- multimatchanyindex
SELECT re2multimatchanyindex('hello world', ARRAY['\d+', 'world', 'hello']);
SELECT re2multimatchanyindex('hello world', ARRAY['\d+', '^\d+$']);

-- multimatchallindices
SELECT re2multimatchallindices('hello world', ARRAY['hello', '\d+', 'world', 'o']);
SELECT re2multimatchallindices('test', ARRAY['\d+', '[A-Z]+']);

-- invalid pattern
\set ON_ERROR_STOP off
SELECT re2match('hello', '[invalid');
\set ON_ERROR_STOP on

-- ==== bytea overloads (zero-byte handling, CH tests 01083/01085) ====

-- match with \0 in haystack (CH: match('\0 key="v" ', 'key="(.*?)"') -> 1)
SELECT re2match('\x00'::bytea || ' key="v" '::bytea, 'key="(.*?)"');
SELECT re2match('a'::bytea || '\x00'::bytea || 'b'::bytea, 'a.b');  -- dot matches \0

-- extract from bytea with \0
SELECT re2extract('a'::bytea || '\x00'::bytea || 'key="val"'::bytea, 'key="([^"]*)"');
SELECT re2extract('hello'::bytea || '\x00'::bytea || 'world'::bytea, '\w+');

-- extractall with \0
SELECT re2extractall('a'::bytea || '\x00'::bytea || '1'::bytea || '\x00'::bytea || '2'::bytea || '\x00'::bytea || '3'::bytea, '\d');

-- regexpextract with \0
SELECT re2regexpextract('a'::bytea || '\x00'::bytea || '100-200'::bytea, '(\d+)-(\d+)', 1);
SELECT re2regexpextract('a'::bytea || '\x00'::bytea || '100-200'::bytea, '(\d+)-(\d+)', 2);

-- extractgroups with \0
SELECT re2extractgroups('a'::bytea || '\x00'::bytea || 'hello world'::bytea, '(\w+) (\w+)');

-- replaceregexpone/all with \0 in haystack, pattern matches \0 via \x00
SELECT re2replaceregexpone('a'::bytea || '\x00'::bytea || 'b'::bytea || '\x00'::bytea || 'c'::bytea, '\x00', 'X');
SELECT re2replaceregexpall('a'::bytea || '\x00'::bytea || 'b'::bytea || '\x00'::bytea || 'c'::bytea, '\x00', 'X');

-- countmatches with \0
SELECT re2countmatches('a'::bytea || '\x00'::bytea || 'b'::bytea || '\x00'::bytea || 'c'::bytea, '\x00');

-- multimatchany with \0 haystack
SELECT re2multimatchany('a'::bytea || '\x00'::bytea || 'key="v"'::bytea, ARRAY['key', 'nope']);

Compares `re2` throughput against PostgreSQL builtin [POSIX regex (ARE)](https://www.postgresql.org/docs/current/functions-matching.html)

![Benchmark](graph.png)

| Category       | re2                       | builtin                       |
| -------------- | ------------------------- | ----------------------------- |
| match          | `re2match`                | `regexp_like`                 |
| extract        | `re2extract`              | `regexp_substr`               |
| extract all    | `re2extractall`           | `regexp_matches(…, 'g')`      |
| replace one    | `re2replaceregexpone`     | `regexp_replace`              |
| replace all    | `re2replaceregexpall`     | `regexp_replace(…, 'g')`      |
| count matches  | `re2countmatches`         | `regexp_count`                |

Patterns span literal, character class, alternation, nested quantifier, IP /
email validation, deep alternation, and a ReDoS-shaped `(e?){10}e{10}` case.
Both RE2 (automaton) and PG (ARE) handle last one without catastrophic
backtracking

Data is 10000 rows of:

- `email` ~40 chars
- `logline` ~200 chars
- `longtext` ~2000 chars (400 words)

Methodology
-----------

- JIT and query parallelism disabled to compare single-thread engine throughput reliably
- `gen_graph.py` takes the median time per (pattern, engine) across all iterations

Running
-------

Requires `re2` extension installed (see [README]) and PostgreSQL 15+ for builtin comparisons

Connection uses libpq environment variables; override the `psql` binary with
`PSQL`:

``` sh
PGDATABASE=mydb ./run_bench.sh        # 5 iterations (default)
PGDATABASE=mydb ./run_bench.sh 10     # 10 iterations
./gen_graph.py                        # regenerate graph.png from results.csv
```

  [README]: ../README.md

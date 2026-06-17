#!/bin/bash
# Connection via libpq env vars (PGHOST/PGPORT/PGUSER/PGDATABASE), override binary with PSQL
# Usage: ./run_bench.sh [iterations]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ITERATIONS="${1:-100}"
OUTFILE="$SCRIPT_DIR/results.csv"
PSQL="${PSQL:-psql}"

echo "category,test_name,engine,rows,time_ms,iteration" > "$OUTFILE"

for i in $(seq 1 "$ITERATIONS"); do
    echo "=== Iteration $i/$ITERATIONS ===" >&2
    "$PSQL" -X -q -f "$SCRIPT_DIR/bench.sql" | while IFS= read -r line; do
        case "$line" in
            match,*|extract,*|extract_all,*|extract_all_unnest,*|replace_one,*|replace_all,*|count_matches,*)
                echo "${line},${i}" ;;
        esac
    done >> "$OUTFILE"
done

echo "Results written to $OUTFILE" >&2
column -t -s',' "$OUTFILE" >&2

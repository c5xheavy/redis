#!/usr/bin/env bash
# Benchmark the Release build with redis-benchmark and append one row per test to BENCH.md.
#
#   ./bench.sh "what changed since the last entry"
#
# Env overrides:
#   BENCH_ARGS  redis-benchmark arguments   (default: -t ping -c 50 -n 200000 -P 1)
#   BENCH_LOG   log file                    (default: BENCH.md)
#   BENCH_PORT  port the server listens on  (default: 6379)
#   BENCH_DRY   parse this saved redis-benchmark output instead of running anything
set -euo pipefail
cd "$(dirname "$0")"

NOTE=${1:?usage: ./bench.sh \"note: what changed since the last benchmark\"}
LOG=${BENCH_LOG:-BENCH.md}
ARGS=${BENCH_ARGS:--t ping -c 50 -n 200000 -P 1}
PORT=${BENCH_PORT:-6379}

HASH=$(git rev-parse --short HEAD)
git diff --quiet HEAD -- src CMakeLists.txt || HASH="$HASH+dirty"
DATE=$(date '+%Y-%m-%d %H:%M')

# Commit of the previous entry, to show how much changed since then.
PREV=$( { grep -oE '^\| [0-9-]+ [0-9:]+ \| [0-9a-f]{7,}' "$LOG" 2>/dev/null || true; } | tail -1 | awk '{print $5}')
SINCE=""
if [ -n "$PREV" ] && git cat-file -e "$PREV" 2>/dev/null; then
  SINCE="; since $PREV: $(git rev-list --count "$PREV..HEAD") commits"
fi

if [ -n "${BENCH_DRY:-}" ]; then
  OUT=$(cat "$BENCH_DRY")
else
  command -v redis-benchmark >/dev/null || { echo "redis-benchmark not found: sudo apt install redis-tools" >&2; exit 1; }
  if ss -ltn | grep -q ":$PORT "; then
    echo "port $PORT is busy — stop that server first:" >&2; ss -ltnp | grep ":$PORT " >&2; exit 1
  fi
  cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build-release >/dev/null

  ./build-release/redis >/dev/null 2>&1 &
  SRV=$!
  trap 'kill $SRV 2>/dev/null; wait $SRV 2>/dev/null || true' EXIT
  for _ in $(seq 1 50); do nc -z 127.0.0.1 "$PORT" 2>/dev/null && break; sleep 0.1; done
  nc -z 127.0.0.1 "$PORT" 2>/dev/null || { echo "server did not come up on port $PORT" >&2; exit 1; }

  # shellcheck disable=SC2086
  redis-benchmark -p "$PORT" $ARGS >/dev/null 2>&1 || true   # warm-up: full run, CPU ramp-up takes seconds
  # shellcheck disable=SC2086
  OUT=$(redis-benchmark -p "$PORT" $ARGS 2>/dev/null)
fi

# One row per test: rps, p50, p99, p99.9 (first percentile line >= 99.9), max.
ROWS=$(printf '%s\n' "$OUT" | LC_ALL=C awk -v date="$DATE" -v hash="$HASH" -v note="$NOTE$SINCE" '
  match($0, /====== [A-Z_]+ ======/) { test = substr($0, RSTART + 7, RLENGTH - 14); inpct = 0 }
  /Latency by percentile distribution/ { inpct = 1 }
  /Cumulative distribution of latencies/ { inpct = 0 }
  inpct && /% <= / { pct = $1; sub(/%/, "", pct); if (pct + 0 >= 99.9 && !(test in p999)) p999[test] = $3 }
  /throughput summary:/ { rps[test] = $3 }
  /^ +avg +min +p50/ { getline; p50[test] = $3; p99[test] = $5; max[test] = $6; order[++n] = test }
  END {
    for (k = 1; k <= n; k++) {
      t = order[k]
      printf "| %s | %s | %s | %.0f | %s | %s | %s | %s | %s |\n", date, hash, t, rps[t], p50[t], p99[t], p999[t], max[t], note
    }
  }')

[ -n "$ROWS" ] || { echo "could not parse redis-benchmark output" >&2; printf '%s\n' "$OUT" >&2; exit 1; }

if [ -z "${BENCH_DRY:-}" ]; then
  if [ ! -f "$LOG" ] || ! grep -q '^| date |' "$LOG"; then
    {
      echo "# Benchmark history"
      echo
      echo "Release build only (\`-O3 -DNDEBUG\`); numbers from Asan/Tsan/Debug builds are numbers about the sanitizer."
      echo "Same machine, same conditions, loopback. p99.9 is the first percentile line >= 99.9 in redis-benchmark's distribution."
      echo
      echo "| date | commit | test | rps | p50 ms | p99 ms | p99.9 ms | max ms | note |"
      echo "|---|---|---|---|---|---|---|---|---|"
    } >> "$LOG"
  fi
  printf '%s\n' "$ROWS" >> "$LOG"
fi

echo "cmd: redis-benchmark $ARGS"
printf '%s\n' "$ROWS"
[ -n "$PREV" ] && echo "commits since previous entry: git log --oneline $PREV..HEAD"
exit 0

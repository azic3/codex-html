#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PORT="${PORT:-9006}"
MODES="${MODES:-0 1 2 3}"
SQL_POOL="${SQL_POOL:-4}"
THREAD_POOL="${THREAD_POOL:-8}"
CONNECTIONS="${CONNECTIONS:-100}"
BENCH_THREADS="${BENCH_THREADS:-4}"
DURATION="${DURATION:-30s}"
WARMUP_DURATION="${WARMUP_DURATION:-5s}"
RESULT_DIR="${RESULT_DIR:-$PROJECT_DIR/logs/bench-$(date +%Y%m%d-%H%M%S)}"
BASE_URL="http://127.0.0.1:$PORT"
SERVER_PID=""

cd "$PROJECT_DIR"

set -a
if [ -f "$PROJECT_DIR/config/local/.env" ]; then
  # shellcheck disable=SC1091
  source "$PROJECT_DIR/config/local/.env"
fi
if [ -f "$PROJECT_DIR/config/local/smtp.env" ]; then
  # shellcheck disable=SC1091
  source "$PROJECT_DIR/config/local/smtp.env"
fi
if [ -f "$PROJECT_DIR/config/local/redis.env" ]; then
  # shellcheck disable=SC1091
  source "$PROJECT_DIR/config/local/redis.env"
fi
set +a

if [ -z "${DB_PASS:-}" ]; then
  printf 'DB_PASS is required. Put it in config/local/.env or export it before running this script.\n' >&2
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  printf 'curl is required but was not found in PATH.\n' >&2
  exit 1
fi

BENCH_TOOL=""
if command -v wrk >/dev/null 2>&1; then
  BENCH_TOOL="wrk"
elif command -v ab >/dev/null 2>&1; then
  BENCH_TOOL="ab"
else
  printf 'Install wrk or apache2-utils first. Recommended: sudo apt install wrk apache2-utils\n' >&2
  exit 1
fi

if ss -ltn "( sport = :$PORT )" | grep -q ":$PORT"; then
  printf 'Port %s is already listening. Stop the existing service first, then rerun this script.\n' "$PORT" >&2
  exit 1
fi

mkdir -p "$RESULT_DIR"

cleanup_server() {
  if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
  SERVER_PID=""
}
trap cleanup_server EXIT

wait_ready() {
  local attempt

  for attempt in $(seq 1 50); do
    if curl -fsS "$BASE_URL/login.html" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.2
  done

  return 1
}

run_wrk() {
  local mode="$1"
  local name="$2"
  local path="$3"
  local output="$RESULT_DIR/mode-${mode}-${name}.txt"

  printf '\n[%s] mode=%s %s\n' "$(date '+%F %T')" "$mode" "$path" | tee -a "$RESULT_DIR/summary.txt"
  wrk -t"$BENCH_THREADS" -c"$CONNECTIONS" -d"$WARMUP_DURATION" "$BASE_URL$path" >/dev/null || true
  wrk -t"$BENCH_THREADS" -c"$CONNECTIONS" -d"$DURATION" --latency "$BASE_URL$path" | tee "$output"
  grep -E 'Requests/sec|Transfer/sec|Latency|Socket errors|Non-2xx' "$output" | tee -a "$RESULT_DIR/summary.txt" || true
}

run_ab() {
  local mode="$1"
  local name="$2"
  local path="$3"
  local output="$RESULT_DIR/mode-${mode}-${name}.txt"
  local total_requests="${TOTAL_REQUESTS:-10000}"

  printf '\n[%s] mode=%s %s\n' "$(date '+%F %T')" "$mode" "$path" | tee -a "$RESULT_DIR/summary.txt"
  ab -n 200 -c "$CONNECTIONS" "$BASE_URL$path" >/dev/null || true
  ab -n "$total_requests" -c "$CONNECTIONS" "$BASE_URL$path" | tee "$output"
  grep -E 'Requests per second|Time per request|Transfer rate|Failed requests|Non-2xx' "$output" | tee -a "$RESULT_DIR/summary.txt" || true
}

run_case() {
  local mode="$1"
  local name="$2"
  local path="$3"

  if [ "$BENCH_TOOL" = "wrk" ]; then
    run_wrk "$mode" "$name" "$path"
  else
    run_ab "$mode" "$name" "$path"
  fi
}

make

{
  printf 'XIAOCHEN WebServer benchmark\n'
  printf 'time=%s\n' "$(date '+%F %T')"
  printf 'tool=%s\n' "$BENCH_TOOL"
  printf 'port=%s\n' "$PORT"
  printf 'modes=%s\n' "$MODES"
  printf 'sql_pool=%s thread_pool=%s bench_threads=%s connections=%s duration=%s\n' \
    "$SQL_POOL" "$THREAD_POOL" "$BENCH_THREADS" "$CONNECTIONS" "$DURATION"
} | tee "$RESULT_DIR/summary.txt"

for mode in $MODES; do
  cleanup_server

  printf '\nStarting server with -m %s\n' "$mode" | tee -a "$RESULT_DIR/summary.txt"
  ./build/server -p "$PORT" -s "$SQL_POOL" -t "$THREAD_POOL" -m "$mode" \
    >"$RESULT_DIR/mode-${mode}-server.out" 2>&1 &
  SERVER_PID="$!"

  if ! wait_ready; then
    printf 'Server did not become ready for mode %s. See %s\n' "$mode" "$RESULT_DIR/mode-${mode}-server.out" >&2
    exit 1
  fi

  run_case "$mode" "login-html" "/login.html"
  run_case "$mode" "api-images" "/api/images?page=1&limit=12"

  cleanup_server
done

printf '\nBenchmark complete. Summary: %s\n' "$RESULT_DIR/summary.txt"

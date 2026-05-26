# Stress Testing

This project runs on the remote Linux server. Run these tests on the server
under `/root/codexhtml`, not from the Windows workspace.

## Install tools

`wrk` is preferred because it reports latency percentiles. `ab` is supported as
a fallback.

```bash
sudo apt update
sudo apt install -y wrk apache2-utils
```

## Prepare runtime config

The benchmark script loads these local files if they exist:

```text
config/local/.env
config/local/smtp.env
config/local/redis.env
```

At minimum, `DB_PASS` must be configured:

```bash
mkdir -p config/local
printf 'export DB_USER="root"\nexport DB_PASS="your_mysql_password"\nexport DB_NAME="qgydb"\n' > config/local/.env
chmod 600 config/local/.env
```

Do not commit `config/local/.env`.

## Run the mode benchmark

Stop any existing process that is already listening on `9006`, then run:

```bash
cd /root/codexhtml
bash scripts/bench-modes.sh
```

The script builds the server, then tests:

```text
-m 0: listen LT, conn LT
-m 1: listen LT, conn ET
-m 2: listen ET, conn LT
-m 3: listen ET, conn ET
```

Each mode starts a fresh `./build/server`, tests `/login.html` and
`/api/images?page=1&limit=12`, then stops only the process started by the
script.

Results are written under:

```text
logs/bench-YYYYMMDD-HHMMSS/
```

The main comparison file is:

```text
logs/bench-YYYYMMDD-HHMMSS/summary.txt
```

## Useful parameters

```bash
CONNECTIONS=200 DURATION=60s BENCH_THREADS=8 bash scripts/bench-modes.sh
```

Common knobs:

```text
MODES="0 1 2 3"       modes to test
PORT=9006             local C++ server port
SQL_POOL=4            -s value passed to the server
THREAD_POOL=8         -t value passed to the server
CONNECTIONS=100       benchmark concurrency
BENCH_THREADS=4       wrk worker threads
DURATION=30s          measured duration per case
WARMUP_DURATION=5s    warmup duration per case
RESULT_DIR=...        custom output directory
```

For `ab`, you can also set:

```bash
TOTAL_REQUESTS=20000 bash scripts/bench-modes.sh
```

## Recommended test matrix

Start with a light run:

```bash
CONNECTIONS=50 DURATION=20s bash scripts/bench-modes.sh
```

Then test normal pressure:

```bash
CONNECTIONS=200 DURATION=60s BENCH_THREADS=8 bash scripts/bench-modes.sh
```

Then test a higher connection count:

```bash
CONNECTIONS=500 DURATION=60s BENCH_THREADS=8 bash scripts/bench-modes.sh
```

Watch system resources in another SSH session:

```bash
top
ss -ant state established '( sport = :9006 )' | wc -l
tail -f logs/error.log
```

## How to read the result

For `wrk`, compare:

```text
Requests/sec    higher is better
Latency Avg     lower is better
Latency 99%     lower and stable is better
Socket errors   should be 0 or very low
Non-2xx          should be expected for protected endpoints only
```

For `ab`, compare:

```text
Requests per second    higher is better
Time per request       lower is better
Failed requests        should be 0
Transfer rate          useful for static files
```

## Public IP testing

Use public testing carefully. For security, keep direct public access to `9006`
closed and benchmark the public entry through Nginx:

```bash
wrk -t4 -c100 -d30s --latency http://116.62.240.214/login.html
```

The `-m` mode comparison should be done locally against `127.0.0.1:9006`,
because those modes are C++ WebServer internals.

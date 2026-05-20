#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BASE_URL="${BASE_URL:-http://localhost:9006}"
WORK_DIR="${TMPDIR:-/tmp}/xiaochen-smoke-$$"
COOKIE_JAR="$WORK_DIR/cookies.txt"
IMAGE_FILE="$WORK_DIR/smoke.png"
VIDEO_FILE="$WORK_DIR/smoke.mp4"
LAST_BODY="$WORK_DIR/last-body.txt"
LAST_HEADERS="$WORK_DIR/last-headers.txt"
FAILURES=0

cd "$PROJECT_DIR" || exit 1

if ! command -v curl >/dev/null 2>&1; then
  printf '[FAIL] curl is required but was not found in PATH.\n'
  exit 1
fi

mkdir -p "$WORK_DIR"

pass() {
  printf '[PASS] %s\n' "$1"
}

fail() {
  printf '[FAIL] %s\n' "$1"
  if [ -s "$LAST_BODY" ]; then
    printf '       response: '
    head -c 300 "$LAST_BODY"
    printf '\n'
  fi
  FAILURES=$((FAILURES + 1))
}

request() {
  local method="$1"
  local path="$2"
  shift 2

  curl -sS \
    -X "$method" \
    -D "$LAST_HEADERS" \
    -o "$LAST_BODY" \
    -w '%{http_code}' \
    "$@" \
    "$BASE_URL$path"
}

expect_status() {
  local name="$1"
  local expected="$2"
  local actual="$3"

  if [ "$actual" = "$expected" ]; then
    pass "$name"
  else
    fail "$name: expected HTTP $expected, got $actual"
  fi
}

expect_body_contains() {
  local name="$1"
  local pattern="$2"

  if grep -Fq "$pattern" "$LAST_BODY"; then
    pass "$name"
  else
    fail "$name: response did not contain '$pattern'"
  fi
}

extract_json_path() {
  sed -n 's/.*"path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$LAST_BODY" | head -n 1
}

cleanup_file_from_url_path() {
  local url_path="$1"
  local file_path

  case "$url_path" in
    /images/*) file_path="public${url_path}" ;;
    /videos/*) file_path="public${url_path}" ;;
    *) return 0 ;;
  esac

  if [ -f "$file_path" ]; then
    rm "$file_path"
  fi
}

cleanup() {
  rm "$COOKIE_JAR" 2>/dev/null || true
  rm "$IMAGE_FILE" 2>/dev/null || true
  rm "$VIDEO_FILE" 2>/dev/null || true
  rm "$LAST_BODY" 2>/dev/null || true
  rm "$LAST_HEADERS" 2>/dev/null || true
  rmdir "$WORK_DIR" 2>/dev/null || true
}
trap cleanup EXIT

printf 'Smoke testing %s\n\n' "$BASE_URL"

status="$(request GET /login.html)"
expect_status "static login page" 200 "$status"
expect_body_contains "static login page content" "<html"

status="$(request GET /app.html)"
expect_status "static image app page" 200 "$status"

status="$(request GET /video.html)"
expect_status "static video page" 200 "$status"

status="$(request GET /images/)"
expect_status "image directory browsing is forbidden" 403 "$status"

status="$(request GET /videos/)"
expect_status "video directory browsing is forbidden" 403 "$status"

status="$(request GET /api/images)"
expect_status "image list api" 200 "$status"
expect_body_contains "image list api json" "["

status="$(request GET /api/videos)"
expect_status "video list api" 200 "$status"
expect_body_contains "video list api json" "["

status="$(request POST /api/login \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  -d 'username=admin&password=12345&remember=on' \
  -c "$COOKIE_JAR")"
expect_status "admin login api" 200 "$status"
expect_body_contains "admin login api json" '"ok":true'

status="$(request POST /api/login \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  -d 'username=admin&password=wrong')"
expect_status "login rejects bad password" 401 "$status"
expect_body_contains "login failure json" '"ok":false'

status="$(request POST /api/send-email-code \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  -d 'email=not-an-email')"
expect_status "email code validates email" 400 "$status"
expect_body_contains "email code validation json" '"ok":false'

status="$(request POST /api/register \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  -d 'phone=10000000000&email=smoke@example.com&email_code=000000&password=12345')"
expect_status "register validates email code" 400 "$status"
expect_body_contains "register validation json" '"ok":false'

printf '\x89PNG\r\n\x1a\n' > "$IMAGE_FILE"
status="$(request POST /api/upload \
  -b "$COOKIE_JAR" \
  -F "image=@$IMAGE_FILE;type=image/png")"
expect_status "image upload api" 200 "$status"
expect_body_contains "image upload api json" '"ok":true'
uploaded_image="$(extract_json_path)"
cleanup_file_from_url_path "$uploaded_image"

printf '\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom' > "$VIDEO_FILE"
status="$(request POST /api/upload-video \
  -b "$COOKIE_JAR" \
  -F "video=@$VIDEO_FILE;type=video/mp4")"
expect_status "video upload api" 200 "$status"
expect_body_contains "video upload api json" '"ok":true'
uploaded_video="$(extract_json_path)"
cleanup_file_from_url_path "$uploaded_video"

printf '\n'
if [ "$FAILURES" -eq 0 ]; then
  printf 'All smoke checks passed.\n'
  exit 0
fi

printf '%s smoke check(s) failed.\n' "$FAILURES"
exit 1

# Runtime Configuration

`scripts/run_server.local.sh` loads `config/local/.env` and `config/local/smtp.env`
before starting the server. Keep those files local and do not commit secrets.

## Database

```bash
export DB_HOST="127.0.0.1"
export DB_PORT="3306"
export DB_USER="root"
export DB_PASS="your_mysql_password"
export DB_NAME="qgydb"
```

`DB_PASS` is required. The server refuses to start when it is empty.

## Static Files

```bash
export XIAOCHEN_STATIC_ROOT="/absolute/path/to/public"
```

When this value is empty, the server uses `<current working directory>/public`.

## Upload Limits

The limits are byte counts.

```bash
export XIAOCHEN_MAX_IMAGE_UPLOAD_BYTES="20971520"
export XIAOCHEN_MAX_VIDEO_UPLOAD_BYTES="1073741824"
```

Defaults:

- Images: 20 MB
- Videos: 1 GB

## Redis

Redis is used for email verification codes, verification-code rate limits, failed
attempt counters, and short-lived image/video list caches.

```bash
export XIAOCHEN_REDIS_ENABLED="1"
export XIAOCHEN_REDIS_HOST="127.0.0.1"
export XIAOCHEN_REDIS_PORT="6379"
export XIAOCHEN_REDIS_PASSWORD=""
export XIAOCHEN_REDIS_DB="0"
export XIAOCHEN_REDIS_TIMEOUT_MS="1000"
```

Defaults:

- Redis enabled: yes
- Host: `127.0.0.1`
- Port: `6379`
- DB: `0`
- Timeout: 1000 ms

When Redis is enabled but unavailable, `/api/send-email-code` fails explicitly
instead of silently storing verification codes in process memory. Image and video
list APIs fall back to scanning the filesystem if the Redis list cache is missed
or unavailable.

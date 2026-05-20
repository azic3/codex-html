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


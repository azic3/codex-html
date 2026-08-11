#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REMOTE="${DEPLOY_REMOTE:-origin}"
BRANCH="${1:-main}"
SERVICE="${CODEXHTML_SERVICE:-codexhtml}"

log() {
    printf '[deploy] %s\n' "$*"
}

fail() {
    printf '[deploy] 失败：%s\n' "$*" >&2
    exit 1
}

if ! git -C "$APP_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    fail "$APP_DIR 不是 Git 仓库"
fi

cd "$APP_DIR"

if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    git status --short
    fail "服务器存在未提交的受版本控制文件"
fi

log "branch: $BRANCH"
git fetch --prune "$REMOTE" "$BRANCH"

REMOTE_REF="refs/remotes/$REMOTE/$BRANCH"
if ! git show-ref --verify --quiet "$REMOTE_REF"; then
    fail "远程分支 $REMOTE/$BRANCH 不存在"
fi

OLD_COMMIT="$(git rev-parse HEAD)"
TARGET_COMMIT="$(git rev-parse "$REMOTE_REF")"

if [[ "$OLD_COMMIT" == "$TARGET_COMMIT" ]]; then
    log "已是最新版本：$OLD_COMMIT"
    exit 0
fi

if ! git merge-base --is-ancestor "$OLD_COMMIT" "$TARGET_COMMIT"; then
    fail "服务器提交不是 $REMOTE/$BRANCH 的祖先，请人工检查提交图"
fi

CHANGED_FILES="$(git diff --name-only "$OLD_COMMIT" "$TARGET_COMMIT")"

git merge --ff-only "$TARGET_COMMIT"

log "本次更新文件："
printf '%s\n' "$CHANGED_FILES"

if grep -Eq '^(src/|Makefile$)' <<<"$CHANGED_FILES"; then
    log "检测到后端变化，开始编译"
    make -j"$(nproc)"

    if systemctl cat "$SERVICE.service" >/dev/null 2>&1; then
        log "重启 $SERVICE.service"
        systemctl restart "$SERVICE.service"
        systemctl --no-pager --full status "$SERVICE.service"
    else
        fail "编译完成，但未找到 $SERVICE.service，请人工重启后端"
    fi
else
    log "仅静态文件或文档变化，无需重新编译后端"
fi

if curl --fail --silent --show-error --output /dev/null http://127.0.0.1/login.html; then
    log "HTTP 检查通过"
else
    fail "代码已更新，但 HTTP 检查失败"
fi

log "部署成功：$(git rev-parse HEAD)"

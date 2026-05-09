# XIAOCHEN WebServer

XIAOCHEN WebServer 是一个基于 C++ 的轻量级 WebServer 示例项目，使用 Linux 网络编程模型实现静态资源访问、用户登录注册、邮箱验证码、图片上传和视频上传等功能。

## 功能特性

- 使用 `epoll` 处理 I/O 事件
- 使用线程池处理请求
- 支持静态文件访问和目录列表
- 支持登录、手机号注册、邮箱验证码校验
- 支持真实 SMTP 邮箱验证码发送
- 支持图片上传、图片列表和预览
- 支持视频上传和在线播放
- 使用 MySQL 保存用户账号信息
- 用户密码使用带盐哈希存储
- 使用 `alarm + SIGALRM + socketpair` 将定时器信号纳入 epoll 主循环，清理非活跃连接

## 项目结构

```text
.
├── src/                  # C++ 后端源码
│   ├── config/           # 命令行参数解析
│   ├── db/               # MySQL 连接池与用户表操作
│   ├── http/             # HTTP 请求解析
│   ├── mail/             # SMTP 邮箱验证码发送
│   ├── security/         # 密码哈希与验证
│   ├── server/           # WebServer 主体逻辑
│   ├── threadpool/       # 线程池
│   └── main.cpp          # 程序入口
├── public/               # 浏览器可访问的页面和静态资源
│   ├── css/
│   ├── js/
│   ├── images/
│   ├── login.html
│   ├── app.html
│   └── video.html
├── docs/                 # 补充说明文档
├── scripts/              # 本地启动脚本
├── config/local/         # 本地密钥配置，已被 git 忽略
└── Makefile
```

## 环境要求

项目依赖 Linux/WSL 环境，Windows 原生编译通常不可用。

需要安装：

- C++11 编译器，例如 `g++`
- MySQL 客户端开发库
- libcurl 开发库
- system crypt 支持

Ubuntu/WSL 示例：

```bash
sudo apt update
sudo apt install g++ make libmysqlclient-dev libcurl4-openssl-dev
```

如果系统缺少 `libcrypt` 开发包，可额外安装：

```bash
sudo apt install libxcrypt-dev
```

## 数据库准备

项目默认连接 MySQL 数据库 `qgydb`，用户表名为 `user`，核心字段：

```sql
CREATE TABLE IF NOT EXISTS user (
  username VARCHAR(64) NOT NULL PRIMARY KEY,
  passwd VARCHAR(255) NOT NULL
);
```

如果已有旧表，请确认密码字段长度足够保存哈希：

```sql
ALTER TABLE user MODIFY passwd VARCHAR(255) NOT NULL;
```

当前 `main.cpp` 中仍保留了演示用数据库账号配置，实际部署时建议改为环境变量或配置文件。

## SMTP 配置

邮箱验证码通过 SMTP 真实发送。启动服务前需要配置环境变量：

```bash
export XIAOCHEN_SMTP_URL="smtps://smtp.qq.com:465"
export XIAOCHEN_SMTP_USER="your_email@qq.com"
export XIAOCHEN_SMTP_PASSWORD="your_smtp_auth_code"
export XIAOCHEN_SMTP_FROM="your_email@qq.com"
export XIAOCHEN_SMTP_FROM_NAME="XIAOCHEN WebServer"
export XIAOCHEN_SMTP_LOGIN_OPTIONS="AUTH=LOGIN"
```

注意：`XIAOCHEN_SMTP_PASSWORD` 通常是邮箱服务商提供的 SMTP 授权码，不是邮箱登录密码。

更多说明见 [docs/SMTP_SETUP.md](docs/SMTP_SETUP.md)。

## 构建

```bash
make
```

或手动编译：

```bash
g++ -std=c++11 -pthread \
  -Isrc/config -Isrc/server -Isrc/http -Isrc/threadpool -Isrc/db -Isrc/mail -Isrc/security \
  src/main.cpp src/config/config.cpp src/server/webserver.cpp src/http/http_conn.cpp \
  src/threadpool/threadpool.cpp src/db/CGmysql.cpp src/mail/smtp_client.cpp \
  src/security/password_hasher.cpp -lmysqlclient -lcurl -lcrypt -o build/server
```

## 运行

```bash
./build/server -p 9006 -s 4 -t 8 -m 0
```

访问：

```text
http://localhost:9006/login.html
http://localhost:9006/app.html
http://localhost:9006/video.html
```

## 命令行参数

```text
-p 监听端口
-l 日志写入模式，当前为保留字段
-m 触发模式
-o linger 配置
-s MySQL 连接池数量
-t 线程池线程数
-c 关闭日志标志，当前为保留字段
-a actor model 模式
```

触发模式：

```text
0: listen LT, conn LT
1: listen LT, conn ET
2: listen ET, conn LT
3: listen ET, conn ET
```

## API 概览

登录注册：

```text
POST /api/login
POST /api/register
POST /api/send-email-code
POST /api/reset
```

图片和视频：

```text
POST /api/upload
POST /api/upload-video
GET  /api/images
GET  /api/videos
```

## 安全说明

- 不要提交 `config/local/smtp.env`、`scripts/run_server.local.sh` 等本地密钥配置文件
- 不要提交编译产物 `build/server`
- 用户密码已改为带盐哈希存储，旧明文密码登录成功后会自动迁移为哈希
- SMTP 授权码泄露后应立即在邮箱服务商后台重新生成
- 当前项目仍为学习/演示项目，生产环境还应补充 HTTPS、请求限流、日志脱敏、密码重置流程和更完整的输入校验

## 相关文档

- [docs/SMTP_SETUP.md](docs/SMTP_SETUP.md)
- [docs/PASSWORD_HASHING.md](docs/PASSWORD_HASHING.md)

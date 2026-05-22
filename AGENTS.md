# 项目说明：XIAOCHEN WebServer

## 项目概览

这是一个基于 C++ 的轻量 WebServer 项目，核心使用 Linux 网络编程模型：

- 使用 `epoll` 处理 I/O 事件。
- 使用线程池处理请求。
- 支持静态资源访问、目录列表、图片上传、视频上传。
- 支持登录、手机号注册、邮箱验证码校验。
- 使用 MySQL 保存用户账号信息。
- 使用 `alarm + SIGALRM + socketpair` 将定时器信号统一纳入 epoll 主循环，用于清理非活跃连接。

项目根目录按功能拆分：`src/` 放后端 C++ 源码，`public/` 放前端页面和静态资源，`docs/` 放补充文档，`scripts/` 放本地脚本，`config/local/` 放本地私密配置。

## 目录结构

```text
.
├── src/
│   ├── main.cpp          # 程序入口，初始化配置和 WebServer
│   ├── config/           # 命令行参数解析
│   ├── server/           # WebServer 主体：epoll、路由、静态文件、上传、定时器
│   ├── http/             # HTTP 请求解析、URL 解码、响应序列化
│   ├── db/               # MySQL 连接池与用户表操作
│   ├── threadpool/       # 简单线程池
│   ├── mail/             # SMTP 邮箱验证码发送
│   └── security/         # 密码哈希与校验
├── public/
│   ├── index.html        # 静态演示首页
│   ├── login.html        # 登录、手机号注册、找回密码页面
│   ├── app.html          # 图片库页面
│   ├── video.html        # 视频库页面
│   ├── css/              # 页面样式
│   ├── js/               # 前端交互
│   ├── images/           # 内置图片资源与上传图片目录
│   └── videos/           # 上传视频目录
├── docs/                 # SMTP、密码哈希等说明
├── scripts/              # 本地启动脚本
├── config/local/         # 本地密钥配置，不提交
├── build/server          # 已编译出的 Linux 可执行文件
└── .vs/                  # Visual Studio 本地缓存/索引，通常不要手动维护
```

## 后端架构

### 启动流程

`main.cpp` 从环境变量读取 MySQL 连接信息：

- 用户名：`DB_USER`，默认 `root`
- 密码：`DB_PASS`，必须配置
- 数据库：`DB_NAME`，默认 `qgydb`

启动时流程如下：

1. `Config::parse_arg()` 读取命令行参数。
2. `WebServer::init()` 保存配置、初始化 MySQL 连接池、创建线程池。
3. `WebServer::eventListen()` 创建监听 socket、配置 epoll、注册信号管道。
4. `WebServer::eventLoop()` 进入 epoll 主循环。

默认监听端口是 `9006`。

### 命令行参数

`config.cpp` 支持这些参数：

```text
-p 端口
-l 日志写入模式（当前代码中保留字段）
-m 触发模式
-o linger 配置
-s MySQL 连接池数量
-t 线程池线程数
-c 关闭日志标志（当前代码中保留字段）
-a actor model 模式
```

触发模式 `TRIGMode`：

```text
0: listen LT, conn LT
1: listen LT, conn ET
2: listen ET, conn LT
3: listen ET, conn ET
```

### 网络与连接处理

`src/server/webserver.cpp` 是核心文件：

- `addfd()` 将 fd 加入 epoll，并设置非阻塞。
- `modfd()` 修改 epoll 事件，并使用 `EPOLLONESHOT`。
- `dealclientdata()` 接受新连接，并为连接创建定时器。
- `read_http_request()` 读取请求，支持未读完整请求时缓存到 `m_pending_requests`。
- `dealwithread()` 读取并分发请求。
- `process_request()` 解析 HTTP 请求、调用路由、发送响应。
- `closefd()` 负责从 epoll、定时器映射、pending 请求中清理连接。

### 定时器与统一事件源

项目实现了基于升序链表的定时器：

- `UtilTimer` 保存 `sockfd`、过期时间、前后指针。
- `SortTimerList` 维护按 `expire` 升序排列的定时器链表。
- 新连接默认超时时间是 `3 * TIMESLOT`，其中 `TIMESLOT = 5`，即约 15 秒。
- `alarm(TIMESLOT)` 周期触发 `SIGALRM`。
- 信号处理函数只向 `socketpair` 管道写入信号值。
- epoll 主循环监听管道读端，收到 `SIGALRM` 后调用 `timer_handler()` 清理过期连接。

这个设计避免在信号处理函数里做复杂逻辑，符合统一事件源思路。

## HTTP 接口

后端通过 `handle_request()` 分发接口。

### 登录注册

```text
POST /api/login
POST /api/register
POST /api/send-email-code
POST /api/reset
```

说明：

- `/api/login` 使用表单字段 `username`、`password`、`remember`。
- `/api/register` 使用 `phone`、`email`、`email_code`、`password`。
- `/api/reset` 使用 `phone`、`email`、`email_code`、`password`，验证码校验成功后会把新密码哈希写入手机号账号。
- 手机号按 `1` 开头的 11 位数字校验。
- 邮箱验证码为 6 位数字，内存保存，5 分钟过期。
- 邮箱验证码已接入真实 SMTP 发送，SMTP 配置通过环境变量读取，发送成功后才会保存验证码。
- 当前用户表没有邮箱字段，所以找回密码流程尚不能校验邮箱与手机号的绑定关系。
- 如果数据库不可用，登录保留了兜底账号：`admin / 12345`。

### 图片和视频

```text
POST /api/upload
POST /api/upload-video
GET  /api/images
GET  /api/videos
```

说明：

- 图片上传字段名是 `image`。
- 视频上传字段名是 `video`。
- 单文件大小限制是 20MB。
- 图片允许：`png`、`jpg`、`jpeg`、`gif`、`bmp`、`webp`、`svg`。
- 视频允许：`mp4`、`webm`、`ogg`、`mov`、`avi`、`mkv`、`m4v`。
- 上传文件会经过文件名清理和唯一命名。
- 图片保存到 `public/images/`，视频保存到 `public/videos/`，浏览器路径分别是 `/images/...` 和 `/videos/...`。

### 静态资源和目录

- 根路径 `/` 会优先返回 `/login.html`。
- 普通文件按扩展名返回 Content-Type。
- 目录请求会生成 HTML 目录列表。
- `is_safe_path()` 禁止包含 `..` 的路径，避免路径穿越。

## 数据库相关

`CGmysql` 实现了一个简单连接池：

- 编译期通过 `__has_include` 判断是否存在 MySQL 头文件。
- 如果没有 MySQL 客户端头文件，`CGMYSQL_HAS_CLIENT` 为 `0`，数据库功能会不可用。
- 用户表默认名为 `user`。
- 代码使用字段：
  - `username`
  - `passwd`

当前手机号注册会把手机号写入 `user.username` 字段，密码哈希写入 `user.passwd` 字段。

注意：新注册密码会使用 SHA-512 crypt 带盐哈希保存。旧数据库中已存在的明文密码仍可登录一次，登录成功后会自动迁移为哈希。

## 前端页面

### 登录页

文件：

- `public/login.html`
- `public/css/login.css`
- `public/js/login.js`

功能：

- 登录面板。
- 手机号注册面板。
- 邮箱验证码获取。
- 找回密码面板，支持邮箱验证码校验后重置手机号账号密码。
- 前端会校验手机号、邮箱、验证码格式和两次密码是否一致。

### 图片库

文件：

- `public/app.html`
- `public/css/app.css`
- `public/js/app.js`

功能：

- 从 `/api/images` 加载图片列表。
- 上传图片到 `/api/upload`。
- 点击图片卡片打开预览弹窗。
- 首图会同步显示在头部预览区。

### 视频库

文件：

- `public/video.html`
- `public/css/app.css`
- `public/js/video.js`

功能：

- 从 `/api/videos` 加载视频列表。
- 上传视频到 `/api/upload-video`。
- 列表中直接使用 `<video controls>` 播放。

## 构建和运行

### 运行环境说明

本项目实际运行和验证环境是远程 Linux 服务器，用户通过 FinalShell 连接服务器运行 `make`、启动 `./build/server`、查看日志和测试接口。

本地 Windows 工作区主要用于查看和编辑代码，不作为项目的真实运行环境。后续协作时，不要反复把本机 Windows/WSL 权限问题作为主要阻塞原因；需要编译、运行、重启服务或查看远程日志时，应明确提示用户在远程 Linux 服务器上执行对应命令，或让用户贴出远程执行结果。

项目依赖 Linux 网络头文件，例如：

- `sys/epoll.h`
- `sys/socket.h`
- `unistd.h`

因此应在 Linux 或 WSL/Linux 容器中编译运行。Windows 原生编译通常会失败。

参考编译命令：

```bash
make
```

运行示例：

```bash
./build/server -p 9006 -s 4 -t 8 -m 0
```

然后访问：

```text
http://localhost:9006/login.html
http://localhost:9006/app.html
http://localhost:9006/video.html
```

## 已知注意事项

- 这是 Linux epoll 项目，不适合用 Windows 原生编译器直接构建。
- 本地曾遇到 WSL 权限问题，导致无法完成 Linux 编译验证。
- GitHub CLI `gh` 当前不可用；如果要创建 PR，需要先安装并登录 `gh`。
- `.vs/` 是 Visual Studio 生成目录，可能包含缓存文件，不建议作为业务代码修改。
- 部分终端读取 UTF-8 中文文件时可能显示乱码；浏览器按 `<meta charset="UTF-8">` 解析。
- 邮箱验证码已接入真实 SMTP 发送；运行前需要配置 SMTP 环境变量。
- `index.html` 是静态演示页，主入口当前更偏向 `login.html`。
- `build/server` 是已编译二进制文件，改源码后需要重新编译才会生效。
- 运行日志写入 `logs/app.log`，错误日志写入 `logs/error.log`；日志写入前会脱敏密码、验证码、token、SMTP 授权码等敏感信息。

## 后续开发建议

- 将 MySQL 账号、数据库名、端口、静态资源目录、上传大小限制等运行配置从源码迁移到环境变量或配置文件。
- 为用户表增加 `email`、`created_at`、`updated_at` 等字段，并在注册和找回密码时校验手机号与邮箱绑定关系。
- 增加数据库初始化 SQL 或轻量迁移脚本，避免部署时手动创建和调整表结构。
- 将用户查询、注册、改密等 SQL 拼接逻辑改为参数化语句，降低输入边界和 SQL 注入风险。
- 为验证码、登录、注册、找回密码、上传接口增加限流、失败次数限制、验证码尝试次数限制和验证码过期清理策略。
- 将 SHA-512 crypt 密码哈希升级为 Argon2id 或 bcrypt，并提供清晰的哈希版本迁移方案。
- 强化登录态安全：增加会话过期、退出登录、Cookie `HttpOnly` / `SameSite` / `Secure` 配置和简单 CSRF 防护。
- 为上传文件增加 MIME 嗅探、随机存储名、访问权限控制、磁盘配额、文件清理任务和危险 SVG 内容防护。
- 为视频播放补充 HTTP Range 请求支持，让大视频可以拖动进度和断点加载。
- 为登录、注册、验证码、图片上传、视频上传、静态文件和目录列表补充最小 curl 验证脚本或自动化测试。
- 增加 CI 构建检查、格式化检查和基础接口回归测试，减少改动后才发现 Linux 编译或运行问题。
- 增加 HTTPS/反向代理部署示例，例如 Nginx + systemd 服务配置，并补充 WSL/Linux 容器本地运行说明。
- 为前端补充上传进度、加载状态、空列表状态、可重试错误提示和基础无障碍属性。

# XIAOCHEN WebServer 当前协作架构补充

## 最新协作上下文：当前项目架构

当前项目已从“C++ WebServer 直接发送全部页面和媒体文件”升级为 **Nginx + C++ WebServer** 的分层架构。

### 部署入口

- 公网入口：`http://116.62.240.214`
- C++ 服务端口：`9006`
- 服务器项目路径：`/root/codexhtml`
- Nginx 示例配置：`docs/nginx-x-accel-example.conf`

### 分层职责

```text
浏览器
  |
  v
Nginx :80
  |
  |-- /css/、/js/：Nginx 直接发送
  |-- /api/：反向代理到 127.0.0.1:9006
  |-- /media/images/、/media/videos/：反向代理到 C++ 做登录态鉴权
      |
      v
    C++ WebServer
      |
      |-- 未登录：302 /login.html 或 401 JSON
      |-- 已登录：返回 X-Accel-Redirect
              |
              v
            Nginx internal alias 发送 public/images 或 public/videos 中的真实文件
```

### 媒体访问规则

- 浏览器可见媒体路径使用 `/media/images/文件名` 和 `/media/videos/文件名`。
- 真实文件存储路径仍是 `public/images/` 和 `public/videos/`。
- Nginx internal 路径为 `/_protected_images/` 和 `/_protected_videos/`。
- `/_protected_images/` 和 `/_protected_videos/` 必须配置 `internal`，不能允许公网直接访问。
- `/media/` 位置需要代理到 C++ 服务，并在 X-Accel 正式启用时设置 `proxy_set_header X-Accel-Enabled 1;`。

### 登录态和鉴权

- 登录成功后，后端生成随机 Session Token。
- Cookie 名称：`XIAOCHEN_SESSION`。
- Cookie 属性：`HttpOnly; Path=/; SameSite=Lax; Max-Age=...`。
- 默认 Session 有效期：2 小时。
- 勾选记住登录后有效期：7 天。
- `/app.html`、`/video.html`、`/profile.html` 和除登录/注册/验证码/找回密码外的 `/api/*` 都需要登录态。
- 未登录访问受保护页面会跳转 `/login.html`；未登录访问受保护 API 返回 `401 JSON`。

### 图片和视频能力

- `/api/images?page=1&limit=12` 支持分页。
- 图片库前端使用瀑布流布局和懒加载。
- 视频上传优先走 2MB 分片接口：`POST /api/upload-video-chunk`。
- 普通图片上传仍走：`POST /api/upload`。
- 上传后的浏览器可见路径统一返回 `/media/images/...` 或 `/media/videos/...`。

### 协作注意事项

- 本地 Windows 工作区只用于编辑代码，不作为真实运行环境。
- 需要编译、运行、重启服务、验证接口时，应让用户在远程 Linux 服务器 `/root/codexhtml` 执行。
- 修改 C++ 后端后，需要在服务器执行 `cd /root/codexhtml && make`。
- 修改 Nginx 配置后，需要执行 `sudo nginx -t` 和 `sudo systemctl reload nginx`。
- 不要把本地 Windows 路径写入 Nginx；Nginx 使用服务器 Linux 路径 `/root/codexhtml/...`。

---

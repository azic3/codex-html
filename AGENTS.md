# 项目说明：XIAOCHEN WebServer

## 项目概览

这是一个基于 C++ 的轻量 WebServer 项目，核心使用 Linux 网络编程模型：

- 使用 `epoll` 处理 I/O 事件。
- 使用线程池处理请求。
- 支持静态资源访问、目录列表、图片上传、视频上传。
- 支持登录、手机号注册、邮箱验证码校验。
- 使用 MySQL 保存用户账号信息。
- 使用 `alarm + SIGALRM + socketpair` 将定时器信号统一纳入 epoll 主循环，用于清理非活跃连接。

项目根目录同时包含后端 C++ 源码、前端页面、CSS/JS 静态资源和图片资源。

## 目录结构

```text
.
├── main.cpp              # 程序入口，初始化配置和 WebServer
├── config.h/.cpp         # 命令行参数解析
├── webserver.h/.cpp      # WebServer 主体：epoll、路由、静态文件、上传、定时器
├── http_conn.h/.cpp      # HTTP 请求解析、URL 解码、响应序列化
├── CGmysql.h/.cpp        # MySQL 连接池与用户表操作
├── threadpool.h/.cpp     # 简单线程池
├── index.html            # 静态演示首页
├── login.html            # 登录、手机号注册、找回密码页面
├── app.html              # 图片库页面
├── video.html            # 视频库页面
├── css/
│   ├── app.css           # 图片/视频管理页样式
│   └── login.css         # 登录注册页样式
├── js/
│   ├── app.js            # 图片列表加载、上传、预览弹窗
│   ├── video.js          # 视频列表加载、上传
│   └── login.js          # 登录、注册、邮箱验证码交互
├── images/               # 内置图片资源与上传图片目录
├── videos/               # 运行时可能创建；上传视频目录
├── server                # 已编译出的 Linux 可执行文件
└── .vs/                  # Visual Studio 本地缓存/索引，通常不要手动维护
```

## 后端架构

### 启动流程

`main.cpp` 中硬编码了 MySQL 连接信息：

- 用户名：`root`
- 密码：`Chencong123..`
- 数据库：`qgydb`

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

`webserver.cpp` 是核心文件：

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
- 手机号按 `1` 开头的 11 位数字校验。
- 邮箱验证码为 6 位数字，内存保存，5 分钟过期。
- 当前验证码是开发演示模式：后端会打印到控制台，并在响应里返回 `debug_code`。
- `/api/reset` 目前仍是占位接口。
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
- 图片保存到 `images/`，视频保存到 `videos/`。

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

当前手机号注册会把手机号写入 `user.username` 字段，密码写入 `user.passwd` 字段。

注意：密码是明文保存，没有哈希；如果要用于真实生产环境，需要改成安全哈希存储。

## 前端页面

### 登录页

文件：

- `login.html`
- `css/login.css`
- `js/login.js`

功能：

- 登录面板。
- 手机号注册面板。
- 邮箱验证码获取。
- 找回密码占位面板。
- 前端会校验手机号、邮箱、验证码格式和两次密码是否一致。

### 图片库

文件：

- `app.html`
- `css/app.css`
- `js/app.js`

功能：

- 从 `/api/images` 加载图片列表。
- 上传图片到 `/api/upload`。
- 点击图片卡片打开预览弹窗。
- 首图会同步显示在头部预览区。

### 视频库

文件：

- `video.html`
- `css/app.css`
- `js/video.js`

功能：

- 从 `/api/videos` 加载视频列表。
- 上传视频到 `/api/upload-video`。
- 列表中直接使用 `<video controls>` 播放。

## 构建和运行

项目依赖 Linux 网络头文件，例如：

- `sys/epoll.h`
- `sys/socket.h`
- `unistd.h`

因此应在 Linux 或 WSL/Linux 容器中编译运行。Windows 原生编译通常会失败。

参考编译命令：

```bash
g++ -std=c++11 -pthread main.cpp config.cpp webserver.cpp http_conn.cpp threadpool.cpp CGmysql.cpp -lmysqlclient -o server
```

运行示例：

```bash
./server -p 9006 -s 4 -t 8 -m 0
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
- 邮箱验证码目前不是真实 SMTP 发送，只是开发演示模式。
- `index.html` 是静态演示页，主入口当前更偏向 `login.html`。
- `server` 是已编译二进制文件，改源码后需要重新编译才会生效。

## 后续开发建议

- 增加 `.gitignore`，排除 `.vs/`、编译产物、运行时上传目录中的临时文件等。
- 将数据库账号密码移出 `main.cpp`，改为配置文件或环境变量。
- 为用户密码增加哈希存储。
- 接入真实 SMTP 服务，实现真正邮箱验证码发送。
- 为 C++ 后端增加构建脚本或 `Makefile`。
- 给核心接口增加自动化测试或最小 curl 验证脚本。
- 前端中文文件建议统一保存为 UTF-8，避免 Windows 控制台误读。

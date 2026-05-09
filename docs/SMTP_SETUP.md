# SMTP 邮箱验证码配置

后端现在通过 libcurl 调用真实 SMTP 服务发送注册验证码。验证码发送成功后才会写入内存缓存，5 分钟内有效。

## 依赖

Linux/WSL 环境需要安装 libcurl 开发包，例如：

```bash
sudo apt update
sudo apt install libcurl4-openssl-dev
```

## 环境变量

启动服务器前配置以下变量：

```bash
export XIAOCHEN_SMTP_URL="smtps://smtp.example.com:465"
export XIAOCHEN_SMTP_USER="your_email@example.com"
export XIAOCHEN_SMTP_PASSWORD="your_smtp_auth_code"
export XIAOCHEN_SMTP_FROM="your_email@example.com"
export XIAOCHEN_SMTP_FROM_NAME="XIAOCHEN WebServer"
export XIAOCHEN_SMTP_LOGIN_OPTIONS="AUTH=LOGIN"
```

也支持常见 587 端口 STARTTLS：

```bash
export XIAOCHEN_SMTP_URL="smtp://smtp.example.com:587"
```

部分邮箱服务商要求使用“SMTP 授权码”而不是登录密码，例如 QQ 邮箱、163 邮箱、Gmail 应用专用密码。

## 构建

使用 Makefile：

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

访问注册页，点击“获取验证码”后，验证码会发送到用户填写的邮箱。

## 可选项

如果本地测试环境证书链不完整，可以临时跳过 SMTP TLS 证书校验：

```bash
export XIAOCHEN_SMTP_INSECURE_SKIP_VERIFY=1
```

这个选项只建议本地调试使用，正式环境不要开启。

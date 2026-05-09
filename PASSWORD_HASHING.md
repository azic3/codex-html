# 用户密码哈希存储

新注册用户的密码不再明文写入 `user.passwd`，而是保存为 SHA-512 crypt 带盐哈希，格式类似：

```text
$6$rounds=100000$...salt...$...hash...
```

## 数据库字段

哈希字符串比原始密码长，建议确认 `passwd` 字段长度至少为 255：

```sql
ALTER TABLE user MODIFY passwd VARCHAR(255) NOT NULL;
```

## 兼容旧用户

如果数据库里已有明文密码，用户仍可以用原密码登录。登录成功后，服务端会自动把该用户的 `passwd` 字段更新为哈希。

## 构建依赖

Linux 环境需要系统 crypt 支持，构建时链接 `-lcrypt`。Makefile 已经加入：

```makefile
LDLIBS ?= -lmysqlclient -lcurl -lcrypt
```

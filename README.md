# titlebot

titlebot 是一个 IRC 网页标题机器人。它从每条 `PRIVMSG` 中提取第一个 HTTP 或 HTTPS 链接，把网页标题回复到原频道。私聊发来的链接会私聊回复给发送者。

例如，下面这条消息会让机器人抓取 `https://example.org/wiki/Go_(game)`。链接路径中的括号会保留，消息中的第二个链接会跳过。

```text
:alice!user@host PRIVMSG #example :看 (https://example.org/wiki/Go_(game)) https://example.org/next
```

## 构建与测试

项目使用 C++23，以 Debian g++ 14.2.0 和配套 libstdc++ 为兼容基线。构建需要 CMake 3.20 或更高版本、pkg-config、Boost 1.81 或更高版本（含 Boost.URL），以及 uchardet、zlib 和 OpenSSL 开发包。

在 Debian trixie 上，可用下面的命令安装依赖。构建命令从仓库根目录执行。

```sh
sudo apt-get install ca-certificates git cmake make g++-14 pkg-config libboost-url-dev libuchardet-dev zlib1g-dev libssl-dev
CXX=g++-14 cmake -S . -B build -DTITLEBOT_ENABLE_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

测试默认开启，首次配置时 CMake 会通过 Git 获取 GoogleTest v1.15.2。会话和抓取集成测试需要允许本地回环连接。只构建库和可执行文件时，可在配置命令中使用 `-DTITLEBOT_ENABLE_TESTS=OFF`。

## 连接 IRC

```sh
./build/titlebot irc.example.org 6667 titlebot '#example,#dev'
```

四个参数依次为服务器、端口、机器人昵称和频道列表。多个频道用逗号分隔，在 shell 中用引号包住以 `#` 开头的参数。IRC 连接使用明文 TCP，请填写服务器的明文端口。机器人注册成功后自动加入频道。注册等待超过 30 秒会触发重连，连续失败时重试间隔逐步增加，最长为 60 秒。

## 抓取与诊断

- 机器人只连接允许的公网地址，每次重定向都会重新检查目标，最多跟随五次重定向。抓取 HTTPS 网页时使用系统信任的证书验证服务器身份。
- 抓取器收到 `103 Early Hints` 后会继续等待最终响应。解析 HTML 时跳过模板中的标题和字符集声明，读到完整的文档标题即可回复。
- 抓取失败或页面缺少标题时，频道里不会收到回复。可查看标准错误中的汇总日志：`no_title` 统计无标题结果，`timeouts` 和 `decode` 分别统计超时和解码失败，`queue_drops` 和 `reply_drops` 分别统计入队丢弃和回复丢弃。

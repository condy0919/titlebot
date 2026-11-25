# titlebot

titlebot 是一个 IRC 网页标题机器人。它从每条 `PRIVMSG` 中提取第一个 HTTP 或 HTTPS 链接，把网页标题回复到原频道。私聊发来的链接会私聊回复给发送者。

IRC 连接层作为独立的 `ircbot` 库提供，titlebot 通过消息回调使用该库。协议解析、连接、TLS、重连、心跳、能力协商和认证由库负责；网页抓取、标题缓存、请求合并与回复限流由 titlebot 负责。

例如，下面这条消息会让机器人抓取 `https://example.org/wiki/Go_(game)`。链接路径中的括号会保留，消息中的第二个链接会跳过。

```text
:alice!user@host PRIVMSG #example :看 (https://example.org/wiki/Go_(game)) https://example.org/next
```

## 构建与测试

项目使用 C++23，以 Debian g++ 14.2.0 和配套 libstdc++ 为兼容基线。构建需要 CMake 3.20 或更高版本、pkg-config、Boost 1.81 或更高版本（含 Boost.URL），以及 uchardet、zlib 和 OpenSSL 开发包。首次配置时，CMake 通过 FetchContent 获取 spdlog v1.17.0。

在 Debian trixie 上，可用下面的命令安装依赖。构建命令从仓库根目录执行。

```sh
sudo apt-get install ca-certificates git cmake make g++-14 pkg-config libboost-url-dev libuchardet-dev zlib1g-dev libssl-dev
CXX=g++-14 cmake -S . -B build -DTITLEBOT_ENABLE_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

测试默认开启，首次配置时 CMake 会通过 Git 获取 GoogleTest v1.15.2。会话和抓取集成测试需要允许本地回环连接。只构建库和可执行文件时，可在配置命令中使用 `-DTITLEBOT_ENABLE_TESTS=OFF`。

## 独立使用 IRC 库

`ircbot` 的公开头文件位于 `include/ircbot/`，CMake 目标为 `ircbot::ircbot`。它只依赖 C++23、Boost 头文件、OpenSSL 和线程支持，不依赖标题解析、HTTP 抓取、uchardet、zlib、Boost.URL 或 spdlog。只构建 IRC 库和示例：

```sh
cmake -S . -B build-irc -DTITLEBOT_BUILD_APP=OFF -DTITLEBOT_ENABLE_TESTS=OFF -DIRCBOT_BUILD_EXAMPLES=ON
cmake --build build-irc --parallel
```

其他 CMake 项目可以用 `add_subdirectory` 引入本项目，关闭 `TITLEBOT_BUILD_APP` 后将机器人目标链接到 `ircbot::ircbot`。完整使用示例见 [examples/echo_bot.cpp](examples/echo_bot.cpp)。

能力协商、TLS/SASL、错误处理、异步字段复制和多实例使用方式详见 [ircbot 框架指南](docs/ircbot.org)。

`ircbot::Run(config, handlers)` 在调用者的 Asio executor 上运行，返回 `std::expected<void, ClientError>`。每个实例独立维护连接与协议状态；使用一个线程运行 executor，或把实例放在 strand 上。普通网络失败按退避策略重连，配置错误、必需认证失败及业务回调异常返回错误。取消操作会先尝试发送 `QUIT`。

业务代码通过 `Handlers::message` 处理 `MessageView`，通过 `Connection::Send`、`Privmsg` 或 `SendLine` 发消息。`MessageView` 中的视图仅在回调期间有效；异步操作应复制需要保留的字段。`Connection` 是当前连接的弱句柄，重连后旧句柄不能向新会话发送迟到消息。

协议解析支持 IRCv3 标签和大小写混合命令；标签值可通过 `MessageView::Tag` 解码。`Connection::IsSelf` 与 `casemapping()` 使用服务器 `005 CASEMAPPING` 声明的 ASCII、RFC1459 或 strict-rfc1459 规则。默认协商 `message-tags`、`server-time`，其他能力可通过 `RegistrationConfig::caps` 配置。

## 连接 IRC

```sh
./build/titlebot -s irc.example.org -p 6667 -n titlebot '#example,#dev'
```

使用 `-s` 指定服务器、`-n` 指定昵称，频道列表作为位置参数传入。多个频道用逗号分隔，在 shell 中用引号包住以 `#` 开头的参数。明文连接需要用 `-p` 指定端口。使用 `--tls` 时默认连接 6697 端口，并使用系统信任库校验证书与服务器名称。

```sh
./build/titlebot --tls -s irc.example.org -n titlebot '#room'
```

注册成功后机器人自动加入频道。注册等待超过 30 秒会触发重连，连续失败时重试间隔最多为 60 秒。已注册连接空闲 90 秒时发送 `PING`，再过 30 秒仍无响应就重连。按 Ctrl+C 或发送 SIGTERM 会结束运行，已建立的 IRC 会话会先尝试发送 `QUIT`。

单次 IRC 写入超过 10 秒会关闭连接并重连，即使对端仍在发送消息也能恢复。带标签或大小写混合的 `PRIVMSG` 与普通消息使用相同的标题处理逻辑。例如 `@label=x :alice!u@h privmsg #room :https://one/ https://two/` 仍只抓取第一个链接。

## IRC 认证

SASL PLAIN 使用 TLS，并从环境变量读取密码。先在运行环境中设置 `TITLEBOT_SASL_PASSWORD`，再运行：

```sh
./build/titlebot --tls -s irc.example.org -n titlebot --sasl-user account '#room'
```

`--sasl-password-env NAME` 可指定另一环境变量；密码不进入命令行参数或应用日志。服务器 `PASS` 密码可通过 `--server-password-env NAME` 配置。配置 SASL 后必须认证成功才能进入正常会话，服务器不支持或拒绝认证时程序返回失败。

SASL EXTERNAL 可配合客户端证书使用：

```sh
./build/titlebot --tls -s irc.example.org -n titlebot --sasl-external --cert-file client.crt --key-file client.key '#room'
```

`--ca-file FILE` 为 IRC TLS 增加受信任的 CA。库调用者可直接设置 `Config` 的证书路径与 `RegistrationConfig::sasl`，其中 `required` 控制认证是否为必需。

## 抓取与诊断

- 网页抓取跳过以方括号直接指定 IPv6 地址的 URL。域名可解析为 IPv4 或 IPv6，只连接允许的公网地址，每次重定向都会重新检查目标，最多跟随五次重定向。抓取 HTTPS 网页时使用系统信任的证书验证服务器身份。
- 抓取器收到 `103 Early Hints` 后会继续等待最终响应。解析 HTML 时跳过模板中的标题和字符集声明，读到完整的文档标题即可回复。
- 抓取失败或页面缺少标题时，频道里不会收到回复。可查看标准错误中的汇总日志：`no_title` 统计无标题结果，`timeouts` 和 `decode` 分别统计超时和解码失败，`queue_drops` 和 `reply_drops` 分别统计入队丢弃和回复丢弃。
- 同一 URL 在途时，每个请求频道各收到一次标题。缓存最多保留 256 项，标题可复用 4 分钟，无标题或失败结果可复用 30 秒。
- 重连后清理已失效会话的等待请求；仍有其他有效订阅者的共享请求继续执行。在途下载可完成并更新缓存，但不会向旧会话发送回复。
- 使用 `-v` 或 `--verbose` 开启调试日志。统计有变化时，机器人会在一分钟内打印汇总。`cache_hits` 统计缓存命中次数，`duplicate_replies` 统计同一目标合并的重复回复。

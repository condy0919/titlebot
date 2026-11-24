# titlebot 改进路线图

评审基线为 `e6b8d9f`。本地使用 GCC 16.2.1 构建，40 项 CTest 全部通过。现有测试已覆盖 IRC 重连、首链抓取、重复下载合并、HTTP 重定向和流式解码。建议先处理会让机器人长时间离线或抓错页面的问题，再补协议兼容性和运行诊断。

## 优先修复

### 1. 为 IRC 注册设置截止时间

`src/app/server.cpp` 的 `ServeSession` 发出 `NICK`、`USER` 后等待下一行。若服务器接受连接却始终不给注册结果，会话会一直占用连接，`RunBot` 的重连退避也没有机会启动。建议从注册开始计时，在收到 `001` 时撤销定时器，超时则结束本次会话，复用已有重连逻辑。

**验收**　在 `server_test.cpp` 中让第一条回环连接只发 `PING`。机器人应能回复 `PONG`，并在注册时限到达后重连。让第二条连接发送 `:irc 001 bot :welcome`，应观察到一次 `JOIN #room`，且原注册时限过去后仍能回复心跳。

### 2. 保留 URL 路径内成对的圆括号

`src/irc/irc.cpp` 的 `parse_privmsg` 把左右圆括号都当作 URL 终点。下列消息实测只取到 `https://example.org/wiki/Go_`，可能让频道收到另一个页面的标题。建议保留路径中配对的括号，并剥离包在链接外侧的括号。

```text
:alice!u@h PRIVMSG #r :看 (https://example.org/wiki/Go_(game)) https://example.org/next
```

**验收**　在 `irc_test.cpp` 断言该消息只产生 `https://example.org/wiki/Go_(game)`。同样正文改为 `NOTICE` 时应返回 `std::nullopt`。同时运行已有的 IPv6 方括号和中文标点边界用例。

### 3. 跳过 HTML 模板中的标题与字符集声明

`src/html/title_parser.cpp` 的 `TitleParser` 已跳过脚本、样式和 SVG 中的标题。下面的 HTML 实测返回“占位”，随后抓取器就会提前结束读取。建议在流式解析器中跟踪 `template` 的嵌套深度，只在模板外采纳标题和字符集声明。

```html
<template><title>占位</title></template><title>首页</title>
```

**验收**　在 `title_parser_test.cpp` 沿用现有的分块测试，对每个切分点都得到“首页”。只有模板标题时应无标题。如果嵌套模板声明 `shift_jis`，模板外的 `<meta charset="utf-8">` 仍应生效。

## 后续改进

| 建议 | 现状与改动范围 | 验收 |
| --- | --- | --- |
| 继续读取 HTTP 临时响应后的最终响应 | `src/http/fetcher.cpp` 的 `ReadTitle` 仅读一组响应头。遇到 `103 Early Hints` 会返回无标题。建议复用现有缓冲区读取下一组响应头，沿用整次抓取的截止时间，并单独处理协议切换。 | 在 `fetcher_test.cpp` 的回环服务中，先发 `103`，再在同一连接发 `200 OK` 和 `<title>公告</title>`，应返回“公告”。只发 `103` 并保持连接时，应按期超时。 |
| 区分无标题、抓取失败和回复丢弃 | `src/app/fetch_scheduler.cpp` 的 `Complete` 只把超时单列，`dropped` 同时统计抓取队列满和回复丢弃。频道没有收到标题时，很难从日志判断该查网页、网络还是限流。建议沿用现有汇总日志，按抓取结果和丢弃阶段分别计数。 | 在调度测试中注入有标题、无标题、解码失败和超时四种结果，各自计数一次，总完成数为四。再分别触发入队丢弃和回复丢弃，核对两项计数。 |
| 自动验证部署编译器基线 | 项目以 Debian g++ 14.2.0 和配套 libstdc++ 为兼容基线，本次本地构建使用 GCC 16.2.1。建议增加一个与服务器工具链一致的 CI 作业，直接运行仓库已有的 CMake 和 CTest 命令。 | 在干净构建目录生成库和 CLI，完整执行 CTest。作业允许测试使用本地回环连接，并在日志中输出编译器版本，便于对照服务器。 |

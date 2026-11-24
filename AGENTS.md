# titlebot 开发指南

## 项目范围

titlebot 为 IRC 用户获取网页标题。处理聊天输入时，仅解析 `PRIVMSG`，每条消息只取第一个 URL。修改解析行为时，用具体的 IRC 消息或 HTML 样本说明输入和预期结果。

- `src/irc/irc.cpp` 负责 IRC 文本规范化和消息解析，`src/html/title_parser.cpp` 负责从网页内容提取标题。
- 默认构建解析和会话库 `titlebot`、可执行目标 `titlebot_cli`（产物名为 `titlebot`）及 GoogleTest。程序入口在 `src/app/main.cpp`，会话和网页抓取分别在 `src/app/server.cpp` 与 `src/http/fetcher.cpp`。
- 构建要求见顶层 CMakeLists.txt，运行参数见 README。

## 构建与验证

使用 C++23（`-std=c++23`），以服务器的 Debian g++ 14.2.0 和配套 libstdc++ 为兼容基线。构建需要 CMake 3.20 或更高版本，并安装 pkg-config、Boost 1.81 或更高版本（含 Boost.URL）、uchardet、zlib 和 OpenSSL 开发包。开启测试时，CMake 通过 FetchContent 获取固定版本的 GoogleTest v1.15.2。

```sh
cmake -S . -B build -DTITLEBOT_ENABLE_TESTS=ON
cmake --build build --parallel
(cd build && ctest --output-on-failure)
```

修复缺陷先复现，再运行相关测试和完整测试套件。源码命名为 `xxx.hpp` 或 `xxx.cpp`，对应的 GoogleTest 用例放在同一目录的 `xxx_test.cpp` 中。网页内容优先使用内联样本或 `tests/fixtures/` 中的本地文件，使测试可重复运行。解析器的回归用例应覆盖触发问题的输入，例如截断命令或缺少分隔符的消息。用中文注释解释难以从样本或断言看出的测试意图。

## 实现与协作

- 只修改与任务相关的文件，保留工作区已有改动。先检查实际调用方和测试，再决定是否修改接口。涉及多种可行设计时，在计划中说明影响范围、取舍与验证方式。示意图使用 Mermaid。
- 格式和 include 排序遵循 `.clang-format`，沿用所在模块的命名约定，例如 `tb::PrivMsg` 和 `tb::parse_privmsg`。头文件使用 include guard 并直接包含所需依赖。控制语句使用花括号。
- 新增需要返回错误原因的 C++ 接口使用 `std::expected<T, Error>`，无返回值时使用 `std::expected<void, Error>`。失败用 `std::unexpected(error)` 返回，Error 只表示失败。调用方先检查结果，再访问值或错误；资源由 RAII 管理。系统调用和第三方接口在项目边界转换。
- 保持现有错误语义。例如 `parse_privmsg` 通过 `std::nullopt` 表示没有可处理的消息。比较协议文本时检查长度，避免依赖主机字节序或未对齐的整数读取。
- 标识符与提交信息使用英文，新增注释与 Markdown 文档使用中文。提交采用 Conventional Commits，例如 `fix(irc): handle truncated PRIVMSG commands`。
- 每次提交聚焦一个目的。提交前用 `git diff --cached --numstat` 检查暂存内容，新增与删除合计不超过 1000 行。仅在明确要求时推送到远端或执行破坏性 Git 操作。

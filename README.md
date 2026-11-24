Titlebot
------------
fetch title for IRC user.

use at your own risk.


介绍
------------
titlebot 仅解析 PRIVMSG 类型的消息，且仅解析消息中第一条 url。


外部依赖
------------
必要依赖:

* [make](http://www.gnu.org/software/make)
* [cmake](http://www.cmake.org/)
* [gcc](http://gcc.gnu.org) 需要能够使用 C++23 特性
* [Boost](https://www.boost.org/) 1.81 或更高版本，包含 Boost.URL 开发库
* [zlib](http://www.zlib.net/)
* [openssl](https://www.openssl.org)
* [uchardet](https://github.com/BYVoid/uchardet)

可选依赖:

* [GoogleTest](https://github.com/google/googletest)（启用单元测试时由 CMake FetchContent 获取 v1.15.2）


安装使用
------------
使用 CMake 3.20 或更高版本构建。首次开启测试时需要获取 GoogleTest。

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/titlebot irc.example.org 6667 titlebot '#example'
```

四个参数分别为 IRC 服务器、TCP 端口、昵称和频道列表。多个频道用逗号分隔。当前 IRC 连接使用明文 TCP，网页抓取支持 HTTP 和 HTTPS。

后记
------------
2年后看了下这东西，写的是啥玩意啊，果然是垃圾。

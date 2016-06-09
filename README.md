[![Build Status](https://travis-ci.org/condy0919/titlebot.png)](https://travis-ci.org/condy0919/titlebot)

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

* [gcc](http://gcc.gnu.org) 需要能够使用 C++14 特性

* [boost](http://www.boost.org/)

* [zlib](http://www.zlib.net/)

* [openssl](https://www.openssl.org)

* [uchardet](https://github.com/BYVoid/uchardet)


可选依赖:

* [jemalloc](http://www.canonware.com/jemalloc/)

* [catch](http://catch-lib.net)


安装使用
------------
> git clone https://github.com/condy0919/titlebot

修改 scripts/deploy.sh 文件，可供配置项分别为 TITLEBOT\_NICK, TITLEBOT\_USER, TITLEBOT\_CHANNELS。当 TITLEBOT\_CHANNELS 中有多个 channels 的情况下必须带有引号。

./build/titlebot 运行程序。

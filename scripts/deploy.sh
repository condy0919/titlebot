#!bash

set -eu

mkdir -p build
cd build
cmake -DTITLEBOT_NICK=anotitlebot \
      -DTITLEBOT_USER=anotitlebot \
      -DTITLEBOT_CHANNELS="#xiyoulinux,#linuxba,##shuati" ..
make && make test

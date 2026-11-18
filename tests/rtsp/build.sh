#!/bin/sh
# Builds and runs the RTSP parser tests (MSYS2 UCRT64 or Linux with gcc). RtspParser.c is built
# with malloc()/free() redirected to a guard-page allocator, so out-of-bounds reads fault.
# Usage: sh tests/rtsp/build.sh   (from the moonlight-common-c directory)
set -e
OUT=${OUT:-/tmp/rtsptest}
CC=${CC:-gcc}
mkdir -p "$OUT"
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Werror -Isrc -Ienet/include \
    -Dmalloc=rtsptest_malloc -Dfree=rtsptest_free -include stdlib.h -c src/RtspParser.c -o "$OUT/RtspParser.o"
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Werror -Isrc -Ienet/include tests/rtsp/rtsptest.c "$OUT/RtspParser.o" -o "$OUT/rtsptest"
"$OUT/rtsptest"

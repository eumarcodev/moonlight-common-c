#!/bin/sh
# Builds and runs the PyroWave frame assembly test on the host (MSYS2 UCRT64 or Linux with gcc).
# Usage: sh tests/pyrowave/build.sh   (from the moonlight-common-c directory)
set -e
OUT=${OUT:-/tmp/pwtest}
mkdir -p "$OUT"
INC="-Isrc -Ienet/include -Ireedsolomon"
for f in VideoDepacketizer RtpVideoQueue LinkedBlockingQueue Platform ByteBuffer; do
    gcc -c -O0 $INC src/$f.c -o "$OUT/$f.o"
done
gcc -c -O0 -Ireedsolomon reedsolomon/rs.c -o "$OUT/rs.o"
case "$(uname -s)" in MINGW*|MSYS*) LIBS="-lws2_32 -lwinmm" ;; *) LIBS="-lpthread" ;; esac
gcc -O0 -g $INC tests/pyrowave/pwtest.c "$OUT"/VideoDepacketizer.o "$OUT"/RtpVideoQueue.o \
    "$OUT"/LinkedBlockingQueue.o "$OUT"/Platform.o "$OUT"/ByteBuffer.o "$OUT"/rs.o -o "$OUT/pwtest" $LIBS
"$OUT/pwtest"

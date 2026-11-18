#!/bin/sh
# Builds and runs the Reed-Solomon FEC tests (MSYS2 UCRT64 or Linux with gcc).
# Usage: sh tests/fec/build.sh   (from the moonlight-common-c directory)
# CC and CFLAGS can be overridden, e.g. CFLAGS="-O2 -mssse3" for the SSSE3 path.
set -e
OUT=${OUT:-/tmp/fectest}
CC=${CC:-gcc}
CFLAGS=${CFLAGS:-"-O2"}
mkdir -p "$OUT"
$CC $CFLAGS -Wall -Wextra -Wno-unused-parameter -Werror -Ireedsolomon tests/fec/fectest.c reedsolomon/rs.c -o "$OUT/fectest"
"$OUT/fectest"

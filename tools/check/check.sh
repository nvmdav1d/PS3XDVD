#!/bin/sh
# Host compile check with gcc or clang. Type-checks the sources against the
# stub PSL1GHT / SDL declarations in ./stubs. Generates no PowerPC code.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$ROOT/build/hostcheck"
CC=${CC:-cc}

mkdir -p "$OUT"

$CC -c -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -std=c99 \
    -I "$HERE/stubs" -I "$ROOT/include" \
    -o "$OUT/" \
    "$ROOT"/source/*.c "$HERE/host_stubs.c"

echo "HOST COMPILE CHECK PASSED"
#!/bin/sh
# Host compile check with gcc or clang. Type-checks the sources against the
# stub PSL1GHT / SDL declarations in ./stubs. Generates no PowerPC code.
#
# One compiler invocation per file: gcc rejects -o when -c is given more than
# one input, so a single combined invocation silently cannot work.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$ROOT/build/hostcheck"
CC=${CC:-gcc}

mkdir -p "$OUT"

for f in "$ROOT"/source/*.c "$HERE/host_stubs.c"; do
  obj="$OUT/$(basename "${f%.c}").o"
  printf '  %s\n' "$(basename "$f")"
  $CC -c \
      -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
      -std=c99 \
      -I "$HERE/stubs" -I "$ROOT/include" \
      -o "$obj" "$f"
done

echo "HOST COMPILE CHECK PASSED"
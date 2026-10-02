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

# Unit tests for the two pieces of logic that are worth more than a type check:
# the xRegistry writer (the only code that can brick a console) and the
# DVD-Video region detection (which used to report discs as region free).
run_tests() {
  name=$1
  shift
  printf '  %s\n' "linking $name"
  $CC -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -std=c99 \
      -I "$HERE/stubs" -I "$ROOT/include" \
      -o "$OUT/$name" "$@" "$HERE/host_stubs.c"
  "$OUT/$name"
}

run_tests test_xreg.exe "$ROOT/source/xreg.c" "$ROOT/source/util.c" "$HERE/test_xreg.c"
run_tests test_disc.exe "$ROOT/source/disc.c" "$ROOT/source/util.c" \
                      "$ROOT/source/storage.c" "$HERE/test_disc.c"
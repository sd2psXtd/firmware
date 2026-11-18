#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/ps1-tests.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
cd "$root"
flags="-std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -pthread"
includes="-Itests/ps1/stubs -Itests/mca/command_stubs -Itests/mca/stubs -Isrc -Isrc/ps1"
${CC:-cc} $flags $includes tests/ps1/test_lifecycle.c -o "$out/lifecycle"
${CC:-cc} $flags $includes tests/ps1/test_pio.c -o "$out/pio"
"$out/lifecycle"
"$out/pio"

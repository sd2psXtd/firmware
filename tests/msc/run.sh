#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d "${TMPDIR:-/tmp}/msc-tests.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Itests/msc/stubs -Isrc -Isrc/usb -Iext/ESP8266SdFatWrapper/include \
    src/usb/usb_storage.c tests/msc/test_storage.c -o "$out/storage"
"$out/storage"

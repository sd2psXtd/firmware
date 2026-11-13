#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/mca-tests.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
cd "$root"
flags="-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread"
case $(uname -s) in
    Darwin) section_gc=-Wl,-dead_strip ;;
    *) section_gc=-Wl,--gc-sections ;;
esac
${CC:-cc} $flags -Isrc/usb src/usb/mca_comms.c tests/mca/test_comms.c -o "$out/comms"
${CC:-cc} $flags -Itests/mca/stubs -Isrc/usb src/usb/mca_bridge.c tests/mca/test_bridge.c -o "$out/bridge"
${CC:-cc} $flags -Itests/mca/stubs -Isrc/usb src/usb/mca_bridge.c tests/mca/test_bridge_threads.c -o "$out/bridge_threads"
${CC:-cc} $flags -Itests/mca/stubs -Isrc/usb src/usb/mca_bridge.c src/usb/mca_page.c tests/mca/test_page.c -o "$out/page"
${CC:-cc} $flags -Itests/mca/device_stubs -Isrc/usb src/usb/mca_device.c tests/mca/test_device.c -o "$out/device"
${CC:-cc} $flags -Itests/mca/stubs -Isrc/usb src/usb/mca_protocol.c src/usb/mca_comms.c \
    src/usb/mca_bridge.c src/usb/mca_page.c tests/mca/test_protocol.c -o "$out/protocol"
${CC:-cc} $flags -DDEBUG_USB_UART -Itests/mca/stubs -Isrc/usb \
    src/usb/mca_protocol.c src/usb/mca_comms.c src/usb/mca_bridge.c src/usb/mca_page.c \
    tests/mca/test_protocol.c -o "$out/protocol_debug"
${CC:-cc} $flags -Itests/mca/stubs -Isrc -Isrc/usb -Isrc/ps1 -Isrc/ps2 src/usb/mca_bridge.c \
    src/ps1/ps1_card_backend_mca.c src/ps2/ps2_card_backend_mca.c tests/mca/test_backends.c -o "$out/backends"
${CC:-cc} $flags -Itests/mca/stubs -Isrc src/ps2/card_emu/ps2_card_backend.c \
    tests/mca/test_selector.c -o "$out/selector"
${CC:-cc} $flags -Wno-unused-parameter -ffunction-sections -fdata-sections \
    -Itests/mca/command_stubs -Itests/mca/stubs -Isrc/usb -Isrc -Isrc/ps2 \
    src/usb/mca_protocol.c src/usb/mca_comms.c src/usb/mca_bridge.c src/usb/mca_page.c \
    src/ps2/ps2_card_backend_mca.c src/ps2/card_emu/ps2_mc_commands.c \
    tests/mca/test_ps3mca_commands.c "$section_gc" -o "$out/ps3mca_commands"
${CC:-cc} $flags -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
    -Itests/mca/command_stubs -Itests/mca/stubs -Isrc/usb -Isrc -Isrc/ps1 -Isrc/ps2 \
    src/usb/mca_protocol.c src/usb/mca_comms.c src/usb/mca_bridge.c src/usb/mca_page.c \
    src/ps1/ps1_card_backend_mca.c src/ps1/ps1_mc_data_interface.c src/bigmem.c \
    tests/mca/test_ps1_commands.c "$section_gc" -o "$out/ps1_commands"
${CC:-cc} $flags -Itests/mca/device_stubs -Itests/mca/stubs -Isrc/usb \
    src/usb/mca_device.c src/usb/mca_protocol.c src/usb/mca_comms.c src/usb/mca_bridge.c \
    src/usb/mca_page.c tests/mca/test_device_protocol_roundtrip.c -o "$out/device_protocol"
${CC:-cc} $flags -Wno-unused-parameter -Wno-unused-function -ffunction-sections -fdata-sections \
    -Itests/mca/command_stubs -Itests/mca/stubs -Isrc/usb -Isrc -Isrc/ps1 -Isrc/ps2 \
    tests/mca/test_ps1_dma_order.c "$section_gc" -o "$out/ps1_dma_order"
${CC:-cc} $flags -Wno-unused-parameter -Wno-unused-function -ffunction-sections -fdata-sections \
    -Itests/mca/command_stubs -Isrc -Isrc/ps1 -Isrc/usb -Isrc/wear_leveling \
    -Iext/inih -Iext/ESP8266SdFatWrapper/include -include stdio.h \
    src/settings.c src/ps1/ps1_mmce.c tests/mca/test_mode_reset.c "$section_gc" -o "$out/mode_reset"
for test in comms bridge bridge_threads page device protocol protocol_debug device_protocol backends selector ps3mca_commands ps1_commands ps1_dma_order mode_reset; do
    "$out/$test"
done

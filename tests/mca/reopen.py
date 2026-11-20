#!/usr/bin/env python3
"""Reproduce the PS3 "card missing / XMB freezes ~5 s" issue (#128) from a PC.

When the XMB leaves and re-enters the memory card screen, the PS3 reopens the
adapter. Host-side reopen requests (SET_INTERFACE, SET_CONFIGURATION,
CLEAR_FEATURE(ENDPOINT_HALT)) reset the USB data toggles to DATA0. A device
that does not reset its own toggles sends the next reply with the wrong toggle
about half of the time (depending on how many packets it sent before); the
host silently drops it and waits for its transfer timeout.

This script performs those reopen requests after an odd and an even number of
replies and checks that the very next AA40 is answered. It only sends AA40
(card type) and the harmless raw probe AA42 81 11, so no card data changes.

Install: python3 -m pip install pyusb libusb-package
Run:     python3 tests/mca/reopen.py            # firmware must be in MCA mode
         python3 tests/mca/reopen.py --rounds 20

Close ps3mca-tool and other MCA clients first. Exit status 0 means every check
passed. On macOS, libusb may skip a SET_CONFIGURATION that repeats the active
configuration; that case is then reported as skipped, not passed.
"""
from __future__ import annotations

import argparse
import sys
import time

import usb.backend.libusb1
import usb.core
import usb.util

VID, PID = 0x054C, 0x02EA
EP_OUT, EP_IN, EP_INT = 0x02, 0x81, 0x83
TIMEOUT_MS = 1000

DETECT = bytes([0xAA, 0x40])
PROBE = bytes([0xAA, 0x42, 4, 0, 0x81, 0x11, 0, 0])  # raw reply: 55 5A 04 00 + 4 bytes


def find_device():
    backend = usb.backend.libusb1.get_backend()
    if backend is None:  # libusb-package bundles a libusb matching this Python (e.g. Rosetta).
        try:
            import libusb_package
            backend = libusb_package.get_libusb1_backend()
        except ImportError:
            pass
    if backend is None:  # Homebrew's libusb is not on the default search path.
        for path in ("/opt/homebrew/lib/libusb-1.0.dylib", "/usr/local/lib/libusb-1.0.dylib"):
            backend = usb.backend.libusb1.get_backend(find_library=lambda _name, p=path: p)
            if backend:
                break
    dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
    if dev is None:
        sys.exit("No MCA device (054c:02ea) found. Is the firmware in MCA mode?")
    return dev


def transact(dev, request: bytes, timeout=TIMEOUT_MS) -> bytes | None:
    """Send a request and return its reply, or None if either stage times out."""
    try:
        dev.write(EP_OUT, request, timeout)
        return bytes(dev.read(EP_IN, 64, timeout))
    except usb.core.USBTimeoutError:
        return None


def detect_ok(reply: bytes | None) -> bool:
    return reply is not None and len(reply) == 2 and reply[0] == 0x55 and reply[1] in (0, 1, 2)


def read_presence(dev) -> int | None:
    try:
        data = bytes(dev.read(EP_INT, 1, 500))
        return data[0] if data else None
    except usb.core.USBTimeoutError:
        return None


def drain_presence(dev):
    while read_presence(dev) is not None:
        pass


def reopen_set_interface(dev):
    dev.set_interface_altsetting(interface=0, alternate_setting=0)


def reopen_set_configuration(dev):
    usb.util.release_interface(dev, 0)
    dev.set_configuration(1)
    usb.util.claim_interface(dev, 0)


def reopen_clear_halt(dev):
    dev.clear_halt(EP_IN)
    dev.clear_halt(EP_OUT)


REOPENS = {
    "SET_INTERFACE": reopen_set_interface,
    "SET_CONFIGURATION": reopen_set_configuration,
    "CLEAR_FEATURE(HALT)": reopen_clear_halt,
}


def run(dev, rounds: int, only: list[str] | None = None) -> int:
    failures = 0
    checks = 0

    def check(name: str, ok: bool, detail: str = ""):
        nonlocal failures, checks
        checks += 1
        if not ok:
            failures += 1
            print(f"  FAIL {name} {detail}")

    for name, reopen in REOPENS.items():
        if only and not any(o.lower() in name.lower() for o in only):
            continue
        lost = 0
        presences = []
        for parity in [1, 2] * rounds:
            for _ in range(parity):
                check(f"{name}: AA40 before reopen", detect_ok(transact(dev, DETECT)))
            drain_presence(dev)
            try:
                reopen(dev)
            except usb.core.USBError as error:
                print(f"  SKIP {name}: host refused the request ({error})")
                break
            started = time.monotonic()
            reply = transact(dev, DETECT)
            if not detect_ok(reply):
                lost += 1
            check(f"{name}: AA40 after {parity} reply(s)", detect_ok(reply),
                  f"reply={reply.hex() if reply else None} after {time.monotonic() - started:.2f}s")
            if reopen is not reopen_clear_halt:
                presences.append(read_presence(dev))
        if presences and all(p is None for p in presences) and reopen is reopen_set_configuration:
            # The device re-sends presence on every reopen it sees, so the host
            # never sent the request (macOS skips repeating the active config).
            print(f"  SKIP {name}: request not sent by this host (presence never re-sent)")
        else:
            for presence in presences:
                check(f"{name}: presence re-sent", presence in (0, 1), f"got {presence}")
        print(f"{name}: {lost} of {2 * rounds} replies lost after reopen")

    # A reply the host abandoned (it never read it) must not answer the next
    # request once the host has cleared the IN halt.
    stale = 0
    for _ in range(rounds):
        try:
            dev.write(EP_OUT, PROBE, TIMEOUT_MS)
        except usb.core.USBTimeoutError:
            pass
        time.sleep(0.05)
        dev.clear_halt(EP_IN)
        reply = transact(dev, DETECT)
        if not detect_ok(reply):
            stale += 1
        check("abandoned reply: next AA40", detect_ok(reply), f"reply={reply.hex() if reply else None}")
    print(f"abandoned reply + CLEAR_FEATURE(HALT): {stale} of {rounds} wrong/missing replies")

    print(f"{checks - failures}/{checks} checks passed")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rounds", type=int, default=10, help="odd/even pairs per reopen type")
    parser.add_argument("--only", action="append",
                        help="run only reopen types containing this text (repeatable), e.g. --only interface")
    args = parser.parse_args()

    dev = find_device()
    dev.set_configuration()
    usb.util.claim_interface(dev, 0)
    try:
        if not detect_ok(transact(dev, DETECT)):
            # A previous run may have left the toggles out of step: one retry.
            if not detect_ok(transact(dev, DETECT)):
                sys.exit("The adapter does not answer AA40.")
        return run(dev, args.rounds, args.only)
    finally:
        usb.util.release_interface(dev, 0)
        usb.util.dispose_resources(dev)


if __name__ == "__main__":
    sys.exit(main())

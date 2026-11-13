#!/usr/bin/env python3
"""Hardware regression/permutation runner for this repository's MCA firmware.

Install: python3 -m pip install pyusb pyserial pycryptodome libusb-package
         libusb-package supplies a library matching Python (including Rosetta).
         Windows still requires a suitable USB device driver.
Run:     python3 test/mca.py
         python3 test/mca.py --ps1-anchor 1
         python3 test/mca.py --list
         python3 test/mca.py --probe  # Backend/device discovery only; no mode/card changes
         python3 test/mca.py --rerun ps2.retail.fragment.63 --verbose
         python3 test/mca.py --read-only --test 'ps1.*'

Requires Python 3.9+, DEBUG_USB_UART firmware with the MCA! escape, and a working
MagicGate keystore already on the device. The runner never reads private keys.
Close other serial/MCA clients. --list and --self-test never contact hardware.

Before testing each mode/variant, CDC stat proves a scratch card folder absent,
MMCE creates it, and reads verify its empty filesystem allocation. Existing
cards are NEVER accepted as scratch targets. PS2 uses a vacant high card number.
PS1 MMCE only has next/previous selection: an EXISTING numeric CardN is selected
through CDC, then MMCE next creates the absent CardN+1. Auto-detects an anchor or
use --ps1-anchor N. A MaxCardIdx limit preventing this is a setup failure.
PS1 emptiness verification scans all user-data pages; PS2 verifies all FAT
entries, the empty root, and the complete scratch erase block. Neither should
be an all-zero image: both have formatting metadata.

Writes/erase are enabled on these newly created, verified cards. --read-only
skips mutations, but still creates fresh images. Before each mutation, touched
pages/block are saved and fsynced to disk, restored in finally, then read-verified.
A failed restoration stops testing immediately. Cards and backups are retained.
A disconnected/crashed device may need manual restoration using those backups.
Original mode/profile/variant and available PS2 selection information are
restored. PS1 has NO selection getter: its scratch remains selected unless
--restore-ps1-card/--restore-ps1-channel specifies a known original selection.

Coverage is bounded and reproducible, not every infinite command sequence:
implemented PS2 card command families, F0 00..14, supported F7 keys, F1/F2
sessions, PS1 ID/R/W/B and MMCE, AA40/42/52/57, USB fragmentation/boundaries,
ordered pairs, seeded mixes, CDC cycles, reset/reconnect, MMCE open continuations.
Excluded: F7 selector 2 (firmware fatal()), failed-confirm F3 (can reset keystore),
invalid raw storage addresses, controller electrical timing, and SD filesystem
mutation/streaming (these are not memory-card commands).

Failures preserve their original trace and rerun that exact case once from a
fresh CDC/MCA fixture, saving full TX/RX hex, timings, seed and tracebacks in a
separate log. A passing rerun never converts the original FAIL to PASS. Unsafe
provisioning/restoration failures abort without mutation reruns. --rerun ID runs
one case with full logs. --verbose also prints the wire trace. summary.json and
scratch-cards.json record outcomes and created cards. Normal output is compact.

Sources: src/usb, ps2_mc_{commands,auth}.c, ps1_memory_card.c,
ps2_mmceman_commands.c, serial_input.c. API documentation:
https://github.com/pyusb/pyusb/blob/master/docs/tutorial.rst
https://pyserial.readthedocs.io/en/latest/pyserial_api.html
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import importlib
from collections import Counter
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
import fnmatch
import hashlib
import itertools
import json
import os
from pathlib import Path
import platform
import random
import re
import struct
import sys
import time
import traceback
from typing import Callable

MCA_VID, MCA_PID = 0x054C, 0x02EA
CDC_VID, CDC_PID = 0x2E8A, 0x000A
EP_OUT, EP_IN, EP_INT = 0x02, 0x81, 0x83
MAX_RAW = 1152
KEYS = {  # Public firmware fixtures, not the device's private CIV/keystore.
    0: bytes.fromhex('1739d3bcd02c18070f7af3b79e73031c'),
    1: bytes.fromhex('06467a6c5b9b82770ddfe97e245b9fca'),
    3: bytes.fromhex('a9fb272a63cfed6fd028a24a9811b82e'),
}
VARIANT_KEYS = {
    'proto': bytes.fromhex('8c4befa6f49a23a09cf146aa171cfe75'),
    'arcade': bytes.fromhex('053d5977c4f7b0d437ae66a51771b8c0'),
    'conquest': KEYS[3],
}


class Failure(Exception):
    pass


class Skip(Exception):
    pass


class RestoreFailure(Failure):
    pass


class ProvisionFailure(Failure):
    pass


class EscapeRejected(Failure):
    pass


class HardwareUnavailable(Failure):
    pass


def require(condition, message):
    if not condition:
        raise Failure(message)


def xor(data):
    value = 0
    for byte in data:
        value ^= byte
    return value


def xor_bytes(*items):
    require(len({len(x) for x in items}) == 1, 'XOR operand length mismatch')
    return bytes(xor(column) for column in zip(*items))


def raw_frame(data):
    require(0 < len(data) <= MAX_RAW, 'raw payload must contain 1..1152 bytes')
    return b'\xaa\x42' + struct.pack('<H', len(data)) + bytes(data)


def ps2_address(command, page):
    address = struct.pack('<I', page)
    return bytes((0x81, command)) + address + bytes((xor(address), 0, 0))


def ps1_request(command, page=0, data=None):
    if command == 'S':
        return b'\x81S' + bytes(8)
    address = struct.pack('>H', page)
    if command == 'R':
        return b'\x81R\0\0' + address + bytes(134)
    require(command == 'W' and data is not None and len(data) == 128, 'bad PS1 write')
    return b'\x81W\0\0' + address + data + bytes((xor(address + data), 0, 0, 0))


def ps2_ecc(data):
    require(len(data) == 512, 'ECC requires a 512-byte page')
    basis = (0x87, 0x96, 0xA5, 0xB4, 0xC3, 0xD2, 0xE1, 0xF0)
    result = bytearray()
    for start in range(0, 512, 128):
        a = b = c = 0
        for i, value in enumerate(data[start:start + 128]):
            code = xor(basis[bit] for bit in range(8) if value & (1 << bit))
            a ^= code
            if code & 0x80:
                b ^= (~i) & 0xff
                c ^= i
        result.extend(((~a) & 0x77, (~b) & 0x7f, (~c) & 0x7f))
    return bytes(result)


class Trace:
    def __init__(self, verbose=False):
        self.started = time.monotonic()
        self.lines = []
        self.verbose = verbose

    def event(self, label, value=''):
        if isinstance(value, (bytes, bytearray)):
            value = f'[{len(value)}] {bytes(value).hex(" ")}'
        line = f'{time.monotonic() - self.started:10.6f} {label}: {value}'
        self.lines.append(line)
        if self.verbose:
            print('  ' + line, flush=True)


def load_usb_backend(library=None):
    """Resolve libusb before CDC changes anything; Python/library CPUs must match."""
    driver = importlib.import_module('usb.backend.libusb1')
    errors = []
    if library:
        try:
            # PyUSB normally hides loader errors behind a None backend. Preserve
            # the actual missing-file/architecture error for an explicit path.
            ctypes.CDLL(library)
            backend = driver.get_backend(find_library=lambda name: library)
            require(backend is not None, 'library loaded but libusb backend initialization failed')
            return backend, str(library)
        except Exception as exc:
            raise Failure(f'Cannot load --libusb {library} in {platform.machine()} Python {sys.executable}: {exc}') from exc
    try:
        package = importlib.import_module('libusb_package')
        backend = package.get_libusb1_backend()
        if backend is not None:
            return backend, 'libusb-package (' + str(package.get_library_path()) + ')'
        errors.append('libusb-package returned no backend')
    except ImportError:
        errors.append('libusb-package not installed')
    except Exception as exc:
        errors.append('libusb-package: ' + str(exc))
    try:
        backend = driver.get_backend()
        if backend is not None:
            return backend, 'system libusb'
    except Exception as exc:
        errors.append('system libusb: ' + str(exc))
    # Homebrew's library may not be in the loader search path, especially under
    # macOS system/Intel Python. An incompatible dylib is diagnosed, not retried.
    candidates = [ctypes.util.find_library('usb-1.0')]
    if sys.platform == 'darwin':
        candidates += ['/opt/homebrew/lib/libusb-1.0.dylib', '/usr/local/lib/libusb-1.0.dylib']
    for path in dict.fromkeys(p for p in candidates if p):
        try:
            ctypes.CDLL(path)
            backend = driver.get_backend(find_library=lambda name, p=path: p)
            if backend is not None:
                return backend, path
        except Exception as exc:
            errors.append(str(exc))
    raise Failure('No compatible libusb backend for ' + platform.machine() + ' Python ' + sys.executable +
                  '. This is a library/architecture problem, not an MCA connection timeout.\n' +
                  'Install in this environment: ' + sys.executable + ' -m pip install libusb-package\n' +
                  'Or supply a matching library with --libusb PATH. Details: ' + '; '.join(errors))


class Hardware:
    """Owns connection handles; never shares them between threads."""
    def __init__(self, args, trace):
        try:
            import usb.core
            import usb.util
            import serial
            import serial.tools.list_ports
            from Crypto.Cipher import DES3  # noqa: F401 - fail before hardware if missing
        except ImportError as exc:
            raise Failure('Install dependencies: python3 -m pip install pyusb pyserial pycryptodome libusb-package') from exc
        self.usb, self.util, self.serial = usb.core, usb.util, serial
        self.ports = serial.tools.list_ports
        self.args, self.trace = args, trace
        self.backend, self.backend_source = load_usb_backend(args.libusb)
        self.trace.event('USB backend preflight', f'{self.backend_source}; Python {sys.executable} ({platform.machine()})')
        self.dev = None
        self.serial_number = None
        self.location = args.usb_path
        self.cdc_location = None
        self.initial = None
        self.context = None
        self.detached = False
        self.identified = False
        self.escape_rejection = None

    @staticmethod
    def usb_location(dev):
        ports = getattr(dev, 'port_numbers', None)
        return f'{dev.bus}-' + '.'.join(map(str, ports)) if ports else None

    def candidates(self, allow_reenumeration=False):
        devices = list(self.usb.find(find_all=True, idVendor=MCA_VID, idProduct=MCA_PID, backend=self.backend))
        if self.location:
            devices = [d for d in devices if self.usb_location(d) == self.location]
        locations = {self.usb_location(d) for d in devices}
        same_port = len(locations) == 1 and None not in locations
        require(len(devices) <= 1 or (allow_reenumeration and same_port),
                'Multiple MCA devices; select --usb-path BUS-PORT[.PORT] '
                '(or allow USB re-enumeration to finish if these are stale instances)')
        return devices

    def close(self):
        if self.dev is not None:
            try:
                self.util.release_interface(self.dev, 0)
                if self.detached:
                    self.dev.attach_kernel_driver(0)
            except self.usb.USBError:
                pass
            finally:
                self.util.dispose_resources(self.dev)
                self.dev = None
                self.detached = False

    def connect_mca(self):
        self.close()
        deadline = time.monotonic() + self.args.reconnect_timeout
        last = None
        while time.monotonic() < deadline:
            try:
                devices = self.candidates(allow_reenumeration=True)
                if len(devices) > 1:
                    last = 'multiple re-enumerating MCA instances at the same physical port'
                    self.trace.event('Waiting for USB re-enumeration', [(d.bus, d.address) for d in devices])
                    time.sleep(0.1)
                    continue
                if devices:
                    dev = devices[0]
                    self.dev = dev  # Dispose this handle too if configuration/claim fails.
                    try:
                        cfg = dev.get_active_configuration()
                    except self.usb.USBError:
                        dev.set_configuration()
                        cfg = dev.get_active_configuration()
                    require(cfg[(0, 0)].bInterfaceClass == 0xff, 'MCA interface is not vendor-specific')
                    try:
                        if dev.is_kernel_driver_active(0):
                            dev.detach_kernel_driver(0)
                            self.detached = True
                    except NotImplementedError:
                        pass
                    self.util.claim_interface(dev, 0)
                    self.dev = dev
                    self.location = self.usb_location(dev) or self.location
                    self.trace.event('USB MCA connected', self.location)
                    return
            except self.usb.USBError as exc:
                last = exc
                self.close()
            time.sleep(0.1)
        raise Failure(f'MCA did not enumerate: {last or "no matching device"}')

    def find_port(self):
        ports = [p for p in self.ports.comports() if (p.vid, p.pid) == (CDC_VID, CDC_PID)]
        if self.serial_number:
            ports = [p for p in ports if p.serial_number == self.serial_number]
        elif self.args.port:
            ports = [p for p in ports if p.device == self.args.port]
        elif self.cdc_location:
            ports = [p for p in ports if p.location == self.cdc_location]
        require(len(ports) <= 1, 'Multiple Pico CDC ports; select --port explicitly')
        return ports[0] if ports else None

    def wait_port(self):
        deadline = time.monotonic() + self.args.reconnect_timeout
        while time.monotonic() < deadline:
            port = self.find_port()
            if port:
                self.serial_number = port.serial_number or self.serial_number
                self.cdc_location = port.location or self.cdc_location
                return port.device
            time.sleep(0.1)
        visible = [p.device for p in self.ports.comports() if (p.vid, p.pid) == (CDC_VID, CDC_PID)]
        raise Failure(f'CDC did not enumerate within {self.args.reconnect_timeout}s. '
                      f'Visible Pico CDC ports: {visible}; selected serial={self.serial_number!r}, port={self.args.port!r}')

    def cdc(self, command, disconnect=False):
        port = self.wait_port()
        self.trace.event('CDC TX', command)
        answer = bytearray()
        with self.serial.Serial(port, 115200, timeout=0.1, write_timeout=2) as serial:
            serial.dtr = True
            serial.reset_input_buffer()
            serial.write(b'\r')
            end = time.monotonic() + 2
            while time.monotonic() < end:
                answer.extend(serial.read(256))
                if b'> ' in answer:
                    break
            answer.clear()
            serial.write(command.encode('ascii') + b'\r')
            end = time.monotonic() + (2 if disconnect else self.args.reconnect_timeout)
            while time.monotonic() < end:
                try:
                    chunk = serial.read(4096)
                except self.serial.SerialException:
                    if disconnect:
                        break
                    raise
                answer.extend(chunk)
                if b'> ' in answer:
                    break
            else:
                if not disconnect:
                    raise Failure(f'CDC command timed out: {command!r}; {answer!r}')
        text = answer.decode('utf-8', errors='replace')
        self.trace.event('CDC RX', text)
        require(not re.search(r'Unknown command|Invalid |Usage:|Busy:', text), f'CDC rejected {command}: {text}')
        if not disconnect:
            require(b'> ' in answer, f'CDC prompt missing for {command}')
        return text

    def drain_mca_reply(self):
        # Recover replies left by a prior aborted run before sending another OUT
        # request. With backpressure, a pending IN reply otherwise blocks OUT.
        # Twenty packet reads bound this to the firmware's 1156-byte maximum.
        for _ in range(20):
            try:
                packet = bytes(self.dev.read(EP_IN, 64, timeout=50))
            except self.usb.USBTimeoutError:
                return
            self.trace.event('MCA pending reply drained before escape', packet)
            if len(packet) < 64:
                return
        raise Failure('MCA kept returning data while draining its previous reply; refusing an ambiguous escape')

    def ensure_cdc(self):
        # Check for a completed switch even if an old, now-disconnected MCA
        # handle is still present. Port discovery is independent of that handle.
        if self.find_port():
            self.close()
            self.context = None
            return
        if self.escape_rejection:
            raise EscapeRejected(self.escape_rejection)
        if self.dev is None:
            self.connect_mca()
        deadline = time.monotonic() + self.args.reconnect_timeout
        last_usb_error = None
        try:
            self.drain_mca_reply()
            self.trace.event('MCA debug escape TX', b'MCA!')
            require(self.dev.write(EP_OUT, b'MCA!', timeout=self.args.timeout) == 4, 'short debug escape')
            while time.monotonic() < deadline:
                if self.find_port():
                    self.context = None
                    self.trace.event('MCA to CDC switch completed')
                    return
                # A successful debug escape has NO reply. A release build
                # rejects it with 55 AF. Consume that reply before closing the
                # handle, otherwise it can leave IN busy and mask the rejection
                # as an OUT timeout during cleanup.
                try:
                    reply = bytes(self.dev.read(EP_IN, 64, timeout=100))
                except self.usb.USBTimeoutError:
                    continue
                except self.usb.USBError as exc:
                    last_usb_error = str(exc)
                    time.sleep(0.1)  # Expected while the old USB identity detaches.
                    continue
                self.trace.event('MCA escape reply', reply)
                if reply == b'\x55\xaf':
                    self.escape_rejection = (
                        'MCA! was explicitly rejected with 55 AF. The running firmware does not '
                        'accept the debug escape in its current state. This reply has been drained. '
                        'Check that the installed UF2 was built with DEBUG_USB_UART=ON; CDC working '
                        'before entering MCA does not prove the MCA escape was compiled in. '
                        'Switch back to Serial on the device to recover. No longer timeout will '
                        'make an explicitly rejected escape succeed.')
                    raise EscapeRejected(self.escape_rejection)
                raise Failure(f'Unexpected reply to MCA!: {reply.hex()}; expected USB disconnect without a reply')
            visible = [p.device for p in self.ports.comports() if (p.vid, p.pid) == (CDC_VID, CDC_PID)]
            mca_paths = [self.usb_location(d) for d in self.candidates()]
            self.trace.event('CDC switch timeout inventory',
                f'CDC={visible}; MCA={mca_paths}; serial filter={self.serial_number!r}; last USB error={last_usb_error}')
            raise Failure(f'MCA! produced no rejection reply, but CDC was not found within '
                          f'{self.args.reconnect_timeout}s. Visible CDC ports={visible}, MCA paths={mca_paths}. '
                          'See the switch inventory in the log; this may be a firmware teardown/reenumeration '
                          'failure or a CDC port-filter mismatch, rather than unsupported debug firmware.')
        finally:
            self.close()

    def start(self):
        initially_cdc = self.find_port() is not None
        self.ensure_cdc()
        text = self.cdc('version')
        mode = re.search(r'^Mode: (PS[12])\s*$', text, re.M)
        variant = re.search(r'^PS2 Variant: (\w+)', text, re.M)
        require(mode and variant and 'HW Variant:' in text, 'CDC version output is not this firmware')
        self.identified = True
        self.initial = {'mode': mode[1].lower(), 'variant': variant[1].lower(),
                        'profile': 'cdc' if initially_cdc else 'mca', 'version': text}
        self.trace.event('Initial state', self.initial)

    def configure(self, mode, variant='retail', force=False):
        if not force and self.context == (mode, variant) and self.dev is not None:
            return
        if force and self.dev is not None:
            self.reset()
        self.ensure_cdc()
        self.cdc(f'set variant {variant}', disconnect=True)
        time.sleep(0.25)
        self.cdc(f'set mode {mode}', disconnect=True)
        time.sleep(0.35)
        text = self.cdc('version')
        require(re.search(rf'^Mode: {mode.upper()}\s*$', text, re.M), f'CDC mode did not become {mode}')
        require(re.search(rf'^PS2 Variant: {variant}\s*$', text, re.M | re.I), 'CDC variant mismatch')
        self.cdc('set mode mca', disconnect=True)
        self.connect_mca()
        self.context = (mode, variant)

    def reset(self):
        require(self.dev is not None, 'no MCA handle to reset')
        self.trace.event('USB bus reset')
        try:
            self.dev.reset()
        except self.usb.USBTimeoutError as exc:
            raise HardwareUnavailable('USB device reset timed out; adapter is unresponsive. '
                                      'Stop testing and reconnect it before replaying the failing case.') from exc
        self.close()
        time.sleep(0.25)
        self.connect_mca()

    def restore(self):
        if not self.initial:
            self.close()
            return
        self.ensure_cdc()
        self.cdc('set variant ' + self.initial['variant'], disconnect=True)
        time.sleep(0.3)
        if self.args.restore_ps1_card is not None:
            self.cdc('set mode ps1', disconnect=True); time.sleep(0.3)
            self.cdc(f'set card {self.args.restore_ps1_card}'); time.sleep(0.6)
            self.cdc(f'set channel {self.args.restore_ps1_channel}'); time.sleep(0.6)
        self.cdc('set mode ' + self.initial['mode'], disconnect=True)
        time.sleep(0.3)
        if self.initial['profile'] == 'mca':
            self.cdc('set mode mca', disconnect=True)
            self.connect_mca()
        self.close()


class MCA:
    def __init__(self, hw):
        self.hw = hw
        self.chunk = 0

    @property
    def trace(self):
        return self.hw.trace

    def write(self, data, chunk=None, timeout=None):
        timeout = self.hw.args.timeout if timeout is None else timeout
        chunk = self.chunk if chunk is None else chunk
        chunk = chunk or len(data)
        for pos in range(0, len(data), chunk):
            part = data[pos:pos + chunk]
            self.trace.event('USB OUT', part)
            count = self.hw.dev.write(EP_OUT, part, timeout=timeout)
            require(count == len(part), f'short USB OUT: {count}/{len(part)}')

    def read_packet(self, size=64, timeout=None, endpoint=EP_IN):
        timeout = self.hw.args.timeout if timeout is None else timeout
        data = bytes(self.hw.dev.read(endpoint, size, timeout=timeout))
        self.trace.event(f'USB IN {endpoint:02x}', data)
        require(data, 'unexpected USB zero-length packet')
        return data

    def reply(self, short=False, timeout=None):
        # Read one packet first, NEVER four bytes into a potentially 64-byte
        # packet (overflow), or an oversized read for an exact-64 reply (no ZLP).
        first = self.read_packet(timeout=timeout)
        if first == b'\x55\xaf':
            return None
        if short:
            require(len(first) == 2 and first[0] == 0x55, f'bad short reply {first.hex()}')
            return first[1:]
        require(len(first) >= 4 and first[:2] == b'\x55\x5a', f'bad frame header {first.hex()}')
        expected = 4 + struct.unpack_from('<H', first, 2)[0]
        require(4 <= expected <= MAX_RAW + 4, f'invalid reply length {expected}')
        result = bytearray(first)
        while len(result) < expected:
            result.extend(self.read_packet(min(64, expected - len(result)), timeout=timeout))
        require(len(result) == expected, f'reply length {len(result)} != declared {expected}')
        return bytes(result[4:])

    def raw(self, data, chunk=None):
        self.write(raw_frame(data), chunk)
        response = self.reply()
        require(response is not None, 'AA42 rejected with 55 AF')
        return response

    def detect(self, deadline=None):
        def timeout():
            if deadline is None:
                return self.hw.args.timeout
            remaining = deadline - time.monotonic()
            require(remaining > 0, 'readiness deadline expired during detection')
            return max(1, int(remaining * 1000))
        # A card switch can temporarily stop USB servicing during exit/flush.
        # Keep one request outstanding within the readiness budget; never resend
        # a timed-out request whose acceptance is unknown.
        self.write(b'\xaa\x40', timeout=timeout())
        result = self.reply(short=True, timeout=timeout())
        require(result is not None and result[0] in (0, 1, 2), f'bad detection {result!r}')
        return result[0]

    def ready(self, mode):
        expected = 1 if mode == 'ps1' else 2
        deadline = time.monotonic() + self.hw.args.reconnect_timeout
        last = None
        while time.monotonic() < deadline:
            last = self.detect(deadline=deadline)
            if last == expected:
                return
            time.sleep(0.1)
        raise Failure(f'card not ready: expected {expected}, last detection {last}')

    def page(self, page, data=None):
        frame = bytes((0xaa, 0x52 if data is None else 0x57, 3)) + struct.pack('<I', page)
        if data is not None:
            require(len(data) == 528, 'AA57 requires 528 bytes')
            frame += data
        self.write(frame + b'\x55\x2b')
        result = self.reply(short=data is not None)
        require(result is not None, 'page operation rejected with 55 AF')
        if data is None:
            require(len(result) == 528, 'AA52 did not return 528 bytes')
        else:
            require(result == b'\x5a', 'AA57 not acknowledged')
        return result


class Card:
    def __init__(self, mca):
        self.m = mca
        self.term = 0xff

    def ack(self, response):
        require(response[-2:] == bytes((0x2b, self.term)), f'bad ACK/term: {response.hex()}')
        return response

    def command(self, command, payload=b'', clocks=0):
        result = self.m.raw(bytes((0x81, command)) + payload + bytes(clocks))
        if command == 0x26:
            require(len(result) == 13 and result[:3] == b'\xff\xff\x2b' and result[-1] == self.term, 'specifications framing')
            return result
        if command == 0x11:
            require(result[-2] == 0x2b and result[-1] in (0x66, self.term), 'card-change ACK/term')
            return result
        return self.ack(result)

    def set_term(self, term):
        response = self.m.raw(bytes((0x81, 0x27, term, 0, 0)))
        require(response[-2:] == bytes((0x2b, term)), 'set terminator failed')
        self.term = term

    def specs(self):
        response = self.command(0x26, clocks=11)
        require(len(response) == 13 and response[:3] == b'\xff\xff\x2b', 'bad card specifications')
        require(xor(response[3:11]) == response[11], 'specifications checksum mismatch')
        size, block, pages = struct.unpack_from('<HHI', response, 3)
        require(size == 512 and block > 0 and pages >= block, f'invalid geometry {(size, block, pages)}')
        return size, block, pages

    def address(self, command, page):
        return self.ack(self.m.raw(ps2_address(command, page)))

    def read_ps2(self, page, chunks=(128, 128, 128, 128, 16)):
        require(sum(chunks) == 528 and all(0 <= n <= 255 for n in chunks), 'bad read partition')
        self.address(0x23, page)
        result = bytearray()
        for count in chunks:
            reply = self.m.raw(bytes((0x81, 0x43, count)) + bytes(count + 3))
            require(len(reply) == count + 6 and reply[:4] == b'\xff\xff\xff\x2b', 'bad read response')
            require(reply[-1] == self.term and xor(reply[4:4 + count]) == reply[-2], 'read checksum/term mismatch')
            result.extend(reply[4:4 + count])
        return bytes(result)

    def write_ps2(self, page, data, chunks=(128, 128, 128, 128, 16)):
        require(len(data) == sum(chunks) == 528, 'bad write partition')
        self.address(0x22, page)
        offset = 0
        for count in chunks:
            part = data[offset:offset + count]
            self.command(0x42, bytes((count,)) + part + bytes((xor(part), 0, 0)))
            offset += count
        self.command(0x81, clocks=2)

    def read_ps1(self, page):
        reply = self.m.raw(ps1_request('R', page))
        require(len(reply) == 140, f'PS1 read length {len(reply)}')
        require(reply[2:4] == b'\x5a\x5d' and reply[6:8] == b'\x5c\x5d', 'PS1 read acknowledgements')
        require(reply[8:10] == struct.pack('>H', page) and reply[-1] == 0x47, 'PS1 read address/status')
        require(xor(reply[8:138]) == reply[138], 'PS1 read checksum mismatch')
        return reply[10:138]

    def write_ps1(self, page, data, bad_checksum=False, truncate=None):
        request = bytearray(ps1_request('W', page, data))
        if bad_checksum:
            request[134] ^= 1
        reply = self.m.raw(request if truncate is None else request[:truncate])
        if truncate is None:
            require(len(reply) == 138 and reply[-3:-1] == b'\x5c\x5d', 'PS1 write acknowledgements')
            require(reply[-1] == (0x4e if bad_checksum else 0x47), 'PS1 write status')
        return reply

    def mmce(self, command, payload=b'', clocks=0):
        return self.m.raw(bytes((0x8b, command)) + payload + bytes(clocks))

    def mmce_ack(self, response, reserved):
        # MMCEMAN management replies have FF AA, reserved zero bytes and the
        # terminator. The card SIO protocol's 2B acknowledgement is absent.
        expected = b'\xff\xaa' + bytes(reserved) + bytes((self.term,))
        require(response == expected, f'bad MMCE management reply: {response.hex()} != {expected.hex()}')
        return response

    def mmce_get(self, command):
        response = self.mmce(command, clocks=4)
        require(len(response) == 6 and response[1] == 0xaa and response[-1] == self.term,
                f'MMCE getter {command:02x}: {response.hex()}')
        return int.from_bytes(response[3:5], 'big')

    def mmce_select(self, card=None, channel=None, mode=0):
        if card is not None:
            self.mmce_ack(self.mmce(4, bytes((0, 0, mode)) + struct.pack('>H', card) + b'\0'), 5)
            time.sleep(0.7)
            self.m.ready('ps2')
        if channel is not None:
            self.mmce_ack(self.mmce(6, bytes((0, mode)) + struct.pack('>H', channel) + b'\0'), 4)
            time.sleep(0.7)
            self.m.ready('ps2')

    def auth(self, key=0, variant='retail', seed=0):
        try:
            from Crypto.Cipher import DES3
        except ImportError as exc:
            raise Skip('install pycryptodome for MagicGate response validation') from exc
        self.command(0xf7, bytes((key, 0, 0)))
        rng = random.Random(seed)
        vectors = {}
        challenge1 = None
        for sub in range(0x15):
            if sub in (1, 2, 4, 0xf, 0x11, 0x13):
                result = self.m.raw(bytes((0x81, 0xf0, sub)) + bytes(11))
                require(len(result) == 14 and result[3] == 0x2b and result[-1] == self.term,
                        f'MagicGate {sub:02x} response framing')
                require(xor(result[4:12]) == result[12], f'MagicGate {sub:02x} checksum')
                vectors[sub] = result[4:12][::-1]
            elif sub in (6, 7, 0xb):
                challenge = rng.randbytes(8)
                if sub == 0xb:
                    challenge1 = challenge
                self.ack(self.m.raw(bytes((0x81, 0xf0, sub)) + challenge[::-1] + bytes((xor(challenge), 0, 0))))
            else:
                self.command(0xf0, bytes((sub, 0, 0)))
        cipher = DES3.new(KEYS[key] if variant == 'retail' else VARIANT_KEYS[variant], DES3.MODE_ECB)
        r1, r2, r3 = (vectors[n] for n in (0xf, 0x11, 0x13))
        require(xor_bytes(cipher.decrypt(r3), r2) == b'MechaPwn', 'MagicGate session-key response mismatch')
        require(xor_bytes(cipher.decrypt(r2), r1, cipher.decrypt(challenge1), cipher.decrypt(r1)) == vectors[4],
                'MagicGate challenge/nonce response mismatch')


@dataclass
class Case:
    name: str
    mode: str
    action: Callable
    variant: str = 'retail'
    writes: bool = False
    selection: bool = False


class Suite:
    def __init__(self, args):
        self.args = args
        self.trace = Trace()
        self.hw = None
        self.m = None
        self.c = None
        self.out = Path(args.log_dir) / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        self.results = []
        self.backup_counter = 0
        self.owned = {}
        self.ps2_baseline = None
        self.cleanup_notes = []
        self.current = ''

    def seed(self, name):
        return self.args.seed ^ int.from_bytes(hashlib.sha256(name.encode()).digest()[:8], 'big')

    def prepare(self, case, force=False):
        self.m.chunk = 0
        rebuild = force or self.hw.context != (case.mode, case.variant)
        self.hw.configure(case.mode, case.variant, force)
        self.m.ready(case.mode)
        if rebuild and case.mode == 'ps1' and (case.mode, case.variant) in self.owned:
            target = self.owned[(case.mode, case.variant)]['card']
            self.hw.ensure_cdc()
            self.hw.cdc(f'set card {target}'); time.sleep(0.6)
            self.hw.cdc('set channel 1'); time.sleep(0.4)
            self.hw.cdc('set mode mca', disconnect=True)
            self.hw.connect_mca(); self.hw.context = (case.mode, case.variant)
            self.m.ready(case.mode)
            self.ps1_default()
        if case.mode == 'ps2':
            self.c.set_term(0xff)
        context = (case.mode, case.variant)
        if context not in self.owned:
            try:
                self.provision(case.mode, case.variant)
            except Exception as exc:
                raise ProvisionFailure(f'Cannot prove new, empty scratch card for {context}: {exc}') from exc
        require(self.owned[context]['verified_empty'], 'scratch card was not verified empty')
        self.select_owned(case.mode, case.variant)

    @staticmethod
    def card_home(mode, variant):
        if mode == 'ps1':
            return 'MemoryCards/PS1'
        return 'MemoryCards/' + {'retail': 'PS2', 'proto': 'PROT', 'arcade': 'COH', 'conquest': 'SC2'}[variant]

    def missing(self, path):
        text = self.hw.cdc('stat ' + path)
        if re.search(r'^Missing: ' + re.escape(path) + r'\s*$', text, re.M):
            return True
        require(re.search(r'^(Dir:|File:|Exists but cannot open:)', text, re.M),
                'cannot prove whether scratch path exists: ' + text)
        return False

    def provision(self, mode, variant):
        """Prove absence, create THROUGH MMCE, verify formatted-empty allocation."""
        home = self.card_home(mode, variant)
        self.hw.ensure_cdc()
        anchor = None
        if mode == 'ps1':
            listing = self.hw.cdc('ls ' + home)
            anchors = sorted({int(n) for n in re.findall(r'^D Card(\d+)\s*$', listing, re.M)}, reverse=True)
            if self.args.ps1_anchor is not None:
                anchors = [self.args.ps1_anchor]
            if self.args.ps1_card is not None:
                anchors = [self.args.ps1_card - 1]
            require(anchors, 'PS1 needs an existing numeric card as an MMCE-next anchor; '
                    'provide --ps1-anchor N (CardN must exist)')
            for candidate in anchors:
                if not 1 <= candidate < 65533:
                    continue
                if self.missing(f'{home}/Card{candidate}'):
                    continue
                if self.missing(f'{home}/Card{candidate + 1}'):
                    anchor, target = candidate, candidate + 1
                    break
            require(anchor is not None, 'No existing PS1 numeric anchor with an absent successor. '
                    'Refusing to reuse a card; provide --ps1-anchor N.')
            self.hw.cdc(f'set card {anchor}'); time.sleep(0.6)
            self.hw.cdc('set channel 1'); time.sleep(0.4)
        else:
            candidates = [self.args.ps2_card] if self.args.ps2_card is not None else range(65000, 64000, -1)
            target = next((n for n in candidates if self.missing(f'{home}/Card{n}')), None)
            require(target is not None, f'No absent scratch card under {home}; existing cards are never reused')
        path = f'{home}/Card{target}/Card{target}-1.mcd'
        self.hw.cdc('set mode mca', disconnect=True)
        self.hw.connect_mca(); self.hw.context = (mode, variant); self.m.ready(mode)
        if mode == 'ps1':
            self.ps1_default()  # Profile startup may have auto-selected BOOT/game-ID.
            self.ps1_mmce(0x25)  # Increment from the persisted numeric anchor.
        else:
            self.c.mmce_select(card=target)
        time.sleep(0.7); self.m.ready(mode)
        if mode == 'ps2':
            require(self.c.mmce_get(3) == target and self.c.mmce_get(5) == 1,
                    'MMCE scratch-card selection did not apply')
        self.hw.ensure_cdc()
        stat = self.hw.cdc('stat ' + path)
        match = re.search(r'^File: ' + re.escape(path) + r' size (\d+)\s*$', stat, re.M)
        require(match is not None, 'MMCE did not create the expected new card file; '
                'check MaxCardIdx/configuration limits. No card-data writes will run.')
        file_size = int(match[1])
        self.hw.cdc('set mode mca', disconnect=True)
        self.hw.connect_mca(); self.hw.context = (mode, variant); self.m.ready(mode)
        record = {'card': target, 'channel': 1, 'path': path, 'anchor': anchor,
                  'absent_before_creation': True, 'file_size': file_size, 'verified_empty': False}
        self.owned[(mode, variant)] = record
        # Every USB profile switch restarts cardman and may choose BOOT again.
        # Never infer the active card from the file we just stat-ed through CDC.
        if mode == 'ps1':
            self.ps1_default()
        self.select_owned(mode, variant)
        print(f'  Created {mode}/{variant} Card{target} via MMCE; verifying empty allocation...', flush=True)
        self.verify_empty(mode)
        record['verified_empty'] = True
        self.save_cards()
        print(f'  Verified empty {mode}/{variant} Card{target}', flush=True)

    def save_cards(self):
        (self.out / 'scratch-cards.json').write_text(json.dumps(
            {f'{mode}/{variant}': record for (mode, variant), record in self.owned.items()}, indent=2) + '\n')

    def select_owned(self, mode, variant):
        record = self.owned[(mode, variant)]
        target = record['card']
        require(self.hw.context == (mode, variant), 'scratch-card fixture context mismatch')
        if mode == 'ps2':
            selected = (self.c.mmce_get(3), self.c.mmce_get(5))
            if selected != (target, 1):
                self.trace.event('Reselect scratch after session restart', f'{selected} -> ({target}, 1)')
                self.c.mmce_select(card=target, channel=1)
            require((self.c.mmce_get(3), self.c.mmce_get(5)) == (target, 1),
                    'active PS2 card/channel does not match newly created scratch card')
            size, _, pages = self.c.specs()
            require(size * pages == record['file_size'],
                    f"active PS2 capacity {size * pages} differs from scratch file {record['file_size']}")

    def ps1_default(self):
        # Persistent PS1 mode is established by configure(). MMCE reset maps
        # BOOT/game-ID to the persisted numeric default; it is a no-op if that
        # default is already active. This is needed after each profile restart.
        self.ps1_mmce(0x27)
        time.sleep(0.35)
        self.m.ready('ps1')

    def verify_empty(self, mode):
        if mode == 'ps1':
            header = self.c.read_ps1(0)
            require(header[:2] == b'MC' and xor(header[:127]) == header[127], 'PS1 empty header/checksum')
            for page in range(1, 16):
                entry = self.c.read_ps1(page)
                require(entry[0] == 0xa0 and xor(entry[:127]) == entry[127],
                        f'PS1 directory slot {page} is not free')
            # Scan every user-data page, not merely a few samples.
            for page in range(64, 1024):
                require(self.c.read_ps1(page) == b'\xff' * 128, f'PS1 data page {page} is not empty')
        else:
            page_bytes, _, pages = self.c.specs()
            sb = self.c.read_ps2(0)[:512]
            require(sb.startswith(b'Sony PS2 Memory Card Format '), 'PS2 blank superblock signature')
            clusters = struct.unpack_from('<I', sb, 0x30)[0]
            pages_per_cluster = struct.unpack_from('<H', sb, 0x2a)[0]
            require(page_bytes == 512 and pages_per_cluster == 2 and clusters * pages_per_cluster == pages,
                    'PS2 superblock capacity does not match the active card specifications')
            alloc_start, alloc_end, root = struct.unpack_from('<III', sb, 0x34)
            require(0 < alloc_start < clusters and 1 < alloc_end < clusters and root == 0,
                    'PS2 blank superblock allocation bounds')
            fat_clusters = (clusters * 4 + 1023) // 1024
            ifc_count = (fat_clusters * 4 + 1023) // 1024
            ifcs = struct.unpack_from('<' + 'I' * ifc_count, sb, 0x50)
            fat_locations = []
            for cluster in ifcs:
                block = self.c.read_ps2(cluster * 2)[:512] + self.c.read_ps2(cluster * 2 + 1)[:512]
                fat_locations.extend(struct.unpack('<256I', block))
            require(len(fat_locations) >= fat_clusters, 'PS2 indirect FAT too short')
            relative = 0
            for cluster in fat_locations[:fat_clusters]:
                require(cluster < clusters, 'PS2 FAT points outside card')
                for page in (cluster * 2, cluster * 2 + 1):
                    for value in struct.unpack('<128I', self.c.read_ps2(page)[:512]):
                        expected = 0x7fffffff if 1 <= relative < alloc_end else 0xffffffff
                        require(value == expected, f'PS2 FAT cluster {relative} is allocated/not empty')
                        relative += 1
            for offset, name in ((0, b'.'), (1, b'..')):
                entry = self.c.read_ps2((alloc_start + root) * 2 + offset)[:512]
                require(entry[0x40:].split(b'\0', 1)[0] == name, 'PS2 root contains unexpected entries')
            _, erase_pages, _ = self.c.specs()
            first_data = (alloc_start + 1) * 2
            minimum = ((first_data + erase_pages - 1) // erase_pages) * erase_pages
            requested = self.args.ps2_page
            page = requested if requested is not None else max(minimum, min(1024, (pages // 2 // erase_pages) * erase_pages))
            block_start = page // erase_pages * erase_pages
            require(block_start >= first_data and block_start + erase_pages <= pages, 'scratch page overlaps metadata or lies outside card')
            self.owned[self.hw.context]['test_page'] = page
            for page in range(block_start, block_start + erase_pages):
                require(self.c.read_ps2(page)[:512] == b'\xff' * 512, f'PS2 scratch page {page} is not erased')

    @contextmanager
    def preserve(self, mode, pages):
        """Durable pre-write backup; any restoration failure stops the suite."""
        require(self.args.write_tests, 'write tests not enabled')
        record = self.owned.get(self.hw.context)
        require(record is not None and record['verified_empty'], 'write requires a verified new scratch card')
        target = record['card']
        self.select_owned(mode, self.hw.context[1])
        self.backup_counter += 1
        prefix = self.out / f'backup-{self.backup_counter:04d}-{mode}'
        read = self.c.read_ps1 if mode == 'ps1' else self.c.read_ps2
        saved = {page: read(page) for page in pages}
        metadata = {'mode': mode, 'variant': self.hw.context[1], 'card': target,
                    'channel': 1, 'case': self.current, 'pages': {},
                    'restore': 'Select the recorded mode/variant/card/channel; write each page with its raw data, '
                               'commit, read back and compare. PS2 raw backups include 16 generated spare bytes; '
                               'the stored 512 data bytes and 12 generated ECC bytes must match.'}
        for page, data in saved.items():
            file = prefix.with_name(prefix.name + f'-page{page}.bin')
            with file.open('xb') as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            metadata['pages'][str(page)] = {'file': file.name, 'sha256': hashlib.sha256(data).hexdigest()}
        meta = prefix.with_suffix('.json')
        with meta.open('w') as stream:
            stream.write(json.dumps(metadata, indent=2) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
        self.trace.event('Backup saved', str(meta))
        try:
            yield saved
        finally:
            try:
                for page, data in saved.items():
                    if mode == 'ps1':
                        self.c.write_ps1(page, data)
                    else:
                        self.c.write_ps2(page, data)
                for page, data in saved.items():
                    restored = read(page)
                    require(restored[:128 if mode == 'ps1' else 524] == data[:128 if mode == 'ps1' else 524],
                            f'page {page} restoration mismatch')
                metadata['restored_and_verified'] = True
                meta.write_text(json.dumps(metadata, indent=2) + '\n')
                self.trace.event('Backup restored and verified', str(meta))
            except BaseException as exc:
                raise RestoreFailure(f'RESTORATION FAILED; stop testing, keep {meta}: {exc}') from exc

    def test_fragment(self, chunk):
        self.m.chunk = chunk
        if self.hw.context[0] == 'ps1':
            before = self.c.read_ps1(2)
            require(self.c.read_ps1(2) == before, 'fragmented PS1 read changed')
        else:
            self.c.specs()
            before = self.c.read_ps2(2)
            require(self.c.read_ps2(2) == before, 'fragmented PS2 read changed')
        self.m.chunk = 0
        self.m.ready(self.hw.context[0])

    def test_raw_lengths(self, size):
        if self.hw.context[0] == 'ps1':
            payload = b'\x81S' + bytes(size - 2)
            reply = self.m.raw(payload)
            require(reply[2:10] == b'\x5a\x5d\x5c\x5d\x04\0\0\x80'[:max(0, min(8, size - 1))],
                    'raw PS1 prefix mismatch')
        else:
            self.c.address(0x23, 2)
            reply = self.m.raw(b'\x81\x43' + bytes((max(0, size - 6),)) + bytes(size - 3)) if 6 <= size <= 261 else self.m.raw(b'\x81\x12' + bytes(size - 2))
            require(reply[:2] == b'\xff\xff', 'raw PS2 prefix mismatch')
        self.m.ready(self.hw.context[0])

    def test_malformed(self, frame):
        self.m.write(frame)
        require(self.m.reply() is None, 'malformed MCA frame was not rejected')
        self.m.ready(self.hw.context[0])

    def test_truncated(self, frame):
        self.m.write(frame)
        time.sleep(0.65)  # Firmware assembly timeout is 500 ms.
        require(self.m.reply() is None, 'partial MCA frame did not time out with 55 AF')
        self.m.ready(self.hw.context[0])

    def test_read_partition(self, chunks):
        before = self.c.read_ps2(2)
        require(self.c.read_ps2(2, chunks) == before, f'read partition mismatch {chunks}')

    def test_page(self, page):
        raw = self.c.read_ps2(page)
        require(raw[512:524] == ps2_ecc(raw[:512]), 'raw page ECC mismatch')
        self.c.auth(seed=self.seed(self.current))
        require(self.m.page(page)[:524] == raw[:524], 'AA52/raw page mismatch')

    def test_page_rejected(self, write, page):
        self.c.auth(seed=self.seed(self.current))
        _, _, pages = self.c.specs()
        target = pages if page == 'capacity' else 0xffffffff
        frame = bytes((0xaa, 0x57 if write else 0x52, 3)) + struct.pack('<I', target)
        if write:
            frame += bytes(528)
        self.m.write(frame + b'\x55\x2b')
        require(self.m.reply(short=write) is None, 'out-of-range high-level page command accepted')
        self.m.ready('ps2')

    def test_page_auth_gate(self):
        self.hw.configure('ps2', force=True)
        self.m.ready('ps2'); self.c.set_term(0xff); self.select_owned('ps2', 'retail')
        self.m.write(b'\xaa\x52\x03' + struct.pack('<I', 0) + b'\x55\x2b')
        require(self.m.reply() is None, 'unauthenticated AA52 must fail')
        self.c.auth(seed=self.seed(self.current))
        require(len(self.m.page(0)) == 528, 'authenticated AA52 must succeed')

    def invalid_game_id(self):
        if self.hw.context[0] == 'ps1':
            reply = self.m.raw(b'\x81\x21\0\0')
            require(reply[2:] == b'\0\0', 'PS1 empty game-ID exchange')
            self.ps1_id()
        else:
            selected = (self.c.mmce_get(3), self.c.mmce_get(5))
            self.c.mmce_ack(self.c.mmce(8, b'\0\0\0'), 2)
            require((self.c.mmce_get(3), self.c.mmce_get(5)) == selected,
                    'empty game-ID changed card selection')

    def game_id_mode_cycle(self):
        if not self.args.ps1_game_id:
            raise Skip('provide --ps1-game-id TITLE-ID known to the device PS1 database')
        game_id = self.args.ps1_game_id.encode('ascii')
        self.c.mmce(8, bytes((0, len(game_id))) + game_id + b'\0')
        self.hw.close(); time.sleep(0.4); self.hw.connect_mca()
        self.hw.context = ('ps1', 'retail'); self.m.ready('ps1'); self.ps1_id()
        self.ps1_mmce(0x27)
        self.hw.close(); time.sleep(0.4); self.hw.connect_mca()
        self.hw.context = ('ps2', 'retail'); self.m.ready('ps2'); self.c.set_term(0xff); self.c.specs()

    def write_fragment(self, mode, chunk):
        self.m.chunk = chunk
        try:
            self.write_case(mode, 'random', 'page')
        finally:
            self.m.chunk = 0

    def test_terminator(self, term):
        try:
            self.c.set_term(term)
            require(self.m.raw(b'\x81\x28\0\0\0')[-2:] == bytes((term, term)), 'get terminator mismatch')
        finally:
            self.c.set_term(0xff)

    def test_session(self, command, base):
        data = random.Random(self.seed(self.current)).randbytes(8)
        data += bytes((xor(data),))
        self.c.command(command, bytes((base, 0, 0)))
        self.c.command(command, bytes((base + 1,)) + data + b'\0\0')
        self.c.command(command, bytes((base + 2, 0, 0)))
        result = self.m.raw(bytes((0x81, command, base + 3)) + bytes(11))
        require(len(result) == 14 and result[3] == 0x2b and result[-1] == self.c.term,
                'session-key reply framing')
        require(result[4:13] == data, 'firmware session-key passthrough mismatch')

    def safe_ps2_actions(self):
        return {
            'detect': lambda: require(self.m.detect() == 2, 'PS2 detection'),
            'specs': lambda: self.c.specs(),
            'changed': lambda: self.c.command(0x11, clocks=2),
            'ready': lambda: self.c.command(0x12, clocks=2),
            'term': lambda: self.m.raw(b'\x81\x28\0\0\0'),
            'read': lambda: self.c.read_ps2(2),
            'ping': lambda: self.mmce_ping(),
            'status': lambda: self.mmce_status(),
            'get-card': lambda: self.c.mmce_get(3),
            'get-channel': lambda: self.c.mmce_get(5),
        }

    def safe_ps1_actions(self):
        return {
            'detect': lambda: require(self.m.detect() == 1, 'PS1 detection'),
            'id': self.ps1_id,
            'read0': lambda: self.c.read_ps1(0),
            'read2': lambda: self.c.read_ps1(2),
            'ping': lambda: self.ps1_mmce(0x20),
        }

    def pair(self, first, second):
        actions = self.safe_ps2_actions() if self.hw.context[0] == 'ps2' else self.safe_ps1_actions()
        actions[first](); actions[second]()

    def random_sequence(self, length):
        rng = random.Random(self.seed(self.current))
        actions = self.safe_ps2_actions() if self.hw.context[0] == 'ps2' else self.safe_ps1_actions()
        for index in range(length):
            name = rng.choice(list(actions))
            self.trace.event('Sequence operation', f'{index}: {name}')
            self.m.chunk = rng.choice((0, 1, 3, 7, 63, 64))
            actions[name]()
        self.m.chunk = 0

    def mmce_ping(self):
        result = self.c.mmce(1, clocks=5)
        require(len(result) == 7 and result[1] == 0xaa and result[3] > 0 and result[-1] == self.c.term,
                'MMCE ping/version/product response')

    def mmce_status(self):
        result = self.c.mmce(2, clocks=4)
        require(len(result) == 6 and result[3] == 0 and result[4] == 0 and result[-1] == self.c.term,
                'MMCE status not idle/error-free')

    def mmce_gameid(self):
        result = self.c.mmce(7, clocks=253)
        require(len(result) == 255 and result[1] == 0xaa, 'MMCE get-game-ID response')
        length = result[3]
        require(1 <= length <= 250 and result[4 + length - 1] == 0, 'MMCE game ID is not terminated')

    def mmce_selection(self, command, mode):
        original = (self.c.mmce_get(3), self.c.mmce_get(5))
        try:
            if command == 4:
                self.c.mmce_select(card=original[0], mode=mode)
            elif command == 6:
                self.c.mmce_select(channel=original[1], mode=mode)
            else:
                self.c.mmce(0xa, b'\0\0' + struct.pack('>HH', *original) + b'\0')
                time.sleep(0.7); self.m.ready('ps2')
            self.mmce_status()
            if mode == 0:
                require((self.c.mmce_get(3), self.c.mmce_get(5)) == original, 'MMCE numeric selection mismatch')
        finally:
            self.c.mmce_select(card=original[0], channel=original[1])

    def mmce_open_missing(self):
        # Exercise the 3 separate transactions and callback/queued-byte path,
        # without writing the SD. The fixed improbable name is read-only.
        self.c.mmce(9, clocks=3)
        try:
            result = self.c.mmce(0x40, b'\0\0\0')  # O_RDONLY
            require(result[-1] == 0xff, 'MMCE OPEN stage 0 terminator')
            self.m.raw(b'/__mca_regression_nonexistent_726b8421__\0\0')
            reply = self.m.raw(bytes(3))
            require(reply[-2] == 0xff and reply[-1] == self.c.term, 'MMCE nonexistent OPEN must return -1')
        finally:
            # A USB reset does not necessarily clear MMCEMAN callbacks. Fresh
            # CDC mode setup is needed if a continuation failed midway.
            self.hw.configure('ps2', 'retail', force=True)
            self.m.ready('ps2'); self.c.set_term(0xff)

    def ps1_id(self):
        result = self.m.raw(ps1_request('S'))
        require(len(result) == 10 and result[2:] == b'\x5a\x5d\x5c\x5d\x04\0\0\x80', 'PS1 card ID')

    def ps1_mmce(self, command):
        result = self.m.raw(bytes((0x81, command)) + bytes(4))
        require(len(result) == 6 and result[2:] == bytes((0, 0, 0x27 if command == 0x20 else 0x20, 0xff)),
                f'PS1 MMCE {command:02x} acknowledgement')

    def ps1_switch(self, command):
        try:
            self.ps1_mmce(command)
            time.sleep(0.6); self.m.ready('ps1'); self.ps1_id(); self.c.read_ps1(0)
        finally:
            self.hw.configure('ps1', force=True)
            self.prepare(Case(self.current, 'ps1', lambda: None), force=True)

    def ps1_long(self, pages):
        expected = b''.join(self.c.read_ps1(2 + n) for n in range(pages))
        reply = self.m.raw(b'\x81B\0\0\0\x02' + bytes(3 + pages * 128))
        require(reply[10:10 + 128 * pages] == expected, 'PS1 long read data mismatch')
        self.ps1_id()  # Recovery after request exhaustion is part of the test.

    def presence_cycle(self):
        mode = self.hw.context[0]
        # Consume only pending interrupt notifications. A timeout means there
        # are none; other USB errors remain test failures.
        for _ in range(8):
            try:
                self.m.read_packet(1, timeout=100, endpoint=EP_INT)
            except self.hw.usb.USBTimeoutError:
                break
        try:
            if mode == 'ps2':
                self.c.mmce_select(channel=2)
                if self.c.mmce_get(5) != 2:
                    raise Skip('card configuration does not allow channel 2')
            else:
                self.ps1_mmce(0x23)
                time.sleep(0.6); self.m.ready('ps1')
            values = []
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and values[-2:] != [0, 1]:
                value = self.m.read_packet(1, timeout=500, endpoint=EP_INT)
                require(len(value) == 1 and value[0] in (0, 1), 'invalid presence notification')
                values.append(value[0])
            require(values[-2:] == [0, 1], f'presence ordering must be removal/insertion: {values}')
        finally:
            if mode == 'ps2':
                self.c.mmce_select(channel=1)
            else:
                self.prepare(Case(self.current, 'ps1', lambda: None), force=True)

    def backpressure(self):
        mode = self.hw.context[0]
        request = ps1_request('S') if mode == 'ps1' else b'\x81\x26' + bytes(11)
        self.m.write(raw_frame(request))
        self.trace.event('Backpressure probe OUT (100 ms)', b'\xaa\x40')
        try:
            self.hw.dev.write(EP_OUT, b'\xaa\x40', timeout=100)
        except self.hw.usb.USBTimeoutError:
            pass
        else:
            raise Failure('device accepted another request before the first USB reply was consumed')
        response = self.m.reply()
        require(response is not None and len(response) == len(request), 'backpressure damaged pending reply')
        self.m.ready(mode)

    def usb_reset_case(self, unfinished=False):
        if unfinished:
            self.m.write(b'\xaa\x42\x20\0\x81')
        self.hw.reset()
        self.m.ready(self.hw.context[0])
        self.ps1_id() if self.hw.context[0] == 'ps1' else self.c.specs()

    def mode_cycle(self, method):
        if method == 'cdc':
            for mode in ('ps1', 'ps2', 'ps1', 'ps2'):
                self.hw.configure(mode, force=True); self.m.ready(mode)
                self.ps1_id() if mode == 'ps1' else self.c.specs()
        else:
            # PS2 dispatch treats identifier 01 as a temporary PS1-mode request.
            # MMCE 27 then restores the persistent PS2 mode.
            self.m.raw(b'\x01')
            self.hw.close(); time.sleep(0.4); self.hw.connect_mca()
            self.hw.context = ('ps1', 'retail'); self.m.ready('ps1'); self.ps1_id()
            self.ps1_mmce(0x27)
            self.hw.close(); time.sleep(0.4); self.hw.connect_mca()
            self.hw.context = ('ps2', 'retail'); self.m.ready('ps2'); self.c.specs()

    def write_case(self, mode, pattern, method):
        rng = random.Random(self.seed(self.current))
        count = 128 if mode == 'ps1' else 512
        data = {'zero': bytes(count), 'ones': b'\xff' * count,
                'alternating': b'\x55\xaa' * (count // 2),
                'walking': bytes(1 << (n % 8) for n in range(count)),
                'random': rng.randbytes(count)}[pattern]
        page = self.args.ps1_page if mode == 'ps1' else self.owned[self.hw.context]['test_page']
        with self.preserve(mode, [page]):
            if mode == 'ps1':
                self.c.write_ps1(page, data)
                require(self.c.read_ps1(page) == data, 'PS1 write/read mismatch')
            else:
                payload = data + ps2_ecc(data) + bytes(4)
                if method == 'page':
                    self.c.auth(seed=self.seed(self.current)); self.m.page(page, payload)
                else:
                    self.c.write_ps2(page, payload, method)
                require(self.c.read_ps2(page)[:512] == data, 'PS2 write/read mismatch')
                self.c.auth(seed=self.seed(self.current))
                require(self.m.page(page)[:524] == payload[:524], 'AA52 readback/ECC mismatch')

    def ps1_bad_write(self, truncate):
        page = self.args.ps1_page
        with self.preserve('ps1', [page]) as saved:
            data = bytes(x ^ 0xff for x in saved[page])
            self.c.write_ps1(page, data, bad_checksum=truncate is None, truncate=truncate)
            require(self.c.read_ps1(page) == saved[page], 'rejected PS1 write changed live card data')

    def write_commit_interleaving(self):
        page = self.owned[self.hw.context]['test_page']
        with self.preserve('ps2', [page]) as saved:
            data = bytes(value ^ 0xa5 for value in saved[page][:512])
            payload = data + ps2_ecc(data) + bytes(4)
            self.c.address(0x22, page)
            for offset in range(0, 528, 128):
                part = payload[offset:offset + 128]
                self.c.command(0x42, bytes((len(part),)) + part + bytes((xor(part), 0, 0)))
                self.c.specs(); self.mmce_ping(); self.m.detect()
            require(self.c.read_ps2(page)[:512] == saved[page][:512], 'raw write committed before SIO commit')
            self.c.command(0x81, clocks=2)
            require(self.c.read_ps2(page)[:512] == data, 'interleaved SIO commit lost staged data')

    def erase(self):
        _, block, pages = self.c.specs()
        start = (self.owned[self.hw.context]['test_page'] // block) * block
        require(start + block <= pages and block <= 256, 'invalid erase-block geometry')
        with self.preserve('ps2', range(start, start + block)):
            for page in range(start, start + block):
                data = bytes(((page - start) % 254 + 1,)) * 512
                self.c.write_ps2(page, data + ps2_ecc(data) + bytes(4))
                require(self.c.read_ps2(page)[:512] == data, f'erase prefill failed at page {page}')
            self.c.address(0x21, start)  # Firmware erases at SET_ERASE_ADDRESS.
            self.c.command(0x82, clocks=2)
            for page in range(start, start + block):
                require(self.c.read_ps2(page)[:512] == b'\xff' * 512, f'erase failed at page {page}')

    def cases(self):
        cases = []
        def add(name, mode, action, variant='retail', writes=False, selection=False):
            cases.append(Case(name, mode, action, variant, writes, selection))
        for mode in ('ps2', 'ps1'):
            for chunk in (1, 2, 3, 4, 7, 31, 63, 64, 65, 127, 128):
                add(f'{mode}.retail.fragment.{chunk}', mode, lambda n=chunk: self.test_fragment(n))
            malformed = {'prefix': b'\x00', 'command': b'\xaa\x99',
                         'zero': b'\xaa\x42\0\0', 'oversize': b'\xaa\x42\x81\x04',
                         'page-marker': b'\xaa\x52\x04\0\0\0\0\x55\x2b',
                         'page-tail': b'\xaa\x52\x03\0\0\0\0\x55\0'}
            for name, frame in malformed.items():
                add(f'{mode}.malformed.{name}', mode, lambda f=frame: self.test_malformed(f))
            for length in (1, 2, 3, 4, 5, 7):
                frame = raw_frame(b'\x81\x12\0\0')[:length]
                add(f'{mode}.partial-frame.{length}', mode, lambda f=frame: self.test_truncated(f))
            for size in (2, 3, 4, 5, 6, 59, 60, 61, 63, 64, 65, 124, 125, 126, 127, 128, 129, 1024, 1152):
                add(f'{mode}.raw-boundary.{size}', mode, lambda n=size: self.test_raw_lengths(n))
            add(f'{mode}.presence.remove-insert', mode, self.presence_cycle, selection=True)
            add(f'{mode}.usb.backpressure', mode, self.backpressure)
            for pending in (False, True):
                add(f'{mode}.usb-reset.{"partial" if pending else "idle"}', mode,
                    lambda flag=pending: self.usb_reset_case(flag))
            actions = self.safe_ps2_actions() if mode == 'ps2' else self.safe_ps1_actions()
            for first, second in itertools.product(actions, repeat=2):
                add(f'{mode}.pair.{first}+{second}', mode, lambda a=first, b=second: self.pair(a, b))
            for index in range(self.args.random_cases):
                add(f'{mode}.random.{index:03d}', mode, lambda: self.random_sequence(self.args.sequence_length))
        for command, clocks in ((0x11, 2), (0x12, 2), (0x26, 11), (0xbf, 3), (0xf3, 3)):
            add(f'ps2.command.{command:02x}', 'ps2', lambda c=command, n=clocks: self.c.command(c, clocks=n))
        for term in (0x00, 0x2b, 0x55, 0x5a, 0x7f, 0xff):
            add(f'ps2.terminator.{term:02x}', 'ps2', lambda t=term: self.test_terminator(t))
        for key in (0, 1, 3):
            add(f'ps2.auth.key{key}', 'ps2', lambda k=key: self.c.auth(k, seed=self.seed(self.current)))
        for command, base in itertools.product((0xf1, 0xf2), (0x40, 0x50)):
            add(f'ps2.session.{command:02x}.{base:02x}', 'ps2', lambda c=command, b=base: self.test_session(c, b))
        for page in dict.fromkeys((0, 1, 2, 15, 16, self.args.ps2_page if self.args.ps2_page is not None else 1024)):
            add(f'ps2.page.{page}', 'ps2', lambda p=page: self.test_page(p))
        for chunks in ((255, 255, 18), (1,) * 528, (64,) * 8 + (16,), (0, 128, 128, 128, 128, 16)):
            name = 'ones' if len(chunks) > 20 else '-'.join(map(str, chunks))
            add(f'ps2.partition.{name}', 'ps2', lambda c=chunks: self.test_read_partition(c))
        add('ps2.mmce.game-id', 'ps2', self.mmce_gameid)
        add('ps2.mmce.invalid-game-id', 'ps2', self.invalid_game_id)
        add('ps1.mmce.invalid-game-id', 'ps1', self.invalid_game_id)
        add('ps2.mmce.open-continuation', 'ps2', self.mmce_open_missing)
        add('ps2.mmce.reset', 'ps2', lambda: self.c.mmce_ack(self.c.mmce(9, clocks=3), 2))
        add('ps2.mmce.unmount-noop', 'ps2', lambda: self.c.mmce_ack(self.c.mmce(0x30, clocks=2), 1))
        for command, mode in itertools.product((4, 6), (0, 1, 2)):
            add(f'ps2.mmce.select.{command:02x}.{mode}', 'ps2', lambda c=command, m=mode: self.mmce_selection(c, m), selection=True)
        add('ps2.mmce.card-channel', 'ps2', lambda: self.mmce_selection(0xa, 0), selection=True)
        for variant in self.args.variants:
            if variant == 'retail':
                continue
            add(f'ps2.variant.{variant}.specs-read', 'ps2', lambda: (self.c.specs(), self.c.read_ps2(2)), variant)
            add(f'ps2.variant.{variant}.auth', 'ps2', lambda v=variant: self.c.auth(variant=v, seed=self.seed(self.current)), variant)
        for page in (0, 1, 2, 63, 64, 1022, 1023):
            add(f'ps1.page.{page}', 'ps1', lambda p=page: self.c.read_ps1(p))
        for pages in (1, 2, 4, 8):
            add(f'ps1.long-read.{pages}', 'ps1', lambda n=pages: self.ps1_long(n))
        for command in (0x22, 0x23, 0x24, 0x25):
            add(f'ps1.mmce.switch.{command:02x}', 'ps1', lambda c=command: self.ps1_switch(c), selection=True)
        add('mode.cdc.cycles', 'ps2', lambda: self.mode_cycle('cdc'))
        add('mode.mmce.temporary-ps1-reset', 'ps2', lambda: self.mode_cycle('mmce'))
        add('mode.mmce.game-id-ps1-reset', 'ps2', self.game_id_mode_cycle)
        add('ps2.page.auth-gate', 'ps2', self.test_page_auth_gate)
        for write, page in itertools.product((False, True), ('capacity', 'uint32max')):
            add(f'ps2.page.reject.{write}.{page}', 'ps2', lambda w=write, p=page: self.test_page_rejected(w, p))
        for mode, chunk in itertools.product(('ps2', 'ps1'), (1, 7, 63, 64, 65)):
            add(f'{mode}.write.fragment.{chunk}', mode, lambda m=mode, c=chunk: self.write_fragment(m, c), writes=True)
        for mode, pattern in itertools.product(('ps2', 'ps1'), ('zero', 'ones', 'alternating', 'walking', 'random')):
            add(f'{mode}.write.{pattern}', mode, lambda m=mode, p=pattern: self.write_case(m, p, 'page'), writes=True)
        for chunks in ((128, 128, 128, 128, 16), (255, 255, 18), (64,) * 8 + (16,)):
            add('ps2.write.raw.' + '-'.join(map(str, chunks)), 'ps2', lambda c=chunks: self.write_case('ps2', 'random', c), writes=True)
        for truncate in (None, 6, 32, 64, 70, 134, 135, 136):
            add(f'ps1.write.rejected.{truncate}', 'ps1', lambda n=truncate: self.ps1_bad_write(n), writes=True)
        add('ps2.write.commit-interleaving', 'ps2', self.write_commit_interleaving, writes=True)
        add('ps2.erase.block', 'ps2', self.erase, writes=True)
        require(len({c.name for c in cases}) == len(cases), 'duplicate test identifiers')
        return cases

    def execute(self, case, force=False):
        if case.writes and not self.args.write_tests:
            raise Skip('disabled by --read-only')
        self.current = case.name
        self.prepare(case, force)
        case.action()

    def failure_log(self, case, original, error):
        path = self.out / (case.name + '.log')
        command = [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]
        with path.open('w') as stream:
            stream.write(f'CASE {case.name}\nSEED {self.args.seed} / CASE SEED {self.seed(case.name)}\n')
            stream.write('ARGV ' + json.dumps(command) + '\n')
            stream.write('ENV ' + platform.platform() + '\n')
            stream.write('FIRMWARE ' + json.dumps(self.hw.initial if hasattr(self.hw, 'initial') else None) + '\n')
            stream.write('SCRATCH ' + json.dumps({str(k): v for k, v in self.owned.items()}) + '\n')
            stream.write('ORIGINAL FAILURE\n' + error + '\n' + '\n'.join(original.lines) + '\n')
            if isinstance(sys.exc_info()[1], (RestoreFailure, ProvisionFailure, HardwareUnavailable)):
                stream.write('No rerun: unsafe provisioning/restoration or unavailable USB.\n')
                return path, 'not attempted: safety precondition failed'
            if self.args.no_rerun:
                stream.write('Automatic rerun disabled.\n')
                return path, 'disabled'
            self.trace = self.hw.trace = Trace(verbose=self.args.verbose)
            try:
                self.execute(case, force=True)
                result = 'PASS (intermittent original failure)'
            except HardwareUnavailable:
                result = 'USB UNAVAILABLE'
                stream.write('\nRERUN USB UNAVAILABLE\n' + traceback.format_exc())
            except RestoreFailure:
                result = 'RESTORATION FAILED'
                stream.write('\nRERUN RESTORATION FAILURE\n' + traceback.format_exc())
            except Exception:
                result = 'FAIL'
                stream.write('\nRERUN FAILURE\n' + traceback.format_exc())
            stream.write('\nDEDICATED RERUN ' + result + '\n' + '\n'.join(self.trace.lines) + '\n')
        return path, result

    def run(self, cases):
        self.out.mkdir(parents=True, exist_ok=False)
        self.hw = Hardware(self.args, self.trace)
        self.m = MCA(self.hw); self.c = Card(self.m)
        started = time.monotonic()
        print(f'{len(cases)} cases; seed={self.args.seed}; logs={self.out}', flush=True)
        if self.args.write_tests:
            print('Writes/erase enabled only after absent→MMCE-created→empty verification.')
        print('New scratch cards are retained; PS1 scratch stays selected unless --restore-ps1-card is supplied.')
        failed = False
        try:
            try:
                self.hw.start()
                if any(case.mode == 'ps2' for case in cases):
                    variant = self.hw.initial['variant']
                    self.hw.configure('ps2', variant); self.m.ready('ps2')
                    response = self.m.raw(b'\x81\x28\0\0\0')
                    require(len(response) == 5 and response[-1] == response[-2], 'initial terminator response')
                    self.c.term = response[-1]
                    self.ps2_baseline = {'variant': variant, 'card': self.c.mmce_get(3),
                                         'channel': self.c.mmce_get(5), 'term': self.c.term}
                    self.c.set_term(0xff)
            except Exception:
                error = traceback.format_exc()
                path = self.out / 'startup.log'
                path.write_text(error + '\n' + '\n'.join(self.trace.lines) + '\n')
                self.results.append({'name': 'setup.connect', 'status': 'FAIL', 'error': error, 'log': str(path)})
                raise
            for index, case in enumerate(cases, 1):
                self.trace = self.hw.trace = Trace(verbose=self.args.verbose)
                before = time.monotonic()
                result = {'name': case.name, 'seed': self.seed(case.name), 'mode': case.mode,
                          'variant': case.variant, 'writes': case.writes}
                try:
                    self.execute(case)
                    result['status'] = 'PASS'
                except Skip as exc:
                    result.update(status='SKIP', reason=str(exc))
                except (RestoreFailure, ProvisionFailure, HardwareUnavailable):
                    result.update(status='FAIL', error=traceback.format_exc())
                    path, rerun = self.failure_log(case, self.trace, result['error'])
                    result.update(log=str(path), rerun=rerun)
                    self.results.append(result)
                    print(f'FAIL {case.name}; aborting unsafe setup/restoration or unavailable USB; {path}', flush=True)
                    raise
                except Exception:
                    failed = True
                    result.update(status='FAIL', error=traceback.format_exc())
                    path, rerun = self.failure_log(case, self.trace, result['error'])
                    result.update(log=str(path), rerun=rerun)
                    print(f'FAIL {case.name} (rerun {rerun}); {path}', flush=True)
                    if rerun in ('RESTORATION FAILED', 'USB UNAVAILABLE'):
                        result['seconds'] = round(time.monotonic() - before, 3)
                        self.results.append(result)
                        if rerun == 'USB UNAVAILABLE':
                            raise HardwareUnavailable(f'USB unresponsive; original failure and trace: {path}')
                        raise RestoreFailure(f'rerun restoration failed; original failure and trace: {path}')
                    # Every subsequent case gets a fresh fixture after a failure.
                    self.hw.context = None
                result['seconds'] = round(time.monotonic() - before, 3)
                self.results.append(result)
                if self.args.verbose and result['status'] == 'PASS':
                    (self.out / (case.name + '.log')).write_text('\n'.join(self.trace.lines) + '\n')
                if index % 25 == 0 or index == len(cases):
                    counts = Counter(r['status'] for r in self.results)
                    print(f'[{index}/{len(cases)}] {counts["PASS"]} pass, {counts["FAIL"]} fail, '
                          f'{counts["SKIP"]} skip ({time.monotonic() - started:.1f}s)', flush=True)
                if result['status'] == 'FAIL' and self.args.fail_fast:
                    break
        finally:
            if self.hw:
                try:
                    if isinstance(sys.exc_info()[1], HardwareUnavailable):
                        self.cleanup_notes.append('USB unavailable: automatic setting restoration was not attempted; reconnect required')
                        self.hw.close()
                        raise Failure('USB unavailable: settings could not be restored automatically; reconnect required')
                    if self.ps2_baseline is not None:
                        saved = self.ps2_baseline
                        self.hw.configure('ps2', saved['variant'], force=True)
                        self.m.ready('ps2'); self.c.set_term(0xff)
                        card, channel = saved['card'], saved['channel']
                        if card == 0:
                            self.c.mmce(4, b'\0\x01\0\0\0\0')
                            time.sleep(0.7); self.m.ready('ps2')
                            self.c.mmce_select(channel=channel)
                        elif card < 65535:
                            self.c.mmce_select(card=card, channel=channel)
                        else:
                            note = 'Original PS2 game-ID/named selection needs manual restoration'
                            self.cleanup_notes.append(note)
                            print('RESTORE NOTE: ' + note, flush=True)
                        self.c.set_term(saved['term'])
                    self.hw.restore()
                except Exception:
                    failed = True
                    self.results.append({'name': 'cleanup.restore-settings', 'status': 'FAIL',
                                         'error': traceback.format_exc()})
                    (self.out / 'cleanup.log').write_text(traceback.format_exc() + '\n' + '\n'.join(self.hw.trace.lines))
                    print(f'FAIL restoring original settings; see {self.out / "cleanup.log"}', file=sys.stderr)
            summary = {'seed': self.args.seed, 'arguments': vars(self.args),
                       'initial_state': self.hw.initial if self.hw else None,
                       'ps2_baseline': self.ps2_baseline,
                       'cleanup_notes': self.cleanup_notes,
                       'scratch_cards': {str(k): v for k, v in self.owned.items()},
                       'coverage_exclusions': ['fatal F7 key 2', 'failed-confirm F3 keystore reset',
                           'invalid raw addresses', 'SD filesystem streaming/mutation', 'electrical timing'],
                       'seconds': round(time.monotonic() - started, 3),
                       'counts': dict(Counter(r['status'] for r in self.results)), 'tests': self.results}
            (self.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        counts = Counter(r['status'] for r in self.results)
        print(f'Done: {counts["PASS"]} pass, {counts["FAIL"]} fail, {counts["SKIP"]} skip. '
              f'Results: {self.out / "summary.json"}')
        if counts['SKIP']:
            for reason, count in Counter(r['reason'] for r in self.results if r['status'] == 'SKIP').items():
                print(f'  SKIP {count}: {reason}')
        return 1 if failed or counts['FAIL'] else 0


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--libusb', help='explicit libusb-1.0 library matching this Python architecture')
    parser.add_argument('--probe', action='store_true', help='check backend and enumerate MCA devices only; no profile/card changes')
    parser.add_argument('--port', help='Pico CDC port; rediscovered by serial number after re-enumeration')
    parser.add_argument('--usb-path', help='select MCA physical USB path, e.g. 1-2.3 (MCA has no serial descriptor)')
    parser.add_argument('--timeout', type=int, default=2500, help='per-transfer USB timeout in milliseconds')
    parser.add_argument('--reconnect-timeout', type=float, default=20, help='discovery/readiness timeout in seconds')
    parser.add_argument('--seed', type=lambda s: int(s, 0), default=0x4d4341)
    parser.add_argument('--random-cases', type=int, default=8)
    parser.add_argument('--sequence-length', type=int, default=24)
    parser.add_argument('--variants', nargs='+', choices=('retail', 'proto', 'arcade', 'conquest'), default=['retail', 'proto', 'arcade', 'conquest'])
    parser.add_argument('--ps2-card', type=int, help='new PS2 scratch number; must NOT exist in any tested variant (default: auto)')
    parser.add_argument('--ps1-card', type=int, help='new PS1 CardN; CardN-1 must already exist (default: auto via --ps1-anchor)')
    parser.add_argument('--channel', type=int, default=1)
    parser.add_argument('--ps2-page', type=int, help='PS2 write/erase page (default: choose a data block from fresh card geometry)')
    parser.add_argument('--ps1-page', type=int, default=900, help='scratch page inside selected PS1 card')
    parser.add_argument('--restore-ps1-card', type=int, help='known original PS1 card; no getter exists in this protocol')
    parser.add_argument('--restore-ps1-channel', type=int, default=1)
    parser.set_defaults(write_tests=True)
    parser.add_argument('--read-only', dest='write_tests', action='store_false', help='skip writes/erase; new cards are still created/verified')
    parser.add_argument('--ps1-game-id', help='optional PS1 TITLE-ID in device database; test MMCE PS2→temporary PS1→reset')
    parser.add_argument('--ps1-anchor', type=int, help='existing PS1 numeric card N; create Card N+1 through MMCE next-card')
    parser.add_argument('--test', action='append', default=[], metavar='GLOB', help='select matching case IDs (repeatable)')
    parser.add_argument('--rerun', metavar='ID', help='run exactly this case with full logging')
    parser.add_argument('--verbose', action='store_true', help='print wire trace and save successful case logs too')
    parser.add_argument('--no-rerun', action='store_true', help='disable automatic dedicated rerun on failure')
    parser.add_argument('--fail-fast', action='store_true')
    parser.add_argument('--log-dir', default='mca-results', help='parent of timestamped log/backup/results directory')
    parser.add_argument('--list', action='store_true', help='list selected cases without opening hardware')
    parser.add_argument('--self-test', action='store_true', help='run offline runner tests; no USB/CDC operations')
    args = parser.parse_args(argv)
    for value in (args.ps1_card, args.ps2_card, args.restore_ps1_card):
        if value is not None and not 1 <= value <= 65534:
            parser.error('card numbers must be 1..65534; boot/game-ID cards are not scratch targets')
    if args.channel != 1:
        parser.error('new scratch cards are provisioned on channel 1')
    if args.ps1_anchor is not None and not 1 <= args.ps1_anchor <= 65532:
        parser.error('--ps1-anchor must be 1..65532')
    if not 1 <= args.restore_ps1_channel <= 255:
        parser.error('channels must be 1..255')
    if args.ps1_game_id and not re.fullmatch(r'[A-Za-z]{2,5}-[0-9]{3,7}', args.ps1_game_id):
        parser.error('--ps1-game-id must be a TITLE-ID such as SLUS-00922')
    if not 64 <= args.ps1_page < 1024 or (args.ps2_page is not None and not 0 <= args.ps2_page < 2**32):
        parser.error('PS1 scratch page must be 64..1023; PS2 page must fit uint32')
    if min(args.timeout, args.reconnect_timeout, args.sequence_length) <= 0 or args.random_cases < 0:
        parser.error('timeouts and sequence length must be positive; random case count nonnegative')
    return args


def self_test():
    import unittest
    from types import SimpleNamespace

    class FakeDevice:
        def __init__(self, packets):
            self.packets = list(packets)
            self.sizes = []
        def read(self, endpoint, size, timeout):
            self.sizes.append(size)
            packet = self.packets.pop(0)
            require(len(packet) <= size, 'fake USB overflow')
            return packet

    class Offline(unittest.TestCase):
        def mca(self, packets):
            return MCA(SimpleNamespace(dev=FakeDevice(packets), trace=Trace(), args=SimpleNamespace(timeout=100)))
        def test_packet_sized_reads(self):
            for size in (0, 1, 59, 60, 61, 124, 125, 128, 528, 1152):
                data = bytes(n & 255 for n in range(size))
                frame = b'\x55\x5a' + struct.pack('<H', size) + data
                m = self.mca([frame[n:n+64] for n in range(0, len(frame), 64)])
                self.assertEqual(m.reply(), data)
                self.assertFalse(m.hw.dev.packets)
                self.assertEqual(m.hw.dev.sizes[0], 64)
        def test_short_and_error(self):
            self.assertEqual(self.mca([b'\x55\x02']).reply(short=True), b'\x02')
            self.assertIsNone(self.mca([b'\x55\xaf']).reply())
            self.assertEqual(self.mca([b'\x55\x5a']).reply(short=True), b'\x5a')
        def test_invalid_frames(self):
            for frame in (b'', b'\0\0\0\0', b'\x55\x5a\xff\xff', b'\x55\x5a\0\0\0'):
                with self.assertRaises(Failure): self.mca([frame]).reply()
        def test_builders(self):
            self.assertEqual(len(raw_frame(bytes(1152))), 1156)
            for n in (0, 1153):
                with self.assertRaises(Failure): raw_frame(bytes(n))
            self.assertEqual(ps2_address(0x23, 7), b'\x81\x23\x07\0\0\0\x07\0\0')
            self.assertEqual(len(ps1_request('R', 1023)), 140)
            self.assertEqual(len(ps1_request('W', 900, bytes(128))), 138)
            self.assertEqual(ps2_ecc(b'\xff' * 512), b'\x77\x7f\x7f' * 4)
        def test_case_ids_and_selection(self):
            suite = Suite(parse_args([]))
            cases = suite.cases()
            self.assertGreater(len(cases), 250)
            self.assertEqual(len(cases), len({c.name for c in cases}))
            self.assertTrue(any(c.name == 'mode.mmce.temporary-ps1-reset' for c in cases))
            self.assertTrue(any(c.writes for c in cases))
            self.assertEqual(suite.seed('x'), Suite(parse_args([])).seed('x'))

        def test_bundled_backend_precedes_system_lookup(self):
            from unittest.mock import Mock, patch
            backend = object()
            driver = SimpleNamespace(get_backend=Mock(side_effect=AssertionError('system loader must not run')))
            package = SimpleNamespace(get_libusb1_backend=lambda: backend, get_library_path=lambda: '/bundled/libusb')
            modules = {'usb.backend.libusb1': driver, 'libusb_package': package}
            with patch.object(importlib, 'import_module', side_effect=lambda name: modules[name]):
                found, source = load_usb_backend()
            self.assertIs(found, backend)
            self.assertIn('/bundled/libusb', source)

        def test_backend_error_reports_architecture_before_usb_access(self):
            from unittest.mock import Mock, patch
            driver = SimpleNamespace(get_backend=Mock(return_value=None))
            def module(name):
                if name == 'libusb_package': raise ImportError('not installed')
                return driver
            with patch.object(importlib, 'import_module', side_effect=module), \
                 patch.object(ctypes.util, 'find_library', return_value='/wrong-arch/libusb'), \
                 patch.object(ctypes, 'CDLL', side_effect=OSError('incompatible architecture')):
                with self.assertRaisesRegex(Failure, 'library/architecture problem') as caught:
                    load_usb_backend()
            self.assertIn('incompatible architecture', str(caught.exception))
            self.assertIn('pip install libusb-package', str(caught.exception))

        def test_explicit_backend_architecture_error(self):
            from unittest.mock import Mock, patch
            driver = SimpleNamespace(get_backend=Mock())
            with patch.object(importlib, 'import_module', return_value=driver), \
                 patch.object(ctypes, 'CDLL', side_effect=OSError('incompatible architecture')):
                with self.assertRaisesRegex(Failure, 'Cannot load --libusb'):
                    load_usb_backend('/wrong-arch/libusb')
            driver.get_backend.assert_not_called()

        def test_device_enumeration_uses_preflight_backend(self):
            from unittest.mock import Mock
            hw = Hardware.__new__(Hardware)
            hw.backend = object(); hw.location = None
            hw.usb = SimpleNamespace(find=Mock(return_value=[]))
            self.assertEqual(hw.candidates(), [])
            hw.usb.find.assert_called_once_with(find_all=True, idVendor=MCA_VID,
                                                idProduct=MCA_PID, backend=hw.backend)

        def test_reenumeration_never_selects_between_physical_adapters(self):
            from unittest.mock import Mock
            hw = Hardware.__new__(Hardware)
            hw.backend = object(); hw.location = None
            devices = [SimpleNamespace(bus=1, port_numbers=(2,), address=n) for n in (3, 4)]
            hw.usb = SimpleNamespace(find=Mock(return_value=devices))
            with self.assertRaisesRegex(Failure, 'Multiple MCA devices'): hw.candidates()
            self.assertEqual(hw.candidates(allow_reenumeration=True), devices)
            devices[1].port_numbers = (3,)
            with self.assertRaisesRegex(Failure, 'Multiple MCA devices'):
                hw.candidates(allow_reenumeration=True)

        def test_reset_timeout_requires_reconnect(self):
            from unittest.mock import Mock
            hw = self.escape_fixture()
            hw.dev.reset = Mock(side_effect=hw.usb.USBTimeoutError('reset timed out'))
            with self.assertRaisesRegex(HardwareUnavailable, 'USB device reset timed out'):
                hw.reset()
            hw.connect_mca.assert_not_called()

        def escape_fixture(self, reply=None, ports=None):
            from unittest.mock import Mock
            class USBError(Exception): pass
            class Timeout(USBError): pass
            hw = Hardware.__new__(Hardware)
            hw.args = SimpleNamespace(timeout=100, reconnect_timeout=0.2)
            hw.trace = Trace()
            hw.escape_rejection = None; hw.context = ('ps2', 'retail')
            hw.serial_number = 'test-serial'
            hw.usb = SimpleNamespace(USBError=USBError, USBTimeoutError=Timeout)
            hw.dev = SimpleNamespace(write=Mock(return_value=4), read=Mock(return_value=reply))
            hw.close = Mock()
            hw.drain_mca_reply = Mock()
            hw.find_port = Mock(side_effect=ports) if ports else Mock(return_value=None)
            hw.connect_mca = Mock(side_effect=AssertionError('unexpected reconnect'))
            return hw

        def test_escape_rejection_is_consumed_and_not_repeated(self):
            hw = self.escape_fixture(reply=b'\x55\xaf')
            with self.assertRaisesRegex(EscapeRejected, 'explicitly rejected with 55 AF'):
                hw.ensure_cdc()
            hw.dev.read.assert_called_once_with(EP_IN, 64, timeout=100)
            hw.close.assert_called_once()
            with self.assertRaises(EscapeRejected): hw.ensure_cdc()
            hw.dev.write.assert_called_once_with(EP_OUT, b'MCA!', timeout=100)

        def test_pending_escape_rejection_is_drained_before_next_out(self):
            hw = self.escape_fixture()
            hw.drain_mca_reply = lambda: Hardware.drain_mca_reply(hw)
            order = []
            pending = [b'\x55\xaf']
            def read(endpoint, size, timeout):
                order.append('read')
                if pending: return pending.pop(0)
                raise hw.usb.USBTimeoutError('no data')
            def write(endpoint, packet, timeout):
                self.assertFalse(pending, 'OUT attempted before the old IN reply was consumed')
                order.append('write'); pending.append(b'\x55\xaf'); return len(packet)
            hw.dev.read.side_effect = read
            hw.dev.write.side_effect = write
            with self.assertRaises(EscapeRejected): hw.ensure_cdc()
            self.assertEqual(order, ['read', 'write', 'read'])
            self.assertFalse(pending)
            hw.close.assert_called_once()

        def test_escape_success_waits_through_disconnect(self):
            from unittest.mock import patch
            hw = self.escape_fixture(ports=[None, None, '/dev/test-cdc'])
            hw.dev.read.side_effect = hw.usb.USBError('old device disconnected')
            with patch.object(time, 'sleep'):
                hw.ensure_cdc()
            self.assertIsNone(hw.context)
            hw.close.assert_called_once()
            self.assertIsNone(hw.escape_rejection)

        def test_escape_timeout_logs_inventory_without_assuming_release_build(self):
            from unittest.mock import Mock, patch
            hw = self.escape_fixture()
            hw.dev.read.side_effect = hw.usb.USBTimeoutError('no reply')
            hw.trace = SimpleNamespace(event=Mock())
            hw.ports = SimpleNamespace(comports=lambda: [])
            hw.candidates = lambda: []
            with patch.object(time, 'monotonic', side_effect=[0, 0, 1]):
                with self.assertRaisesRegex(Failure, 'produced no rejection reply'):
                    hw.ensure_cdc()
            hw.close.assert_called_once()
            self.assertIsNone(hw.escape_rejection)
            self.assertEqual(hw.trace.event.call_args.args[0], 'CDC switch timeout inventory')

        def test_visible_cdc_avoids_writing_stale_mca_handle(self):
            hw = self.escape_fixture(ports=['/dev/test-cdc'])
            hw.ensure_cdc()
            hw.dev.write.assert_not_called()
            hw.dev.read.assert_not_called()
            hw.close.assert_called_once()
            self.assertIsNone(hw.context)

        def test_fragment_writes(self):
            for chunk in (1, 3, 63, 64, 65, 0):
                sent = []
                class Writer:
                    def write(self, ep, data, timeout):
                        sent.append(bytes(data)); return len(data)
                m = MCA(SimpleNamespace(dev=Writer(), trace=Trace(), args=SimpleNamespace(timeout=100)))
                payload = raw_frame(bytes(137))
                m.write(payload, chunk)
                self.assertEqual(b''.join(sent), payload)
                if chunk:
                    self.assertTrue(all(len(part) <= chunk for part in sent))

        def test_readiness_transfer_uses_remaining_switch_deadline(self):
            from unittest.mock import Mock, patch
            dev = SimpleNamespace(write=Mock(return_value=2), read=Mock(return_value=b'\x55\x02'))
            m = MCA(SimpleNamespace(dev=dev, trace=SimpleNamespace(event=lambda *args: None),
                args=SimpleNamespace(timeout=2500, reconnect_timeout=20)))
            with patch.object(time, 'monotonic', side_effect=[0, 0, 0, 2]):
                m.ready('ps2')
            dev.write.assert_called_once_with(EP_OUT, b'\xaa\x40', timeout=20000)
            dev.read.assert_called_once_with(EP_IN, 64, timeout=18000)

        def test_missing_proof_is_strict(self):
            suite = Suite(parse_args([]))
            for text, expected in (('Missing: cards/Card1\r\n> ', True), ('Dir: cards/Card1\r\n> ', False)):
                suite.hw = SimpleNamespace(cdc=lambda cmd, answer=text: answer)
                self.assertEqual(suite.missing('cards/Card1'), expected)
            suite.hw = SimpleNamespace(cdc=lambda cmd: 'Busy: card operation in progress\n> ')
            with self.assertRaises(Failure): suite.missing('cards/Card1')

        def test_mmce_management_replies_do_not_use_sio_ack(self):
            card = Card(None)
            for response, reserved in ((b'\xff\xaa\0\0\xff', 2),
                                       (b'\xff\xaa\0\xff', 1),
                                       (b'\xff\xaa' + bytes(5) + b'\xff', 5)):
                self.assertEqual(card.mmce_ack(response, reserved), response)
                with self.assertRaises(Failure): card.ack(response)
            for response in (b'\xff\xff\0\0\xff', b'\xff\xaa\0\x2b\xff', b'\xff\xaa\0\0\0'):
                with self.assertRaises(Failure): card.mmce_ack(response, 2)
            card.term = 0x5a
            self.assertEqual(card.mmce_ack(b'\xff\xaa\0\0\x5a', 2), b'\xff\xaa\0\0\x5a')

        def test_profile_restart_reselects_scratch_and_checks_capacity(self):
            from unittest.mock import Mock
            suite = Suite(parse_args([])); suite.hw = SimpleNamespace(context=('ps2', 'retail'))
            suite.owned[suite.hw.context] = {'card': 64997, 'file_size': 1024 * 1024}
            selected = [0, 1]  # BOOT was automatically selected on reconnect.
            def select(card, channel): selected[:] = [card, channel]
            suite.c = SimpleNamespace(mmce_get=lambda cmd: selected[0 if cmd == 3 else 1],
                mmce_select=Mock(side_effect=select), specs=lambda: (512, 16, 2048))
            suite.select_owned('ps2', 'retail')
            suite.c.mmce_select.assert_called_once_with(card=64997, channel=1)
            self.assertEqual(selected, [64997, 1])
            suite.c.specs = lambda: (512, 16, 131072)
            with self.assertRaisesRegex(Failure, 'differs from scratch file'):
                suite.select_owned('ps2', 'retail')

        def test_wrong_active_card_prevents_backup_and_write(self):
            from unittest.mock import Mock
            suite = Suite(parse_args([])); suite.hw = SimpleNamespace(context=('ps2', 'retail'))
            suite.owned[suite.hw.context] = {'card': 64997, 'file_size': 1024 * 1024, 'verified_empty': True}
            suite.c = SimpleNamespace(mmce_get=lambda cmd: 0, mmce_select=Mock(), read_ps2=Mock())
            with self.assertRaisesRegex(Failure, 'active PS2 card/channel'):
                with suite.preserve('ps2', [512]): self.fail('must not reach mutation')
            suite.c.read_ps2.assert_not_called()

        def test_ps1_profile_normalization_uses_mmce_reset(self):
            from unittest.mock import Mock, patch
            suite = Suite(parse_args([])); suite.ps1_mmce = Mock()
            suite.m = SimpleNamespace(ready=Mock())
            with patch.object(time, 'sleep'): suite.ps1_default()
            suite.ps1_mmce.assert_called_once_with(0x27)
            suite.m.ready.assert_called_once_with('ps1')

        def test_ps1_empty_verification(self):
            suite = Suite(parse_args([]))
            corrupt = set()
            def read(page):
                value = bytearray(128)
                if page == 0: value[:2] = b'MC'
                elif page < 16: value[0] = 0xa0
                else: value[:] = b'\xff' * 128
                if page < 16: value[127] = xor(value[:127])
                if page in corrupt: value[0] ^= 1
                return bytes(value)
            suite.c = SimpleNamespace(read_ps1=read)
            suite.verify_empty('ps1')
            for page in (1, 900):
                corrupt.add(page)
                with self.assertRaises(Failure): suite.verify_empty('ps1')
                corrupt.clear()

        def test_ps2_empty_allocation(self):
            suite = Suite(parse_args([])); suite.hw = SimpleNamespace(context=('ps2', 'retail'))
            suite.owned[suite.hw.context] = {'card': 3}
            corrupt = False
            def read(page):
                data = bytearray(b'\xff' * 512)
                if page == 0:
                    data[:28] = b'Sony PS2 Memory Card Format '
                    struct.pack_into('<H', data, 0x2a, 2)
                    struct.pack_into('<IIII', data, 0x30, 256, 18, 222, 0)
                    struct.pack_into('<I', data, 0x50, 16)
                elif page == 32: struct.pack_into('<I', data, 0, 17)
                elif page in (34, 35):
                    values = [0x7fffffff if 1 <= i < 222 else 0xffffffff
                              for i in range((page - 34) * 128, (page - 33) * 128)]
                    if corrupt and page == 34: values[7] = 0x80000008
                    data[:] = struct.pack('<128I', *values)
                elif page in (36, 37):
                    data[:] = bytes(512)
                    name = b'.' if page == 36 else b'..'
                    data[0x40:0x40+len(name)] = name
                return bytes(data) + bytes(16)
            suite.c = SimpleNamespace(specs=lambda: (512, 16, 512), read_ps2=read)
            suite.verify_empty('ps2')
            self.assertEqual(suite.owned[suite.hw.context]['test_page'], 256)
            corrupt = True
            with self.assertRaises(Failure): suite.verify_empty('ps2')

        def test_mutations_require_verified_new_card(self):
            suite = Suite(parse_args([])); suite.hw = SimpleNamespace(context=('ps1', 'retail'))
            for record in (None, {'card': 2, 'verified_empty': False}):
                if record: suite.owned[suite.hw.context] = record
                with self.assertRaises(Failure):
                    with suite.preserve('ps1', [900]): self.fail('must not enter write context')

        def test_backup_restores_on_failure(self):
            import tempfile
            with tempfile.TemporaryDirectory() as directory:
                suite = Suite(parse_args([])); suite.out = Path(directory)
                suite.hw = SimpleNamespace(context=('ps1', 'retail'))
                suite.owned[suite.hw.context] = {'card': 2, 'verified_empty': True}
                memory = {900: b'\xff' * 128}
                suite.c = SimpleNamespace(read_ps1=lambda page: memory[page],
                    write_ps1=lambda page, data: memory.__setitem__(page, data))
                with self.assertRaisesRegex(Failure, 'original test failure'):
                    with suite.preserve('ps1', [900]):
                        memory[900] = bytes(128)
                        raise Failure('original test failure')
                self.assertEqual(memory[900], b'\xff' * 128)
                metadata = json.loads(next(suite.out.glob('backup-*.json')).read_text())
                self.assertTrue(metadata['restored_and_verified'])
                self.assertEqual(next(suite.out.glob('*.bin')).read_bytes(), memory[900])

        def test_failed_restore_is_fatal(self):
            import tempfile
            with tempfile.TemporaryDirectory() as directory:
                suite = Suite(parse_args([])); suite.out = Path(directory)
                suite.hw = SimpleNamespace(context=('ps1', 'retail'))
                suite.owned[suite.hw.context] = {'card': 2, 'verified_empty': True}
                def bad_write(page, data): raise OSError('device disconnected')
                suite.c = SimpleNamespace(read_ps1=lambda page: bytes(128), write_ps1=bad_write)
                with self.assertRaisesRegex(RestoreFailure, 'RESTORATION FAILED'):
                    with suite.preserve('ps1', [900]): pass
                self.assertTrue(list(suite.out.glob('*.bin')))

        def test_passing_rerun_keeps_original_failure(self):
            import tempfile
            import io
            from contextlib import redirect_stdout
            from unittest.mock import patch
            class FakeHardware:
                def __init__(self, args, trace): self.trace = trace; self.initial = {}; self.context = None
                def start(self): pass
                def restore(self): pass
            calls = []
            def action():
                calls.append(1)
                if len(calls) == 1: raise Failure('intermittent test failure')
            with tempfile.TemporaryDirectory() as directory:
                suite = Suite(parse_args(['--log-dir', directory]))
                suite.prepare = lambda case, force=False: None
                with patch.object(sys.modules[__name__], 'Hardware', FakeHardware), redirect_stdout(io.StringIO()):
                    code = suite.run([Case('offline.failure', 'ps1', action)])
                self.assertEqual(code, 1)
                self.assertEqual(len(calls), 2)
                summary = json.loads((suite.out / 'summary.json').read_text())
                self.assertEqual(summary['counts']['FAIL'], 1)
                log = (suite.out / 'offline.failure.log').read_text()
                self.assertIn('ORIGINAL FAILURE', log)
                self.assertIn('DEDICATED RERUN PASS', log)
                self.assertIn('intermittent test failure', log)

        def test_unsafe_failure_does_not_rerun(self):
            import tempfile
            with tempfile.TemporaryDirectory() as directory:
                suite = Suite(parse_args([])); suite.out = Path(directory)
                suite.hw = SimpleNamespace(trace=suite.trace, initial={})
                suite.execute = lambda *a, **k: self.fail('unsafe failure must not rerun')
                try:
                    raise RestoreFailure('device disconnected before restoration')
                except RestoreFailure:
                    path, result = suite.failure_log(Case('unsafe', 'ps1', lambda: None), suite.trace, traceback.format_exc())
                self.assertIn('not attempted', result)
                self.assertIn('device disconnected before restoration', path.read_text())

        def test_rerun_restore_failure_preserves_first_result(self):
            import tempfile
            import io
            from contextlib import redirect_stdout
            from unittest.mock import patch
            class FakeHardware:
                def __init__(self, args, trace): self.trace = trace; self.initial = {}; self.context = None
                def start(self): pass
                def restore(self): pass
            calls = []
            def action():
                calls.append(1)
                if len(calls) == 1: raise Failure('original failure')
                raise RestoreFailure('rerun restore failed')
            with tempfile.TemporaryDirectory() as directory:
                suite = Suite(parse_args(['--log-dir', directory]))
                suite.prepare = lambda case, force=False: None
                with patch.object(sys.modules[__name__], 'Hardware', FakeHardware), redirect_stdout(io.StringIO()):
                    with self.assertRaises(RestoreFailure):
                        suite.run([Case('offline.fatal-rerun', 'ps1', action),
                                   Case('must-not-run', 'ps1', lambda: self.fail('continued after failed restoration'))])
                self.assertEqual(len(calls), 2)
                summary = json.loads((suite.out / 'summary.json').read_text())
                self.assertEqual(summary['counts']['FAIL'], 1)
                self.assertEqual(summary['tests'][0]['rerun'], 'RESTORATION FAILED')
                log = (suite.out / 'offline.fatal-rerun.log').read_text()
                self.assertIn('original failure', log)
                self.assertIn('RERUN RESTORATION FAILURE', log)

        def test_unresponsive_usb_aborts_following_cases_and_cleanup_requests(self):
            import tempfile
            import io
            from contextlib import redirect_stdout, redirect_stderr
            from unittest.mock import Mock, patch
            hardware = SimpleNamespace(initial={}, context=None, start=Mock(), close=Mock(), restore=Mock())
            calls = []
            def execute(case, force=False):
                calls.append(force)
                if force: raise HardwareUnavailable('reset timed out')
                raise Failure('original OUT timeout')
            with tempfile.TemporaryDirectory() as directory:
                suite = Suite(parse_args(['--log-dir', directory])); suite.execute = execute
                def make_hardware(args, trace): hardware.trace = trace; return hardware
                with patch.object(sys.modules[__name__], 'Hardware', side_effect=make_hardware), \
                     redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    with self.assertRaises(HardwareUnavailable):
                        suite.run([Case('offline.hang', 'ps1', lambda: None),
                                   Case('must-not-run', 'ps1', lambda: None)])
                self.assertEqual(calls, [False, True])
                hardware.restore.assert_not_called()
                hardware.close.assert_called_once()
                summary = json.loads((suite.out / 'summary.json').read_text())
                self.assertEqual(summary['tests'][0]['rerun'], 'USB UNAVAILABLE')
                self.assertEqual(summary['counts']['FAIL'], 2)  # Original case plus unrestored settings.
                log = (suite.out / 'offline.hang.log').read_text()
                self.assertIn('original OUT timeout', log)
                self.assertIn('reset timed out', log)

    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromTestCase(Offline))
    return 0 if result.wasSuccessful() else 1


def main(argv=None):
    args = parse_args(argv)
    if args.self_test:
        return self_test()
    if args.probe:
        try:
            hw = Hardware(args, Trace(verbose=args.verbose))
            devices = hw.candidates()
            print(f'Python: {sys.executable} ({platform.machine()})')
            print('Backend: ' + hw.backend_source)
            for device in devices:
                print(f'MCA {device.idVendor:04x}:{device.idProduct:04x} bus={device.bus} address={device.address} path={hw.usb_location(device)}')
            if not devices:
                ports = [p.device for p in hw.ports.comports() if (p.vid, p.pid) == (CDC_VID, CDC_PID)]
                print('Pico CDC ports: ' + (', '.join(ports) if ports else 'none visible'))
            print(f'{len(devices)} MCA device(s); no mode/card changes performed.')
            return 0 if devices else 1
        except Exception as exc:
            print(f'ERROR: {exc}', file=sys.stderr)
            return 2
    suite = Suite(args)
    cases = suite.cases()
    if args.rerun:
        cases = [c for c in cases if c.name == args.rerun]
        args.verbose = True
    elif args.test:
        cases = [c for c in cases if any(fnmatch.fnmatchcase(c.name, p) for p in args.test)]
    if not cases:
        print('No matching test IDs. Use --list.', file=sys.stderr)
        return 2
    if args.list:
        for case in cases:
            flags = (' [writes]' if case.writes else '') + (' [selection]' if case.selection else '')
            print(case.name + flags)
        print(f'{len(cases)} cases')
        return 0
    try:
        return suite.run(cases)
    except KeyboardInterrupt:
        print('Interrupted; cleanup attempted. Inspect backups/results before continuing.', file=sys.stderr)
        return 130
    except Exception as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())

# MCA host regression tests

Run `sh tests/mca/run_tests.sh`. A host C compiler and POSIX threads are required;
no Pico SDK checkout or hardware is needed. The runner enables AddressSanitizer
and UndefinedBehaviorSanitizer. Use `CFLAGS=-O2` for optimized coverage, or pass
test names to run selected cases, e.g.:

```sh
sh tests/mca/run_tests.sh protocol protocol_debug ps1_commands ps1_dma_order
```

The tests exercise the current production interfaces, not a compatibility layer
for the removed `mca_comms`/Core 0 page scheduler:

- `comms`: framing through the protocol's OUT callback, fragmented/maximal raw
  requests, malformed headers/lengths, and complete page frames.
- `bridge`, `bridge_threads`: complete-frame ownership, framed replies, pending
  and active timeouts, overflow, USB-owned replies, and 20,000 transactions with
  concurrent cooperative cancellation.
- `page`: synchronous Core 1 page execution with mocked PS2 command handlers;
  checks chunking, checksums, ACKs, terminators, commit, cancellation and recovery.
- `device`, `device_protocol`: endpoint ownership/backpressure, reset-interface
  handling, presence ordering, profile selection, complete USB round trips, and
  host pipe resets (SET_CONFIGURATION, SET_INTERFACE, CLEAR_FEATURE(HALT)) that
  must drop armed transfers and return the data toggles to DATA0.
- `protocol`, `protocol_debug`: readiness/storage transitions, authorization,
  timeout recovery, reset/discard behavior, reply draining, and debug exit.
- `backends`, `selector`: PS1/PS2 backend boundaries, MMCEMAN continuation bytes,
  and selection using the captured USB profile rather than changed settings.
- `ps3mca_commands`: raw and high-level page transfers through the real PS2
  command handlers, including reset during a write's commit.
- `ps1_commands`, `ps1_dma_order`: real PS1 command framing and cache behavior,
  checksum/truncation/bounds protection, and read-DMA ordering before writes.
- `mode_reset`: persisted/temporary settings and PS1 reset behavior.

The host mocks implement only the SDK/USB/storage boundaries needed by these
tests. They do not emulate physical PIO timing. Device hardware tests and firmware
builds remain necessary; `tests/mca/mca.py` is the separate hardware-facing tool.
`tests/mca/reopen.py` reproduces the PS3 reopen issue (#128) against a connected
adapter in MCA mode: it reopens the pipes after an odd and an even number of
replies and checks that the next request is still answered.

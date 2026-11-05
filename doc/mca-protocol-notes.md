# MCA protocol investigation

## Evidence and limits

- The local PS3 dump contains `sys/internal/libmcadpt.sprx`, but no external
  variant was found. SHA-256:
  `70c5d40fc148cba6ecede1315db66dbe0ea188d6a1c45c699f3c2a9cbe5d5138`.
  It is an SCE container. `scetool` decrypted a copy using publicly available
  key records from RPCS3; the dump was not modified. The resulting ELF is
  big-endian PowerPC, SHA-256
  `86a4684bef04b0bb996a8f17706244d7f78ad5a8a98c7acfc40aba13ceaa469f`.
  REA rejected both the SCE container and the PowerPC ELF. Ghidra recovered
  the PRX module tables: `cellMcadpt_Library` v1.1 exports 13 functions, plus
  an unnamed record with two functions and two data entries; imports include
  seven `cellUsbd` and eleven `sysPrxForUser` functions. Export NIDs and code
  addresses are recorded in
  `analysis/libmcadpt-ghidra/report.txt`; a reproducibility recipe and limits
  are in `analysis/libmcadpt-ghidra/REPRODUCE.md` and `summary.md`. The NIDs
  have not been resolved to semantic names. The preliminary decompilation used
  overlapping fixed-size function bodies, so those snippets are leads only.
  The subsequent instruction-level analysis below provides evidence for
  AA42/AA52/AA57 framing and response behavior.
- The local `jimmikaelkael-ps3mca-tool-12a198f` source is independent host
  evidence. Its `src/usbd.c` uses VID:PID 054c:02ea, a vendor interface,
  bulk endpoints 02/81, and 500 ms timeouts. Its recorded adapter descriptor
  lists interrupt endpoint 83, one-byte packets and interval 100.
- `src/usbd.h` defines a four-byte header: command LE16, payload length byte,
  pad byte. `src/mcio.c` sends AA 42 and checks 55 5A on responses. Its PS2
  operations use standard raw SIO commands, including 11, 28, 27, 26, 22,
  23, 42, 43, 81 and MagicGate commands. These observations establish what
  this host sends, not what the PS3 firmware sends.
- A later direct-disassembly pass of the decrypted library confirms the page
  request frames used here. At virtual addresses `0x45f4..0x4664`, Sony's
  code constructs `AA 52 03 <page LE32> 55 2B`, sends 9 bytes and expects
  a 532-byte response. At `0x4780..0x47c0`, it checks a little-endian payload
  length of 528 and copies that many bytes. At `0x4954..0x49d8`, it constructs
  `AA 57 03 <page LE32> <528 bytes> 55 2B`, sending 537 bytes and expecting
  a two-byte response; `0x4ac8..0x4ad0` checks its second byte for `5A`.
  The instruction listing is in `analysis/libmcadpt-ghidra/instructions.txt`.
- At `0x2510..0x2534`, Sony's code sends two bytes `AA 40`; at
  `0x25c4..0x25e0` it accepts response byte 1 values 0, 1 or 2. At
  `0x1ea4..0x1efc` it constructs an `AA 42` transaction with a 16-bit
  little-endian payload length; `0x1f94..0x1fbc` checks response byte 1 and
  the returned length. These observations are static binary evidence; the
  generic raw-transaction routine at `0x2720..0x275c` rejects an unsigned
  length greater than `0x400` (1,024) bytes. At `0x2810..0x285c` it uses that
  length for the AA42 frame and its little-endian header, and at
  `0x2988..0x29a4` it compares the reply length against it. The implementation
  retains its inherited 1,152-byte limit, which covers this observed Sony
  software bound; it is not proof of the physical adapter's maximum. A host
  integration test sends 1,024 bytes as 64-byte USB packets through the frame
  parser and bridge, then verifies the full response and its 16-bit length.
  The 128-byte SIO page chunks are an implementation choice, not evidence of a
  Sony transaction limit.
- Sony's PS1 paths construct AA42 raw requests with payload lengths 138 for
  `81 57` (write, `0x21f4..0x224c`), 10 for `81 53` (ID,
  `0x4fd8..0x501c`), and 140 for `81 52` (read, `0x5310..0x536c`). The host
  integration tests exercise these lengths through the existing PS1 handlers,
  and assert Sony's read and write response checks at `0x54d0..0x557c` and
  `0x23e4..0x244c`, respectively. The library also probes `81 58` with length 5 (`0x4cec..0x4d44`) and checks
  for response byte 2 equal to 2 (`0x4ea4..0x4ecc`). The existing handler does
  not implement that command. In the observed caller (`0x5848..0x5888`), a
  failed probe skips a classification assignment and still attempts a PS1
  page read. A host integration test runs the real PS1 command loop, confirms
  the probe returns a short response, and then successfully reads a page.
  The probe's purpose and whether every PS3 workflow tolerates its failure
  remain unverified; this is not evidence of full PS1 compatibility.
- The interrupt callback at `0x8c8..0x8f0` takes bit 0 of the received byte,
  inverts it and adds 3 to form the posted event number. Thus a byte with bit
  0 set produces event 3, and a cleared bit produces event 4. At
  `0x8f8..0x950` the device probe compares the USB IDs with 054c:02ea.
  The routine at `0x6a4..0x6dc` passes `0x4c4b40` (5,000,000) as the
  timeout argument to syscall 0x82; the timeout's unit and all retry behavior
  have not been independently established here.

## Scheduling and ownership

Core 0 processes USB callbacks without waiting for the card. One outstanding
request uses one shared RX/TX pair; Core 1 publishes its reply before Core 0
uses it, and Core 0 retains the TX buffer until USB completion. During mode or profile
teardown, the adapter stops admitting card operations and Core 0 services USB
for up to 500 ms to let an outstanding reply complete; if the host does not
poll, disconnect releases the buffer. Cancellation
is cooperative: buffers cannot be reused while Core 1 owns them. A Pico SDK
critical section protects ownership transitions and cancellation. Core 0 must
continue the card-manager and storage tasks while a transaction runs, because
Core 1 can wait for storage work on Core 0. An unconditional synchronous wait
would therefore risk deadlock. The page operation is intended to advance as
each SIO transaction completes, without sleeping between chunks.

The nonblocking state machine needs explicit states, deadlines and buffer
ownership, but returns control to USB and storage on each pass. A synchronous
wait would make the page sequence easier to read but could deadlock on Core-0
storage work and delay USB. A bounded cooperative wait could service those
tasks, but would duplicate main-loop scheduling and still need the same
ownership, cancellation and timeout rules. The implementation instead polls
the bridge from the normal main loop; the short cooperative waits used during
profile teardown have a separate, bounded purpose.

For page operations, each `mca_protocol_task()` pass polls at most one SIO
completion and can submit the next transaction immediately in that pass.
Returning to the main loop between completions keeps storage and USB responsive
without a deliberate inter-chunk sleep. Advancing several completions per pass
could reduce loop overhead but adds scheduling complexity for one outstanding
transaction; processing an entire page synchronously has the waiting risks
above. Throughput and loop latency are not claimed to be optimal.

On USB reset, an incomplete USB frame is discarded and any old USB reply is
abandoned. A fully accepted page operation is allowed to finish, including a
pending write commit; its response is released without being sent to the new
USB session. This is deliberate: cancelling the page between its data and
commit transactions could leave an accepted write unfinished. New requests
are not queued while that operation still owns the bridge. A host integration
test exercises this reset point against the existing PS2 command handlers.

If Core 1 does not acknowledge a cancellation, the bridge keeps its buffers
reserved and does not admit a new request. It does not reset Core 1 or claim
automatic recovery. Raw-command recovery and page cancellation report the
card absent until the bridge is released. A host test covers page timeout,
retained ownership, removal and reinsertion; the failure path still needs a
real-device test.

The copied implementation branch starts at upstream `develop` commit
`44ad5deb6e095ca1e712f149c52cbe3800471319`, verified against the remote on
2026-11-04. It was subsequently applied to `feat/ps3_mca_v2` on top of its
existing preparation commit `43e2770`. That commit already modified
`src/ps2/card_emu/ps2_memory_card.c`; the integration retains those existing
changes. The implementation commit changes only
`src/ps2/card_emu/ps2_card_backend.c` in the protected directory and adds no
files there. The integrated branch passes the MCA host suite and links both
SD2PSX normal and debug builds. Hardware results below used the earlier
develop-based build, not the integrated branch with its pre-existing PS2
changes.

`tests/mca/run_tests.sh` includes a host integration test that sends command
patterns from the local `ps3mca-tool` through the USB protocol parser and MCA
backend into the existing PS2 command handlers. It exercises detection,
terminator and card specifications, raw page reads and writes, and Sony's
AA52/AA57 page operations with a mocked in-memory storage interface. The
erased-page ECC fixture covers the lookup values used by that fixture only.
A separate host integration test exercises the existing PS1 card-ID, read,
and write handlers, including a rejected checksum. Another test resets USB
after page data has been accepted and verifies that the pending write commits,
the old reply is discarded, and a subsequent detection command succeeds.
Additional protocol tests reject page requests when authentication is invalid,
the card is unavailable, the mode or variant is unsupported, or the requested
page is outside the card image; the SIO backend stays idle in these cases.
Host tests also cover a USB driver MCA-to-CDC-to-MCA profile sequence and PS2,
PS1, then PS2 detection values. These are not a real-console mode-switch test.
These tests use mocked storage and do not validate real-console timing or all
MagicGate operations.

## USB hardware validation

On 2026-11-04 the connected SD2PSX was tested through USB. A full 16 MiB
flash backup was saved and verified first. All 4,679 blocks in the SDK 2.1.1
SD2PSX UF2 matched their corresponding flash-backup blocks, identifying the
firmware under that first test. A previously absent `Card65000` channel 1 was selected
as a disposable test image; the original active boot card and channel 1
were restored afterward. The prior stored default card/channel were 1/1 and
were restored as well. The test-card image remains on the SD card.

The locally built `ps3mca-tool` v1.3 reported an 8 MiB PS2 card with 512-byte
pages and 16-page erase blocks, listed its root, injected a 4,097-byte file,
extracted it, removed it, and listed the root again. Extraction was repeated
after switching away from the test card and back. Both extracted files matched
the original byte-for-byte; SHA-256 for each was
`042a02b55ba342cdd30322331816b6e4bca073f33bdb80149dc74a6f949802f4`.

Separate USB requests tested AA57 followed by AA52 on test-card page 9000:
the 512 data bytes and first 12 ECC bytes matched. The original page data was
written back and its 512 data bytes verified. These checks do not establish
real PS3, PS1, retail/arcade PIO, all MagicGate modes, or failure recovery.

A later run on the same date used the current SDK 2.3.1 SD2PSX release UF2
(SHA-256 `ca53729c52efd5b888b28d33e688847246e1e75ee9c05674f31226e769c62e06`).
Before flashing, the adapter's complete 16 MiB flash was saved and verified;
`picotool` verified the written UF2. A one-second-delay USB-debug build was
also flashed and its CDC `version` command succeeded before the release build
was installed. This does not test early connection during the wait or an
assertions-enabled runtime.

On the release build, `ps3mca-tool` again reported the 8 MiB card, listed it,
injected and extracted the 4,097-byte file, and extracted it again after a
card switch. Both extractions matched SHA-256
`042a02b55ba342cdd30322331816b6e4bca073f33bdb80149dc74a6f949802f4`.
The test file was removed. Direct AA57/AA52 transfers matched 512 data bytes
and the first 12 ECC bytes; the original data was restored and verified.
The previously stored Card1/channel1 and active boot card/channel1 were
restored. MCA was enabled for testing and remains enabled; it was disabled
in the verified pre-test flash backup. During card switches, requests made
before the new image was ready returned `55 AF`, then succeeded afterward.

A subsequent hardware test read interrupt endpoint `0x83` while switching from
the boot card to the already stored Card1: it observed byte `0` (removal)
followed by byte `1` (insertion). The boot card/channel1 was restored. A host
`libusb_reset_device()` call then returned success; after re-enumeration, AA40,
AA42 and `ps3mca-tool --mc-info` all succeeded and the boot card remained
selected. This first test was an idle USB reset.

A separate test on the disposable card saved page 9000, sent a complete
537-byte AA57 request, and immediately called `libusb_reset_device()` without
waiting for the USB reply. Both calls succeeded. After reconnecting and
reauthenticating, AA52 returned the complete new 512-byte data pattern; the
original 512 data bytes were then restored and verified. The exact point at
which the write committed relative to the reset was not observed, so this
result does not prove that Core 1 was still writing when the reset arrived.
Stored Card1/channel1 and active boot card/channel1 were restored afterward.

An SD2PSX build using the locally checked-out SDK 2.1.1 used
194,372 bytes of linker-reported RAM; the historical MCA branch used 196,284
bytes with the same SDK version. Summed MCA/USB module data symbols were
2,568 bytes in an earlier snapshot and 6,525 bytes historically. These are whole-build and module-level
comparisons, not a controlled measurement of just the transport change. A
PMC+ build used 216,112 bytes of linker-reported RAM. These builds used the
locally checked-out SDK 2.1.1 files. Separately, a clean build using the
repository-pinned SDK revision `079c6f39023649b154152db30f1d781e884879bc`
and its TinyUSB revision `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` linked
both variants: SD2PSX used 194,064 bytes and PMC+ used 215,780 bytes of
linker-reported RAM. UF2 files were created from the linked ELF files using
picotool. The SD2PSX release UF2 was tested on hardware as described above;
the PMC+ UF2 has not been run on hardware.

A clean `develop` build with the same pinned SDK and compiler used 190,948
bytes of linker-reported RAM for SD2PSX and 212,684 for PMC+. The respective
increases in the MCA builds are 3,116 and 3,096 bytes; these are whole-build
measurements, including USB and integration overhead. For the current SD2PSX
build, `arm-none-eabi-size -A` reports `.data` 43,788 bytes and `.bss`
150,080 bytes; the corresponding clean-develop values are 43,188 and 147,564.
These `.data` values include code placed in RAM, not just initialized variables.

Summing allocated RAM input sections in the linker map by object-file path,
`src/usb/` contributes 2,579 bytes (1 in `.data`, 2,578 in `.bss`), excluding
linker padding and the separate PS1/PS2 backends. The `mca_bridge` symbol is
2,336 bytes, including the two 1,156-byte buffers. TinyUSB object files
contribute 4,667 bytes (2,917 in `.data`, including RAM-resident code, and
1,750 in `.bss`), versus 4,663 in clean develop. These object-file sums are
not isolated incremental costs: both builds already use TinyUSB, and compiler
and linker effects can affect other objects. The saved maps and build logs are
in the sibling `mca-artifacts` directory.

For each variant, `arm-none-eabi-objdump -dr` output for the protected PS2 PIO backend object,
excluding its filename header, was byte-identical between the clean `develop`
and MCA builds. This is a compiled-code comparison, not a PIO timing or
real-console test.

## Independent review and candidate fixes

A Sol 6.1 review identified an exact-packet USB IN problem. On the previously
hardware-tested build, a 60-byte AA42 payload produced a 64-byte response;
a host read of exactly 64 bytes succeeded, but the next detection command
received a zero-length packet instead of its response. The cause was an
unconditionally queued zero-length packet. The current source removes it and
releases the response on completion of the requested transfer. The device
integration test drives the real USB device and protocol layers through a
64-byte reply followed by AA40; it fails with the previous device driver and
passes with the candidate. On 2026-11-05 the candidate was flashed and verified
on the connected SD2PSX; the formerly failing exact-64-byte hardware sequence
then passed. Sony's library requests
exactly the framed response size, while the local `ps3mca-tool` requests its
1,024-byte buffer. Without a zero-length packet, a host that requests more
than an exact-64-byte response can wait until its transfer timeout; USB does
not expose the host's requested transfer length to this device code. The
observed normal `ps3mca-tool` command lengths do not produce that boundary on
an 8 MiB card. On the candidate hardware, a 128-byte host read received the
64-byte response and timed out, after which the next AA40 succeeded. The normal
ps3mcatool regression on the candidate also passed.

The review also found inherited PS1 data-interface hazards exposed over MCA:
invalid-checksum or truncated writes could change the live non-PSRAM cache,
and out-of-range pages could reach the card cache or PSRAM. The current source
stages MCA PS1 writes until checksum and address checks pass, waits for an
outstanding read DMA before copying the staged bytes, and avoids out-of-range
MCA reads and speculative prefetches. Existing PIO write-loop behavior remains
in its separate branch; that function reserves an additional 128-byte stack
buffer. Host tests use the production non-PSRAM data interface and cache to
cover bad checksums, truncated writes, invalid pages and the last valid page,
and verify a successful PIO-backend write using a mock transport. A separate
mock verifies that the DMA wait occurs before staged bytes are copied; it also
checks that no out-of-range prefetch is requested. These changes have
not been run on a PS1 or with real PSRAM DMA.

The independent review's initial concern about a stale reply after USB reset
was not reproduced by source review: reset clears the reply-pending flag and
the protocol discards the completed response. Accepted page writes continue
through commit as described above; the host integration test covers this.

The current pinned-SDK candidate uses 194,376 bytes of linker-reported RAM
for SD2PSX and 216,092 for PMC+. Its SD2PSX `.data` is 44,108 bytes and `.bss`
is 150,072 bytes. At the time of that build its `src/` matched the source tree;
all 1,198,204 flash bytes extracted from that candidate ELF matched its 4,681
UF2 blocks. Its PS2 USB path has been run on hardware as described below. A
later MCA temporary-mode reset fix changes the source and has a separate,
not-yet-installed candidate.

## 2026-11-05 candidate hardware validation

The candidate SD2PSX UF2 (SHA-256
`67a7148b15849fec08f5e7b49919af2764da332f2d2df4b58e17235496050362`)
was loaded with flash verification after saving and verifying the device's
full pre-install flash. The 64-byte AA42 response followed immediately by
AA40 succeeded on hardware; the previous build had produced an empty packet
instead of the AA40 reply. A 128-byte host read for the same 64-byte response
timed out after receiving those 64 bytes, and a subsequent AA40 succeeded.

With disposable Card65000/channel1 selected, `ps3mca-tool` v1.3 detected and
listed the 8 MiB card, injected a 4,097-byte file, and extracted it before and
after a card switch. Both results matched SHA-256
`042a02b55ba342cdd30322331816b6e4bca073f33bdb80149dc74a6f949802f4`.
The test file was removed. Direct AA57/AA52 verified 512 data bytes and 12 ECC
bytes, and the original page data was restored and verified. Interrupt endpoint
`0x83` reported removal followed by insertion on a card switch; an idle USB
reset was followed by successful AA40/AA42. Stored Card1/channel1 and active
boot card/channel1 were restored. PS1, real PIO timing and live PSRAM DMA were
not tested on hardware.

## Temporary PS1 mode reset

An independent review confirmed that the inherited settings logic does not
clear temporary PS1 mode when asked to return to an already-persistent PS2
mode. The MCA PS1 MMCE reset and the GUI's explicit PS2 selection now restore
the persistent mode before switching; the non-MCA paths retain their previous
behavior. A host regression test compiles
the actual settings and PS1 MMCE code and verifies the MCA reset, unchanged
non-MCA behavior, and persistent-PS1 default-card reset. The test fails with
the pre-fix handler. The GUI selection path was source-reviewed but is not
exercised by this test. Both firmware variants build. The later mode-reset
candidate (SD2PSX UF2 SHA-256
`7a22754bf4baf9418e44e962b14fec4d3a1ee4246bb9530cc56475bdcce76f70`)
was installed and verified on the RP2040 adapter after a verified full-flash
backup. On hardware, ps3mcatool read the PS2 card information and the
exact-length USB transfer check passed. An MMCEMAN game-ID request selected a
temporary PS1 card; the test helper rejected its acknowledgement, but a fresh
connection confirmed PS1 detection, a card-ID exchange, and a page-0 read with
valid EDC. A PS1 MMCE reset was acknowledged; the USB device re-enumerated and
a fresh ps3mcatool connection confirmed PS2 card information again. This does
not validate the GUI action or all PS1 commands on physical hardware.

## Debug MCA-to-CDC escape

In builds configured with `-DDEBUG_USB_UART=ON`, the MCA bulk OUT endpoint
accepts the exact four-byte packet `4D 43 41 21` (ASCII `MCA!`) as a request
to leave the MCA USB profile. The protocol task clears the persisted MCA
setting; the normal main-loop profile switch then tears down MCA and returns
to the CDC profile. This packet is compiled out unless `DEBUG_USB_UART` is
defined. It has no response packet. The host regression runs the protocol
test both with and without that definition and checks the setting changes
only in the debug build. It also checks that the same four bytes inside an
unfinished raw frame do not trigger the escape. The pinned-SDK SD2PSX debug
and normal builds link successfully; the normal UF2 is byte-identical to the
previously tested mode-reset candidate. On hardware that normal build returned
`55 AF` for `MCA!` and then still returned `55 02` for PS2 detection. The
debug build (UF2 SHA-256
`d92850fe1a76579a5e4589e26e96003fc18c3f137b8b0723b93119f85a6971eb`)
was installed after a verified full-flash backup. On hardware, ps3mcatool
read the PS2 card information, the escape re-enumerated the device as CDC,
and the serial `version` command reported PS2 mode. The serial `set mode mca`
command restored MCA, after which ps3mcatool again read card information.
A host regression also verifies that USB reset discards a queued escape.

## Remaining uncertainties

Real PS3 and PS1 console operation, retail/arcade PIO timing, all MagicGate modes,
and hardware failure recovery have not been verified. Sony protocol limits,
timeout units and retry behavior remain unresolved. The host tests do not
replace a real-console regression test.

The SD2PSX USB-debug configuration with a one-second startup delay links
with the pinned SDK (199,620 bytes of RAM). The startup wait now services
TinyUSB, and the application-owned USB stack detaches the stdio driver using
the public stdio API before disconnecting. This avoids the contradictory
`stdio_usb_deinit()` assertion order in SDK 2.1.1 and 2.3.1; it assumes the
application-owned TinyUSB configuration, where the SDK has no stdio background
task. The configured SDK connection and deinitialization delays are retained.
The debug build with a nonzero delay has been compiled with both SDK versions,
and its CDC command interface was tested on SDK 2.3.1 hardware. The wait's
early-connect behavior, live profile teardown, and assertions-enabled runtime
remain untested.

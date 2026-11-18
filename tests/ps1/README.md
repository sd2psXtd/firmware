# PS1 backend lifecycle regression tests

Run `sh tests/ps1/run_tests.sh` with a host C compiler. The runner enables
AddressSanitizer and UndefinedBehaviorSanitizer; no SDK checkout or hardware is
required. It uses the existing MCA host headers for common SDK declarations.

- `test_lifecycle.c` runs the production worker and existing enter/exit handshake
  with a mock transport. It checks repeated card switches, PS1-to-storage/PS2
  transitions from both PIO and MCA profiles, profile changes, re-entry, and
  interrupted write commands. Storage tests preserve the saved MCA setting.
- `test_pio.c` runs the production PIO backend with mocked hardware. It enforces
  the SDK's handler-before-enable invariant, both-edge IRQ cleanup, DAT/ACK
  release, temporary resource retention, and complete resource reclamation.

These tests do not measure physical SIO timing. On-device checks should cover
ordinary card reads/writes, controller switching, repeated USB storage entry and
exit, PS1/PS2 switching, and both PIO and MCA sessions.

# End-to-End Tests

Use this folder for executable-level smoke tests, packaging checks, CLI checks, and native SDL harnesses. The canonical test map is [`../README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/README.md).

## Native Harnesses

Native harnesses are Playwright-style SDL tests. They run in separate test executables, use real config plus `InputRouter`, and may enable narrow read-only hooks with `PR_ENABLE_TEST_HOOKS`. They do not ship in the player executable.

Current harnesses:

- [`native/title_screen_flow_harness_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/e2e/native/title_screen_flow_harness_tests.cpp): title/menu/options navigation plus music/SFX event contracts.
- [`native/transfer_system_flow_harness_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/e2e/native/transfer_system_flow_harness_tests.cpp): transfer-system keyboard/controller/pointer wiring, Box Space navigation/activation, dropdown activation, Pokemon action-menu flows, speech-bubble focus regressions, and held item move/swap/cancel behavior.
- [`native/transfer_ticket_unicode_harness_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/e2e/native/transfer_ticket_unicode_harness_tests.cpp): transfer-ticket text rendering for non-ASCII save names and titles.

Use this style for app-level flows that need real config, real input routing, and real screen/controller behavior without opening a player-facing test API. Keep assertions focused on player-observable outcomes: current screen/state, selected row, raised flow requests, rendered text path behavior, and persistence/audio requests.

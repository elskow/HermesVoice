# Testing strategy

## Host tests (default, fast, CI-gated)

Pure logic lives in functions that do not touch silicon:
`node_ptt_fold()`, config cache, health counters. They compile with
host gcc under `-DNODE_HOST_TEST`, which swaps `node_log.h` to printf and
stubs WiFi/MQTT/OTA. Run with `make -C tests/host run`.

Rule: any new algorithm must add a host test. If it cannot be host-tested,
it is structured wrong (split logic from driver calls).

## On-target tests (later, when needed)

- `idf.py build` in CI already catches API drift against the real IDF.
- For hardware-in-the-loop, add `test_apps/` using unity
  (`components/esp_rom` style) only after host coverage is solid.

## What not to do

- No mocking frameworks for the base: `#ifdef NODE_HOST_TEST` stubs are
  enough at this scale and stay readable for newcomers.
- No Bazel test runner: `make run` keeps the loop under a second.

# Test Plan — Old-device resources.json fallback

- **Feature ID:** old-device-resources-fallback
- **Requirements:** ./requirements.md
- **Status:** Approved <!-- Draft | In Review | Approved -->
- **Last updated:** 2026-09-09

## 1. Test strategy

The new logic (`resourceCountsFromResourcesJson()`) is pure JSON parsing/validation with no flow
or device-package dependency — the same shape as the existing `deriveResourceCounts()` and
`parseDeviceConfig()` tests in `tests/unittest/Compiler/QLDeviceManager_test.cpp`, which already
cover the formula path this way (inlined `config.json`/expected-value tables, no real device
package or `QLDeviceManager` singleton involved). Per developer decision during spec review, unit
tests on inlined JSON are sufficient here: the wiring inside `deviceResourceInformation()` (#3 in
the design) is a small, directly-readable conditional, and exercising it end-to-end would require
either a real installed device package with a `config.json` deliberately missing a key (none of
the 15 packages that ship `resources.json` today are set up that way without editing device_data)
or a `QLDeviceManager` singleton fixture the existing test file deliberately avoids. A flow/
integration run is not part of this test plan; see Non-goals in requirements.md.

One test case uses real numbers copied from a shipped `resources.json`
(`device_data/QLF_K6N10/GF/12nm/TURNKEY-FPGA126126/resources.json`, `"FPGA126126"` entry — the
layout name for a FIXED device is its `CUSTOMER_NAME`, not a grid string) as ground truth: `clb`:
13356, `bram`: 210, `dsp`: 420, `io`: 10080. These are the exact same numbers already inlined in
`DeriveResourceCountsMatchesShippedResourcesJSON`'s `TURNKEY-FPGA126126` case, so the new test
reuses a value set already established as correct rather than introducing a new one.

## 2. Test cases (traced to requirements)
| Test ID | Requirement(s) | Description | Suite / target | Pass criteria |
|---------|----------------|-------------|----------------|---------------|
| T-001 | REQ-002 | `resourceCountsFromResourcesJson()` on a layout entry with all four keys plus extra unrelated keys (`io_top`, `io_right`, `io_bottom`, `io_left`, `io_bram_top/bottom`, `io_dsp_top/bottom`, `corner_*` — as shipped `resources.json` files have) returns exactly `clb`/`bram`/`dsp`/`io`, matching `TURNKEY-FPGA126126`'s `"FPGA126126"` values. | `tests/unittest/Compiler/QLDeviceManager_test.cpp` | Returned vector has exactly 4 entries with the expected values; no error set. |
| T-002 | REQ-002, REQ-003 | Layout name absent from the JSON document. | same | Returns empty vector, `out_error` set, non-empty. |
| T-003 | REQ-004 | Layout entry present but missing one of `clb`/`bram`/`dsp`/`io`. | same | Returns empty vector, `out_error` set. |
| T-004 | REQ-004 | Layout entry present with a non-integer value for one key (e.g. a string or float). | same | Returns empty vector, `out_error` set, no exception propagates. |
| T-005 | REQ-004 | Document is valid JSON but the layout entry is not an object (e.g. a number or array). | same | Returns empty vector, `out_error` set, no exception propagates. |
| T-006 | REQ-001 | Existing `DeriveResourceCountsMatchesShippedResourcesJSON` and the other `DeriveResourceCounts*` tests are unmodified and still pass. | same | `ctest`/unit test binary: 0 regressions. |
| T-007 | REQ-004 | Layout entry has a value outside `int` range for one key (e.g. `9999999999999`). | same | Returns empty vector, `out_error` set — not silently narrowed to a wrong in-range value. |

REQ-005 (combining the formula's and the fallback's error text in `deviceResourceInformation()`
when `resources.json` exists but is unusable) is the one requirement this test plan does not cover
directly: it lives in the wiring `resourceCountsFromResourcesJson()` unit tests deliberately don't
reach (same limitation noted in §1 for the wiring generally). It is small enough — string
concatenation gated on `fallback_error` being non-empty — to verify by code inspection; T-001–T-005
and T-007 already establish that `fallback_error` is set exactly when the fallback should be
considered "tried and failed" rather than "not present."

## 3. Regression scope
FOEDAG unit test suite (wherever `tests/unittest/Compiler/QLDeviceManager_test.cpp` runs in CI —
the FOEDAG `ctest`/gtest target), full run, to catch any regression in the existing
`DeriveResourceCounts*` and `ParseDeviceGeometry*` cases sharing this file. No aurora2-level
`tests/`/`featuretests/` run is required for this change per the developer's verification decision;
those remain the responsibility of the eventual pin-bump PR if it wants extra confidence.

## 4. Environments / devices
- Devices: no specific device required to run the new tests (inlined JSON); the ground-truth values
  in T-001 are taken from `QLF_K6N10/GF/12nm/TURNKEY-FPGA126126`.
- Platforms: whatever the FOEDAG unit test target already builds/runs on in CI (Linux); no
  platform-specific behavior introduced.

## 5. Entry / exit criteria
- Entry: design approved, build green.
- Exit: T-001–T-007 pass, full `QLDeviceManager_test.cpp` suite green, no open Sev-1 issues.

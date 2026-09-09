# Requirements — Old-device resources.json fallback

- **Feature ID:** old-device-resources-fallback
- **Intent:** https://github.com/QL-Proprietary/aurora2/issues/2370
- **Author:** Oleksandr
- **Status:** Approved <!-- Draft | In Review | Approved -->
- **Reviewers:**
- **Last updated:** 2026-09-09

> The spec-gate hook unlocks source edits on this feature branch only once **Status** here
> reads `Approved`. Keep it `Draft` until the team has reviewed all three documents.

## 1. Problem statement
`QLDeviceManager::deviceResourceInformation()` derives a device's clb/bram/dsp/io counts purely
from geometry keys in `config.json` (`DEVICE_SIZE`, `BRAM_SIZE`, `DSP_SIZE`, `BRAM_COLS`,
`DSP_COLS`, `IO_CAPACITY` — added in aurora2#2257 / FOEDAG#208). New devices ship a complete
`config.json` and derive correctly. Old device packages predate `IO_CAPACITY` and some of the
other geometry keys, so the formula reports zero resources for them and prints which key is
missing — a regression for those packages versus what they reported before #2257.

15 packages under `device_data/QLF_K6N10/...` still ship a `resources.json` (the vpr-generated
file `deviceResourceInformation()` used to read before commit `2be6259e`, "Stop reading
resources.json"). That data was correct for those packages when it was written and is not
expected to change. Issue #2370 asks to use it again, but only as a fallback for packages the
formula cannot answer — not to reintroduce it as the primary source now that `config.json` is
the maintained one going forward.

This restates the issue's framing directly; no divergence identified.

## 2. Goals / Non-goals
### Goals
- When `config.json`'s formula cannot derive resource counts for a device/layout (missing or
  unusable geometry key), fall back to that device type's `resources.json` if it exists and has
  an entry for the layout.
- Keep `config.json` + formula as the primary, preferred source for every device that can answer
  from it — including old devices once `device_data` backfills the missing keys.

### Non-goals (explicitly out of scope)
- Regenerating, validating, or backfilling `resources.json` content in `device_data`.
- Changing the formula in `deriveResourceCounts()` itself.
- Restoring `resources.json` as the primary/first-checked source (pre-#2257 ordering).
- Restoring the `transformer.py` / `generate_device_info` machinery that produced
  `resources.json` (removed in aurora2#2327); this only re-reads files already shipped.
- Passing through `resources.json` keys other than `clb`, `bram`, `dsp`, `io` (e.g. `io_top`,
  `io_right`, `io_bottom`, `io_left`, `hfo`) — no current caller consumes them.

## 3. Functional requirements
| ID | Requirement | Priority (M/S/C) | Acceptance criteria |
|----|-------------|------------------|---------------------|
| REQ-001 | `deviceResourceInformation()` returns the formula-derived counts unchanged when `deriveDeviceResourceInformation()` succeeds. | M | For a device with a complete `config.json`, output and any console error is identical to current `develop` behavior — no `resources.json` read attempted. |
| REQ-002 | When the formula fails (empty result with a non-empty derive error) and `<deviceTypeDirPath>/resources.json` exists and contains an entry for `device_target.device_variant_layout.name`, return `clb`/`bram`/`dsp`/`io` read from that entry instead of reporting the derive error. | M | For a package with a `resources.json` entry for its layout and a `config.json` missing `IO_CAPACITY`, the returned vector matches the four values verbatim from `resources.json`, and no error is reported. |
| REQ-003 | When the formula fails and no usable `resources.json` fallback exists (file absent, or present but no entry for this layout, or entry missing one of the four keys), preserve current behavior: report the derive error via `reportDeviceDataError()`/`ErrorMessage()` and return an empty vector. | M | Same error text and empty result as today for a package with neither source. |
| REQ-004 | A `resources.json` entry that is not a JSON object, or whose `clb`/`bram`/`dsp`/`io` values are not integers, is treated as "no usable fallback" (falls through to REQ-003), not a crash or an uncaught exception. | M | Malformed layout entry is parsed for missing keys/JSON errors and reported as fallback failed, not `std::terminate`/exception. |

## 4. Non-functional requirements
| ID | Category (perf / portability / compat / security / UX) | Requirement | Target |
|----|--------------------------------------------------------|-------------|--------|
| NFR-001 | compat | No change in behavior for any device whose `config.json` already derives successfully today. | Existing `QLDeviceManager_test.cpp` formula-derivation tests pass unmodified. |
| NFR-002 | perf | `resources.json` is only opened when the formula has already failed for this device/layout, not speculatively on every call. | Code inspection: file read is inside the failure branch. |

## 5. Constraints & assumptions
- Target devices / platforms: FOEDAG-only C++ (`QLDeviceManager.cpp`/`.h`), no platform-specific
  code; exercised by the QLF_K6N10 packages under `device_data` that still ship `resources.json`
  (GF 22nm/12nm and TSMC 16nm/12nm foundries — 15 packages as of this writing).
- Submodule / upstream impact: `foedag/foedag-gh` only (`QuickLogic-Corp/FOEDAG`, `develop`).
  Author there per the submodule-workflow; no `device_data` or aurora2-side source change needed.
  The aurora2 pin bump happens after the FOEDAG PR merges.
- Toolchain / dependency versions: none new; reuses the `json` (nlohmann) and `FileUtils`
  utilities `QLDeviceManager.cpp` already depends on.

## 6. Open questions
- None outstanding — priority order (formula-first, `resources.json` fallback), key scope
  (`clb`/`bram`/`dsp`/`io` only), and verification approach (unit test only) were confirmed with
  the developer during spec drafting.

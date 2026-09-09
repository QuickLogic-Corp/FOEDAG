# Design & Install Plan — Old-device resources.json fallback

- **Feature ID:** old-device-resources-fallback
- **Requirements:** ./requirements.md
- **Status:** Approved <!-- Draft | In Review | Approved -->
- **Last updated:** 2026-09-09

> §2 below, `Implementation plan`, is the **accepted plan**: implementation follows it, and
> `/review-feature` flags a diff that departs from it. Departing is allowed — designs meet reality
> — but update that section in the same commit so the plan and the diff never disagree silently.

## 1. Approach / architecture
`QLDeviceManager::deviceResourceInformation(device_target)` (`QLDeviceManager.cpp:4598-4617`) is
the single call site every consumer of resource counts goes through — `CompilerOpenFPGA_ql.cpp:9625`
and `populateVariantLayoutResources()` (`QLDeviceManager.cpp:1541-1578`), which the GUI catalog
listing uses. It currently: calls `deriveDeviceResourceInformation()` (the `config.json` formula
path), and if that comes back empty with a derive error, reports the error and returns empty.

This restores exactly the fallback branch commit `2be6259e` ("Stop reading resources.json")
removed, but re-derived to slot into the current formula-first structure rather than the old
resources.json-first one: when — and only when — `deriveDeviceResourceInformation()` fails with a
real error (not the "not yet known" case), read `<deviceTypeDirPath>/resources.json`, look up the
entry for `device_target.device_variant_layout.name`, and use its `clb`/`bram`/`dsp`/`io` values if
all four are present and are integers. If the fallback also has nothing usable, fall through to the
existing error-reporting path unchanged.

The lookup/parse logic is a new pure static function, `resourceCountsFromResourcesJson()`, mirroring
`deriveResourceCounts()`'s shape (takes already-parsed JSON, returns the tuple vector, takes an
`out_error`) so it is unit-testable the same way, with no device package or singleton needed.

Alternative considered: re-inserting the fallback as a check *before* the formula (matching the
pre-#2257 order exactly). Rejected per the requirements — `config.json` is the maintained source
going forward, and preferring a frozen `resources.json` would mean a device silently ignores a
config.json fix (e.g. `device_data` backfilling `IO_CAPACITY`) as long as a stale `resources.json`
still sits next to it.

## 2. Implementation plan
| # | File / area | Change | What could break |
|---|-------------|--------|------------------|
| 1 | `src/Compiler/QLDeviceManager.h` (~line 417, next to `deriveResourceCounts()`) | Declare `static std::vector<std::tuple<std::string, std::optional<int>>> resourceCountsFromResourcesJson(const json& resources_json, const std::string& layout_name, std::string* out_error = nullptr);` | Header-only; no behavior change by itself. |
| 2 | `src/Compiler/QLDeviceManager.cpp` (~line 4552, right after `deriveResourceCounts()`) | Implement `resourceCountsFromResourcesJson()`: look up `resources_json[layout_name]`; if absent, not an object, or missing/non-integer `clb`/`bram`/`dsp`/`io`, set `*out_error` and return `{}`; otherwise return the four as a tuple vector, in the same `clb, bram, dsp, io` order `deriveResourceCounts()` uses. | A malformed `resources.json` (wrong type, `NaN`, huge int) must be rejected via `nlohmann::json`'s `is_number_integer()`/`.at()` checks, not an uncaught `json::exception` — wrap the whole lookup in try/catch mapping any parse exception to `out_error`. |
| 3 | `src/Compiler/QLDeviceManager.cpp:4598-4617` (`deviceResourceInformation()`) | Before calling `reportDeviceDataError(derive_error)`, when `resources_vector.empty() && !derive_error.empty()`: build `deviceTypeDirPath(device_target) / "resources.json"`, and if `FileUtils::FileExists()`, parse it (try/catch around `json::parse`, catching a parse exception into `fallback_error`) and call `resourceCountsFromResourcesJson()` with `device_target.device_variant_layout.name`. If that returns non-empty, return it directly (skip `reportDeviceDataError`). Otherwise — file present but unusable — append `fallback_error` to `derive_error` (REQ-005) before the existing `reportDeviceDataError(derive_error)` call. If the file is simply absent, `derive_error` is untouched (REQ-003). | Must not call `reportDeviceDataError` twice. Must not read `resources.json` at all when the formula succeeded (NFR-002) — the read only happens inside the `resources_vector.empty() && !derive_error.empty()` branch, which already excludes the "not yet known" case. Must not append an empty `fallback_error` (only set when the file exists and is unusable) to avoid a dangling "; resources.json fallback also failed: " suffix on the absent-file path. |
| 4 | `tests/unittest/Compiler/QLDeviceManager_test.cpp` (after `DeriveResourceCountsMatchesShippedResourcesJSON`, ~line 289) | Add `TEST(QLDeviceManager, ResourceCountsFromResourcesJsonUsesShippedValues)` and failure-mode cases (missing layout key, missing one of the four fields, non-integer value, extra unrelated keys ignored) calling `resourceCountsFromResourcesJson()` directly with inlined JSON, one case using real numbers from a shipped `resources.json` (`QLF_K6N10/GF/12nm/TURNKEY-FPGA126126`'s `"FPGA126126"` entry — the same clb/bram/dsp/io values already inlined in `DeriveResourceCountsMatchesShippedResourcesJSON`'s case for that device) as ground truth, same style as the existing `DerivedResourcesCase` table. | None — purely additive, no existing test touched. |

- **First thing to build:** `resourceCountsFromResourcesJson()` (#1–2) plus its unit tests (#4) —
  proves the parsing/validation logic in isolation before wiring it into the call site.
- **Not touched:** `deriveResourceCounts()` and the `config.json` formula itself; `populateVariantLayoutResources()`
  and `CompilerOpenFPGA_ql.cpp:9625` (both already go through `deviceResourceInformation()`, so they
  pick up the fallback automatically); `device_data` content; `transformer.py` (already removed in
  aurora2#2327, not restored).

## 3. Affected repos, submodules & pins
- aurora2 directories touched: none directly; the `foedag/foedag-gh` pin bumps after this PR
  merges, per the submodule workflow.
- Submodules touched (author in the standalone clone, bump the pin after merge): `foedag/foedag-gh`
  (`QuickLogic-Corp/FOEDAG`, `develop`).
- New/changed submodule pins: `foedag/foedag-gh`, bumped to this PR's merge SHA after review.

## 4. Build / install impact
- New or changed make / cmake targets: none.
- New dependencies (and pinning): none — reuses `nlohmann::json` and `FileUtils`, already linked.
- Packaging / `device_data` / install-layout changes: none; reads `resources.json` files already
  shipped in installed device packages.

## 5. Data & interfaces
- New function `QLDeviceManager::resourceCountsFromResourcesJson()` (public static, like
  `deriveResourceCounts()`), for testability — not a new public API surface consumers are expected
  to call directly.
- No Tcl command, CLI flag, or file format changes. `resources.json`'s on-disk format and location
  (`<deviceTypeDirPath>/resources.json`, keyed by layout name) are unchanged from before commit
  `2be6259e` — this only re-adds a reader for a format that already exists in shipped packages.

## 6. Rollout & back-out plan
- Rollout steps: merge the FOEDAG PR, then bump the `foedag/foedag-gh` pin in an aurora2 PR once
  `submodule-policy` passes (pin must be an ancestor of `develop`).
- Back-out / revert steps: revert the pin bump commit in aurora2; no data or format migration to
  undo, since no files outside FOEDAG's own source changed.

## 7. Risks & mitigations
| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| A device with both a fixable `config.json` (e.g. after `device_data` backfills `IO_CAPACITY`) and a stale `resources.json` keeps reporting the old frozen numbers if the fallback is checked first by mistake. | Low | Medium — silently wrong resource counts | REQ-002/§1 make the formula strictly primary; fallback only triggers when the formula already failed. |
| Malformed or hand-edited `resources.json` throws an uncaught `nlohmann::json` exception, crashing the GUI thread. | Low | High — app crash | REQ-004; #2/#3 wrap parsing in try/catch and validate types before use. |
| Fallback silently masks a `config.json` bug that should be visible (e.g. a typo'd key) because the old `resources.json` happens to answer. | Medium | Low — the same masking risk the pre-#2257 code always had for these packages | Accepted per issue #2370 — these are exactly the packages `config.json` cannot yet describe; out of scope to also add a "using stale data" diagnostic (not requested). |

## 8. Requirement coverage
| Requirement | Where addressed in this design |
|-------------|--------------------------------|
| REQ-001 | #3 — fallback branch only reached when `resources_vector.empty() && !derive_error.empty()`; formula success path untouched. |
| REQ-002 | #1–#3 — `resourceCountsFromResourcesJson()` plus the call site wiring. |
| REQ-003 | #3 — `derive_error` left untouched when `resources.json` is absent, so the existing `reportDeviceDataError(derive_error)` call is unmodified for that case. |
| REQ-004 | #2 — type/presence/range validation; #3 — try/catch around JSON parsing. |
| REQ-005 | #3 — `fallback_error` appended to `derive_error` whenever the file exists but the fallback fails. |
| NFR-001 | Not touched: item in "Not touched" list; existing formula tests unaffected. |
| NFR-002 | #3 — file read is inside the already-failed branch only. |

# Device-free GUI scenarios

Replay scripts that drive the aurora GUI and check what it does. They need no
device data, so they run in FOEDAG's own CI.

```
make test/gui-scenarios                      # build, then run every scenario
FOEDAG_GUI_TEST_BIN=build/bin/aurora tests/GuiScenarios/run_scenarios.sh tests/GuiScenarios/console_puts.tcl
FOEDAG_GUI_TEST_BIN=build/bin/aurora tests/GuiScenarios/run_scenarios.sh --show   # watch them
FOEDAG_GUI_TEST_BIN=build/bin/aurora tests/GuiScenarios/run_scenarios.sh --interactive tests/GuiScenarios/console_puts.tcl
```

`--show` uses the platform's own display instead of the offscreen one. Sync
scripts run without delays, so the window passes quickly.

`--interactive` also prints `NEXT <line>: <command>` before each line and runs
it only when you press Space in an aurora window. That Space is not passed on
to the GUI, so it never presses a focused button. There is no time limit.
`--interactive --timeout <ms>` also runs a waiting line after that time, to
watch a script at a set pace. Without `--timeout` (or with -1) it waits for
Space only.

Each scenario runs in its own directory under `build/gui-scenarios/<name>/`,
with its log in `replay.log`.

## Replay mode

`aurora --replay <script.tcl>` starts the normal GUI and runs the script inside
it. The option is for developers, so `aurora --help` does not list it.

A script whose first line is `# gui-test: sync` runs one command at a time,
with no fixed delay between lines. It must synchronize through
`gui_wait_event` and `gui_wait_for`. The run exits 0 and prints `PASS:` when
every line succeeds. Otherwise it prints `FAIL: <command> <object>: <detail>`,
the script line, and exits 1.

Scripts without the header use the old harness: lines run 500 ms apart.

When a line opens a modal dialog, the next lines run while the dialog is open,
so the script can answer it:

```
gui_click taskSettingsApply   ;# opens a confirmation dialog
gui_dialog accept             ;# answers it; the save then completes
```

The `gui_*` commands, their syntax and the milestone names are listed in
aurora2 `docs/specs/gui-test-automation/` (`design-install.md` §5,
`milestones.md`, `object-names.md`).

## Recording

`aurora --record <file.tcl>` starts the normal GUI and writes each action as the
`gui_*` line that replays it, with `gui_wait_event` lines for the work the GUI
did in between. Ctrl+Shift+S writes `gui_assert` checks for the focused widget.
An action on a widget without an objectName becomes a `# UNRECORDED` comment
and a warning: name the widget, then record again. Like `--replay`, `--record`
is not listed in `aurora --help`.

`--replay <script> --record <file>` records a replayed script.
`run_scenarios.sh --roundtrip` (`make test/gui-record-roundtrip`) does that for
every scenario, checks that the recording has the same action lines, then
replays the recording from a new directory. So scenarios are written the way
the recorder writes them: one command per line, Tcl list quoting, and
`$::gui_test::project_dir` for the run directory.

## Negative scripts

A script with `# expect: <text>` lines must fail: it passes only if aurora
exits non-zero and its `FAIL:` line contains every expected text.

## Repeatable runs

`run_scenarios.sh` pre-accepts the EULA (`AURORA_IMAGE_DOCKER_SCRIPTS_DIR`)
and keeps Qt settings in the run directory (`XDG_CONFIG_HOME`), so no run
depends on an earlier one. The offscreen platform needs no display.

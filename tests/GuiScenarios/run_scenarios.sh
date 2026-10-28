#!/usr/bin/env bash
# Runs device-free GUI scenarios with "<binary> --replay", one run dir each.
# usage: GUI_TEST_FRAMEWORK_TARGET_BIN=<binary> run_scenarios.sh [--show | --interactive [--timeout ms] | --roundtrip] [scenario.tcl ...]
#   GUI_TEST_FRAMEWORK_TARGET_BIN: the GUI application to test; no scenarios: all here
#   --show: use the platform's own Qt display instead of offscreen, to watch the run
#   --interactive: as --show, and run each line only after Space is pressed
#     --timeout ms: or once a line has waited that long (default -1: wait for Space)
#   --roundtrip: replay each scenario with --record, compare its action lines
#     with the recording's (G-013), then replay the recording unchanged (G-015)
# A scenario with "# expect: <text>" lines is a negative script: it passes only
# if <binary> exits non-zero and its FAIL line contains every expected text.
# GUI_TEST_FRAMEWORK_GOLDEN_DIR turns the screenshot gate on; without it,
# screenshots are only saved. Negative scripts always use golden/ here, whose
# goldens fail on any machine. GUI_TEST_FRAMEWORK_GOLDEN=update (gen-goldens mode)
# skips negative scripts.
set -u

platform=(env QT_QPA_PLATFORM=offscreen)
interactive=0
roundtrip=0
case "${1:-}" in
  --show | --interactive)
    [ "$1" = --interactive ] && interactive=1
    if [ "$(uname)" = Linux ] && [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
      echo "[error] $1 needs a display; DISPLAY and WAYLAND_DISPLAY are unset" >&2
      exit 1
    fi
    shift
    hold_timeout=-1
    if [ $interactive -eq 1 ] && [ "${1:-}" = "--timeout" ]; then
      case "${2:-}" in
        -1) ;;
        '' | *[!0-9]*) echo "[error] --timeout needs milliseconds, or -1" >&2; exit 1 ;;
      esac
      hold_timeout=$2
      shift 2
    fi
    platform=(env -u QT_QPA_PLATFORM GUI_TEST_FRAMEWORK_SHOW=1 GUI_TEST_FRAMEWORK_INTERACTIVE=$interactive
      GUI_TEST_FRAMEWORK_INTERACTIVE_TIMEOUT=$hold_timeout)
    ;;
  --roundtrip)
    roundtrip=1
    shift
    # Recordings name their checkpoint screenshots after the recording, so
    # there are no goldens to compare them with.
    GUI_TEST_FRAMEWORK_GOLDEN_DIR=
    ;;
esac

if [ -z "${GUI_TEST_FRAMEWORK_TARGET_BIN:-}" ]; then
  echo "[error] set GUI_TEST_FRAMEWORK_TARGET_BIN=<binary>, the GUI application to test" >&2
  exit 1
fi
binary=$(cd "$(dirname "$GUI_TEST_FRAMEWORK_TARGET_BIN")" && pwd)/$(basename "$GUI_TEST_FRAMEWORK_TARGET_BIN")
echo "binary: $binary"
here=$(cd "$(dirname "$0")" && pwd)
if [ $# -eq 0 ]; then set -- "$here"/*.tcl; fi
out=${GUI_TEST_FRAMEWORK_OUT_DIR:-$PWD/gui-scenarios}
mkdir -p "$out"

# A paused interactive session must not be killed by the time limit.
limit=""
if [ $interactive -eq 0 ] && command -v timeout >/dev/null 2>&1; then
  limit="timeout 300"
fi

# The action lines G-013 compares: what a user does, not waits or checks.
actions='^(gui_click|gui_set_text|gui_select|gui_trigger_action|gui_dialog|gui_drag|gui_menu)( |$)'

# run <run dir> <binary args...>: one run in a fresh dir, log in replay.log.
run() {
  local dir=$1
  shift
  rm -rf "$dir"
  mkdir -p "$dir/eula"
  # The EULA dialog is shown once per install; pre-accept it so every run
  # starts the same way. Settings and the home directory (which holds the
  # user's device registry) go to the run dir, and the device-data variables
  # are dropped, so a local run is as device-free as CI.
  : > "$dir/eula/AURORA_EULA_ACCEPTED.txt"
  mkdir -p "$dir/home"
  (cd "$dir" &&
    AURORA_IMAGE_DOCKER_SCRIPTS_DIR="$dir/eula" XDG_CONFIG_HOME="$dir/config" \
      HOME="$dir/home" USERPROFILE="$dir/home" \
      AURORA2_DEVICE_DATA_DIR= AURORA2_DEVICE_DATA_PATH= \
      GUI_TEST_FRAMEWORK_GOLDEN_DIR="$golden" \
      "${platform[@]}" $limit "$binary" "$@" 2>&1 |
      if [ $interactive -eq 1 ]; then tee "$dir/replay.log"; else cat > "$dir/replay.log"; fi
    exit "${PIPESTATUS[0]}")
}

passed=0
failed=0
skipped=0
for scenario in "$@"; do
  scenario=$(cd "$(dirname "$scenario")" && pwd)/$(basename "$scenario")
  name=$(basename "$scenario" .tcl)
  dir=$out/$name
  negative=0
  grep -q '^# expect: ' "$scenario" && negative=1
  if [ $roundtrip -eq 1 ] && [ $negative -eq 1 ]; then continue; fi
  if [ $negative -eq 1 ] && [ "${GUI_TEST_FRAMEWORK_GOLDEN:-}" = update ]; then
    skipped=$((skipped + 1))
    echo "SKIP $name (gen-goldens mode)"
    continue
  fi
  golden=${GUI_TEST_FRAMEWORK_GOLDEN_DIR:-}
  [ $negative -eq 1 ] && golden=$here/golden

  reason=""
  if [ $roundtrip -eq 1 ]; then
    recording=$out/$name.recording.tcl
    run "$dir" --replay "$scenario" --record "$recording"
    rc=$?
    if [ $rc -ne 0 ] || ! grep -q '^PASS: ' "$dir/replay.log"; then
      reason="the scenario failed with --record"
    elif ! diff <(grep -E "$actions" "$scenario") <(grep -E "$actions" "$recording") \
        > "$dir/actions.diff"; then
      reason="action lines differ, see $dir/actions.diff"
    elif grep -q '^# UNRECORDED' "$recording"; then
      reason="the recording has # UNRECORDED lines"
    elif grep -q '^gui_test_checkpoint' "$scenario" && ! grep -q '^gui_assert' "$recording"; then
      reason="the checkpoints wrote no gui_assert lines"
    elif grep -qE '^[[:space:]]*after[[:space:]]+[0-9]' "$recording"; then
      reason="the recording has a fixed delay"
    else
      dir=$out/$name.replayed
      run "$dir" --replay "$recording"
      rc=$?
      if [ $rc -ne 0 ] || ! grep -q '^PASS: ' "$dir/replay.log"; then
        reason="the recording does not replay"
      fi
    fi
  else
    run "$dir" --replay "$scenario"
    rc=$?
    fail_line=$(grep -m1 '^FAIL: ' "$dir/replay.log")
    if [ $negative -eq 1 ]; then
      [ $rc -ne 0 ] || reason="it passed"
      while IFS= read -r expect; do
        case "$fail_line" in *"$expect"*) ;; *) reason="FAIL line lacks: $expect" ;; esac
      done < <(sed -n 's/^# expect: //p' "$scenario")
    elif [ $rc -ne 0 ] || ! grep -q '^PASS: ' "$dir/replay.log"; then
      reason="exit $rc"
    fi
  fi

  if [ -z "$reason" ]; then
    passed=$((passed + 1))
    echo "PASS $name"
  else
    failed=$((failed + 1))
    echo "FAIL $name ($reason; log $dir/replay.log)"
    grep -A1 '^FAIL: ' "$dir/replay.log" | sed 's/^/    /'
  fi
done

echo "GUI scenarios: $passed passed, $failed failed, $skipped skipped"
[ $failed -eq 0 ]

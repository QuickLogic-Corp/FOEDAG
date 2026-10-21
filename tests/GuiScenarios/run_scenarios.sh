#!/usr/bin/env bash
# Runs device-free GUI scenarios with "aurora --replay", one run dir each.
# usage: [AURORA=<binary>] run_scenarios.sh [--show] [scenario.tcl ...]
#   AURORA: the aurora to run (default: aurora on PATH); no scenarios: all here
#   --show: use the platform's own Qt display instead of offscreen, to watch the run
# A scenario with "# expect: <text>" lines is a negative script: it passes only
# if aurora exits non-zero and its FAIL line contains every expected text.
set -u

platform=(env QT_QPA_PLATFORM=offscreen)
if [ "${1:-}" = "--show" ]; then
  shift
  if [ "$(uname)" = Linux ] && [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    echo "[error] --show needs a display; DISPLAY and WAYLAND_DISPLAY are unset" >&2
    exit 1
  fi
  platform=(env -u QT_QPA_PLATFORM)
fi

aurora=${AURORA:-$(command -v aurora)} || {
  echo "[error] no aurora on PATH; set AURORA=<binary>" >&2
  exit 1
}
aurora=$(cd "$(dirname "$aurora")" && pwd)/$(basename "$aurora")
echo "aurora: $aurora"
here=$(cd "$(dirname "$0")" && pwd)
if [ $# -eq 0 ]; then set -- "$here"/*.tcl; fi
out=${GUI_SCENARIOS_OUT:-$PWD/gui-scenarios}
mkdir -p "$out"

limit=""
if command -v timeout >/dev/null 2>&1; then limit="timeout 300"; fi

passed=0
failed=0
for scenario in "$@"; do
  scenario=$(cd "$(dirname "$scenario")" && pwd)/$(basename "$scenario")
  name=$(basename "$scenario" .tcl)
  dir=$out/$name
  rm -rf "$dir"
  mkdir -p "$dir/eula"
  # The EULA dialog is shown once per install; pre-accept it so every run
  # starts the same way. Settings go to the run dir for the same reason.
  : > "$dir/eula/AURORA_EULA_ACCEPTED.txt"
  (cd "$dir" &&
    AURORA_IMAGE_DOCKER_SCRIPTS_DIR="$dir/eula" XDG_CONFIG_HOME="$dir/config" \
      "${platform[@]}" $limit "$aurora" --replay "$scenario" \
      > "$dir/replay.log" 2>&1)
  rc=$?

  fail_line=$(grep -m1 '^FAIL: ' "$dir/replay.log")
  ok=1
  if grep -q '^# expect: ' "$scenario"; then
    [ $rc -ne 0 ] || ok=0
    while IFS= read -r expect; do
      case "$fail_line" in *"$expect"*) ;; *) ok=0 ;; esac
    done < <(sed -n 's/^# expect: //p' "$scenario")
  else
    [ $rc -eq 0 ] && grep -q '^PASS: ' "$dir/replay.log" || ok=0
  fi

  if [ $ok -eq 1 ]; then
    passed=$((passed + 1))
    echo "PASS $name"
  else
    failed=$((failed + 1))
    echo "FAIL $name (exit $rc, log $dir/replay.log)"
    grep -A1 '^FAIL: ' "$dir/replay.log" | sed 's/^/    /'
  fi
done

echo "GUI scenarios: $passed passed, $failed failed"
[ $failed -eq 0 ]

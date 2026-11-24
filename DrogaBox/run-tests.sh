#!/bin/bash
#
# Builds and runs the host tests of the picker editor, normal and with sanitizers.
#   DrogaBox/run-tests.sh [--no-sanitize]
# Needs the UDK (UDK_PATH, ../UDK by default: the one build_oc.tool fetches).
# Nothing here touches any EFI or any file outside this checkout.
#
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
: "${UDK_PATH:=$ROOT/UDK}"
export UDK_PATH
[ -d "$UDK_PATH/MdePkg" ] || { echo "run-tests: no UDK at $UDK_PATH (set UDK_PATH)"; exit 2; }

SAN=1
[ "${1:-}" = "--no-sanitize" ] && SAN=0
JOBS=$(getconf _NPROCESSORS_ONLN)
FAIL=0
LOG=$(mktemp "${TMPDIR:-/tmp}/drogabox-test.XXXXXX")

run_one() { # dir, binary, args, sanitize(0/1)
  local dir=$1 bin=$2 args=$3 san=$4 label out rc
  label="$dir$([ "$san" = 1 ] && echo ' (SANITIZE=1)')"
  ( cd "$ROOT/Utilities/$dir" && make clean >/dev/null 2>&1; rm -f ./*.o ./*.d )
  if ! ( cd "$ROOT/Utilities/$dir" && if [ "$san" = 1 ]; then SANITIZE=1 make -j "$JOBS"; else make -j "$JOBS"; fi ) >"$LOG" 2>&1; then
    echo "FAIL  $label: build error"; tail -20 "$LOG"; FAIL=1; return
  fi
  out=$( cd "$ROOT/Utilities/$dir" && ./$bin $args 2>&1 ); rc=$?
  echo "$out" | tail -3
  if [ $rc -ne 0 ]; then echo "FAIL  $label: exit $rc"; FAIL=1; else echo "OK    $label"; fi
}

run_one TestPickerEditor PickerEditor ../../Docs/Sample.plist 0
[ $SAN = 1 ] && run_one TestPickerEditor PickerEditor ../../Docs/Sample.plist 1
run_one TestEditPlist EditPlist ../../Docs/Sample.plist 0
[ $SAN = 1 ] && run_one TestEditPlist EditPlist ../../Docs/Sample.plist 1

rm -f "$LOG"
# Leave no objects of the sanitized build behind.
for d in TestPickerEditor TestEditPlist; do ( cd "$ROOT/Utilities/$d" && make clean >/dev/null 2>&1 ); done
[ $FAIL = 0 ] && echo "ALL TESTS PASSED" || echo "TESTS FAILED"
exit $FAIL

#!/bin/bash
#
# Builds and runs the host tests of the picker editor and of OpenCanopy, normal and with sanitizers.
#   DrogaBox/run-tests.sh [--no-sanitize]
# Needs the UDK (UDK_PATH, ../UDK by default: the one build_oc.tool fetches).
# The OpenCanopy test also needs the font of OcBinaryData: set OC_RESOURCES to a Resources folder
# holding Font/ (for example a copy of EFI/OC/Resources); without it that test is skipped.
# Nothing here touches any EFI or any file outside this checkout (and a temporary folder).
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
  out=$( cd "$ROOT/Utilities/$dir" && "./$bin" "$args" 2>&1 ); rc=$?
  echo "$out" | tail -3
  if [ "$rc" -ne 0 ]; then echo "FAIL  $label: exit $rc"; FAIL=1; else echo "OK    $label"; fi
}

run_one TestPickerEditor PickerEditor ../../Docs/Sample.plist 0
if [ "$SAN" = 1 ]; then run_one TestPickerEditor PickerEditor ../../Docs/Sample.plist 1; fi
run_one TestEditPlist EditPlist ../../Docs/Sample.plist 0
if [ "$SAN" = 1 ]; then run_one TestEditPlist EditPlist ../../Docs/Sample.plist 1; fi

# OpenCanopy: the real picker drawn on the host (Utilities/TestCanopy), with the theme of this repo.
canopy_check() { # label, expected text, Canopy arguments...
  local label=$1 want=$2 out
  shift 2
  out=$( cd "$ROOT/Utilities/TestCanopy" && CANOPY_DEBUG=1 ./Canopy "$CANOPY_RES" "$@" 2>&1 )
  if echo "$out" | grep -q -e "runtime error" -e "Sanitizer" -e "^FAIL"; then
    echo "$out" | grep -e "runtime error" -e "Sanitizer" -e "^FAIL" | head -5
    echo "FAIL  TestCanopy $label"; FAIL=1
  elif echo "$out" | grep -q -e "$want"; then
    echo "OK    TestCanopy $label"
  else
    echo "$out" | tail -3
    echo "FAIL  TestCanopy $label: expected '$want'"; FAIL=1
  fi
}

run_canopy() { # sanitize(0/1)
  local san=$1 tag=""
  [ "$san" = 1 ] && tag=" (SANITIZE=1)"
  ( cd "$ROOT/Utilities/TestCanopy" && make clean >/dev/null 2>&1 )
  if ! ( cd "$ROOT/Utilities/TestCanopy" && if [ "$san" = 1 ]; then SANITIZE=1 make -j "$JOBS"; else make -j "$JOBS"; fi ) >"$LOG" 2>&1; then
    echo "FAIL  TestCanopy$tag: build error"; tail -20 "$LOG"; FAIL=1; return
  fi
  canopy_check "1080p zooms to 1.5x$tag" "zoom 1" 'Auto' "$CANOPY_RES/shot" -n 6 -t 5 wait:90 snap end
  canopy_check "720p stays at 1x$tag" "zoom 0" 'Auto' "$CANOPY_RES/shot" -n 6 -w 1280 -h 720 wait:90 snap end
  canopy_check "OC_ATTR_NO_ZOOM keeps 1x$tag" "zoom 0" 'Auto' "$CANOPY_RES/shot" -n 6 -a 0x411 wait:90 snap end
  canopy_check "4K at UIScale 2$tag" "scale 2, zoom 0" 'Auto' "$CANOPY_RES/shot" -n 6 -w 3840 -h 2160 -s 2 wait:90 snap end
  canopy_check "Enter starts the selected entry$tag" "chosen an entry" 'Auto' "$CANOPY_RES/shot" -n 6 -t 5 wait:60 right wait:30 enter
  canopy_check "F2 asks for the editor$tag" "editor shown" 'Auto' "$CANOPY_RES/shot" -n 6 wait:60 f2 wait:10
}

if [ -n "${OC_RESOURCES:-}" ] && [ -f "$OC_RESOURCES/Font/Font_2x.bin" ]; then
  CANOPY_RES=$(mktemp -d "${TMPDIR:-/tmp}/drogabox-canopy.XXXXXX")
  mkdir -p "$CANOPY_RES/Resources/Image/DrogaBox"
  cp -R "$OC_RESOURCES/Font" "$CANOPY_RES/Resources/"
  cp -R "$ROOT/DrogaBox/Theme/AETHER" "$CANOPY_RES/Resources/Image/DrogaBox/"
  run_canopy 0
  if [ "$SAN" = 1 ]; then run_canopy 1; fi
  rm -rf "$CANOPY_RES"
else
  echo "SKIP  TestCanopy: set OC_RESOURCES to a Resources folder with Font/ to run it"
fi

rm -f "$LOG"
# Leave no objects of the sanitized build behind.
for d in TestPickerEditor TestEditPlist TestCanopy; do ( cd "$ROOT/Utilities/$d" && make clean >/dev/null 2>&1 ); done
if [ "$FAIL" = 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; fi
exit "$FAIL"

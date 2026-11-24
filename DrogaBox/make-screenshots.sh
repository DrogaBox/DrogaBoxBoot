#!/bin/bash
#
# Regenerates the README screenshots in DrogaBox/screenshots from the real editor code.
#   DrogaBox/make-screenshots.sh
#
# The editor runs on the host through Utilities/TestPickerEditor (an 80x25 console, scripted keys),
# and each screen is drawn into a PNG by DrogaBox/tools/render-screen.swift (light grey on black,
# like the firmware text console). The kexts screens use a throwaway OpenCore folder with empty
# kexts whose Info.plist files declare the usual Lilu dependencies, and Lilu moved after
# WhateverGreen so that the order check has something to show.
#
# With OC_RESOURCES set to a Resources folder holding Font/ (the font of OcBinaryData), it also draws
# the OpenCanopy picker and the editor sheet with the real code (Utilities/TestCanopy):
# picker.png, sheet-menu.png, sheet-config.png, sheet-files.png.
#
# Needs the UDK (UDK_PATH, ../UDK by default) and swiftc. Touches no EFI.
#
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
: "${UDK_PATH:=$ROOT/UDK}"
export UDK_PATH
TMP=$(mktemp -d "${TMPDIR:-/tmp}/drogabox-shots.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

swiftc -O DrogaBox/tools/render-screen.swift -o "$TMP/render"
( cd Utilities/TestPickerEditor && make clean >/dev/null 2>&1; make -j "$(getconf _NPROCESSORS_ONLN)" >/dev/null )

# Sample session (main menu, boot flags, config editor, config files).
( cd Utilities/TestPickerEditor && PICKER_TEST_SHOW=1 ./PickerEditor ../../Docs/Sample.plist ) > "$TMP/session.txt"

# Kexts screen, before and after F5.
OC="$TMP/oc"
mkdir -p "$OC/Kexts"
cp Docs/Sample.plist "$OC/config.plist"
mkkext() { # name, bundle id, dependency
  mkdir -p "$OC/Kexts/$1.kext/Contents/MacOS"
  : > "$OC/Kexts/$1.kext/Contents/MacOS/$1"
  local libs=""
  [ -n "$3" ] && libs="<key>$3</key><string>1.0.0</string>"
  cat > "$OC/Kexts/$1.kext/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict><key>CFBundleIdentifier</key><string>$2</string><key>CFBundleExecutable</key><string>$1</string><key>CFBundleVersion</key><string>1.0.0</string><key>OSBundleLibraries</key><dict>$libs<key>com.apple.kpi.libkern</key><string>8.0.0</string></dict></dict></plist>
EOF
}
mkkext Lilu as.vit9696.Lilu ""
mkkext VirtualSMC as.vit9696.VirtualSMC as.vit9696.Lilu
mkkext WhateverGreen as.vit9696.WhateverGreen as.vit9696.Lilu
mkkext AppleALC as.vit9696.AppleALC as.vit9696.Lilu
python3 -I - "$OC/config.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]
c = plistlib.load(open(p, 'rb'))
a = c['Kernel']['Add']
a[0], a[2] = a[2], a[0]
plistlib.dump(c, open(p, 'wb'))
PY
( cd Utilities/TestPickerEditor && PICKER_TEST_DIR="$OC" PICKER_TEST_KEXTS=1 ./PickerEditor ../../Docs/Sample.plist ) > "$TMP/kexts.txt"
( cd Utilities/TestPickerEditor && make clean >/dev/null 2>&1 )

# Pick the screens: the n-th screen with a given title (counted from 0).
python3 -I - "$TMP" <<'PY'
import re, sys
tmp = sys.argv[1]
session = re.split(r'=== waiting[^\n]*===\n', open(tmp + '/session.txt').read())[1:]
kexts = re.findall(r'----- screen -----\n(.*?)------------------', open(tmp + '/kexts.txt').read(), re.S)
def nth(title, n=0, skip_first=0):
    hits = [s for s in session if s.split('\n')[0].strip() == title]
    return hits[n]
want = {
    '01-options': nth('DrogaBoxBoot Options', 0),
    '02-boot-flags': nth('Boot flags', 0),
    '03-edit-config': nth('Edit config.plist', 14),
    '04-kexts-wrong-order': kexts[0],
    '05-kexts-after-f5': kexts[1],
    '06-config-files': nth('Config files', 0),
    '07-config-start': nth('Start OpenCore with config3.plist', 0),
}
for name, screen in want.items():
    lines = [l[:80] for l in screen.split('\n')[:25]]
    lines += [''] * (25 - len(lines))
    open('%s/%s.txt' % (tmp, name), 'w').write('\n'.join(lines))
PY
mkdir -p DrogaBox/screenshots
for f in "$TMP"/0*.txt; do
  "$TMP/render" "$f" "DrogaBox/screenshots/$(basename "$f" .txt).png"
done

if [ -n "${OC_RESOURCES:-}" ] && [ -f "$OC_RESOURCES/Font/Font_2x.bin" ]; then
  RES="$TMP/res"
  mkdir -p "$RES/Resources/Image/DrogaBox"
  cp -R "$OC_RESOURCES/Font" "$RES/Resources/"
  cp -R DrogaBox/Theme/AETHER "$RES/Resources/Image/DrogaBox/"
  ( cd Utilities/TestPickerEditor && make -j "$(getconf _NPROCESSORS_ONLN)" >/dev/null \
    && PICKER_TEST_SHOW=1 PICKER_TEST_CANVAS="$TMP/canvas.log" ./PickerEditor ../../Docs/Sample.plist >/dev/null )
  ( cd Utilities/TestPickerEditor && make clean >/dev/null 2>&1 )
  ( cd Utilities/TestCanopy && make clean >/dev/null 2>&1; make -j "$(getconf _NPROCESSORS_ONLN)" >/dev/null )
  ( cd Utilities/TestCanopy && ./Canopy "$RES" Auto "$TMP/picker" -n 6 -t 5 wait:120 snap end >/dev/null )
  ( cd Utilities/TestCanopy && ./Canopy "$RES" Auto "$TMP/sheet" -replay "$TMP/canvas.log" -pick 0,50,72 >/dev/null )
  ( cd Utilities/TestCanopy && make clean >/dev/null 2>&1 )
  python3 -I Utilities/TestCanopy/png.py "$TMP"/picker-0.bgra "$TMP"/sheet-*.bgra >/dev/null
  cp "$TMP/picker-0.png" DrogaBox/screenshots/picker.png
  cp "$TMP/sheet-0.png" DrogaBox/screenshots/sheet-menu.png
  cp "$TMP/sheet-1.png" DrogaBox/screenshots/sheet-config.png
  cp "$TMP/sheet-2.png" DrogaBox/screenshots/sheet-files.png
else
  echo "OC_RESOURCES not set: the picker and sheet pictures were not drawn"
fi

ls DrogaBox/screenshots

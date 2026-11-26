#!/bin/bash
#
# Apple's HFS+ driver in the zip. build_oc.tool sources this file and calls db_hfsplus_package.
#
# Docs/Sample.plist loads HfsPlus.efi, and OpenCore's docs recommend Apple's driver, but the
# official zip only carries OpenHfsPlus.efi, because Apple's binary can't be shipped in
# OpenCorePkg. On a Z790 board OpenHfsPlus.efi froze the machine before the picker, with the
# keyboard dead, and Apple's HfsPlus.efi fixed it (2026-11-25).
#
# So the zip gets HfsPlus.efi from OcBinaryData at a pinned commit, with its sha256 checked. The
# binary is never committed here. If it can't be had, the zip keeps only OpenHfsPlus.efi, its
# sample configs are switched to it so they still match the zip, and the build says so.
# OpenHfsPlus.efi stays in the zip either way.
#
#   HFSPLUS_DIR=/path/to/OcBinaryData/Drivers   take the files from there instead of downloading
#                                               (same sha256 check)
#   REQUIRE_HFSPLUS=1                           stop the build instead of falling back (releases)
#

DB_HFSPLUS_COMMIT=c2a98980d30e39a571a2843ab26aa8d2b9188094
DB_HFSPLUS_URL="https://raw.githubusercontent.com/acidanthera/OcBinaryData/${DB_HFSPLUS_COMMIT}/Drivers"

# Prints "<file in OcBinaryData/Drivers> <sha256>" for an arch.
db_hfsplus_source() {
  case "$1" in
    X64)  echo "HfsPlus.efi a55b5fff36578864ba6792c4c6369c71f6f35b61dd5a853ddf8583cd36c31d8f" ;;
    IA32) echo "HfsPlus32.efi 40badc5338e4cb373ed8370aba52bad9668f1970c68f546b9cba46ed55153d18" ;;
    *)    return 1 ;;
  esac
}

db_sha256() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | cut -d ' ' -f 1
  else
    sha256sum "$1" | cut -d ' ' -f 1
  fi
}

# db_hfsplus_fetch <tmpdir> <arch>...: puts <tmpdir>/<arch>.efi for every arch, checked.
db_hfsplus_fetch() {
  local tmp="$1"
  shift
  local arch src name sha got
  for arch in "$@"; do
    src=$(db_hfsplus_source "$arch") || {
      echo "HfsPlus.efi: OcBinaryData has no Apple driver for ${arch}"
      return 1
    }
    read -r name sha <<<"$src"
    if [ -n "${HFSPLUS_DIR}" ]; then
      cp "${HFSPLUS_DIR}/${name}" "${tmp}/${arch}.efi" || {
        echo "HfsPlus.efi: cannot read ${HFSPLUS_DIR}/${name}"
        return 1
      }
    else
      curl -LfsS --retry 2 --connect-timeout 20 -o "${tmp}/${arch}.efi" "${DB_HFSPLUS_URL}/${name}" || {
        echo "HfsPlus.efi: cannot download ${DB_HFSPLUS_URL}/${name}"
        return 1
      }
    fi
    got=$(db_sha256 "${tmp}/${arch}.efi")
    if [ "$got" != "$sha" ]; then
      echo "HfsPlus.efi: ${name} for ${arch} has sha256 ${got}, expected ${sha}"
      return 1
    fi
  done
  return 0
}

# db_hfsplus_package <package dir> <arch>...: call once the drivers and Docs are in the package dir.
db_hfsplus_package() {
  local dst="$1"
  shift
  local tmp arch plist n ok=1

  tmp=$(mktemp -d) || return 1
  if db_hfsplus_fetch "$tmp" "$@"; then
    for arch in "$@"; do
      cp "${tmp}/${arch}.efi" "${dst}/${arch}/EFI/OC/Drivers/HfsPlus.efi" || ok=0
    done
  else
    ok=0
  fi
  rm -rf "$tmp"

  if [ "$ok" = "1" ]; then
    echo "HfsPlus.efi: Apple's driver from OcBinaryData ${DB_HFSPLUS_COMMIT:0:12} (sha256 checked) is in the zip;" \
      "OpenHfsPlus.efi stays as the fallback"
    return 0
  fi

  for arch in "$@"; do
    rm -f "${dst}/${arch}/EFI/OC/Drivers/HfsPlus.efi"
  done

  if [ "${REQUIRE_HFSPLUS}" = "1" ]; then
    echo "ERROR: Apple's HfsPlus.efi is not available and REQUIRE_HFSPLUS=1"
    return 1
  fi

  for plist in "${dst}/Docs/Sample.plist" "${dst}/Docs/SampleCustom.plist"; do
    n=$(grep -c '<string>HfsPlus.efi</string>' "$plist")
    if [ "$n" != "1" ]; then
      echo "ERROR: expected one HfsPlus.efi driver entry in ${plist##*/}, found ${n}"
      return 1
    fi
    sed 's|<string>HfsPlus.efi</string>|<string>OpenHfsPlus.efi</string>|' "$plist" > "${plist}.tmp" || return 1
    mv "${plist}.tmp" "$plist" || return 1
  done

  echo "WARNING: FALLING BACK TO OpenHfsPlus.efi. Apple's HfsPlus.efi is NOT in this zip, and the zip's"
  echo "WARNING: Docs/Sample.plist and SampleCustom.plist now load OpenHfsPlus.efi instead. OpenHfsPlus.efi"
  echo "WARNING: froze at least one machine before the picker. Rebuild with network access, or with"
  echo "WARNING: HFSPLUS_DIR=/path/to/OcBinaryData/Drivers, before handing this zip to anyone."
  return 0
}

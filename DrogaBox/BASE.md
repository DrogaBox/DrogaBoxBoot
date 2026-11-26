# Base pinned by this branch

`main` is the official OpenCore plus our own commits on top.
`DrogaBox/update-base.sh` rebases from the `official_commit` line below. Keep the format: one
`key: value` per line, nothing else on those lines.

```
official_repo:   https://github.com/acidanthera/OpenCorePkg.git
official_commit: ebea824880706ac9faa8ee45604245de62be658b
official_date:   2026-11-02
official_title:  OcConsoleLib: Downgrade Eg2Info revision in DirectGop
official_release_before: 1.0.8
fork_repo:       https://github.com/OverlordDev/OpenCorePkg.git
fork_tag:        v1.0.9-picker-editor.2
fork_commits:    e103f25b (2026-10-24), d11999a3 (2026-10-26), author Illia Haidai
fork_based_on:   78d5ff824992d5b227f842e34d8221eb8f31baf1 (2026-10-23)
pinned_on:       2026-11-24
```

## What is on top of the official commit

1. The fork's two commits, unchanged and with the author kept: the `F2` options editor, the
   `config*.plist` chooser, the kexts screen, and the host tests `Utilities/TestPickerEditor` and
   `Utilities/TestEditPlist`. They were cherry-picked, so their hashes differ from the fork's. The only
   conflict was `Changelog.md`, resolved by keeping both sides.
2. Our commits after that: this folder (`DrogaBox/`), `.github/README.md`, the 1.5x picker, the editor
   sheet and the help line in OpenCanopy, `Utilities/TestCanopy`, and whatever follows.

## What is in this folder

| File | What it is |
|---|---|
| `BASE.md` | This file: the pinned official commit |
| `update-base.sh` | Moves `main` onto a newer official commit (see below) |
| `run-tests.sh` | Builds and runs the editor tests and the OpenCanopy host tests, plain and with `SANITIZE=1` |
| `make-screenshots.sh` | Regenerates `screenshots/` from the real code: the text mode editor, and with `OC_RESOURCES` the OpenCanopy picker and the editor sheet |
| `EDITOR.md` | The user guide of the `F2` editor |
| `screenshots/` | Images for the README and the guide |
| `Theme/AETHER` | The default theme, packaged into the zip (see `Theme/README.md`) |
| `hfsplus.sh` | Puts Apple's `HfsPlus.efi` in the zip, from OcBinaryData at a fixed commit with its sha256 checked; falls back to `OpenHfsPlus.efi` |
| `tools/` | `render-screen.swift` (text console to PNG) and `banner.swift` (README banner) |

## Moving to a newer official commit

```
DrogaBox/update-base.sh            # onto official master
DrogaBox/update-base.sh <commit>   # onto a given official commit or tag
DrogaBox/update-base.sh --no-build # skip the OpenCore build (tests still run)
```

1. It fetches `upstream` (acidanthera) and makes a separate worktree, `../drogabox-next`, at the target.
2. It replays every commit between the pin and `main`, keeping the authors. A conflict in `Changelog.md`
   only is resolved by keeping both sides; any other conflict stops the script.
3. It runs `run-tests.sh` and builds RELEASE and DEBUG.
4. Only if all of that passed, it commits the new pin, saves the old tip as the tag
   `before-base-<date>-<short>`, moves `main`, and copies the zips with their `SHA256SUMS` to `../builds/<tip>/`.

If anything fails, `main` is untouched and the attempt stays in `../drogabox-next` for you to look at. The
next run removes it. The script never writes to an EFI.

Needs the remote `upstream` (`git remote add upstream https://github.com/acidanthera/OpenCorePkg.git`) and,
for the build, `nasm` and `mtoc` in `PATH`.

## Tests and build

```
DrogaBox/run-tests.sh              # TestPickerEditor (1669 checks) and TestEditPlist (84455), plain and SANITIZE=1
ARCHS=X64 TARGETS=RELEASE,DEBUG TOOLCHAINS=XCODE5 ./build_oc.tool
```

`build_oc.tool` downloads the UDK into `UDK/`, which git ignores.

It also downloads Apple's `HfsPlus.efi` (X64) and `HfsPlus32.efi` (packaged as `HfsPlus.efi` for IA32)
from OcBinaryData at `DB_HFSPLUS_COMMIT` in `hfsplus.sh` and checks their sha256. Offline, set
`HFSPLUS_DIR=/path/to/OcBinaryData/Drivers`. Without either, the zip keeps only `OpenHfsPlus.efi`, its
`Docs/Sample*.plist` are switched to it, and the build prints `FALLING BACK TO OpenHfsPlus.efi`. Build
releases with `REQUIRE_HFSPLUS=1`, which stops instead. To move to a newer OcBinaryData, change the commit
and both hashes together.

## Versions

Our version is the OpenCore base plus our number, for example `1.0.9-db.1`. It goes in tags and release
names. `OPEN_CORE_VERSION` and the `opencore-version` NVRAM string keep OpenCore's own format, because
other tools read it.

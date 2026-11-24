<p align="center">
  <img src="../DrogaBox/screenshots/banner.png" alt="DrogaBoxBoot" width="100%">
</p>

My OpenCore fork. I wanted two things stock OpenCore doesn't do:

- Fix the config from the boot picker. Press `F2` and you can add `-v`, turn off the kext you installed
  last night, go back to a config that worked, or read the last kernel panic, all without a USB stick
  and a second machine.
- A picker that doesn't look tiny on a 1080p monitor.

Everything else is plain [acidanthera/OpenCorePkg](https://github.com/acidanthera/OpenCorePkg). My changes
are separate commits on top, and a script moves them to each new OpenCore release.

> [!NOTE]
> Zips are in [Releases](../../releases). So far it has booted on one machine, mine (Ryzen 9 5900XT,
> RX 6800 XT, 1080p): try it from a USB stick first.

![The picker](../DrogaBox/screenshots/picker.png)

## The picker

Stock OpenCanopy draws 1080p at 1x, so the icons are small and the text is 10 px tall. This one draws the
2x artwork and scales it down to 1.5x on output, so icons and names come out half again as big and the
names are readable. Under the entries there's a countdown and the keys, and Restart and Shut Down have
labels.

It kicks in with any theme that has 2x images, on screens at least 1000 px tall and under 4 megapixels
whose width and height divide by 3: 1920×1080 and 1920×1200, for example. Smaller screens stay at 1x,
and 4K keeps OpenCore's own 2x. To keep 1x, set bit `0x0400` in `PickerAttributes`.

The zip comes with my theme, AETHER. `PickerVariant` `Auto` (the default) picks it, unless the firmware's
background colour is light gray, which gets Chardonnay as before. The icons are the GoldenGate ones from
[OcBinaryData](https://github.com/acidanthera/OcBinaryData); the background is mine.

## The F2 editor

In OpenCanopy it opens on a sheet over the picker:

| | |
|---|---|
| ![Main menu](../DrogaBox/screenshots/sheet-menu.png) | ![config.plist](../DrogaBox/screenshots/sheet-config.png) |

With the text picker it uses the console, like the rest of OpenCore. What it can do:

- Boot flags (`-v`, `-x`, `keepsyms=1`, `debug=0x100`…) for one boot, kept in NVRAM, or saved to the
  config.
- Kexts in load order. Turn them on and off, move them with `+` and `-`. It reads each kext's
  `OSBundleLibraries` and puts a `!` on any kext loaded before something it needs. `F5` fixes the order.
- The whole `config.plist` as a tree. Saving keeps your formatting and comments, and the old file stays as
  `config.plist.bak`.
- Several configs side by side (`config2.plist`, `config-test.plist`…). Pick one at boot, for good or for a
  single boot; after a single boot it goes back to `config.plist` on its own.
- Drivers, ACPI, tools and patches on and off, quirks, a config check, the OpenCore log, the kernel panic
  macOS left in NVRAM, comparing two configs, and undoing the last save.

If you set an OpenCore password, the editor asks for it. Every screen is in
[DrogaBox/EDITOR.md](../DrogaBox/EDITOR.md).

<details>
<summary>Text mode screenshots</summary>

![Options](../DrogaBox/screenshots/01-options.png)
![Boot flags](../DrogaBox/screenshots/02-boot-flags.png)
![Kexts in the wrong order](../DrogaBox/screenshots/04-kexts-wrong-order.png)
![Kexts after F5](../DrogaBox/screenshots/05-kexts-after-f5.png)
![config.plist](../DrogaBox/screenshots/03-edit-config.png)
![Config files](../DrogaBox/screenshots/06-config-files.png)
![Use a config once](../DrogaBox/screenshots/07-config-start.png)

</details>

All of these screenshots come from the real code. `Utilities/TestCanopy` and `Utilities/TestPickerEditor`
run it on a Mac with a fake screen and scripted keys, and `DrogaBox/make-screenshots.sh` saves the
pictures.

## What's changed from OpenCore

Every change is a commit on top of the official base (`git log ebea824..main`). By area:

**OpenCanopy (`Platform/OpenCanopy`)**
- `Output/OutputStGop.c`: a zoomed output. The picker draws at scale 2 into a virtual screen 4/3 the size of
  the real one, and each 4×4 block is averaged down to 3×3 as it goes to the screen. It only kicks in when
  the screen is at least 1000 px tall, under 4 megapixels, and divisible by 3 both ways.
- `GuiApp.c`: picks 1x, 2x or zoomed 2x at start. It falls back to 1x if the theme has no 2x images or
  bit `0x0400` is set, and scales the background by 4/3 for the zoom (`GuiCreateScaledImage` in
  `Images.c`). `PickerVariant` `Auto` prefers `DrogaBox\AETHER` when it is installed.
- `Views/BootPicker.c`: the help line under the entries (countdown, Enter, Ctrl+Enter only if
  setting a default is allowed, F2 only if the editor is on) and the "DrogaBoxBoot, made by DrogaBox"
  line at the bottom left. `OpenCanopy.c` keeps the elapsed seconds for the countdown.
- `Views/Common.c`: names under the Restart and Shut Down buttons.
- `Views/EditorCanvas.c` (new): the sheet the F2 editor draws on. 80 columns, the theme's font, selected
  row in blue, keys drawn as caps, the picker dimmed behind it. `OcBootstrap.c` hands it to OpenCore.
- `Blending.c`, `OpenCanopy.c`: `PIXEL_TO_UINT32` casts each byte before shifting. The old macro shifted
  a signed int into the sign bit, which UBSan flags.

**OpenCore (`Library/OcMainLib`, `Include`)**
- The F2 editor, config chooser and kexts screen (`OpenCoreEditor*.c`, `OpenCoreConfigSelect.c`,
  `OpenCoreKextOrder.c`, `OpenCoreEditPlist.c`, `OpenCoreNvram.c`) come from Illia Haidai's two
  commits. On top of them I changed the title and the drawing layer below, and split the editor into
  one file per screen without changing its code.
- `OpenCoreEditorUi.c`: the editor draws through a small interface (`OC_EDITOR_CANVAS` in
  `OcBootManagementLib.h`: begin, end, clear, put text with a role, flush). It uses OpenCanopy's sheet
  when there is one and the text console otherwise.
- `OcBootManagementLib.h`: `OC_ATTR_NO_ZOOM` (`0x0400`) in `PickerAttributes`, and the `EditorCanvas`
  field in the picker context.
- `OcXmlLib.c` (also from Illia's commits): source ranges of parsed nodes, so a save can rewrite only
  the values that changed and keep your formatting.

**Theme, docs, packaging**
- `DrogaBox/Theme/AETHER`: GoldenGate icons plus my background. `build_oc.tool` puts it in the zip under
  `Resources/Image/DrogaBox/AETHER`.
- `Docs/Configuration.tex`: `AllowPickerEdit`, the editor, bit `0x0400` and AETHER with `Auto`.

**Tests and tools**
- `Utilities/TestCanopy` (new): runs the real OpenCanopy on a Mac with a fake 1920×1080 screen and
  scripted keys and dumps the frames. It can also replay what the editor drew and draw it on the sheet.
- `Utilities/TestPickerEditor` and `Utilities/TestEditPlist`: 1669 checks for the editor (also at 80x25, 100x30, 128x40 and 240x67 consoles, with very long names and broken configs) and 84455 for
  the plist writer. They run plain and with ASan/UBSan through `DrogaBox/run-tests.sh`.
- `DrogaBox/update-base.sh`: moves the fork to a newer OpenCore (see below).
- [`DrogaBox/WRITES.md`](../DrogaBox/WRITES.md): what each save writes to the disk, and what a power cut
  at each step would leave.

## Install

This is your boot loader. A bad one means a machine that doesn't start, so don't touch the EFI you boot
from until the new one has started your Mac from a USB stick.

It's built on OpenCore 1.0.8 plus the official commits after it, up to `ebea824`. The config format
hasn't changed since 1.0.8. The zip and `ocvalidate` say 1.0.9 because official master already carries
that number. If you're on 1.0.8, only the files below change. If you're older, update to 1.0.8 first the
usual way.

1. Get the zip from [Releases](../../releases), or [build it](#build).
2. Copy your whole `EFI` folder to a FAT32 USB stick. Do the next steps on the stick, not on the disk.
3. From the zip's `X64/EFI`, replace `OC/OpenCore.efi`, `OC/Drivers/OpenCanopy.efi` and
   `BOOT/BOOTx64.efi`. All three have to come from the same build. For the theme, also copy
   `OC/Resources/Image/DrogaBox` and set `PickerMode` to `External`. The zip has no font, same as stock
   OpenCore: OpenCanopy needs `Resources/Font` from [OcBinaryData](https://github.com/acidanthera/OcBinaryData),
   so copy it if you don't have it yet.
4. In `config.plist`, under `Misc > Security`, add:
   ```xml
   <key>AllowPickerEdit</key>
   <true/>
   ```
5. Run `Utilities/ocvalidate` from the same zip on that config. Stock `ocvalidate` doesn't know
   `AllowPickerEdit` and reports it.
6. Start from the stick (the firmware's boot menu) and press `F2` at the picker.
7. Only once it starts macOS fine from the stick, keep a copy of your current `EFI` folder somewhere off
   that partition and copy the stick's `EFI` over. Keep the stick: it's your way back.

To undo, put your old `OpenCore.efi`, `OpenCanopy.efi` and `BOOTx64.efi` back, or set `AllowPickerEdit` to
`false`. Stock OpenCore skips that key.

With a vault (`vault.plist`, as `Vault` `Basic` and `Secure` use), the editor opens but can't change the
config, since it would no longer match the vault, and only `config.plist` can be used.

## Build

On macOS with the Xcode command line tools, plus `nasm` and `mtoc` in `PATH`:

```bash
git clone https://github.com/DrogaBox/DrogaBoxBoot.git
cd DrogaBoxBoot
ARCHS=X64 TARGETS=RELEASE,DEBUG TOOLCHAINS=XCODE5 ./build_oc.tool
```

The zips end up in `Binaries/`. The first run downloads the UDK into `UDK/`.

Tests (the editor, the plist writer, and the picker and sheet drawn on the host, plain and with
ASan/UBSan):

```bash
OC_RESOURCES=/path/to/OcBinaryData/Resources DrogaBox/run-tests.sh
```

## Keeping up with OpenCore

The OpenCore commit this is built on is in [`DrogaBox/BASE.md`](../DrogaBox/BASE.md) (`ebea824` right now,
1.0.8 plus 21 official commits). `DrogaBox/update-base.sh` moves to the latest master, and
`DrogaBox/update-base.sh <tag>` moves to an official tag, such as 1.0.9 once it's out. It replays my commits in a separate worktree, runs the
tests and builds. `main` only moves if all of that passes; on a conflict it stops and tells you the file.

Versions look like `1.0.9-db.1`. The `opencore-version` in NVRAM keeps OpenCore's own format
(`REL-109-…`), so Hackintool, OCLP and the like still see a normal OpenCore.

## Credits

- [Acidanthera](https://github.com/acidanthera/OpenCorePkg) for OpenCore (BSD-3-Clause, see
  [`LICENSE.txt`](../LICENSE.txt)). Please don't send them issues about this fork.
- Illia Haidai ([OverlordDev/OpenCorePkg](https://github.com/OverlordDev/OpenCorePkg)) wrote the original
  options editor, config chooser and kexts screen. The first two commits are his, under his name.
- The rest (1.5x picker, editor sheet, AETHER, test harnesses, packaging) is mine. AETHER's icons are from
  the GoldenGate theme in OcBinaryData.

<p align="center"><sub>made by DrogaBox</sub></p>

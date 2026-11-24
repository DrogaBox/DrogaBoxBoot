# What the editor writes, and what a power cut would leave

Checked 2026-11-24. OpenCore starts with `config.plist.bak` when `config.plist` cannot be loaded. Read
from the code, tested on the Mac and in QEMU (below).

## Choosing a config (`OcMiscSetConfigChoice`, `OpenCoreConfigSelect.c`)

It deletes `config.choice` and writes a new one: the name on the first line, `keep` or `once` on the
second.

- Cut after the delete: no choice, the next start uses `config.plist`. Safe.
- Cut halfway through the write: the name is cut short or empty. `ConfigReadChoice` only takes a name
  that passes `OcMiscIsConfigName`, so a cut name is dropped (and the file deleted) and `config.plist` is
  used. A cut `keep` reads as a choice for one start. Safe.
- A choice that points to a config that later breaks: `OcMiscLoadConfig` falls back to `config.plist`
  and the menu says so. Safe.

## Saving `config.plist` (`EdWriteConfig`, `OpenCoreEditor.c`)

Used by every save in the editor (config tree, boot flags saved to the config, kexts and the other lists,
quirks) and by putting the backup back. The steps:

1. Delete `config.plist.bak`, write the file being replaced into it, read it back and compare.
   If that fails, stop: `config.plist` has not been touched.
2. Delete `config.plist`, write the new one, read it back and compare. If that fails, write the old
   one back.

Before step 1 the new text has already been parsed, and it is refused if it has more problems than the
file it replaces.

- Cut during step 1: `config.plist` is untouched. Safe; `config.plist.bak` may be missing or cut.
- Cut during step 2, between the delete and the end of the write: `config.plist` is missing or cut.
  At the next start `OcMiscLoadConfig` cannot load it and loads `config.plist.bak` instead, which
  step 1 had written and checked. The editor's main menu and the Config files screen say so, and
  "Undo the last save" puts the backup back as `config.plist`. Safe.
- The read-back in steps 1 and 2 may come from the firmware's cache rather than the disk, so it shows
  that the write was accepted, not that it reached the disk.

## Starting with the backup (`OcMiscLoadConfig`)

Only when `config.plist` itself cannot be loaded (missing, not a plist, cut short), after a chosen
config has failed or when there is none. Never with a vault: the vault only signs `config.plist`. The
name in use stays `config.plist`, the file to put right, and `OcMiscGetConfigSource` returns
`OcConfigSourceBackup`.

While OpenCore runs from the backup, the editor never writes `config.plist.bak`: it is the only
config known to work. Every save then writes `config.plist` alone, so a cut leaves the backup as it
was, and the next start uses it again.

## How it is tested

`TestPickerEditor` has a fake file system that can cut the power after any number of writes and
deletes: the next write leaves half the file, and nothing after it is written. `TestPowerCut` does it
at every step of a save (with an old backup and with none) and of putting the backup back (with
`config.plist` missing and broken), and each time loads the config the way OpenCore does at start. It
always gets a config, the old one or the new one. With the fallback at start taken out, 21 checks
fail; with the editor allowed to write the backup while running from it, 4 fail.

In QEMU (OVMF, TCG) with the real OpenCore.efi, RELEASE and DEBUG, `Target` 67 and `HaltLevel`
0x80000000: a `config.plist` cut in half or missing, with a good backup, starts with the backup and
the editor says so; without a backup it stops, saying that both failed; with a vault the backup is
not used; "Undo the last save" writes `config.plist` back through the firmware's FAT driver and the
next start loads it. A chosen config that cannot be loaded falls back to `config.plist`, with and
without `keep`.

Messages from before the log is set up are logged again once it is, with the `HaltLevel` of the
config loaded in the end. So a failed file that is then replaced by another one is logged as a
warning, not an error: as an error, the usual `HaltLevel` stopped OpenCore right after it had
started with the backup or the default.

None of it has been tried with a real power cut.

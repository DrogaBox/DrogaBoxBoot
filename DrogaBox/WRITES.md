# What the editor writes, and what a power cut would leave

Checked 2026-11-24 on the branch `d7-config-backup`, which starts with `config.plist.bak` when `config.plist`
cannot be loaded. Read from the code and tested on the Mac (below).

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

None of it has been tried with a real power cut.

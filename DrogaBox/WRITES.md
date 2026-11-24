# What the editor writes, and what a power cut would leave

Checked 2026-11-24 against `main` (after the split into one file per screen). Read from the code; none
of it has been tried with a real power cut.

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
- **Cut during step 2, between the delete and the end of the write: `config.plist` is missing or cut.**
  At the next start `OcMiscLoadConfig` fails and OpenCore stops in `CpuDeadLoop`
  (`OpenCoreMisc.c`, after `OcMiscLoadConfig`). The machine hangs before the picker. The previous file
  is whole in `config.plist.bak`, but only another boot disk (the USB stick) can put it back.
- The read-back in steps 1 and 2 may come from the firmware's cache rather than the disk, so it shows
  that the write was accepted, not that it reached the disk.

## Proposal (not done: it needs the owner's yes)

Two parts, either one helps:

1. **At boot**, in `OcMiscLoadConfig`: if `config.plist` cannot be loaded, try `config.plist.bak` before
   stopping, and show "started with the backup" in the editor's main menu, the way a failed chosen
   config is shown today. This closes the gap completely, but it changes the code that runs on every
   start, which stage 2 said it would leave alone.
2. **In the editor**: write the new file as `config.plist.new`, check it, then delete `config.plist` and
   rename `config.plist.new` to `config.plist` (`SetInfo` with the new name). The window shrinks from a
   whole write to one rename. If the firmware's FAT driver refuses the rename, it falls back to the
   current steps. It cannot close the window on its own: FAT cannot rename over an existing file.

Both can be tested on the Mac by making the harness's fake file system stop after the n-th write, for
every n, and checking that the next start finds a config it can load.

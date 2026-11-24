# Base pinned by this branch

This branch (`deluxe`) is the official OpenCore plus our own commits on top.
The line `official_commit` below is what `Deluxe/update-base.sh` rebases from, so keep that
format: one `key: value` per line, nothing else on those lines.

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

1. The fork's two commits, as they were written (author kept): the `F2` options editor, the
   `config*.plist` chooser, the kexts screen, the host tests `Utilities/TestPickerEditor` and
   `Utilities/TestEditPlist`.
   Cherry-picked, so their hashes differ from the fork's; the only conflict was `Changelog.md`
   (both sides kept).
2. Our commits after that (this folder, `Deluxe/`, and whatever follows).

## Moving to a newer official commit

```
Deluxe/update-base.sh            # onto official master
Deluxe/update-base.sh <commit>   # onto a given official commit
```

It works in a separate worktree and moves `deluxe` only if everything passed.
See the comments at the top of the script.

## Tests and build

```
Deluxe/run-tests.sh              # editor tests (TestPickerEditor, TestEditPlist), normal and SANITIZE=1
```

Full build (needs `nasm` and `mtoc` in `PATH`): see `Deluxe/update-base.sh` for the exact call.
The UDK (`UDK/`) is fetched by `build_oc.tool` into this folder and is ignored by git.

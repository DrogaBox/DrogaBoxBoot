#!/bin/bash
#
# Moves our branch (`deluxe`) onto a newer official OpenCore commit.
#
#   Deluxe/update-base.sh [<official commit or ref>] [--no-build] [--keep]
#
# Default target: upstream/master (the official acidanthera/OpenCorePkg).
#
# What it does, in order:
#   1. fetches the official repository (remote `upstream`);
#   2. makes a SEPARATE worktree (../deluxe-next) on a new branch `deluxe-next` at the target;
#   3. replays our commits (everything between the pinned official commit in Deluxe/BASE.md and
#      `deluxe`) on it, one by one, keeping the original authors. A conflict in Changelog.md only
#      is resolved by keeping both sides; any other conflict stops the script;
#   4. runs the editor tests (normal and SANITIZE=1) and builds OpenCore RELEASE and DEBUG
#      (skip the build with --no-build);
#   5. only if all of that passed: writes the new pin to Deluxe/BASE.md, commits it, saves the
#      old tip of `deluxe` as the tag `deluxe-before-<date>-<short>` and moves `deluxe` to the new one.
#
# The working branch is not touched until step 5, so a failure never leaves it half rebased:
# the whole attempt lives in ../deluxe-next, which is left in place for inspection (use
# `git worktree remove --force ../deluxe-next; git branch -D deluxe-next` to throw it away; the next
# run does it by itself).
#
# Never writes to an EFI. Needs nasm and mtoc in PATH for the build.
#
set -u

die() { echo; echo "STOPPED: $*" >&2; echo "The branch 'deluxe' was not changed." >&2; exit 1; }

TARGET=upstream/master
BUILD=1
while [ $# -gt 0 ]; do
  case "$1" in
    --no-build) BUILD=0 ;;
    -*) die "unknown option $1" ;;
    *) TARGET=$1 ;;
  esac
  shift
done

cd "$(dirname "$0")/.." || exit 1
MAIN=$(pwd)
WT="$(dirname "$MAIN")/deluxe-next"
WORK=deluxe
NEXT=deluxe-next

[ "$(git rev-parse --abbrev-ref HEAD)" = "$WORK" ] || die "check out '$WORK' first (now on $(git rev-parse --abbrev-ref HEAD))"
[ -z "$(git status --porcelain --untracked-files=no)" ] || die "uncommitted changes in $MAIN; commit or stash them first"
git remote get-url upstream >/dev/null 2>&1 || die "no remote 'upstream' (git remote add upstream https://github.com/acidanthera/OpenCorePkg.git)"

OLD_BASE=$(sed -n 's/^official_commit: *\([0-9a-f]\{40\}\).*/\1/p' Deluxe/BASE.md | head -1)
[ -n "$OLD_BASE" ] || die "official_commit not found in Deluxe/BASE.md"
git merge-base --is-ancestor "$OLD_BASE" "$WORK" || die "the pinned commit $OLD_BASE is not an ancestor of $WORK"

echo "== fetching the official repository"
git fetch upstream --tags || die "fetch failed"
NEW_BASE=$(git rev-parse --verify "$TARGET^{commit}" 2>/dev/null) || die "unknown target '$TARGET'"
if [ "$NEW_BASE" = "$OLD_BASE" ]; then echo "Already on $NEW_BASE, nothing to do."; exit 0; fi
git merge-base --is-ancestor "$OLD_BASE" "$NEW_BASE" || die "$NEW_BASE does not descend from the pinned $OLD_BASE (official history was rewritten?)"

COMMITS=$(git rev-list --reverse --topo-order "$OLD_BASE..$WORK")
N=$(echo "$COMMITS" | grep -c .)
echo "== $(git rev-list --count "$OLD_BASE..$NEW_BASE") new official commits since $(git rev-parse --short "$OLD_BASE"); replaying our $N commits onto $(git rev-parse --short "$NEW_BASE")"
git log --oneline "$OLD_BASE..$NEW_BASE" | head -40

# Fresh worktree.
if [ -e "$WT" ]; then git worktree remove --force "$WT" 2>/dev/null || rm -rf "$WT"; git worktree prune; fi
git branch -D "$NEXT" >/dev/null 2>&1
git worktree add -q -b "$NEXT" "$WT" "$NEW_BASE" || die "cannot create the worktree $WT"
cd "$WT" || exit 1

resolve_changelog_only() {
  local conflicted
  conflicted=$(git diff --name-only --diff-filter=U)
  [ "$conflicted" = "Changelog.md" ] || return 1
  python3 -I - <<'PY' || return 1
import re, sys
s = open('Changelog.md').read()
s2, n = re.subn(r'<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n', lambda m: m.group(1) + m.group(2), s, flags=re.S)
if n == 0 or re.search(r'^(<<<<<<<|=======$|>>>>>>>)', s2, flags=re.M):
    sys.exit(1)
open('Changelog.md', 'w').write(s2)
PY
  git add Changelog.md && GIT_EDITOR=true git cherry-pick --continue >/dev/null
}

echo "== replaying"
for c in $COMMITS; do
  echo "   $(git log -1 --format='%h %an: %s' "$c")"
  if ! git cherry-pick --allow-empty "$c" >/dev/null 2>&1; then
    if resolve_changelog_only; then
      echo "     (Changelog.md conflict resolved keeping both sides)"
    else
      echo; echo "CONFLICT replaying $(git log -1 --format='%h %s' "$c") in:"; git diff --name-only --diff-filter=U | sed 's/^/   /'
      git cherry-pick --abort >/dev/null 2>&1
      die "resolve it by hand in $WT (git cherry-pick $c), then run the tests; or wait for a newer official commit"
    fi
  fi
done

echo "== editor tests"
UDK_PATH="$MAIN/UDK" Deluxe/run-tests.sh || die "the editor tests fail on the new base (worktree kept in $WT)"

if [ "$BUILD" = 1 ]; then
  echo "== building OpenCore (RELEASE + DEBUG)"
  command -v nasm >/dev/null && command -v mtoc >/dev/null || die "nasm and mtoc must be in PATH"
  # The build wants its own UDK inside the checkout; clone the one of the main checkout (APFS clone, cheap).
  [ -f "$MAIN/UDK/UDK.ready" ] || die "$MAIN/UDK is not ready; do a build there once first"
  cp -cR "$MAIN/UDK" "$WT/UDK" || die "cannot copy the UDK"
  ( cd "$WT" && ARCHS=X64 TARGETS=RELEASE,DEBUG TOOLCHAINS=XCODE5 ./build_oc.tool ) >"$WT/build.log" 2>&1 \
    || { tail -30 "$WT/build.log"; die "the OpenCore build fails on the new base (log: $WT/build.log)"; }
  ls Binaries/*.zip >/dev/null 2>&1 || die "no zip produced"
  shasum -a 256 Binaries/*.zip
fi

echo "== all passed; pinning the new base"
NEW_DATE=$(git log -1 --format=%cs "$NEW_BASE")
NEW_TITLE=$(git log -1 --format=%s "$NEW_BASE")
python3 -I - "$NEW_BASE" "$NEW_DATE" "$NEW_TITLE" "$(date +%Y-%m-%d)" <<'PY' || die "cannot rewrite Deluxe/BASE.md"
import re, sys
base, date, title, today = sys.argv[1:5]
s = open('Deluxe/BASE.md').read()
s = re.sub(r'^official_commit: .*$', 'official_commit: ' + base, s, flags=re.M)
s = re.sub(r'^official_date: .*$', 'official_date:   ' + date, s, flags=re.M)
s = re.sub(r'^official_title: .*$', 'official_title:  ' + title, s, flags=re.M)
s = re.sub(r'^pinned_on: .*$', 'pinned_on:       ' + today, s, flags=re.M)
open('Deluxe/BASE.md', 'w').write(s)
PY
git add Deluxe/BASE.md && git commit -q -m "Pin official base $(git rev-parse --short "$NEW_BASE")" || die "cannot commit the new pin"
NEW_TIP=$(git rev-parse HEAD)

cd "$MAIN" || exit 1
OLD_TIP=$(git rev-parse "$WORK")
TAG="deluxe-before-$(date +%Y%m%d-%H%M%S)-$(git rev-parse --short "$OLD_BASE")"
git tag "$TAG" "$OLD_TIP" || die "cannot save the old tip"
git reset -q --hard "$NEW_TIP" || die "cannot move $WORK (old tip saved as $TAG)"
if [ "$BUILD" = 1 ]; then
  # Keep the zips of the verified build next to the checkout (they are of the commit before the pin commit,
  # which only changes Deluxe/BASE.md).
  DEST="$(dirname "$MAIN")/builds/$(git rev-parse --short "$NEW_TIP")"
  mkdir -p "$DEST" && cp "$WT"/Binaries/*.zip "$DEST"/ && ( cd "$DEST" && shasum -a 256 ./*.zip > SHA256SUMS )
fi
git worktree remove --force "$WT" >/dev/null 2>&1; git branch -D "$NEXT" >/dev/null 2>&1
echo
echo "Done. '$WORK' is now on official $(git rev-parse --short "$NEW_BASE") ($NEW_DATE). Old tip: tag $TAG"
[ "$BUILD" = 1 ] && echo "Built zips: $DEST (nothing was copied to any EFI)"
exit 0

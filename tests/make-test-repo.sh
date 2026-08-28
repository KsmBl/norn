#!/usr/bin/env bash
# Builds a scratch repository exercising every state norn has to render:
# staged, unstaged, untracked, renamed, deleted, mode change, awkward filenames,
# a stash, a submodule, a linked worktree and an unpushed branch.
#
# Usage: make-test-repo.sh <directory>
#
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

DIR="${1:?usage: make-test-repo.sh <directory>}"
rm -rf "$DIR"
mkdir -p "$DIR"

export GIT_CONFIG_GLOBAL=/dev/null
export GIT_CONFIG_SYSTEM=/dev/null
git_q() { git -C "$1" -c user.name=Test -c user.email=test@example.invalid "${@:2}"; }

git init -q -b main --bare "$DIR/remote.git"

git init -q -b main "$DIR/submodule-src"
echo "lib" > "$DIR/submodule-src/lib.txt"
git_q "$DIR/submodule-src" add -A
git_q "$DIR/submodule-src" commit -qm "Add the library"

REPO="$DIR/repo"
git init -q -b main "$REPO"
git_q "$REPO" remote add origin "$DIR/remote.git"

mkdir -p "$REPO/src"
echo "one"   > "$REPO/src/one.txt"
echo "two"   > "$REPO/src/two.txt"
echo "old"   > "$REPO/src/renamed-away.txt"
echo "gone"  > "$REPO/src/deleted.txt"
echo "exec"  > "$REPO/src/mode.sh"
printf 'a\nb\nc\nd\ne\n' > "$REPO/src/conflict.txt"
echo "space" > "$REPO/a file with spaces.txt"
echo "quote" > "$REPO/weird[1].txt"
printf 'unicode\n' > "$REPO/ümlaut-äöü.txt"
git_q "$REPO" add -A
git_q "$REPO" commit -qm "Add the initial tree"
git_q "$REPO" push -q -u origin main

echo "more" >> "$REPO/src/one.txt"
git_q "$REPO" commit -qam "Extend one.txt"

git_q "$REPO" checkout -q -b feature
printf 'a\nB-from-feature\nc\nd\ne\n' > "$REPO/src/conflict.txt"
git_q "$REPO" commit -qam "Change the middle line on feature"

git_q "$REPO" checkout -q main
printf 'a\nB-from-main\nc\nd\ne\n' > "$REPO/src/conflict.txt"
git_q "$REPO" commit -qam "Change the same line on main"

echo "unpushed" > "$REPO/src/unpushed.txt"
git_q "$REPO" add -A
git_q "$REPO" commit -qm "Add an unpushed file"

echo "stashed" >> "$REPO/src/two.txt"
git_q "$REPO" stash push -q -m "work in progress"

git_q "$REPO" -c protocol.file.allow=always submodule -q add "$DIR/submodule-src" vendor/lib
git_q "$REPO" commit -qm "Add the vendored library as a submodule"

git_q "$REPO" worktree add -q --detach "$DIR/worktree" HEAD

# Dirty the working tree in every way at once.
git_q "$REPO" mv src/renamed-away.txt src/renamed-to.txt
git_q "$REPO" rm -q src/deleted.txt
echo "staged change"   >> "$REPO/src/one.txt"
git_q "$REPO" add src/one.txt
echo "then unstaged"   >> "$REPO/src/one.txt"
echo "unstaged only"   >> "$REPO/src/two.txt"
chmod +x "$REPO/src/mode.sh"
echo "brand new"       >  "$REPO/src/untracked.txt"
mkdir -p "$REPO/newdir"
echo "in a new dir"    >  "$REPO/newdir/deep.txt"
echo "dirty submodule" >  "$REPO/vendor/lib/dirty.txt"

echo "$REPO"

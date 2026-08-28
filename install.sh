#!/usr/bin/env bash
#
# Norn - build and install the Git client
#
# Installs /usr/bin/norn, the norn-editor helper into libexec, and the desktop
# entry, icon and metadata. Nothing existing is replaced, so uninstalling is just a
# matter of removing what was added; uninstall.sh does exactly that.
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
PREFIX="/usr"
HOOK_FILE="/etc/pacman.d/hooks/95-norn.hook"

install_hook=1
run_tests=0
build_type="Release"

msg()  { printf '\033[1;34m::\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m::\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m::\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: ${0##*/} [options]

  --prefix <dir>   Where to install (default /usr)
  --debug          Build with debug symbols and assertions
  --tests          Run the test suite before installing
  --no-hook        Skip the pacman rebuild-reminder hook
  -h, --help       This text

Installing into $PREFIX needs your sudo password.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix) PREFIX="${2:?--prefix needs a directory}"; shift ;;
        --debug) build_type="Debug" ;;
        --tests) run_tests=1 ;;
        --no-hook) install_hook=0 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

[[ $EUID -ne 0 ]] || die "run this as your normal user - it calls sudo where it needs to."
command -v pacman >/dev/null || die "this installer targets Arch Linux (pacman not found)."

# --- dependencies ------------------------------------------------------------
# git is a runtime dependency rather than a build one: Norn drives the git
# command line rather than linking a library, so a missing git means an
# application that starts and then cannot do anything.
command -v git >/dev/null || die "git is not installed, and Norn runs git to do its work."

missing=()
for pkg in cmake ninja gcc pkgconf gtkmm3 gtksourceview4 glib2; do
    pacman -Qq "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
done
if (( run_tests )); then
    pacman -Qq gtest >/dev/null 2>&1 || missing+=(gtest)
fi
if (( ${#missing[@]} )); then
    msg "Installing build dependencies: ${missing[*]}"
    sudo pacman -S --needed --noconfirm "${missing[@]}"
fi

# --- build -------------------------------------------------------------------
msg "Building ($build_type) ..."
BUILD_LOG="$BUILD_DIR/build.log"
mkdir -p "$BUILD_DIR"
: > "$BUILD_LOG"

if ! cmake -S "$PROJECT_DIR" -B "$BUILD_DIR/cmake" -G Ninja \
           -DCMAKE_BUILD_TYPE="$build_type" \
           -DCMAKE_INSTALL_PREFIX="$PREFIX" \
           -DBUILD_TESTING=$([[ $run_tests -eq 1 ]] && echo ON || echo OFF) \
           >>"$BUILD_LOG" 2>&1; then
    tail -25 "$BUILD_LOG" >&2
    die "cmake configure failed (full log: $BUILD_LOG)"
fi

if ! cmake --build "$BUILD_DIR/cmake" >>"$BUILD_LOG" 2>&1; then
    tail -25 "$BUILD_LOG" >&2
    die "build failed (full log: $BUILD_LOG)"
fi

[[ -x "$BUILD_DIR/cmake/src/norn" ]] || die "build produced no norn binary."
msg "Built $BUILD_DIR/cmake/src/norn"

if (( run_tests )); then
    msg "Running the test suite ..."
    if ! ctest --test-dir "$BUILD_DIR/cmake" --output-on-failure >>"$BUILD_LOG" 2>&1; then
        tail -40 "$BUILD_LOG" >&2
        die "tests failed (full log: $BUILD_LOG)"
    fi
    msg "Tests passed."
fi

# --- install -----------------------------------------------------------------
msg "Installing to $PREFIX ..."
if ! sudo cmake --install "$BUILD_DIR/cmake" >>"$BUILD_LOG" 2>&1; then
    tail -25 "$BUILD_LOG" >&2
    die "install failed (full log: $BUILD_LOG)"
fi

# So the launcher and the icon appear without a logout.
sudo update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
sudo gtk-update-icon-cache -qtf "$PREFIX/share/icons/hicolor" 2>/dev/null || true

# --- pacman hook -------------------------------------------------------------
# GTK and its C++ bindings break ABI between releases, so an upgrade leaves Norn
# linked against libraries that are no longer there. The hook says so rather than
# leaving the failure to be discovered the next time it is launched.
if (( install_hook )); then
    msg "Installing the upgrade reminder to $HOOK_FILE ..."
    sudo install -d "$(dirname "$HOOK_FILE")"
    sudo tee "$HOOK_FILE" >/dev/null <<EOF
[Trigger]
Operation = Upgrade
Type = Package
Target = gtk3
Target = gtkmm3
Target = glibmm
Target = gtksourceview4

[Action]
Description = Norn was built against these libraries and needs rebuilding
When = PostTransaction
Exec = /usr/bin/bash -c 'printf "\033[1;33m::\033[0m Norn may need rebuilding: run %s\n" "$PROJECT_DIR/install.sh"'
EOF
fi

msg "Done. Run 'norn' in a repository, or 'norn <directory>'."

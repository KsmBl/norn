#!/usr/bin/env bash
#
# Norn - remove what install.sh added
#
# Only files Norn installed are removed. Your repositories and your Git
# configuration are untouched; Norn never wrote to either.
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="/usr"
HOOK_FILE="/etc/pacman.d/hooks/95-norn.hook"

keep_settings=0

msg() { printf '\033[1;34m::\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m::\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: ${0##*/} [options]

  --prefix <dir>     Where it was installed (default /usr)
  --keep-settings    Leave ~/.config/norn in place
  -h, --help         This text

Removing from $PREFIX needs your sudo password.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix) PREFIX="${2:?--prefix needs a directory}"; shift ;;
        --keep-settings) keep_settings=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

[[ $EUID -ne 0 ]] || die "run this as your normal user - it calls sudo where it needs to."

msg "Removing Norn from $PREFIX ..."

# cmake --install records exactly what it wrote, which is more reliable than a
# hand-maintained list: the helper goes to libexec rather than bin, and that kind
# of detail is easy to get wrong twice.
MANIFEST="$PROJECT_DIR/build/cmake/install_manifest.txt"
if [[ -f $MANIFEST ]]; then
    while IFS= read -r file; do
        [[ -n $file ]] && sudo rm -f "$file"
    done < "$MANIFEST"
else
    sudo rm -f "$PREFIX/bin/norn"
    sudo rm -f "$PREFIX/libexec/norn-editor"
    sudo rm -f "$PREFIX/share/applications/de.synthelicz.Norn.desktop"
    sudo rm -f "$PREFIX/share/metainfo/de.synthelicz.Norn.metainfo.xml"
    sudo rm -f "$PREFIX/share/icons/hicolor/scalable/apps/de.synthelicz.Norn.svg"
fi

sudo rm -f "$HOOK_FILE"

sudo update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
sudo gtk-update-icon-cache -qtf "$PREFIX/share/icons/hicolor" 2>/dev/null || true

if (( keep_settings )); then
    msg "Leaving your settings in ~/.config/norn."
else
    rm -rf ~/.config/norn
fi

msg "Done. The build directory at $PROJECT_DIR/build can be deleted too."

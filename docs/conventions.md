# Norn — design notes

A graphical Git client built on **GTK 3 via gtkmm**, the toolkit Thunar uses.

The previous Qt 6 / KDE Frameworks implementation is preserved on the `qt-kf6`
branch. Do not reintroduce Qt or KDE Frameworks here.

## Hard rules

- **Commits carry no tooling attribution** — not in the subject, not in the body,
  not in trailers or `Co-authored-by:` lines. A commit message describes the change
  and why it was made, never how it was produced. This applies to Norn's own history
  and to any commit message Norn generates for the user.
- Commits are authored as the repository owner's configured git identity. Do not
  override `user.name` or `user.email`.

## Interface style

The window is laid out the way a file manager is — Thunar specifically:

- **Fixed panes, not dockable panels.** A side pane and the main area sit in
  `Gtk::Paned` splitters. Nothing floats, nothing carries its own title bar.
- **Menus are File / Edit / View / Go**, not Repository / Branch / Remote.
- **The toolbar is flat and icon-only**, and does not compete with the location bar
  under it, which shows the repository path and the checked-out branch.
- **Item views are frameless** (`set_shadow_type(Gtk::SHADOW_NONE)`) with no
  alternating row colours — panes sit flush against each other.
- **The side pane is a real tree**: tree lines on and extra level indentation, or
  nested rows read as a flat list. An empty section is dimmed, since it has no
  expander and would otherwise look identical to a collapsed one.
- **A view rebuilt on refresh must restore its own state.** The side pane is
  regenerated whenever refs change, which is after every write and every disk
  change the watcher sees. Anything the tree view holds — expansion, selection —
  is lost unless rows carry a stable key and the rebuild puts it back. Ending a
  rebuild with `expand_all()` silently reopens what the user just collapsed.
- A `Gtk::Paned` position is clamped against its current allocation, which during
  construction is nothing, so a starting size comes from `set_size_request()`
  instead. Re-applying a position after the first allocation runs too early to
  survive the layout that follows.

## Architecture

Three layers, strictly separated:

- `src/core/` — knows git, knows nothing about widgets. Every git invocation goes
  through `GitRunner`. Parsers under `src/core/parsers/` are pure functions.
- `src/ui/` — widgets only.
- `helper/` — `norn-editor`, the program git runs as its editor.

The git backend wraps the `git` CLI (not libgit2), so it inherits the user's
`~/.gitconfig`, hooks and credential helpers.

## Git invocation rules

These are correctness rules, not style. Each corresponds to a real failure mode.

**Config prelude.** Every invocation carries `-c color.ui=false`,
`-c core.quotepath=false`, `-c log.showSignature=false`, `-c diff.external=`,
`-c diff.noprefix=false`, `-c diff.mnemonicPrefix=false`, `-c core.pager=cat`,
`-c advice.detachedHead=false`, `-c advice.statusHints=false`, plus `-c gc.auto=0`
on mutating commands. Without `log.showSignature=false` a user with signature
verification gets GPG output interleaved into `git log`, destroying the parse.

Never override `core.autocrlf`, `commit.gpgsign`, `rebase.autostash`,
`merge.conflictStyle` or `status.renames` — those are semantics the user chose.

**Environment.** Derive from the real one; never build a clean environment. The
user's credential helper may be a shell alias needing `PATH`, `HOME` and
`SSH_AUTH_SOCK`. Remove `LC_ALL` *then* set `LC_MESSAGES=C` — `LC_ALL` outranks it.

**Never derive state from stderr text.** Re-derive from `git status --porcelain=v2`
plus filesystem probes. Exit codes and porcelain output are the contract.

**Pathspecs are glob-interpreted.** A file named `weird[1].txt` passed as a bare
pathspec also matches `weird1.txt`. Always feed paths as NUL-separated
`:(literal)<path>` entries via `--pathspec-from-file=- --pathspec-file-nul`.

**Always drain stdout and stderr, even when ignored.** Writing a large patch to
stdin without draining deadlocks both processes on full pipes.

**Progress goes to stderr and uses `\r`, not `\n`.**

## gtkmm specifics worth remembering

- giomm 2.4 does not wrap `GSubprocess`. Processes are spawned with
  `Glib::spawn_async_with_pipes` and their pipes wrapped from the Unix stream
  constructors; exit status arrives through a child watch and is decoded with
  `WIFEXITED`.
- A test binary must call `Glib::init()` and `Gio::init()` itself — the wrapper
  types are registered there, and `Glib::wrap` cannot wrap an unregistered type.
  The application gets this from `Gtk::Application`.
- `Glib::spawn_sync` returns captured output as a `std::string` built from a
  `char*`, so it truncates at the first NUL. Useless for any `-z` format.
- A finished `GitJob` cannot be freed inside its own completion signal; it is
  parked and swept from an idle whose connection the runner owns.
- `set_no_show_all()` on a `Gtk::InfoBar` also suppresses `show_all()` for its
  children, so its label must be shown explicitly.
- `Gtk::ToolButton` does not reliably pass a tooltip to the button it builds
  internally, so toolbar items are a plain `Gtk::Button` inside a `Gtk::ToolItem`,
  which owns its tooltip directly. The tooltip is set on both: a staging button is
  insensitive until something is selected, and the sensitive wrapper answers for it.
- Icon names are chosen at runtime from a list of candidates: the `vcs-*` icons
  exist in Breeze but not in Adwaita, so a name that looks right on one desktop is
  a missing-image square on another.

## Conventions

- Filenames all-lowercase, run together: `gitrunner.cc`, `statusparser.h`.
- `PascalCase` classes, `m_` members, `snake_case` methods (gtkmm's own style).
- `std::string` for git bytes, `Glib::ustring` for display text.
- Comments explain *why*, not what.

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/norn [directory]
```

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "diffdocument.h"

#include <map>
#include <set>
#include <string>
#include <vector>

/*!
 * Builds the patches that hunk- and line-level staging apply.
 *
 * The rule that governs everything here: whichever side is the current state of
 * the apply target must be represented completely, so unselected changes on that
 * side become context lines, while the other side's unselected lines are dropped.
 *
 * | Operation | Target side    | Unselected removals | Unselected additions |
 * |-----------|----------------|---------------------|----------------------|
 * | Stage     | old (index)    | become context      | dropped              |
 * | Unstage   | new (index)    | dropped             | become context       |
 * | Discard   | new (worktree) | dropped             | become context       |
 *
 * Everything is bytes end to end. Decoding to text and back can mangle invalid
 * UTF-8 and normalise line endings, and git has to receive exactly what it made.
 */
namespace PatchBuilder
{
enum class Direction {
    /*! Applied to the index with `--cached`; the old side is the current state. */
    Stage,
    /*! Applied to the index with `--cached --reverse`; the new side is current. */
    Unstage,
    /*! Applied to the worktree with `--reverse`; the new side is current. */
    Discard,
};

/*! Builds a patch containing whole hunks. @p hunk_indexes selects which. */
std::string build_for_hunks(const DiffFile &file, const std::set<int> &hunk_indexes, Direction direction);

/*!
 * Builds a patch containing only selected lines.
 *
 * @p selected_lines maps a hunk index to the indexes of the changed lines chosen
 * within it. Hunks carrying a no-newline marker must be staged whole; a partial
 * selection inside one is promoted to the entire hunk.
 */
std::string build_for_lines(const DiffFile &file, const std::map<int, std::set<int>> &selected_lines, Direction direction);

/*!
 * True when @p patch contains a hunk with a zero-length side, which only arises
 * for a file being created or removed in its entirety.
 *
 * `git apply` refuses such a patch unless told to expect it. The check is worth
 * keeping for every other patch, so the flag is derived per patch rather than
 * passed unconditionally.
 */
bool needs_unidiff_zero(const std::string &patch);

/*! The `git apply` arguments matching @p direction, excluding the patch itself. */
std::vector<std::string> apply_arguments(Direction direction, bool dry_run, bool unidiff_zero = false);
}

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

/*!
 * Builds pathspec input for git commands that accept `--pathspec-from-file`.
 *
 * Paths are never passed as plain arguments. Two reasons, both of which produce
 * silent wrong behaviour rather than an error:
 *
 * - A bare pathspec is glob-interpreted, so selecting `weird[1].txt` also matches
 *   an unrelated `weird1.txt` sitting next to it and stages both.
 * - A long selection overflows ARG_MAX, and a filename beginning with `-` is read
 *   as an option.
 *
 * The `:(literal)` prefix disables the globbing, and feeding the list on stdin
 * removes the length limit.
 */
namespace Pathspec
{
/*! The arguments that make a command read its pathspecs from stdin. */
inline std::vector<std::string> from_stdin_arguments()
{
    return {"--pathspec-from-file=-", "--pathspec-file-nul"};
}

/*! NUL-separated `:(literal)<path>` entries, ready to write to a command's stdin. */
std::string encode(const std::vector<std::string> &paths);

/*! A single `:(literal)<path>` argument, for commands that take one inline. */
std::string literal(const std::string &path);
}

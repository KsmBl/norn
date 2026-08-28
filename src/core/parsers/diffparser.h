/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/diffdocument.h"

#include <string>
#include <vector>

/*!
 * Parses unified diff output into a DiffDocument.
 *
 * Works on raw bytes throughout. Decoding to text and back would mangle invalid
 * UTF-8 and can normalise line endings, and the parsed result is fed straight back
 * to `git apply`, so it has to survive the round trip byte-exact.
 */
namespace DiffParser
{
/*!
 * The diff arguments the parser and PatchBuilder assume.
 *
 * The prefixes are forced rather than left to configuration, because diff.noprefix
 * would otherwise strip them and shift what `git apply -p1` expects.
 */
std::vector<std::string> arguments(int context_lines = 3);

DiffDocument parse(const std::string &output);
}

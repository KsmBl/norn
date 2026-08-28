/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/statussnapshot.h"

#include <string>
#include <vector>

/*!
 * Parses `git status --porcelain=v2 -z` output.
 *
 * A pure function over bytes, so it is directly testable without a repository.
 */
namespace StatusParser
{
/*! The arguments this parser expects, so the caller cannot drift out of sync. */
std::vector<std::string> arguments();

/*! Parses raw NUL-separated porcelain v2 output. */
StatusSnapshot parse(const std::string &output);
}

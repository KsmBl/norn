/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/commitrecord.h"

#include <string>
#include <vector>

/*! Parses `git log` output in the format log_arguments() requests. */
namespace LogParser
{
/*!
 * Arguments for a page of history: @p limit commits, starting @p skip in.
 *
 * `--skip` rescans from the start of the traversal, so paging deep into a very
 * large history gets progressively slower. It is used anyway because the
 * alternative — resuming from the last commit of the previous page — does not work
 * under `--topo-order`, where the traversal is not a linear walk that can be picked
 * up from a single commit. Pages are large enough that the rescan is not noticeable
 * at any realistic scroll depth.
 */
std::vector<std::string> log_arguments(int limit, int skip = 0);

std::vector<CommitRecord> parse(const std::string &output);
}

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/refrecord.h"

#include <string>
#include <vector>

/*!
 * Parses ref, stash, worktree and submodule listings.
 *
 * Branches, remote branches and tags all come from a single for-each-ref
 * invocation with one format, so there is one parser rather than three.
 */
namespace RefParser
{
std::vector<std::string> ref_arguments();
std::vector<RefRecord> parse_refs(const std::string &output);

std::vector<std::string> stash_arguments();
std::vector<StashRecord> parse_stashes(const std::string &output);

std::vector<std::string> worktree_arguments();
/*! @p current_path marks which entry the application has open. */
std::vector<WorktreeRecord> parse_worktrees(const std::string &output, const std::string &current_path);

std::vector<std::string> submodule_arguments();
std::vector<SubmoduleRecord> parse_submodules(const std::string &output);
}

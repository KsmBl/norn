/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "statusentry.h"

#include <string>
#include <vector>

/*! A complete `git status` result: the branch headers plus every reported path. */
class StatusSnapshot
{
public:
    /*! HEAD's commit, or empty on an unborn branch. */
    std::string m_head_oid;

    /*! Current branch name, or empty when HEAD is detached. */
    std::string m_branch;

    /*! Upstream branch, e.g. "origin/main". Empty when none is configured. */
    std::string m_upstream;

    int m_ahead = 0;
    int m_behind = 0;
    /*! False when no upstream exists or it is not present locally. */
    bool m_has_ahead_behind = false;

    /*!
     * True on a branch with no commits yet, reported as `# branch.oid (initial)`.
     * Nearly every history, diff and ref path needs to special-case this.
     */
    bool m_is_unborn = false;

    /*! True when HEAD points at a commit rather than a branch. */
    bool m_is_detached = false;

    int m_stash_count = 0;

    std::vector<StatusEntry> m_entries;

    bool is_clean() const
    {
        for (const StatusEntry &entry : m_entries) {
            if (entry.m_kind != StatusEntry::Kind::Ignored) {
                return false;
            }
        }
        return true;
    }

    bool has_conflicts() const
    {
        for (const StatusEntry &entry : m_entries) {
            if (entry.is_conflicted()) {
                return true;
            }
        }
        return false;
    }
};

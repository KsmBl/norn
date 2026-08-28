/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

/*! One path's state, as reported by `git status --porcelain=v2`. */
class StatusEntry
{
public:
    enum class Kind {
        /*! Record type `1`. */
        Ordinary,
        /*! Record type `2`. Carries orig_path and a similarity score. */
        RenamedOrCopied,
        /*! Record type `u`. Never appears in the staged or unstaged list. */
        Unmerged,
        /*! Record type `?`. */
        Untracked,
        /*! Record type `!`. */
        Ignored,
    };

    Kind m_kind = Kind::Ordinary;

    /*!
     * The staged column: index versus HEAD. `.` means unchanged.
     * For unmerged entries this is the first character of the conflict code.
     */
    char m_index_status = '.';

    /*!
     * The unstaged column: worktree versus index. `.` means unchanged.
     * For unmerged entries this is the second character of the conflict code.
     */
    char m_worktree_status = '.';

    /*!
     * Path relative to the working tree root, exactly as git reported it. Kept as
     * bytes so it can be handed back as a literal pathspec without a lossy decode.
     */
    std::string m_path;

    /*! Rename or copy source. Only set for Kind::RenamedOrCopied. */
    std::string m_orig_path;
    /*! Similarity percentage, e.g. 100 for R100. */
    int m_score = 0;

    bool m_is_submodule = false;
    bool m_submodule_commit_changed = false;
    bool m_submodule_has_modified = false;
    bool m_submodule_has_untracked = false;

    unsigned m_mode_head = 0;
    unsigned m_mode_index = 0;
    unsigned m_mode_worktree = 0;

    std::string m_oid_head;
    std::string m_oid_index;

    /*! Conflict stages: 1 base, 2 ours, 3 theirs. Empty when that stage is absent. */
    std::string m_oid_stage1;
    std::string m_oid_stage2;
    std::string m_oid_stage3;

    /*! True when the index differs from HEAD, so the entry belongs in the staged list. */
    bool is_staged() const
    {
        return m_kind != Kind::Unmerged && m_kind != Kind::Untracked && m_kind != Kind::Ignored && m_index_status != '.';
    }

    /*!
     * True when the worktree differs from the index. Untracked files count as
     * unstaged changes; conflicts do not, they are their own category.
     */
    bool is_unstaged() const
    {
        if (m_kind == Kind::Untracked) {
            return true;
        }
        if (m_kind == Kind::Unmerged || m_kind == Kind::Ignored) {
            return false;
        }
        return m_worktree_status != '.';
    }

    bool is_conflicted() const
    {
        return m_kind == Kind::Unmerged;
    }

    /*! The two-letter conflict code, e.g. "UU" or "DU". Only meaningful when conflicted. */
    std::string conflict_code() const
    {
        return std::string{m_index_status, m_worktree_status};
    }

    /*!
     * Compares everything a view renders, so a refresh that changed nothing can
     * skip rebuilding the model and preserve the user's selection.
     */
    bool operator==(const StatusEntry &) const = default;
};

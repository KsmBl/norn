/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

/*! A branch, remote-tracking branch or tag. */
class RefRecord
{
public:
    enum class Kind {
        LocalBranch,
        RemoteBranch,
        Tag,
    };

    Kind m_kind = Kind::LocalBranch;

    /*! Full ref name, e.g. refs/heads/main. Unambiguous, so operations use this. */
    std::string m_ref_name;
    /*! Short name, e.g. main or origin/main. What the user sees. */
    std::string m_short_name;

    std::string m_object_name;
    /*! For an annotated tag, the commit it points at; otherwise empty. */
    std::string m_peeled_object_name;
    /*! True when the tag carries its own object rather than pointing at a commit. */
    bool m_is_annotated_tag = false;

    /*! Configured upstream, e.g. origin/main. Local branches only. */
    std::string m_upstream;
    int m_ahead = 0;
    int m_behind = 0;
    bool m_has_tracking = false;
    /*! True when the upstream ref no longer exists on the remote. */
    bool m_upstream_gone = false;

    /*! True for the branch HEAD currently points at. */
    bool m_is_head = false;

    std::string m_subject;
    std::string m_date;

    /*! For a remote branch, the remote's name; empty otherwise. */
    std::string remote_name() const
    {
        if (m_kind != Kind::RemoteBranch) {
            return {};
        }
        const std::size_t slash = m_short_name.find('/');
        return slash == std::string::npos ? std::string() : m_short_name.substr(0, slash);
    }

    /*! The commit this ref ultimately refers to, following an annotated tag. */
    std::string commit() const
    {
        return m_peeled_object_name.empty() ? m_object_name : m_peeled_object_name;
    }
};

/*! One stash entry. */
class StashRecord
{
public:
    /*! The selector, e.g. stash@{0}. Renumbers whenever the list changes. */
    std::string m_selector;
    /*! The stash commit. Stable, so it can be used to detect renumbering. */
    std::string m_object_name;
    std::string m_message;
    std::string m_date;
    int m_index = 0;
};

/*! One entry from `git worktree list`. */
class WorktreeRecord
{
public:
    std::string m_path;
    std::string m_head;
    /*! Short branch name, or empty when HEAD is detached there. */
    std::string m_branch;
    bool m_is_detached = false;
    /*! True for the worktree the application is currently open on. */
    bool m_is_current = false;
    bool m_is_prunable = false;
    bool m_is_locked = false;
    std::string m_lock_reason;
    /*! True for the main worktree, which cannot be removed. */
    bool m_is_main = false;
};

/*! One entry from `git submodule status`. */
class SubmoduleRecord
{
public:
    std::string m_path;
    /*! The commit the superproject records. */
    std::string m_commit;
    /*! Branch or tag description git appends, e.g. "heads/main". */
    std::string m_describe;

    bool m_is_uninitialised = false;
    /*! True when the checked-out commit differs from the recorded one. */
    bool m_is_modified = false;
    bool m_has_conflicts = false;
};

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "refrecord.h"

#include <glibmm/ustring.h>
#include <sigc++/signal.h>

#include <string>
#include <vector>

class GitJob;
class Repository;

/*! Branches, tags, stashes, worktrees and submodules. */
class RefService
{
public:
    explicit RefService(Repository &repository);

    const std::vector<RefRecord> &refs() const
    {
        return m_refs;
    }
    const std::vector<StashRecord> &stashes() const
    {
        return m_stashes;
    }
    const std::vector<WorktreeRecord> &worktrees() const
    {
        return m_worktrees;
    }
    const std::vector<SubmoduleRecord> &submodules() const
    {
        return m_submodules;
    }

    void refresh();

    void checkout(const std::string &name);
    /*! Checks out @p commit without attaching HEAD to a branch. */
    void checkout_detached(const std::string &commit);
    void create_branch(const std::string &name, const std::string &start_point, bool checkout_after);
    /*!
     * Deletes @p name. Not forced: git refuses when the branch holds commits that
     * exist nowhere else, and that refusal is surfaced rather than overridden.
     */
    void delete_branch(const std::string &name);
    void rename_branch(const std::string &old_name, const std::string &new_name);
    void merge(const std::string &name, bool no_fast_forward);

    void create_tag(const std::string &name, const std::string &target, const std::string &message);
    void delete_tag(const std::string &name);

    void stash_push(const std::string &message, bool include_untracked);
    /*!
     * Applies a stash and drops it.
     *
     * @p expected_object_name guards against the list having renumbered since the
     * UI read it: stash@{1} is a position, not an identity, so acting on a stale
     * selector silently drops the wrong stash and that is unrecoverable.
     */
    void stash_pop(int index, const std::string &expected_object_name);
    void stash_apply(int index, const std::string &expected_object_name);
    void stash_drop(int index, const std::string &expected_object_name);

    void add_worktree(const std::string &path, const std::string &branch, bool create_branch);
    void remove_worktree(const std::string &path);
    void prune_worktrees();

    void update_submodule(const std::string &path);
    void update_submodule_to_remote(const std::string &path);

    sigc::signal<void()> &signal_changed()
    {
        return m_signal_changed;
    }
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_failed()
    {
        return m_signal_failed;
    }

private:
    void load_refs();
    void load_stashes();
    void load_worktrees();
    void load_submodules();
    void track_write(GitJob *job, const Glib::ustring &summary);
    void run_stash_command(const std::string &subcommand, int index, const std::string &expected_object_name, const Glib::ustring &summary);

    Repository &m_repository;

    std::vector<RefRecord> m_refs;
    std::vector<StashRecord> m_stashes;
    std::vector<WorktreeRecord> m_worktrees;
    std::vector<SubmoduleRecord> m_submodules;

    sigc::signal<void()> m_signal_changed;
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_failed;
};

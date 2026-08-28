/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <glibmm/ustring.h>
#include <sigc++/signal.h>

#include <string>

class Repository;

/*!
 * Which multi-step git operation, if any, is currently in progress.
 *
 * Derived entirely from files on disk rather than from any command's output. A
 * merge or rebase that stops on a conflict exits non-zero and prints an
 * explanation, but that text is not a contract; the presence of MERGE_HEAD or a
 * rebase-merge directory is. It also means the state is correct after a restart,
 * or when the user ran the command in a terminal instead.
 */
class OperationState
{
public:
    enum class Kind {
        None,
        Merge,
        /*! `git rebase`, merge backend. */
        Rebase,
        /*! `git rebase` using the am backend, or `git am`. */
        RebaseApply,
        CherryPick,
        Revert,
        Bisect,
    };

    explicit OperationState(Repository &repository);

    Kind kind() const
    {
        return m_kind;
    }

    bool in_progress() const
    {
        return m_kind != Kind::None;
    }

    /*! True for `rebase -i`, where a todo list exists and can be edited. */
    bool is_interactive_rebase() const
    {
        return m_is_interactive;
    }

    const std::string &rebase_head_name() const
    {
        return m_rebase_head_name;
    }

    int step() const
    {
        return m_step;
    }
    int step_count() const
    {
        return m_step_count;
    }

    /*! A one-line description of the current state, fit for a banner. */
    Glib::ustring description() const;

    /*! The git subcommand driving the current operation, e.g. "rebase". */
    std::string command() const;

    /*! True when the operation in progress supports skipping a step. */
    bool can_skip() const;

    /*! Re-reads the state from disk. Cheap: a handful of stat calls. */
    void refresh();

    sigc::signal<void()> &signal_changed()
    {
        return m_signal_changed;
    }

private:
    std::string git_path(const std::string &name) const;
    std::string read_trimmed(const std::string &name) const;

    Repository &m_repository;

    Kind m_kind = Kind::None;
    bool m_is_interactive = false;
    std::string m_rebase_head_name;
    int m_step = 0;
    int m_step_count = 0;

    sigc::signal<void()> m_signal_changed;
};

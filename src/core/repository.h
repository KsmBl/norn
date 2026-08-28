/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "gitrunner.h"
#include "statussnapshot.h"

#include <glibmm/ustring.h>
#include <sigc++/signal.h>

#include <memory>
#include <string>

/*!
 * One open repository: its resolved paths, its current status, and the runner
 * every operation goes through.
 *
 * Deliberately not a god object. It owns identity and the status snapshot; the
 * individual operation families live in their own services on top of the runner.
 */
class Repository
{
public:
    /*! @p toplevel must already be a resolved working tree root. */
    explicit Repository(std::string toplevel);
    ~Repository();

    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;

    const std::string &toplevel() const
    {
        return m_toplevel;
    }

    /*!
     * $GIT_DIR for this working tree. Not necessarily <toplevel>/.git: in a linked
     * worktree or a submodule, .git is a file pointing elsewhere.
     */
    const std::string &git_dir() const
    {
        return m_git_dir;
    }

    /*!
     * The git directory shared between linked worktrees, where refs, packed-refs
     * and logs/HEAD actually live.
     */
    const std::string &common_dir() const
    {
        return m_common_dir;
    }

    /*! The directory name, for the window title. */
    std::string display_name() const;

    GitRunner &runner()
    {
        return *m_runner;
    }

    const StatusSnapshot &status() const
    {
        return m_status;
    }

    bool has_status() const
    {
        return m_has_status;
    }

    /*!
     * Resolves the paths that depend on git and runs the first status query.
     * Emits signal_ready() once the paths are known.
     */
    void open();

    /*!
     * Re-runs `git status`. Coalesced, so a burst of filesystem events costs one
     * query rather than one per event.
     */
    void refresh_status();

    sigc::signal<void()> &signal_ready()
    {
        return m_signal_ready;
    }
    sigc::signal<void()> &signal_status_changed()
    {
        return m_signal_status_changed;
    }
    /*! A git command failed; the message is already redacted and fit to display. */
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_operation_failed()
    {
        return m_signal_operation_failed;
    }

private:
    void resolve_paths();

    std::string m_toplevel;
    std::string m_git_dir;
    std::string m_common_dir;

    std::unique_ptr<GitRunner> m_runner;
    StatusSnapshot m_status;
    bool m_has_status = false;

    sigc::signal<void()> m_signal_ready;
    sigc::signal<void()> m_signal_status_changed;
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_operation_failed;
};

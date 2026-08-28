/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "rebasetodo.h"

#include <glibmm/ustring.h>
#include <sigc++/signal.h>

#include <string>
#include <vector>

class EditorBridge;
class Repository;

/*! Interactive rebase, cherry-pick and revert. */
class RebaseService
{
public:
    RebaseService(Repository &repository, EditorBridge &editor_bridge);

    /*!
     * Reads the commits between @p upstream and HEAD as a starting todo list.
     *
     * The plan is generated rather than obtained from git, which lets the editor be
     * shown before anything on disk changes.
     */
    void request_plan(const std::string &upstream);

    /*!
     * Runs `rebase -i` with @p steps, delivered through a copying sequence editor
     * so git never opens one of its own.
     */
    void start_interactive_rebase(const std::string &upstream, const std::vector<RebaseStep> &steps);

    void cherry_pick(const std::string &commit);
    void revert(const std::string &commit);

    sigc::signal<void(const std::string &, const std::vector<RebaseStep> &)> &signal_plan_ready()
    {
        return m_signal_plan_ready;
    }
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_failed()
    {
        return m_signal_failed;
    }

private:
    Repository &m_repository;
    EditorBridge &m_editor_bridge;
    /*! Kept for the lifetime of the rebase git reads it during. */
    std::string m_todo_path;

    sigc::signal<void(const std::string &, const std::vector<RebaseStep> &)> m_signal_plan_ready;
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_failed;
};

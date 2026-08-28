/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <giomm/filemonitor.h>
#include <glibmm/refptr.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <vector>

class Repository;

/*!
 * Notices when the repository changes underneath the application.
 *
 * Watches the git directory rather than the working tree. Recursively watching a
 * large checkout exhausts the inotify watch limit and then silently stops
 * delivering events, which is worse than not watching at all; re-running
 * `git status` on the events that do arrive is fast enough.
 */
class RepositoryWatcher
{
public:
    explicit RepositoryWatcher(Repository &repository);
    ~RepositoryWatcher();

    /*! Reads the repository layout and starts watching. Call after signal_ready(). */
    void start();

    /*! Turns automatic reloading on or off without tearing the monitors down. */
    void set_enabled(bool enabled);

    /*! Something changed and a refresh is due. Already debounced. */
    sigc::signal<void()> &signal_changed()
    {
        return m_signal_changed;
    }

private:
    void watch_directory(const std::string &path);
    void schedule_refresh();
    bool emit_if_idle();

    Repository &m_repository;
    std::vector<Glib::RefPtr<Gio::FileMonitor>> m_monitors;

    sigc::connection m_debounce;
    /*! True when a refresh came due while the application was mid-write. */
    bool m_pending = false;
    bool m_enabled = true;

    sigc::signal<void()> m_signal_changed;
};

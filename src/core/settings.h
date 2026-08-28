/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <sigc++/signal.h>

#include <string>

/*!
 * The application's own settings, stored as a key file under the user's config
 * directory.
 *
 * Deliberately short. Anything belonging to the repository rather than to this
 * application — name, email, signing key, line ending handling — stays in the
 * user's Git configuration, where their terminal can see it too. Duplicating those
 * here would create two places to look and one of them would be wrong.
 *
 * A key file rather than GSettings, so there is no schema to install and norn
 * runs correctly straight out of a build directory.
 */
class Settings
{
public:
    static Settings &instance();

    void load();
    void save();

    int context_lines() const
    {
        return m_context_lines;
    }
    void set_context_lines(int value)
    {
        m_context_lines = value;
    }

    int max_diff_lines() const
    {
        return m_max_diff_lines;
    }
    void set_max_diff_lines(int value)
    {
        m_max_diff_lines = value;
    }

    int history_page_size() const
    {
        return m_history_page_size;
    }
    void set_history_page_size(int value)
    {
        m_history_page_size = value;
    }

    bool show_graph() const
    {
        return m_show_graph;
    }
    void set_show_graph(bool value)
    {
        m_show_graph = value;
    }

    bool sign_off_by_default() const
    {
        return m_sign_off_by_default;
    }
    void set_sign_off_by_default(bool value)
    {
        m_sign_off_by_default = value;
    }

    int subject_soft_limit() const
    {
        return m_subject_soft_limit;
    }
    void set_subject_soft_limit(int value)
    {
        m_subject_soft_limit = value;
    }

    bool avoid_optional_locks() const
    {
        return m_avoid_optional_locks;
    }
    void set_avoid_optional_locks(bool value)
    {
        m_avoid_optional_locks = value;
    }

    /*! Which tab the window reopens on, so it comes back where it was left. */
    int active_tab() const
    {
        return m_active_tab;
    }
    void set_active_tab(int value)
    {
        m_active_tab = value;
    }

    bool auto_refresh() const
    {
        return m_auto_refresh;
    }
    void set_auto_refresh(bool value)
    {
        m_auto_refresh = value;
    }

    /*! Emitted after save(), so every open window picks the change up. */
    sigc::signal<void()> &signal_changed()
    {
        return m_signal_changed;
    }

private:
    Settings() = default;

    std::string config_path() const;

    /*! Three is git's own default and what most reviewers expect. */
    int m_context_lines = 3;
    /*! A generated file can produce a diff long enough to make the view unusable. */
    int m_max_diff_lines = 20000;
    /*! Large enough that a skipped page's rescan stays irrelevant at realistic depths. */
    int m_history_page_size = 1000;
    bool m_show_graph = true;
    bool m_sign_off_by_default = false;
    /*! The conventional limit; past it, tools start truncating. */
    int m_subject_soft_limit = 50;
    /*! Off by default: it makes every status re-stat the whole working tree. */
    bool m_avoid_optional_locks = false;
    bool m_auto_refresh = true;
    int m_active_tab = 0;

    sigc::signal<void()> m_signal_changed;
};

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settings.h"

#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>

#include <glib/gstdio.h>

namespace
{
constexpr const char *s_group_view = "View";
constexpr const char *s_group_commit = "Commit";
constexpr const char *s_group_git = "Git";
}

Settings &Settings::instance()
{
    static Settings settings;
    return settings;
}

std::string Settings::config_path() const
{
    return Glib::build_filename(Glib::get_user_config_dir(), "norn", "norn.conf");
}

void Settings::load()
{
    Glib::KeyFile file;

    try {
        if (!file.load_from_file(config_path())) {
            return;
        }
    } catch (const Glib::Error &) {
        // No settings yet, or an unreadable file. The defaults are all valid.
        return;
    }

    const auto read_int = [&file](const char *group, const char *key, int fallback) {
        try {
            return file.get_integer(group, key);
        } catch (const Glib::Error &) {
            return fallback;
        }
    };

    const auto read_bool = [&file](const char *group, const char *key, bool fallback) {
        try {
            return file.get_boolean(group, key);
        } catch (const Glib::Error &) {
            return fallback;
        }
    };

    m_context_lines = read_int(s_group_view, "ContextLines", m_context_lines);
    m_max_diff_lines = read_int(s_group_view, "MaxDiffLines", m_max_diff_lines);
    m_history_page_size = read_int(s_group_view, "HistoryPageSize", m_history_page_size);
    m_show_graph = read_bool(s_group_view, "ShowGraph", m_show_graph);
    m_active_tab = read_int(s_group_view, "ActiveTab", m_active_tab);

    m_sign_off_by_default = read_bool(s_group_commit, "SignOffByDefault", m_sign_off_by_default);
    m_subject_soft_limit = read_int(s_group_commit, "SubjectSoftLimit", m_subject_soft_limit);

    m_avoid_optional_locks = read_bool(s_group_git, "AvoidOptionalLocks", m_avoid_optional_locks);
    m_auto_refresh = read_bool(s_group_git, "AutoRefresh", m_auto_refresh);
}

void Settings::save()
{
    Glib::KeyFile file;

    file.set_integer(s_group_view, "ContextLines", m_context_lines);
    file.set_integer(s_group_view, "MaxDiffLines", m_max_diff_lines);
    file.set_integer(s_group_view, "HistoryPageSize", m_history_page_size);
    file.set_boolean(s_group_view, "ShowGraph", m_show_graph);
    file.set_integer(s_group_view, "ActiveTab", m_active_tab);

    file.set_boolean(s_group_commit, "SignOffByDefault", m_sign_off_by_default);
    file.set_integer(s_group_commit, "SubjectSoftLimit", m_subject_soft_limit);

    file.set_boolean(s_group_git, "AvoidOptionalLocks", m_avoid_optional_locks);
    file.set_boolean(s_group_git, "AutoRefresh", m_auto_refresh);

    const std::string path = config_path();
    g_mkdir_with_parents(Glib::path_get_dirname(path).c_str(), 0700);

    try {
        file.save_to_file(path);
    } catch (const Glib::Error &) {
        // Nothing useful to do: the settings simply will not persist.
    }

    m_signal_changed.emit();
}

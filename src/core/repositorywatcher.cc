/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "repositorywatcher.h"

#include "repository.h"

#include <giomm/file.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

namespace
{
/*!
 * A single git command touches several files in quick succession, so events are
 * collected rather than acted on individually.
 */
constexpr unsigned s_debounce_ms = 250;

/*!
 * True for git's own lock files, which are not repository state.
 *
 * This matters more than it looks. `git status` takes index.lock to write back the
 * refreshed stat cache, so watching for it means every status refresh schedules the
 * next one: the lock appears, the watcher fires, a status runs, and it takes the
 * lock again. An untouched repository was left running about four git invocations a
 * second for as long as the window stayed open. Every lock has a real file beside it
 * whose own change is reported separately, so nothing is missed by ignoring these.
 */
bool is_lock_file(const Glib::RefPtr<Gio::File> &file)
{
    if (!file) {
        return false;
    }

    const std::string name = file->get_basename();
    static const std::string suffix = ".lock";
    return name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}

RepositoryWatcher::RepositoryWatcher(Repository &repository)
    : m_repository(repository)
{
    // A refresh that arrived mid-write was deferred; fire it once the lane drains.
    m_repository.runner().signal_write_lane_idle().connect([this] {
        if (m_pending) {
            m_pending = false;
            m_signal_changed.emit();
        }
    });
}

RepositoryWatcher::~RepositoryWatcher()
{
    m_debounce.disconnect();

    for (const Glib::RefPtr<Gio::FileMonitor> &monitor : m_monitors) {
        monitor->cancel();
    }
}

void RepositoryWatcher::watch_directory(const std::string &path)
{
    if (path.empty() || !Glib::file_test(path, Glib::FILE_TEST_IS_DIR)) {
        return;
    }

    try {
        const Glib::RefPtr<Gio::FileMonitor> monitor = Gio::File::create_for_path(path)->monitor_directory();
        monitor->signal_changed().connect([this](const Glib::RefPtr<Gio::File> &file, const Glib::RefPtr<Gio::File> &, Gio::FileMonitorEvent) {
            if (is_lock_file(file)) {
                return;
            }
            schedule_refresh();
        });
        m_monitors.push_back(monitor);
    } catch (const Glib::Error &) {
        // Out of inotify watches, or a directory that vanished. Losing automatic
        // refresh is a degradation, not a failure: F5 still works.
    }
}

void RepositoryWatcher::start()
{
    const std::string git_dir = m_repository.git_dir();
    const std::string common_dir = m_repository.common_dir();

    if (git_dir.empty()) {
        return;
    }

    // Watch directories, not individual files. git replaces the index and HEAD by
    // renaming a temporary over them, which breaks a watch held on the file itself
    // but not one held on its directory.
    watch_directory(git_dir);

    if (common_dir != git_dir) {
        watch_directory(common_dir);
    }

    // Every ref update appends here, which makes it the single most reliable
    // "something happened" signal in a repository.
    watch_directory(Glib::build_filename(common_dir, "logs"));
    watch_directory(Glib::build_filename(common_dir, "refs"));

    // The working tree root catches files created or removed at the top level.
    // Deeper changes are picked up on the next explicit or write-driven refresh
    // rather than by watching the whole tree.
    watch_directory(m_repository.toplevel());
}

void RepositoryWatcher::set_enabled(bool enabled)
{
    m_enabled = enabled;
}

void RepositoryWatcher::schedule_refresh()
{
    if (!m_enabled) {
        return;
    }

    m_debounce.disconnect();
    m_debounce = Glib::signal_timeout().connect(sigc::mem_fun(*this, &RepositoryWatcher::emit_if_idle), s_debounce_ms);
}

bool RepositoryWatcher::emit_if_idle()
{
    if (m_repository.runner().is_writing()) {
        // The application caused this itself and is not finished yet. Refreshing now
        // would read a half-applied state and be immediately superseded.
        m_pending = true;
        return false;
    }

    m_signal_changed.emit();
    return false;
}

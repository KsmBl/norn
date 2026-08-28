/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "repositorylocator.h"

#include <giomm/file.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <glibmm/spawn.h>

namespace
{
std::string trimmed(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}
}

RepositoryLocator RepositoryLocator::locate(const std::string &path)
{
    RepositoryLocator locator;

    const Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(path);
    locator.m_requested_path = file->get_path();

    if (!Glib::file_test(locator.m_requested_path, Glib::FILE_TEST_IS_DIR)) {
        locator.m_result = Result::NoSuchDirectory;
        return locator;
    }

    // Ask git itself rather than walking upwards looking for a .git entry: that way
    // linked worktrees, submodules and $GIT_DIR overrides all resolve correctly.
    std::string out;
    std::string err;
    int status = 0;

    try {
        Glib::spawn_sync(locator.m_requested_path,
                         std::vector<std::string>{"git", "rev-parse", "--show-toplevel"},
                         Glib::SPAWN_SEARCH_PATH,
                         Glib::SlotSpawnChildSetup(),
                         &out,
                         &err,
                         &status);
    } catch (const Glib::Error &) {
        locator.m_result = Result::GitUnavailable;
        return locator;
    }

    if (status != 0) {
        locator.m_result = Result::NotARepository;
        return locator;
    }

    const std::string toplevel = trimmed(out);
    if (toplevel.empty()) {
        // A bare repository answers successfully but has no working tree.
        locator.m_result = Result::NotARepository;
        return locator;
    }

    locator.m_toplevel = toplevel;
    locator.m_result = Result::Found;
    return locator;
}

Glib::ustring RepositoryLocator::error_text() const
{
    switch (m_result) {
    case Result::Found:
        return {};
    case Result::NotARepository:
        return Glib::ustring::compose("“%1” is not inside a Git repository.", m_requested_path);
    case Result::NoSuchDirectory:
        return Glib::ustring::compose("“%1” is not an existing directory.", m_requested_path);
    case Result::GitUnavailable:
        return "Could not run git. Make sure it is installed and on your PATH.";
    }
    return {};
}

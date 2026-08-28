/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <glibmm/ustring.h>

#include <string>

/*!
 * Resolves a user-supplied path to the root of the git working tree containing it.
 *
 * Deliberately synchronous: it runs once at startup, before there is a Repository
 * to hang anything off, and the window cannot be built until the answer is known.
 */
class RepositoryLocator
{
public:
    enum class Result {
        /*! The path is inside a git working tree; toplevel() is its root. */
        Found,
        /*! The path exists but is not inside any git working tree. */
        NotARepository,
        /*! The path does not exist, or is not a directory. */
        NoSuchDirectory,
        /*! git could not be run at all. */
        GitUnavailable,
    };

    /*! Resolves @p path, following it up to the enclosing working tree root. */
    static RepositoryLocator locate(const std::string &path);

    Result result() const
    {
        return m_result;
    }

    /*! The working tree root, only meaningful when result() is Found. */
    const std::string &toplevel() const
    {
        return m_toplevel;
    }

    const std::string &requested_path() const
    {
        return m_requested_path;
    }

    bool is_found() const
    {
        return m_result == Result::Found;
    }

    /*! A human-readable explanation of a non-Found result. */
    Glib::ustring error_text() const;

private:
    Result m_result = Result::NoSuchDirectory;
    std::string m_toplevel;
    std::string m_requested_path;
};

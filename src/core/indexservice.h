/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <glibmm/ustring.h>
#include <sigc++/signal.h>

#include <string>
#include <vector>

class GitJob;
class Repository;

/*!
 * Moves changes between the working tree, the index and HEAD.
 *
 * Every path is passed as a literal pathspec on stdin rather than as an argument;
 * see Pathspec for why that is a correctness requirement and not a style choice.
 */
class IndexService
{
public:
    explicit IndexService(Repository &repository);

    /*! Stages @p paths, including untracked files and deletions. */
    void stage(const std::vector<std::string> &paths);

    /*! Unstages @p paths, leaving the working tree untouched. */
    void unstage(const std::vector<std::string> &paths);

    /*!
     * Throws away working tree changes to @p paths. Destructive and
     * unrecoverable, so callers must confirm first.
     */
    void discard(const std::vector<std::string> &paths);

    /*!
     * Deletes untracked files. Separate from discard() because `git restore` has
     * nothing to restore them from.
     */
    void delete_untracked(const std::vector<std::string> &paths);

    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_failed()
    {
        return m_signal_failed;
    }

private:
    /*! Wires the shared failure reporting and status refresh onto a write job. */
    void track(GitJob *job, const Glib::ustring &summary);

    Repository &m_repository;
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_failed;
};

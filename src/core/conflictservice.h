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
class OperationState;
class Repository;

/*!
 * Resolving conflicts and driving the operation they interrupted to a conclusion.
 *
 * Continue, skip and abort are shared by merge, rebase, cherry-pick and revert, so
 * they are dispatched from OperationState rather than duplicated per command.
 */
class ConflictService
{
public:
    ConflictService(Repository &repository, OperationState &operation_state);

    /*! Keeps our side of @p path entirely and marks it resolved. */
    void take_ours(const std::string &path);
    /*! Keeps their side of @p path entirely and marks it resolved. */
    void take_theirs(const std::string &path);
    /*! Marks @p path resolved as it currently stands in the working tree. */
    void mark_resolved(const std::string &path);
    /*! Resolves a delete/modify conflict by removing the file. */
    void remove_conflicted(const std::string &path);

    /*!
     * Rewrites @p path's conflict markers in the zdiff3 style, which shows the
     * common ancestor alongside both sides. Useful after a manual edit has gone
     * wrong, since it restores a clean starting point without losing the operation.
     */
    void restore_markers(const std::string &path);

    void continue_operation();
    /*! Skips the current commit. Only meaningful for rebase and cherry-pick. */
    void skip_operation();
    /*! Abandons the operation and returns to where it started. */
    void abort_operation();

    /*! True when the operation in progress supports skipping a step. */
    bool can_skip() const;

    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_failed()
    {
        return m_signal_failed;
    }

private:
    void run_resolution(std::vector<std::string> args, const std::string &path, const Glib::ustring &summary);
    void run_operation_step(const std::string &flag, const Glib::ustring &label, const Glib::ustring &summary);

    Repository &m_repository;
    OperationState &m_operation_state;

    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_failed;
};

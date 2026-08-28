/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "diffdocument.h"
#include "patchbuilder.h"

#include <glibmm/ustring.h>
#include <sigc++/signal.h>

#include <map>
#include <set>
#include <string>

class Repository;

/*! Which side of the index a diff describes. */
enum class DiffSide {
    /*! `git diff`: index versus worktree. */
    Unstaged,
    /*! `git diff --cached`: HEAD versus index. */
    Staged,
};

/*! How the content of a path should be obtained. */
enum class DiffMode {
    /*! An ordinary two-sided diff. */
    Changes,
    /*!
     * The whole file, rendered as additions.
     *
     * Used for an untracked file, which has no diff at all, and for a conflicted
     * one, whose real diff is a combined diff but whose useful content is the
     * working tree copy with its conflict markers in place.
     */
    WholeFile,
};

/*!
 * Produces diffs and applies partial ones.
 *
 * Every apply is preceded by a `--check` dry run. Partial patches are the one
 * place where a mistake corrupts the index rather than merely showing something
 * wrong, so a refusal is turned into an error message instead of a half-applied
 * tree.
 */
class DiffService
{
public:
    explicit DiffService(Repository &repository);

    /*! Requests the diff for @p path. The answer arrives as signal_diff_ready(). */
    void request_diff(const std::string &path, DiffSide side, DiffMode mode = DiffMode::Changes);

    void apply_hunks(const DiffFile &file, const std::set<int> &hunk_indexes, PatchBuilder::Direction direction);
    void apply_lines(const DiffFile &file, const std::map<int, std::set<int>> &selected_lines, PatchBuilder::Direction direction);

    sigc::signal<void(const std::string &, DiffSide, const DiffFile &)> &signal_diff_ready()
    {
        return m_signal_diff_ready;
    }
    /*! The requested path has no differences on that side. */
    sigc::signal<void(const std::string &, DiffSide)> &signal_diff_empty()
    {
        return m_signal_diff_empty;
    }
    sigc::signal<void()> &signal_applied()
    {
        return m_signal_applied;
    }
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_failed()
    {
        return m_signal_failed;
    }

private:
    void run_apply(const std::string &patch, PatchBuilder::Direction direction);

    Repository &m_repository;

    sigc::signal<void(const std::string &, DiffSide, const DiffFile &)> m_signal_diff_ready;
    sigc::signal<void(const std::string &, DiffSide)> m_signal_diff_empty;
    sigc::signal<void()> m_signal_applied;
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_failed;
};

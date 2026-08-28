/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "operationstate.h"

#include "repository.h"

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <cstdlib>
#include <fstream>

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

OperationState::OperationState(Repository &repository)
    : m_repository(repository)
{
}

std::string OperationState::git_path(const std::string &name) const
{
    // Sequencer and rebase state live in the per-worktree git directory, not the
    // common one, so a rebase in a linked worktree is not mistaken for one here.
    return Glib::build_filename(m_repository.git_dir(), name);
}

std::string OperationState::read_trimmed(const std::string &name) const
{
    std::ifstream file(git_path(name));
    if (!file) {
        return {};
    }
    return trimmed({std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()});
}

void OperationState::refresh()
{
    const Kind previous_kind = m_kind;
    const int previous_step = m_step;

    m_kind = Kind::None;
    m_is_interactive = false;
    m_rebase_head_name.clear();
    m_step = 0;
    m_step_count = 0;

    if (m_repository.git_dir().empty()) {
        return;
    }

    const auto exists = [this](const char *name) {
        return Glib::file_test(git_path(name), Glib::FILE_TEST_EXISTS);
    };

    // Order matters: a rebase that stops to let a conflict be resolved also leaves
    // CHERRY_PICK_HEAD behind, so the rebase has to win.
    if (exists("rebase-merge")) {
        m_kind = Kind::Rebase;
        m_is_interactive = exists("rebase-merge/interactive");

        std::string head_name = read_trimmed("rebase-merge/head-name");
        // Stored as a full ref; the short name is what belongs in a banner.
        if (head_name.rfind("refs/heads/", 0) == 0) {
            head_name = head_name.substr(11);
        }
        m_rebase_head_name = head_name;

        m_step = std::atoi(read_trimmed("rebase-merge/msgnum").c_str());
        m_step_count = std::atoi(read_trimmed("rebase-merge/end").c_str());
    } else if (exists("rebase-apply")) {
        m_kind = Kind::RebaseApply;
        m_step = std::atoi(read_trimmed("rebase-apply/next").c_str());
        m_step_count = std::atoi(read_trimmed("rebase-apply/last").c_str());
    } else if (exists("MERGE_HEAD")) {
        m_kind = Kind::Merge;
    } else if (exists("CHERRY_PICK_HEAD")) {
        m_kind = Kind::CherryPick;
    } else if (exists("REVERT_HEAD")) {
        m_kind = Kind::Revert;
    } else if (exists("BISECT_LOG")) {
        m_kind = Kind::Bisect;
    }

    if (m_kind != previous_kind || m_step != previous_step) {
        m_signal_changed.emit();
    }
}

std::string OperationState::command() const
{
    switch (m_kind) {
    case Kind::Merge:
        return "merge";
    case Kind::Rebase:
    case Kind::RebaseApply:
        return "rebase";
    case Kind::CherryPick:
        return "cherry-pick";
    case Kind::Revert:
        return "revert";
    case Kind::Bisect:
        return "bisect";
    case Kind::None:
        break;
    }
    return {};
}

bool OperationState::can_skip() const
{
    switch (m_kind) {
    case Kind::Rebase:
    case Kind::RebaseApply:
    case Kind::CherryPick:
    case Kind::Revert:
        return true;
    default:
        // A merge is a single step; there is nothing to skip past.
        return false;
    }
}

Glib::ustring OperationState::description() const
{
    switch (m_kind) {
    case Kind::None:
        return {};

    case Kind::Merge:
        return "A merge is in progress.";

    case Kind::Rebase:
    case Kind::RebaseApply: {
        Glib::ustring text = m_rebase_head_name.empty() ? Glib::ustring("A rebase is in progress.")
                                                        : Glib::ustring::compose("Rebasing “%1”.", m_rebase_head_name);
        if (m_step_count > 0) {
            text += Glib::ustring::compose(" Step %1 of %2.", m_step, m_step_count);
        }
        return text;
    }

    case Kind::CherryPick:
        return "A cherry-pick is in progress.";

    case Kind::Revert:
        return "A revert is in progress.";

    case Kind::Bisect:
        return "A bisect is in progress.";
    }
    return {};
}

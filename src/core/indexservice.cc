/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "indexservice.h"

#include "pathspec.h"
#include "repository.h"

namespace
{
/*! Appends the stdin-pathspec flags to a command's arguments. */
std::vector<std::string> with_pathspec(std::vector<std::string> args)
{
    const std::vector<std::string> flags = Pathspec::from_stdin_arguments();
    args.insert(args.end(), flags.begin(), flags.end());
    return args;
}
}

IndexService::IndexService(Repository &repository)
    : m_repository(repository)
{
}

void IndexService::track(GitJob *job, const Glib::ustring &summary)
{
    job->signal_finished().connect([this, job, summary] {
        if (!job->succeeded()) {
            m_signal_failed.emit(summary, job->error_text());
        }
        // Refresh either way: a partial failure still changed the index.
        m_repository.refresh_status();
    });
}

void IndexService::stage(const std::vector<std::string> &paths)
{
    if (paths.empty()) {
        return;
    }

    // --all so that deletions are staged too, not just modifications and additions.
    GitCommand command(GitLane::Write, with_pathspec({"add", "--all"}));
    command.m_stdin = Pathspec::encode(paths);
    command.m_label = "Staging";

    track(m_repository.runner().run(command), "Could not stage the selected files.");
}

void IndexService::unstage(const std::vector<std::string> &paths)
{
    if (paths.empty()) {
        return;
    }

    // `restore --staged` rather than `reset`, because it behaves correctly on an
    // unborn branch where there is no HEAD to reset against.
    GitCommand command(GitLane::Write, with_pathspec({"restore", "--staged"}));
    command.m_stdin = Pathspec::encode(paths);
    command.m_label = "Unstaging";

    track(m_repository.runner().run(command), "Could not unstage the selected files.");
}

void IndexService::discard(const std::vector<std::string> &paths)
{
    if (paths.empty()) {
        return;
    }

    GitCommand command(GitLane::Write, with_pathspec({"restore", "--worktree"}));
    command.m_stdin = Pathspec::encode(paths);
    command.m_label = "Discarding changes";

    track(m_repository.runner().run(command), "Could not discard the selected changes.");
}

void IndexService::delete_untracked(const std::vector<std::string> &paths)
{
    if (paths.empty()) {
        return;
    }

    // -f is required for clean to do anything at all; -d so untracked directories
    // go too. No -x: ignored files are excluded deliberately.
    GitCommand command(GitLane::Write, with_pathspec({"clean", "-f", "-d"}));
    command.m_stdin = Pathspec::encode(paths);
    command.m_label = "Deleting untracked files";

    track(m_repository.runner().run(command), "Could not delete the selected files.");
}

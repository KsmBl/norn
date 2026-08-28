/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "conflictservice.h"

#include "operationstate.h"
#include "pathspec.h"
#include "repository.h"

ConflictService::ConflictService(Repository &repository, OperationState &operation_state)
    : m_repository(repository)
    , m_operation_state(operation_state)
{
}

bool ConflictService::can_skip() const
{
    return m_operation_state.can_skip();
}

void ConflictService::run_resolution(std::vector<std::string> args, const std::string &path, const Glib::ustring &summary)
{
    const std::vector<std::string> flags = Pathspec::from_stdin_arguments();
    args.insert(args.end(), flags.begin(), flags.end());

    GitCommand command(GitLane::Write, args);
    command.m_stdin = Pathspec::encode({path});
    command.m_label = summary;

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job, summary] {
        if (!job->succeeded()) {
            m_signal_failed.emit(summary, job->error_text());
        }
        m_repository.refresh_status();
    });
}

void ConflictService::take_ours(const std::string &path)
{
    // checkout --ours writes our stage into the working tree; the file still counts
    // as conflicted until it is added, which is what actually clears the stages.
    std::vector<std::string> args{"checkout", "--ours"};
    const std::vector<std::string> flags = Pathspec::from_stdin_arguments();
    args.insert(args.end(), flags.begin(), flags.end());

    GitCommand command(GitLane::Write, args);
    command.m_stdin = Pathspec::encode({path});
    command.m_label = "Keeping our version";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job, path] {
        if (!job->succeeded()) {
            m_signal_failed.emit("Could not keep our version of the file.", job->error_text());
            m_repository.refresh_status();
            return;
        }
        mark_resolved(path);
    });
}

void ConflictService::take_theirs(const std::string &path)
{
    std::vector<std::string> args{"checkout", "--theirs"};
    const std::vector<std::string> flags = Pathspec::from_stdin_arguments();
    args.insert(args.end(), flags.begin(), flags.end());

    GitCommand command(GitLane::Write, args);
    command.m_stdin = Pathspec::encode({path});
    command.m_label = "Keeping their version";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job, path] {
        if (!job->succeeded()) {
            m_signal_failed.emit("Could not keep their version of the file.", job->error_text());
            m_repository.refresh_status();
            return;
        }
        mark_resolved(path);
    });
}

void ConflictService::mark_resolved(const std::string &path)
{
    // Adding a conflicted path collapses its three stages into one, which is what
    // "resolved" means to git.
    run_resolution({"add"}, path, "Marking the file resolved");
}

void ConflictService::remove_conflicted(const std::string &path)
{
    run_resolution({"rm", "--quiet"}, path, "Removing the file");
}

void ConflictService::restore_markers(const std::string &path)
{
    // zdiff3 keeps the common ancestor between the two sides, which is usually what
    // makes an otherwise baffling conflict readable.
    run_resolution({"checkout", "--conflict=zdiff3"}, path, "Restoring the conflict markers");
}

void ConflictService::run_operation_step(const std::string &flag, const Glib::ustring &label, const Glib::ustring &summary)
{
    const std::string command = m_operation_state.command();
    if (command.empty()) {
        return;
    }

    // core.editor=true makes the editor a no-op that exits successfully. Continuing
    // a merge or rebase would otherwise try to open one for the commit message, and
    // the message git has already prepared is the right one to accept unchanged.
    GitCommand git(GitLane::Write, {"-c", "core.editor=true", command, flag});
    git.m_label = label;

    GitJob *job = m_repository.runner().run(git);
    job->signal_finished().connect([this, job, summary] {
        if (!job->succeeded()) {
            m_signal_failed.emit(summary, job->error_text());
        }
        m_repository.refresh_status();
    });
}

void ConflictService::continue_operation()
{
    run_operation_step("--continue", "Continuing", "Could not continue. There may still be conflicts to resolve.");
}

void ConflictService::skip_operation()
{
    run_operation_step("--skip", "Skipping this step", "Could not skip this step.");
}

void ConflictService::abort_operation()
{
    run_operation_step("--abort", "Aborting", "Could not abort.");
}

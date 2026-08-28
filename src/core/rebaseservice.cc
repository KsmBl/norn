/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "rebaseservice.h"

#include "editorbridge.h"
#include "repository.h"

#include <glibmm/miscutils.h>

#include <fstream>

namespace
{
/*! Quotes a path for the shell git runs its sequence editor through. */
std::string shell_quote(const std::string &path)
{
    std::string quoted = "'";
    for (const char c : path) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}
}

RebaseService::RebaseService(Repository &repository, EditorBridge &editor_bridge)
    : m_repository(repository)
    , m_editor_bridge(editor_bridge)
{
}

void RebaseService::request_plan(const std::string &upstream)
{
    // --reverse because a todo list is applied oldest first, whereas git log
    // reports newest first.
    GitCommand command(GitLane::Read, {"log", "--reverse", "--no-merges", "--format=%h%x1f%s", upstream + "..HEAD"});
    command.m_label = "Listing commits to rebase";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job, upstream] {
        if (!job->succeeded()) {
            m_signal_failed.emit("Could not work out which commits to rebase.", job->error_text());
            return;
        }

        std::vector<RebaseStep> steps;

        const std::string &output = job->stdout_data();
        std::size_t start = 0;
        while (start < output.size()) {
            const std::size_t end = output.find('\n', start);
            const std::string line = output.substr(start, end == std::string::npos ? std::string::npos : end - start);

            const std::size_t separator = line.find('\x1f');
            if (separator != std::string::npos) {
                RebaseStep step;
                step.m_action = RebaseStep::Action::Pick;
                step.m_commit = line.substr(0, separator);
                step.m_subject = line.substr(separator + 1);
                steps.push_back(step);
            }

            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }

        m_signal_plan_ready.emit(upstream, steps);
    });
}

void RebaseService::start_interactive_rebase(const std::string &upstream, const std::vector<RebaseStep> &steps)
{
    if (steps.empty()) {
        return;
    }

    // Written somewhere git can read it for the duration of the rebase.
    m_todo_path = Glib::build_filename(Glib::get_tmp_dir(), "norn-todo-" + std::to_string(g_random_int()));

    std::ofstream file(m_todo_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        m_signal_failed.emit("Could not prepare the rebase plan.", m_todo_path);
        return;
    }
    const std::string todo = RebaseTodo::render(steps);
    file.write(todo.data(), static_cast<std::streamsize>(todo.size()));
    file.close();

    // The plan is already decided, so the sequence editor only has to put it in
    // place. `cp` is enough, and avoids needing an editor round trip at all for the
    // case that matters.
    const std::string sequence_editor = "cp -- " + shell_quote(m_todo_path);

    GitCommand command(GitLane::Write, {"-c", "sequence.editor=" + sequence_editor, "rebase", "--interactive", upstream});
    command.m_label = "Rebasing";
    // The plan is delivered by the copying sequence editor, but a reword step still
    // needs a real editor, which the helper provides.
    m_editor_bridge.apply_to(command);

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this] {
        // A rebase that stops on a conflict exits non-zero; that is an outcome
        // rather than a failure, and the operation banner reports it from the
        // repository state instead.
        m_repository.refresh_status();
    });
}

void RebaseService::cherry_pick(const std::string &commit)
{
    GitCommand command(GitLane::Write, {"-c", "core.editor=true", "cherry-pick", commit});
    command.m_label = "Cherry-picking " + commit;

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this] {
        m_repository.refresh_status();
    });
}

void RebaseService::revert(const std::string &commit)
{
    // --no-edit: the message git generates names the reverted commit and is exactly
    // right, so there is nothing to open an editor for.
    GitCommand command(GitLane::Write, {"revert", "--no-edit", commit});
    command.m_label = "Reverting " + commit;

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this] {
        m_repository.refresh_status();
    });
}

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "commitservice.h"

#include "repository.h"

CommitService::CommitService(Repository &repository)
    : m_repository(repository)
{
}

void CommitService::commit(const CommitOptions &options)
{
    std::vector<std::string> args{"commit", "-F", "-"};

    // Set explicitly so a user with commit.cleanup=strip does not silently lose
    // lines beginning with '#' from a message they typed deliberately.
    args.emplace_back("--cleanup=whitespace");

    if (options.m_amend) {
        args.emplace_back("--amend");
    }
    if (options.m_sign_off) {
        args.emplace_back("--signoff");
    }
    if (options.m_no_verify) {
        args.emplace_back("--no-verify");
    }
    if (options.m_reset_author) {
        args.emplace_back("--reset-author");
    }
    if (options.m_allow_empty) {
        args.emplace_back("--allow-empty");
    }

    GitCommand command(GitLane::Write, args);
    command.m_stdin = options.m_message;
    command.m_label = options.m_amend ? "Amending the last commit" : "Committing";

    const bool amending = options.m_amend;
    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job, amending] {
        if (job->succeeded()) {
            m_signal_committed.emit();
        } else {
            // Hook output arrives on stderr and is the whole point of the message: a
            // rejected commit is usually a pre-commit hook explaining itself.
            m_signal_failed.emit(amending ? "Could not amend the last commit." : "Could not commit.", job->error_text());
        }
        m_repository.refresh_status();
    });
}

void CommitService::request_head_message()
{
    GitCommand command(GitLane::Read, {"log", "-1", "--format=%B"});
    command.m_label = "Reading the last commit message";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job] {
        if (!job->succeeded()) {
            // An unborn branch has no HEAD to read; not an error worth showing.
            m_signal_head_message_ready.emit({});
            return;
        }

        std::string message = job->stdout_data();
        while (!message.empty() && message.back() == '\n') {
            message.pop_back();
        }
        m_signal_head_message_ready.emit(message);
    });
}

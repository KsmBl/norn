/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "remoteservice.h"

#include "repository.h"

#include <utility>

namespace
{
/*! How many commits to list in a force push preview before truncating. */
constexpr int s_preview_limit = 50;

std::string ref_for_branch(const std::string &branch)
{
    return "refs/heads/" + branch;
}

std::string remote_tracking_ref(const std::string &remote, const std::string &branch)
{
    return "refs/remotes/" + remote + "/" + branch;
}

std::vector<std::string> split_lines(const std::string &value)
{
    std::vector<std::string> lines;
    std::size_t start = 0;

    while (start < value.size()) {
        const std::size_t end = value.find('\n', start);
        std::string line = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return lines;
}

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

RemoteService::RemoteService(Repository &repository)
    : m_repository(repository)
{
}

void RemoteService::track_network(GitJob *job, const Glib::ustring &summary)
{
    job->signal_progress().connect([this](const Glib::ustring &phase, int current, int total) {
        m_signal_progress.emit(phase, current, total);
    });

    job->signal_stderr_line().connect([this](const Glib::ustring &line) {
        // Servers speak to the user through "remote:" lines; GitHub's pull request
        // URL arrives this way and is worth surfacing rather than burying.
        if (line.compare(0, 7, "remote:") == 0) {
            const Glib::ustring message = trimmed(line.substr(7));
            if (!message.empty()) {
                m_signal_remote_message.emit(message);
            }
        }
    });

    job->signal_finished().connect([this, job, summary] {
        if (!job->succeeded()) {
            m_signal_failed.emit(summary, job->error_text());
        }
    });
}

void RemoteService::fetch(const std::string &remote, bool prune)
{
    std::vector<std::string> args{"fetch", "--progress"};
    if (prune) {
        args.emplace_back("--prune");
    }
    if (remote.empty()) {
        args.emplace_back("--all");
    } else {
        args.push_back(remote);
    }

    GitCommand command(GitLane::Network, args);
    command.m_wants_progress = true;
    command.m_label = remote.empty() ? "Fetching all remotes" : "Fetching " + remote;

    GitJob *job = m_repository.runner().run(command);
    track_network(job, "Could not fetch.");

    job->signal_finished().connect([this, job] {
        if (job->succeeded()) {
            m_signal_fetched.emit();
        }
        m_repository.refresh_status();
    });
}

void RemoteService::push(const std::string &remote, const std::string &branch, bool set_upstream, bool push_tags)
{
    std::vector<std::string> args{"push", "--progress"};
    if (set_upstream) {
        args.emplace_back("--set-upstream");
    }
    if (push_tags) {
        args.emplace_back("--follow-tags");
    }
    // The explicit refspec avoids depending on push.default, which varies.
    args.push_back(remote);
    args.push_back("HEAD:" + ref_for_branch(branch));

    GitCommand command(GitLane::Network, args);
    command.m_wants_progress = true;
    command.m_label = "Pushing " + branch + " to " + remote;

    GitJob *job = m_repository.runner().run(command);
    track_network(job, "Could not push.");

    job->signal_finished().connect([this, job] {
        if (job->succeeded()) {
            m_signal_pushed.emit();
        }
        m_repository.refresh_status();
    });
}

void RemoteService::prepare_force_push(const std::string &remote, const std::string &branch)
{
    // Fetch first. Without this the lease would be taken against a remote-tracking
    // ref that may be hours stale, which is exactly the case --force-with-lease is
    // supposed to protect against.
    GitCommand fetch_command(GitLane::Network, {"fetch", "--progress", remote, branch});
    fetch_command.m_wants_progress = true;
    fetch_command.m_label = "Checking what " + remote + " currently holds";

    GitJob *fetch_job = m_repository.runner().run(fetch_command);
    fetch_job->signal_progress().connect([this](const Glib::ustring &phase, int current, int total) {
        m_signal_progress.emit(phase, current, total);
    });

    fetch_job->signal_finished().connect([this, fetch_job, remote, branch] {
        ForcePushPreview preview;
        preview.m_remote = remote;
        preview.m_branch = branch;

        if (!fetch_job->succeeded()) {
            m_signal_failed.emit(Glib::ustring::compose("Could not contact %1 to check what it holds.", remote), fetch_job->error_text());
            return;
        }

        // Resolve what the remote branch points at now.
        GitCommand resolve(GitLane::Read, {"rev-parse", "--verify", "--quiet", remote_tracking_ref(remote, branch)});
        resolve.m_label = "Resolving the remote branch";

        GitJob *resolve_job = m_repository.runner().run(resolve);
        resolve_job->signal_finished().connect([this, resolve_job, preview]() mutable {
            const std::string oid = trimmed(resolve_job->stdout_data());
            if (!resolve_job->succeeded() || oid.empty()) {
                // No such branch on the remote yet: an ordinary push will do.
                preview.m_remote_branch_exists = false;
                m_signal_force_push_preview_ready.emit(preview);
                return;
            }

            preview.m_remote_branch_exists = true;
            preview.m_expected_oid = oid;
            collect_preview_commits(preview);
        });
    });
}

void RemoteService::collect_preview_commits(ForcePushPreview preview)
{
    GitCommand added(GitLane::Read,
                     {"log", "--oneline", "--no-decorate", "--max-count=" + std::to_string(s_preview_limit), preview.m_expected_oid + "..HEAD"});
    added.m_label = "Listing commits to add";

    GitJob *added_job = m_repository.runner().run(added);
    added_job->signal_finished().connect([this, added_job, preview]() mutable {
        preview.m_added = split_lines(added_job->stdout_data());

        // Commits the push would destroy. This is the list that matters.
        GitCommand removed(GitLane::Read,
                           {"log", "--oneline", "--no-decorate", "--max-count=" + std::to_string(s_preview_limit), "HEAD.." + preview.m_expected_oid});
        removed.m_label = "Listing commits that would be lost";

        GitJob *removed_job = m_repository.runner().run(removed);
        removed_job->signal_finished().connect([this, removed_job, preview]() mutable {
            preview.m_removed = split_lines(removed_job->stdout_data());
            m_signal_force_push_preview_ready.emit(preview);
        });
    });
}

void RemoteService::force_push(const ForcePushPreview &preview)
{
    std::vector<std::string> args{"push", "--progress"};

    if (preview.m_remote_branch_exists) {
        // The explicit :<oid> form. The bare --force-with-lease only compares against
        // the local remote-tracking ref, so it still passes when that ref is stale.
        args.push_back("--force-with-lease=" + ref_for_branch(preview.m_branch) + ":" + preview.m_expected_oid);
        // Additionally require that local history contains the leased tip, closing
        // the gap where another tool fetched behind our back.
        args.emplace_back("--force-if-includes");
    }

    args.push_back(preview.m_remote);
    args.push_back("HEAD:" + ref_for_branch(preview.m_branch));

    GitCommand command(GitLane::Network, args);
    command.m_wants_progress = true;
    command.m_label = "Force pushing " + preview.m_branch + " to " + preview.m_remote;

    GitJob *job = m_repository.runner().run(command);
    track_network(job, "Could not force push. The remote may have moved since it was checked.");

    job->signal_finished().connect([this, job] {
        if (job->succeeded()) {
            m_signal_pushed.emit();
        }
        m_repository.refresh_status();
    });
}

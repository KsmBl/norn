/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "repository.h"

#include "parsers/statusparser.h"
#include "settings.h"

#include <glibmm/miscutils.h>

#include <utility>

namespace
{
/*! Collapses a burst of refresh requests into a single query. */
constexpr const char *s_status_dedupe_key = "status";

/*! Catches a hung askpass or a stuck GPG pinentry rather than waiting forever. */
constexpr int s_read_timeout_ms = 10000;

std::string trimmed(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::vector<std::string> split_lines(const std::string &value)
{
    std::vector<std::string> lines;
    std::size_t start = 0;

    while (start < value.size()) {
        const std::size_t end = value.find('\n', start);
        const std::string line = trimmed(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return lines;
}
}

Repository::Repository(std::string toplevel)
    : m_toplevel(std::move(toplevel))
    , m_runner(std::make_unique<GitRunner>(m_toplevel))
{
}

Repository::~Repository() = default;

std::string Repository::display_name() const
{
    const std::string name = Glib::path_get_basename(m_toplevel);
    return name.empty() ? m_toplevel : name;
}

void Repository::open()
{
    resolve_paths();
}

void Repository::resolve_paths()
{
    GitCommand command(GitLane::Read, {"rev-parse", "--absolute-git-dir", "--git-common-dir"});
    command.m_label = "Resolving repository paths";
    command.m_timeout_ms = s_read_timeout_ms;

    GitJob *job = m_runner->run(command);
    job->signal_finished().connect([this, job] {
        if (!job->succeeded()) {
            m_signal_operation_failed.emit("Could not resolve the repository layout.", job->error_text());
            return;
        }

        const std::vector<std::string> lines = split_lines(job->stdout_data());
        if (!lines.empty()) {
            m_git_dir = lines[0];
        }
        if (lines.size() > 1) {
            // --git-common-dir may answer relatively; anchor it to the git dir.
            m_common_dir = Glib::path_is_absolute(lines[1]) ? lines[1] : Glib::build_filename(m_git_dir, lines[1]);
        } else {
            m_common_dir = m_git_dir;
        }

        m_signal_ready.emit();
        refresh_status();
    });
}

void Repository::refresh_status()
{
    std::vector<std::string> args;
    if (Settings::instance().avoid_optional_locks()) {
        // Costly: git can no longer write back the refreshed index stat cache, so
        // every status re-stats the whole tree. Only worth it when another process
        // may be holding the repository.
        args.emplace_back("--no-optional-locks");
    }

    const std::vector<std::string> status_args = StatusParser::arguments();
    args.insert(args.end(), status_args.begin(), status_args.end());

    GitCommand command(GitLane::Read, args);
    command.m_label = "Reading status";
    command.m_dedupe_key = s_status_dedupe_key;
    command.m_timeout_ms = s_read_timeout_ms;

    GitJob *job = m_runner->run(command);
    job->signal_finished().connect([this, job] {
        if (!job->succeeded()) {
            m_signal_operation_failed.emit("Could not read the repository status.", job->error_text());
            return;
        }

        m_status = StatusParser::parse(job->stdout_data());
        m_has_status = true;
        m_signal_status_changed.emit();
    });
}

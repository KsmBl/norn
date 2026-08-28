/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "diffservice.h"

#include "parsers/diffparser.h"
#include "pathspec.h"
#include "repository.h"
#include "settings.h"

DiffService::DiffService(Repository &repository)
    : m_repository(repository)
{
}

void DiffService::request_diff(const std::string &path, DiffSide side, DiffMode mode)
{
    std::vector<std::string> args{"diff"};
    if (side == DiffSide::Staged) {
        args.emplace_back("--cached");
    }

    const std::vector<std::string> diff_args = DiffParser::arguments(Settings::instance().context_lines());
    args.insert(args.end(), diff_args.begin(), diff_args.end());

    const bool whole_file = mode == DiffMode::WholeFile;

    if (whole_file) {
        // --no-index compares two paths directly rather than resolving pathspecs,
        // so a filename containing glob characters needs no escaping here.
        args.emplace_back("--no-index");
        args.emplace_back("--");
        args.emplace_back("/dev/null");
        args.push_back(path);
    } else {
        // Literal, so a filename containing glob characters resolves to itself alone.
        args.emplace_back("--");
        args.push_back(Pathspec::literal(path));
    }

    GitCommand command(GitLane::Read, args);
    command.m_label = "Reading the diff";
    // One in-flight diff per file, side and mode; scrolling must not queue dozens.
    command.m_dedupe_key = "diff:" + std::to_string(static_cast<int>(side)) + ":" + std::to_string(static_cast<int>(mode)) + ":" + path;

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job, path, side, whole_file] {
        // --no-index reports "differences found" as exit code 1, which is the normal
        // outcome here rather than a failure.
        const bool acceptable = job->succeeded() || (whole_file && job->exit_code() == 1);
        if (!acceptable) {
            m_signal_failed.emit("Could not read the diff.", job->error_text());
            return;
        }

        const DiffDocument document = DiffParser::parse(job->stdout_data());
        if (document.empty()) {
            m_signal_diff_empty.emit(path, side);
            return;
        }

        m_signal_diff_ready.emit(path, side, document.m_files.front());
    });
}

void DiffService::apply_hunks(const DiffFile &file, const std::set<int> &hunk_indexes, PatchBuilder::Direction direction)
{
    run_apply(PatchBuilder::build_for_hunks(file, hunk_indexes, direction), direction);
}

void DiffService::apply_lines(const DiffFile &file, const std::map<int, std::set<int>> &selected_lines, PatchBuilder::Direction direction)
{
    run_apply(PatchBuilder::build_for_lines(file, selected_lines, direction), direction);
}

void DiffService::run_apply(const std::string &patch, PatchBuilder::Direction direction)
{
    if (patch.empty()) {
        return;
    }

    const bool unidiff_zero = PatchBuilder::needs_unidiff_zero(patch);

    // Dry run first. A patch git would refuse is far better caught before it has
    // touched anything than diagnosed afterwards from a partially updated index.
    GitCommand check(GitLane::Write, PatchBuilder::apply_arguments(direction, true, unidiff_zero));
    check.m_stdin = patch;
    check.m_label = "Checking whether the patch applies";

    GitJob *check_job = m_repository.runner().run(check);
    check_job->signal_finished().connect([this, check_job, patch, direction, unidiff_zero] {
        if (!check_job->succeeded()) {
            m_signal_failed.emit("That selection cannot be applied on its own. The file may have changed since the diff was read.",
                                 check_job->error_text());
            m_repository.refresh_status();
            return;
        }

        GitCommand apply(GitLane::Write, PatchBuilder::apply_arguments(direction, false, unidiff_zero));
        apply.m_stdin = patch;
        apply.m_label = "Applying the selected changes";

        GitJob *apply_job = m_repository.runner().run(apply);
        apply_job->signal_finished().connect([this, apply_job] {
            if (apply_job->succeeded()) {
                m_signal_applied.emit();
            } else {
                m_signal_failed.emit("Could not apply the selected changes.", apply_job->error_text());
            }
            m_repository.refresh_status();
        });
    });
}

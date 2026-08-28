/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "refservice.h"

#include "parsers/refparser.h"
#include "repository.h"

RefService::RefService(Repository &repository)
    : m_repository(repository)
{
}

void RefService::refresh()
{
    load_refs();
    load_stashes();
    load_worktrees();
    load_submodules();
}

void RefService::track_write(GitJob *job, const Glib::ustring &summary)
{
    job->signal_finished().connect([this, job, summary] {
        if (!job->succeeded()) {
            m_signal_failed.emit(summary, job->error_text());
        }
        m_repository.refresh_status();
        refresh();
    });
}

void RefService::load_refs()
{
    GitCommand command(GitLane::Read, RefParser::ref_arguments());
    command.m_label = "Reading branches and tags";
    command.m_dedupe_key = "for-each-ref";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job] {
        if (!job->succeeded()) {
            m_signal_failed.emit("Could not read the branches and tags.", job->error_text());
            return;
        }
        m_refs = RefParser::parse_refs(job->stdout_data());
        m_signal_changed.emit();
    });
}

void RefService::load_stashes()
{
    GitCommand command(GitLane::Read, RefParser::stash_arguments());
    command.m_label = "Reading the stash list";
    command.m_dedupe_key = "stash-list";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job] {
        // A repository with no stash ref answers with nothing, which is not an error.
        m_stashes = job->succeeded() ? RefParser::parse_stashes(job->stdout_data()) : std::vector<StashRecord>();
        m_signal_changed.emit();
    });
}

void RefService::load_worktrees()
{
    GitCommand command(GitLane::Read, RefParser::worktree_arguments());
    command.m_label = "Reading worktrees";
    command.m_dedupe_key = "worktree-list";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job] {
        if (job->succeeded()) {
            m_worktrees = RefParser::parse_worktrees(job->stdout_data(), m_repository.toplevel());
            m_signal_changed.emit();
        }
    });
}

void RefService::load_submodules()
{
    GitCommand command(GitLane::Read, RefParser::submodule_arguments());
    command.m_label = "Reading submodules";
    command.m_dedupe_key = "submodule-status";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job] {
        // A repository without submodules answers with nothing, not an error.
        m_submodules = job->succeeded() ? RefParser::parse_submodules(job->stdout_data()) : std::vector<SubmoduleRecord>();
        m_signal_changed.emit();
    });
}

void RefService::checkout(const std::string &name)
{
    // `switch` rather than `checkout`: it refuses to silently detach HEAD when the
    // argument is ambiguous between a branch and a path.
    GitCommand command(GitLane::Write, {"switch", "--", name});
    command.m_label = "Switching to " + name;

    track_write(m_repository.runner().run(command), Glib::ustring::compose("Could not switch to %1.", name));
}

void RefService::checkout_detached(const std::string &commit)
{
    GitCommand command(GitLane::Write, {"switch", "--detach", commit});
    command.m_label = "Checking out " + commit;

    track_write(m_repository.runner().run(command), Glib::ustring::compose("Could not check out %1.", commit));
}

void RefService::create_branch(const std::string &name, const std::string &start_point, bool checkout_after)
{
    std::vector<std::string> args;
    if (checkout_after) {
        args = {"switch", "--create", name};
    } else {
        args = {"branch", name};
    }
    if (!start_point.empty()) {
        args.push_back(start_point);
    }

    GitCommand command(GitLane::Write, args);
    command.m_label = "Creating branch " + name;

    track_write(m_repository.runner().run(command), Glib::ustring::compose("Could not create branch %1.", name));
}

void RefService::delete_branch(const std::string &name)
{
    // Lowercase -d, never -D. git refuses when the branch holds commits that exist
    // nowhere else, and that refusal is the only thing standing between the user
    // and losing them.
    GitCommand command(GitLane::Write, {"branch", "-d", name});
    command.m_label = "Deleting branch " + name;

    track_write(m_repository.runner().run(command),
                Glib::ustring::compose("Could not delete branch %1. It may have commits that are not merged anywhere else.", name));
}

void RefService::rename_branch(const std::string &old_name, const std::string &new_name)
{
    GitCommand command(GitLane::Write, {"branch", "-m", old_name, new_name});
    command.m_label = "Renaming " + old_name;

    track_write(m_repository.runner().run(command), Glib::ustring::compose("Could not rename %1.", old_name));
}

void RefService::merge(const std::string &name, bool no_fast_forward)
{
    std::vector<std::string> args{"merge"};
    if (no_fast_forward) {
        args.emplace_back("--no-ff");
    }
    // --no-edit so git never tries to open an editor for the merge message.
    args.emplace_back("--no-edit");
    args.push_back(name);

    GitCommand command(GitLane::Write, args);
    command.m_label = "Merging " + name;

    // A merge that stops on conflicts exits non-zero, which is an outcome rather
    // than a failure; the operation banner picks it up from the repository state.
    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this] {
        m_repository.refresh_status();
        refresh();
    });
}

void RefService::create_tag(const std::string &name, const std::string &target, const std::string &message)
{
    std::vector<std::string> args{"tag"};
    if (!message.empty()) {
        // An annotated tag carries its own object, author and message.
        args.emplace_back("--annotate");
        args.emplace_back("--file");
        args.emplace_back("-");
    }
    args.push_back(name);
    if (!target.empty()) {
        args.push_back(target);
    }

    GitCommand command(GitLane::Write, args);
    command.m_stdin = message;
    command.m_label = "Creating tag " + name;

    track_write(m_repository.runner().run(command), Glib::ustring::compose("Could not create tag %1.", name));
}

void RefService::delete_tag(const std::string &name)
{
    GitCommand command(GitLane::Write, {"tag", "-d", name});
    command.m_label = "Deleting tag " + name;

    track_write(m_repository.runner().run(command), Glib::ustring::compose("Could not delete tag %1.", name));
}

void RefService::stash_push(const std::string &message, bool include_untracked)
{
    std::vector<std::string> args{"stash", "push"};
    if (include_untracked) {
        args.emplace_back("--include-untracked");
    }
    if (!message.empty()) {
        args.emplace_back("--message");
        args.push_back(message);
    }

    GitCommand command(GitLane::Write, args);
    command.m_label = "Stashing the working tree";

    track_write(m_repository.runner().run(command), "Could not stash the working tree.");
}

void RefService::run_stash_command(const std::string &subcommand, int index, const std::string &expected_object_name, const Glib::ustring &summary)
{
    // stash@{n} is a position, not an identity. Anything that changed the list since
    // the UI read it has renumbered the entries, and acting on a stale selector
    // would silently drop or apply the wrong one, with no way to get it back.
    if (index < 0 || static_cast<std::size_t>(index) >= m_stashes.size() || m_stashes[static_cast<std::size_t>(index)].m_object_name != expected_object_name) {
        m_signal_failed.emit("The stash list changed. Refresh and try again.", {});
        refresh();
        return;
    }

    GitCommand command(GitLane::Write, {"stash", subcommand, m_stashes[static_cast<std::size_t>(index)].m_selector});
    command.m_label = "Running stash " + subcommand;

    track_write(m_repository.runner().run(command), summary);
}

void RefService::stash_pop(int index, const std::string &expected_object_name)
{
    run_stash_command("pop", index, expected_object_name, "Could not restore the stash.");
}

void RefService::stash_apply(int index, const std::string &expected_object_name)
{
    run_stash_command("apply", index, expected_object_name, "Could not apply the stash.");
}

void RefService::stash_drop(int index, const std::string &expected_object_name)
{
    run_stash_command("drop", index, expected_object_name, "Could not drop the stash.");
}

void RefService::add_worktree(const std::string &path, const std::string &branch, bool create_branch)
{
    std::vector<std::string> args{"worktree", "add"};
    if (create_branch) {
        args.emplace_back("-b");
        args.push_back(branch);
        args.push_back(path);
    } else if (branch.empty()) {
        // No branch given: detach, since two worktrees cannot share one branch.
        args.emplace_back("--detach");
        args.push_back(path);
    } else {
        args.push_back(path);
        args.push_back(branch);
    }

    GitCommand command(GitLane::Write, args);
    command.m_label = "Adding a worktree at " + path;

    track_write(m_repository.runner().run(command), "Could not add the worktree.");
}

void RefService::remove_worktree(const std::string &path)
{
    GitCommand command(GitLane::Write, {"worktree", "remove", path});
    command.m_label = "Removing the worktree at " + path;

    track_write(m_repository.runner().run(command), "Could not remove the worktree. It may have uncommitted changes.");
}

void RefService::prune_worktrees()
{
    GitCommand command(GitLane::Write, {"worktree", "prune"});
    command.m_label = "Pruning worktrees";

    track_write(m_repository.runner().run(command), "Could not prune the worktree list.");
}

void RefService::update_submodule(const std::string &path)
{
    std::vector<std::string> args{"submodule", "update", "--init", "--progress"};
    if (!path.empty()) {
        args.emplace_back("--");
        args.push_back(path);
    }

    GitCommand command(GitLane::Network, args);
    command.m_wants_progress = true;
    command.m_label = path.empty() ? "Updating submodules" : "Updating " + path;

    track_write(m_repository.runner().run(command), "Could not update the submodule.");
}

void RefService::update_submodule_to_remote(const std::string &path)
{
    // --remote moves the submodule to the tip of its configured branch rather than
    // to the commit the superproject records, which then shows up as a change to
    // commit here.
    GitCommand command(GitLane::Network, {"submodule", "update", "--remote", "--progress", "--", path});
    command.m_wants_progress = true;
    command.m_label = "Moving " + path + " to the latest commit";

    track_write(m_repository.runner().run(command), "Could not move the submodule to the latest commit.");
}

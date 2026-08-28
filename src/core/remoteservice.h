/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <glibmm/ustring.h>
#include <sigc++/signal.h>

#include <string>
#include <vector>

class GitJob;
class Repository;

/*!
 * What a force push would do, computed before anything is sent.
 *
 * The important field is m_removed: those are commits that exist on the remote and
 * would stop existing. A non-empty list is the signal that someone else's work is
 * about to be destroyed.
 */
class ForcePushPreview
{
public:
    std::string m_remote;
    std::string m_branch;
    /*! The remote ref's current value, used as the lease. */
    std::string m_expected_oid;
    /*! Commits that would be added, newest first, as "abc1234 subject". */
    std::vector<std::string> m_added;
    /*! Commits that would be destroyed, newest first. Non-empty means danger. */
    std::vector<std::string> m_removed;
    /*! False when the remote branch does not exist yet, so no force is needed. */
    bool m_remote_branch_exists = false;

    bool is_destructive() const
    {
        return !m_removed.empty();
    }
};

/*! Fetch, push and force push. */
class RemoteService
{
public:
    explicit RemoteService(Repository &repository);

    /*! Fetches @p remote, or every remote when it is empty. Prunes deleted branches. */
    void fetch(const std::string &remote = {}, bool prune = true);

    /*! An ordinary, non-forcing push. */
    void push(const std::string &remote, const std::string &branch, bool set_upstream, bool push_tags);

    /*!
     * Fetches @p remote and integrates @p upstream_ref into the current branch.
     *
     * Run as two commands rather than one `git pull`, because pull straddles two
     * lanes: the fetch is network work that can overlap everything else, and the
     * integration takes index.lock and must be serialised against every other
     * write. One job could only sit in one lane, and either choice is wrong.
     *
     * How the integration happens is read from the user's own configuration —
     * `branch.<name>.rebase`, then `pull.rebase`, then `pull.ff` — and nothing is
     * passed on the command line to override it. That choice is theirs.
     */
    void pull(const std::string &remote, const std::string &upstream_ref);

    /*!
     * Works out what a force push would add and destroy.
     *
     * Fetches first, so the lease is taken against what the remote actually holds
     * rather than a possibly stale remote-tracking ref.
     */
    void prepare_force_push(const std::string &remote, const std::string &branch);

    /*!
     * Performs the force push described by @p preview.
     *
     * Always `--force-with-lease=<ref>:<oid>` with the OID from the preview, never a
     * bare `--force` and never the bare lease form, which only checks the local
     * remote-tracking ref and so passes even when that ref is stale.
     * `--force-if-includes` additionally requires that local history actually
     * contains the leased tip.
     */
    void force_push(const ForcePushPreview &preview);

    sigc::signal<void()> &signal_fetched()
    {
        return m_signal_fetched;
    }
    sigc::signal<void()> &signal_pushed()
    {
        return m_signal_pushed;
    }
    /*! The pull fetched and integrated cleanly. */
    sigc::signal<void()> &signal_pulled()
    {
        return m_signal_pulled;
    }
    sigc::signal<void(const ForcePushPreview &)> &signal_force_push_preview_ready()
    {
        return m_signal_force_push_preview_ready;
    }
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_failed()
    {
        return m_signal_failed;
    }
    sigc::signal<void(const Glib::ustring &, int, int)> &signal_progress()
    {
        return m_signal_progress;
    }
    /*! A "remote:" line, such as GitHub's pull request URL. */
    sigc::signal<void(const Glib::ustring &)> &signal_remote_message()
    {
        return m_signal_remote_message;
    }

private:
    /*! How the user's configuration says a pull should integrate. */
    struct PullStyle {
        bool m_rebase = false;
        /*! `pull.rebase = merges`: keep merge commits when replaying. */
        bool m_rebase_merges = false;
        /*! `pull.ff = only`: refuse anything that is not a fast-forward. */
        bool m_ff_only = false;
        /*! `pull.ff = false`: always record a merge commit. */
        bool m_no_ff = false;
    };

    void track_network(GitJob *job, const Glib::ustring &summary);
    /*! Reads the pull-related configuration, then integrates. */
    void read_pull_style(const std::string &upstream_ref);
    void integrate(const std::string &upstream_ref, const PullStyle &style);
    void collect_preview_commits(ForcePushPreview preview);

    Repository &m_repository;

    sigc::signal<void()> m_signal_fetched;
    sigc::signal<void()> m_signal_pushed;
    sigc::signal<void()> m_signal_pulled;
    sigc::signal<void(const ForcePushPreview &)> m_signal_force_push_preview_ready;
    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_failed;
    sigc::signal<void(const Glib::ustring &, int, int)> m_signal_progress;
    sigc::signal<void(const Glib::ustring &)> m_signal_remote_message;
};

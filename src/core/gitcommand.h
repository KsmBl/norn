/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <glibmm/ustring.h>

#include <string>
#include <vector>

/*!
 * Which queue a command runs in.
 *
 * The split exists because git's locking model is not uniform: writers contend on
 * index.lock and ref locks and must be strictly serialised, reads do not and would
 * otherwise queue pointlessly behind a streaming log, and network operations are
 * long-running but touch neither.
 */
enum class GitLane {
    /*! Read-only. Bounded concurrency. status, diff, log, show, cat-file, for-each-ref. */
    Read,
    /*! Mutating. Strictly one at a time. add, reset, commit, merge, rebase, checkout. */
    Write,
    /*! Network. Concurrent with everything else. fetch, push, ls-remote, clone. */
    Network,
};

/*!
 * A single git invocation, described declaratively so GitRunner can decide how to
 * schedule, environment-prepare and report it without a special case per command.
 */
class GitCommand
{
public:
    GitCommand() = default;
    GitCommand(GitLane lane, std::vector<std::string> args)
        : m_args(std::move(args))
        , m_lane(lane)
    {
    }

    /*! Arguments after the fixed prelude, e.g. {"status", "--porcelain=v2"}. */
    std::vector<std::string> m_args;

    /*!
     * Fed to the process on stdin and then closed. Patches, commit messages,
     * pathspecs. Bytes rather than text: a patch has to reach git exactly as git
     * produced it.
     */
    std::string m_stdin;

    GitLane m_lane = GitLane::Read;

    /*! stderr carries --progress output rather than error text. */
    bool m_wants_progress = false;

    /*! Emit stdout in chunks as it arrives instead of buffering it all. */
    bool m_streams_stdout = false;

    /*!
     * git may invoke an editor during this command, so the norn-editor helper
     * must be wired up for it. Set through EditorBridge::apply_to() rather than
     * directly, since the socket path and token come from there.
     */
    bool m_needs_editor = false;
    std::string m_editor_path;
    std::string m_editor_socket;
    std::string m_editor_token;

    /*!
     * Collapse key for the Read lane. At most one running or queued per key; a
     * further request returns the job already in flight rather than adding another.
     */
    std::string m_dedupe_key;

    /*! Milliseconds before the job is killed. 0 means never, required for Network. */
    int m_timeout_ms = 0;

    /*! Short human-readable label for the command log and any progress UI. */
    Glib::ustring m_label;

    /*! Bumped by the owner on repository change; stale results are discarded. */
    unsigned long m_generation = 0;
};

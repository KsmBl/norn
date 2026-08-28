/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "gitcommand.h"
#include "gitjob.h"

#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

/*!
 * Schedules every git invocation for one repository.
 *
 * Three queues, because git's locking model is not uniform. See GitLane. Callers
 * hand over a GitCommand and connect to the job they get back; jobs are created
 * immediately but only started once their lane has room.
 */
class GitRunner
{
public:
    explicit GitRunner(std::string working_directory);
    ~GitRunner();

    GitRunner(const GitRunner &) = delete;
    GitRunner &operator=(const GitRunner &) = delete;

    /*!
     * Queues @p command and returns the job that will run it.
     *
     * The job always exists by the time this returns, so connecting to it is safe,
     * but it may not have started yet. It stays alive until it has finished and its
     * signals have been delivered; do not hold the pointer beyond that.
     *
     * When @p command carries a dedupe key and an equivalent Read job is already
     * queued or running, that job is returned instead and nothing new is queued.
     */
    GitJob *run(const GitCommand &command);

    unsigned long generation() const
    {
        return m_generation;
    }

    /*!
     * Invalidates everything in flight, cancelling running jobs and dropping queued
     * ones. Used on repository change and when a view that owns queries goes away.
     */
    unsigned long bump_generation();

    /*! True while the Write lane has a running or queued job. */
    bool is_writing() const;

    /*! Emitted for every job the moment it is queued, so the log can list it. */
    sigc::signal<void(GitJob *)> &signal_job_queued()
    {
        return m_signal_job_queued;
    }
    /*! Emitted once a job has finished, before it is destroyed. */
    sigc::signal<void(GitJob *)> &signal_job_finished()
    {
        return m_signal_job_finished;
    }
    /*! Emitted when the Write lane drains, so a watcher can fire one refresh. */
    sigc::signal<void()> &signal_write_lane_idle()
    {
        return m_signal_write_lane_idle;
    }

private:
    struct Lane {
        std::deque<GitJob *> m_queued;
        std::vector<GitJob *> m_running;
        std::size_t m_max_concurrent = 1;
    };

    Lane &lane_for(GitLane lane);
    const Lane &lane_for(GitLane lane) const;
    GitJob *find_duplicate(const std::string &dedupe_key) const;
    void pump();
    void on_job_finished(GitJob *job);

    std::string m_working_directory;
    Lane m_read_lane;
    Lane m_write_lane;
    Lane m_network_lane;

    bool sweep_finished();

    /*! Owns every live job; the lanes only hold observing pointers into this. */
    std::vector<std::unique_ptr<GitJob>> m_jobs;

    /*!
     * Jobs that have finished and are waiting to be destroyed.
     *
     * A job cannot be freed from inside its own finished signal, so it is parked
     * here and swept from an idle callback. The connection is owned so that the
     * sweep can be cancelled if the runner is destroyed first — otherwise the idle
     * fires later into a runner that no longer exists.
     */
    std::vector<std::unique_ptr<GitJob>> m_graveyard;
    sigc::connection m_sweep_connection;

    unsigned long m_generation = 1;

    sigc::signal<void(GitJob *)> m_signal_job_queued;
    sigc::signal<void(GitJob *)> m_signal_job_finished;
    sigc::signal<void()> m_signal_write_lane_idle;
};

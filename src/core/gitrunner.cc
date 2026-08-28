/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gitrunner.h"

#include <glibmm/main.h>

#include <algorithm>
#include <thread>
#include <utility>

namespace
{
/*!
 * Reads may overlap, and should: a diff must not have to wait behind a streaming
 * log. Kept modest so a burst cannot spawn dozens of git processes at once.
 */
std::size_t read_lane_concurrency()
{
    const unsigned hardware = std::max(2u, std::thread::hardware_concurrency());
    return std::clamp<std::size_t>(hardware / 2, 2, 4);
}

/*! Network jobs are long and mostly independent, but not unbounded. */
constexpr std::size_t s_network_lane_concurrency = 4;
}

GitRunner::GitRunner(std::string working_directory)
    : m_working_directory(std::move(working_directory))
{
    m_read_lane.m_max_concurrent = read_lane_concurrency();
    // Writers contend on index.lock and ref locks, so exactly one at a time.
    m_write_lane.m_max_concurrent = 1;
    m_network_lane.m_max_concurrent = s_network_lane_concurrency;
}

GitRunner::~GitRunner()
{
    // Without this the idle would fire after the runner is gone and sweep through
    // freed memory.
    m_sweep_connection.disconnect();

    // Cancel rather than destroy: a job's async callbacks reference it, and the
    // unique_ptrs below tear everything down once this returns.
    for (const std::unique_ptr<GitJob> &job : m_jobs) {
        job->cancel();
    }
}

GitRunner::Lane &GitRunner::lane_for(GitLane lane)
{
    switch (lane) {
    case GitLane::Write:
        return m_write_lane;
    case GitLane::Network:
        return m_network_lane;
    case GitLane::Read:
        break;
    }
    return m_read_lane;
}

const GitRunner::Lane &GitRunner::lane_for(GitLane lane) const
{
    return const_cast<GitRunner *>(this)->lane_for(lane);
}

GitJob *GitRunner::find_duplicate(const std::string &dedupe_key) const
{
    // Only the Read lane coalesces: two identical status queries return the same
    // answer, whereas two identical `git add` calls are two deliberate actions.
    for (GitJob *job : m_read_lane.m_running) {
        if (job->command().m_dedupe_key == dedupe_key) {
            return job;
        }
    }
    for (GitJob *job : m_read_lane.m_queued) {
        if (job->command().m_dedupe_key == dedupe_key) {
            return job;
        }
    }
    return nullptr;
}

GitJob *GitRunner::run(const GitCommand &command)
{
    GitCommand queued = command;
    queued.m_generation = m_generation;

    if (queued.m_label.empty() && !queued.m_args.empty()) {
        queued.m_label = queued.m_args.front();
    }

    if (queued.m_lane == GitLane::Read && !queued.m_dedupe_key.empty()) {
        if (GitJob *existing = find_duplicate(queued.m_dedupe_key)) {
            return existing;
        }
    }

    // Created up front so the caller always has something to connect to, even
    // though its lane may not have room to start it yet.
    auto owned = std::make_unique<GitJob>(queued, m_working_directory);
    GitJob *job = owned.get();
    m_jobs.push_back(std::move(owned));

    job->signal_finished().connect([this, job] {
        on_job_finished(job);
    });

    lane_for(queued.m_lane).m_queued.push_back(job);
    m_signal_job_queued.emit(job);

    pump();
    return job;
}

void GitRunner::pump()
{
    for (Lane *lane : {&m_write_lane, &m_read_lane, &m_network_lane}) {
        while (!lane->m_queued.empty() && lane->m_running.size() < lane->m_max_concurrent) {
            GitJob *job = lane->m_queued.front();
            lane->m_queued.pop_front();
            lane->m_running.push_back(job);
            job->start();
        }
    }
}

void GitRunner::on_job_finished(GitJob *job)
{
    const GitCommand command = job->command();

    Lane &lane = lane_for(command.m_lane);
    std::erase(lane.m_running, job);
    std::erase(lane.m_queued, job);

    // Only report results the caller still wants.
    if (command.m_generation == m_generation) {
        m_signal_job_finished.emit(job);

        if (command.m_lane == GitLane::Write && !is_writing()) {
            // Coalesces the refresh storm a write produces into a single update.
            m_signal_write_lane_idle.emit();
        }
    }

    // Parked rather than freed here: this is running inside the job's own finished
    // signal, and destroying it underneath that emission would be a use-after-free
    // the moment the emission returns.
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        if (it->get() == job) {
            m_graveyard.push_back(std::move(*it));
            m_jobs.erase(it);
            break;
        }
    }

    if (!m_sweep_connection.connected()) {
        m_sweep_connection = Glib::signal_idle().connect(sigc::mem_fun(*this, &GitRunner::sweep_finished));
    }
}

bool GitRunner::sweep_finished()
{
    m_graveyard.clear();
    pump();

    // One shot: reconnected by the next job that finishes.
    return false;
}

bool GitRunner::is_writing() const
{
    return !m_write_lane.m_queued.empty() || !m_write_lane.m_running.empty();
}

unsigned long GitRunner::bump_generation()
{
    ++m_generation;

    for (Lane *lane : {&m_write_lane, &m_read_lane, &m_network_lane}) {
        // Queued jobs never started, so they can simply be forgotten.
        for (GitJob *job : lane->m_queued) {
            std::erase_if(m_jobs, [job](const std::unique_ptr<GitJob> &owned) {
                return owned.get() == job;
            });
        }
        lane->m_queued.clear();

        // Running ones have to be asked to stop; on_job_finished cleans them up.
        for (GitJob *job : lane->m_running) {
            job->cancel();
        }
    }

    return m_generation;
}

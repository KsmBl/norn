/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "gitcommand.h"

#include <giomm/cancellable.h>
#include <giomm/inputstream.h>
#include <giomm/outputstream.h>
#include <glibmm/refptr.h>
#include <glibmm/spawn.h>
#include <sigc++/signal.h>

#include <memory>
#include <string>

/*!
 * One running git invocation.
 *
 * Owned by GitRunner, which decides when it starts. Everything is signal-driven;
 * no method here ever blocks the main loop.
 */
class GitJob
{
public:
    GitJob(GitCommand command, std::string working_directory);
    ~GitJob();

    GitJob(const GitJob &) = delete;
    GitJob &operator=(const GitJob &) = delete;

    const GitCommand &command() const
    {
        return m_command;
    }

    /*! The full argument vector, prelude included, as it will be executed. */
    const std::vector<std::string> &arguments() const
    {
        return m_arguments;
    }

    /*! Everything written to stdout, unless the command streams it. */
    const std::string &stdout_data() const
    {
        return m_stdout_data;
    }

    const std::string &stderr_data() const
    {
        return m_stderr_data;
    }

    int exit_code() const
    {
        return m_exit_code;
    }

    bool succeeded() const
    {
        return m_finished && m_exit_code == 0;
    }

    /*! How long the invocation took, in milliseconds. */
    long elapsed_ms() const
    {
        return m_elapsed_ms;
    }

    bool finished() const
    {
        return m_finished;
    }

    /*! stderr decoded and redacted, suitable for showing to the user. */
    Glib::ustring error_text() const;

    void start();

    /*! Asks the process to stop, escalating to a kill if it does not. */
    void cancel();

    sigc::signal<void()> &signal_started()
    {
        return m_signal_started;
    }
    /*! Only emitted when the command has m_streams_stdout set. */
    sigc::signal<void(const std::string &)> &signal_stdout_chunk()
    {
        return m_signal_stdout_chunk;
    }
    /*! One line at a time, already redacted. Progress lines arrive here too. */
    sigc::signal<void(const Glib::ustring &)> &signal_stderr_line()
    {
        return m_signal_stderr_line;
    }
    /*! Parsed from --progress output. @p total is -1 when the phase has no total. */
    sigc::signal<void(const Glib::ustring &, int, int)> &signal_progress()
    {
        return m_signal_progress;
    }
    sigc::signal<void()> &signal_finished()
    {
        return m_signal_finished;
    }

private:
    void read_stdout();
    void read_stderr();
    void write_stdin();
    void on_stdout_ready(const Glib::RefPtr<Gio::AsyncResult> &result);
    void on_stderr_ready(const Glib::RefPtr<Gio::AsyncResult> &result);
    void on_child_exited(Glib::Pid pid, int status);
    void consume_stderr(const std::string &chunk);
    void complete(int exit_code);

    GitCommand m_command;
    std::string m_working_directory;
    std::vector<std::string> m_arguments;

    /*!
     * giomm 2.4 does not wrap GSubprocess, so the process is spawned through
     * Glib::spawn_async_with_pipes and its pipes wrapped as GIO streams. That keeps
     * the reads asynchronous, which is the part that matters.
     */
    Glib::Pid m_pid = 0;
    Glib::RefPtr<Gio::InputStream> m_stdout_stream;
    Glib::RefPtr<Gio::InputStream> m_stderr_stream;
    Glib::RefPtr<Gio::OutputStream> m_stdin_stream;
    Glib::RefPtr<Gio::Cancellable> m_cancellable;

    /*! Reused across async reads; one per stream so they cannot interleave. */
    std::unique_ptr<char[]> m_stdout_buffer;
    std::unique_ptr<char[]> m_stderr_buffer;

    std::string m_stdout_data;
    std::string m_stderr_data;
    /*! stderr tail not yet terminated by \r or \n. */
    std::string m_stderr_pending;

    gint64 m_started_at = 0;
    long m_elapsed_ms = 0;
    int m_exit_code = -1;
    bool m_finished = false;
    bool m_cancelled = false;
    /*! Set once both streams have hit end of file and the process has been reaped. */
    int m_open_streams = 0;
    bool m_process_reaped = false;
    int m_reaped_code = -1;
    sigc::connection m_child_watch;

    sigc::signal<void()> m_signal_started;
    sigc::signal<void(const std::string &)> m_signal_stdout_chunk;
    sigc::signal<void(const Glib::ustring &)> m_signal_stderr_line;
    sigc::signal<void(const Glib::ustring &, int, int)> m_signal_progress;
    sigc::signal<void()> m_signal_finished;
};

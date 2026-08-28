/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gitjob.h"

#include "gitenvironment.h"

#include <giomm/outputstream.h>
#include <glibmm/main.h>
#include <glibmm/regex.h>
#include <glibmm/shell.h>

#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include <csignal>
#include <utility>

namespace
{
/*! Read chunk size. Large enough that a big diff is a handful of reads. */
constexpr gsize s_read_size = 64 * 1024;

/*! Grace period between asking a cancelled process to stop and killing it. */
constexpr unsigned s_terminate_grace_ms = 5000;

/*! "Receiving objects:  42% (420/1000)" - a phase with a known total. */
const Glib::RefPtr<Glib::Regex> &progress_with_total()
{
    static const Glib::RefPtr<Glib::Regex> re = Glib::Regex::create("^([^:]+):\\s+(\\d+)%\\s+\\((\\d+)/(\\d+)\\)");
    return re;
}

/*! "Enumerating objects: 123" - a counting phase with no total yet. */
const Glib::RefPtr<Glib::Regex> &progress_without_total()
{
    static const Glib::RefPtr<Glib::Regex> re = Glib::Regex::create("^([^:]+):\\s+(\\d+)(?:,|$)");
    return re;
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

GitJob::GitJob(GitCommand command, std::string working_directory)
    : m_command(std::move(command))
    , m_working_directory(std::move(working_directory))
    , m_cancellable(Gio::Cancellable::create())
    , m_stdout_buffer(new char[s_read_size])
    , m_stderr_buffer(new char[s_read_size])
{
    m_arguments.emplace_back("git");

    const std::vector<std::string> prelude = GitEnvironment::config_prelude(m_command.m_lane == GitLane::Write);
    m_arguments.insert(m_arguments.end(), prelude.begin(), prelude.end());
    m_arguments.insert(m_arguments.end(), m_command.m_args.begin(), m_command.m_args.end());
}

GitJob::~GitJob()
{
    m_child_watch.disconnect();

    if (m_pid != 0) {
        ::kill(m_pid, SIGKILL);
        Glib::spawn_close_pid(m_pid);
    }
}

void GitJob::start()
{
    m_started_at = g_get_monotonic_time();

    std::vector<std::string> environment = GitEnvironment::process_environment();
    if (m_command.m_needs_editor) {
        GitEnvironment::apply_editor_bridge(environment, m_command.m_editor_path, m_command.m_editor_socket, m_command.m_editor_token);
    }

    int stdin_fd = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;

    try {
        // DO_NOT_REAP_CHILD so a child watch can collect the exit status;
        // SEARCH_PATH so "git" resolves the way it does in a shell.
        Glib::spawn_async_with_pipes(m_working_directory,
                                     m_arguments,
                                     environment,
                                     Glib::SPAWN_SEARCH_PATH | Glib::SPAWN_DO_NOT_REAP_CHILD,
                                     Glib::SlotSpawnChildSetup(),
                                     &m_pid,
                                     &stdin_fd,
                                     &stdout_fd,
                                     &stderr_fd);
    } catch (const Glib::Error &error) {
        // The only way "git is not installed" reaches us. There is no process to
        // wait on, so the job has to complete itself.
        m_stderr_data += error.what();
        complete(-1);
        return;
    }

    // The streams take ownership of the descriptors and close them at destruction.
    m_stdout_stream = Glib::wrap(g_unix_input_stream_new(stdout_fd, TRUE));
    m_stderr_stream = Glib::wrap(g_unix_input_stream_new(stderr_fd, TRUE));
    m_stdin_stream = Glib::wrap(g_unix_output_stream_new(stdin_fd, TRUE));
    m_open_streams = 2;

    m_signal_started.emit();

    write_stdin();
    read_stdout();
    read_stderr();

    m_child_watch = Glib::signal_child_watch().connect(sigc::mem_fun(*this, &GitJob::on_child_exited), m_pid);

    if (m_command.m_timeout_ms > 0) {
        Glib::signal_timeout().connect_once(
            [this] {
                if (!m_finished) {
                    cancel();
                }
            },
            m_command.m_timeout_ms);
    }
}

void GitJob::write_stdin()
{
    const Glib::RefPtr<Gio::OutputStream> stream = m_stdin_stream;
    if (!stream) {
        return;
    }

    if (!m_command.m_stdin.empty()) {
        try {
            // A patch can exceed the pipe buffer, so this loops rather than
            // assuming one write takes everything. stdout and stderr are already
            // being drained asynchronously, which is what stops it deadlocking.
            gsize written = 0;
            while (written < m_command.m_stdin.size()) {
                const gssize count = stream->write(m_command.m_stdin.data() + written, m_command.m_stdin.size() - written);
                if (count <= 0) {
                    break;
                }
                written += static_cast<gsize>(count);
            }
        } catch (const Glib::Error &error) {
            m_stderr_data += error.what();
        }
    }

    try {
        stream->close();
    } catch (const Glib::Error &) {
        // git may have exited already; nothing useful to do about it.
    }
}

void GitJob::read_stdout()
{
    m_stdout_stream->read_async(m_stdout_buffer.get(), s_read_size, sigc::mem_fun(*this, &GitJob::on_stdout_ready), m_cancellable);
}

void GitJob::on_stdout_ready(const Glib::RefPtr<Gio::AsyncResult> &result)
{
    gssize count = 0;
    try {
        count = m_stdout_stream->read_finish(result);
    } catch (const Glib::Error &) {
        count = 0;
    }

    if (count > 0) {
        const std::string chunk(m_stdout_buffer.get(), static_cast<std::size_t>(count));
        if (m_command.m_streams_stdout) {
            m_signal_stdout_chunk.emit(chunk);
        } else {
            m_stdout_data += chunk;
        }
        read_stdout();
        return;
    }

    --m_open_streams;
    if (m_process_reaped && m_open_streams == 0) {
        complete(m_reaped_code);
    }
}

void GitJob::read_stderr()
{
    m_stderr_stream->read_async(m_stderr_buffer.get(), s_read_size, sigc::mem_fun(*this, &GitJob::on_stderr_ready), m_cancellable);
}

void GitJob::on_stderr_ready(const Glib::RefPtr<Gio::AsyncResult> &result)
{
    gssize count = 0;
    try {
        count = m_stderr_stream->read_finish(result);
    } catch (const Glib::Error &) {
        count = 0;
    }

    if (count > 0) {
        consume_stderr(std::string(m_stderr_buffer.get(), static_cast<std::size_t>(count)));
        read_stderr();
        return;
    }

    // Whatever is left without a terminator still belongs to this job.
    if (!m_stderr_pending.empty()) {
        const Glib::ustring line = GitEnvironment::redact_text(trimmed(m_stderr_pending));
        m_stderr_pending.clear();
        if (!line.empty()) {
            m_signal_stderr_line.emit(line);
        }
    }

    --m_open_streams;
    if (m_process_reaped && m_open_streams == 0) {
        complete(m_reaped_code);
    }
}

void GitJob::consume_stderr(const std::string &chunk)
{
    m_stderr_data += chunk;
    m_stderr_pending += chunk;

    // Progress output separates updates with \r, not \n, so splitting on newlines
    // alone would deliver nothing until the very end and then one enormous line.
    std::size_t start = 0;
    for (std::size_t i = 0; i < m_stderr_pending.size(); ++i) {
        const char c = m_stderr_pending[i];
        if (c != '\n' && c != '\r') {
            continue;
        }

        const std::string raw = m_stderr_pending.substr(start, i - start);
        start = i + 1;
        if (raw.empty()) {
            continue;
        }

        const Glib::ustring line = GitEnvironment::redact_text(trimmed(raw));
        if (line.empty()) {
            continue;
        }

        if (m_command.m_wants_progress) {
            Glib::MatchInfo match;
            if (progress_with_total()->match(line, match)) {
                m_signal_progress.emit(trimmed(match.fetch(1)), std::stoi(match.fetch(3)), std::stoi(match.fetch(4)));
            } else if (progress_without_total()->match(line, match)) {
                m_signal_progress.emit(trimmed(match.fetch(1)), std::stoi(match.fetch(2)), -1);
            }
        }

        m_signal_stderr_line.emit(line);
    }

    m_stderr_pending.erase(0, start);
}

void GitJob::on_child_exited(Glib::Pid pid, int status)
{
    Glib::spawn_close_pid(pid);
    m_pid = 0;

    // WIFEXITED/WEXITSTATUS: a signalled process has no exit code of its own, and
    // reporting the raw status would look like a wildly wrong one.
    m_reaped_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    m_process_reaped = true;

    // The process can exit before its pipes have been drained, so completion waits
    // for both: reporting now would lose whatever is still buffered.
    if (m_open_streams == 0) {
        complete(m_reaped_code);
    }
}

void GitJob::complete(int exit_code)
{
    if (m_finished) {
        return;
    }

    m_child_watch.disconnect();

    m_exit_code = exit_code;
    m_elapsed_ms = static_cast<long>((g_get_monotonic_time() - m_started_at) / 1000);
    m_finished = true;

    m_signal_finished.emit();
}

void GitJob::cancel()
{
    if (m_finished || m_pid == 0 || m_cancelled) {
        return;
    }

    m_cancelled = true;
    ::kill(m_pid, SIGTERM);

    Glib::signal_timeout().connect_once(
        [this] {
            if (!m_finished && m_pid != 0) {
                ::kill(m_pid, SIGKILL);
            }
        },
        s_terminate_grace_ms);
}

Glib::ustring GitJob::error_text() const
{
    return GitEnvironment::redact_text(trimmed(m_stderr_data));
}

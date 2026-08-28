/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/remoteservice.h"
#include "core/repository.h"
#include "core/repositorylocator.h"
#include "core/settings.h"
#include "core/statussnapshot.h"
#include "ui/mainwindow.h"

#include <giomm/init.h>

#include <glibmm/init.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <gtkmm/application.h>

#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
const char *const s_usage =
    "Usage: norn [OPTION]... [DIRECTORY]\n"
    "\n"
    "Open the Git repository that contains DIRECTORY, or the current directory\n"
    "if none is given. Any directory inside a working tree will do — norn asks\n"
    "git for the root rather than insisting on being pointed at it.\n"
    "\n"
    "Options:\n"
    "  -c, --commit     Open ready to commit: the working copy, with the cursor\n"
    "                   already in the commit message. Meant for a keyboard\n"
    "                   shortcut or a file manager action that means\n"
    "                   \"commit here\".\n"
    "      --pull       Fetch and integrate the upstream branch, in the terminal,\n"
    "                   without opening a window. How history is integrated comes\n"
    "                   from your own configuration — branch.<name>.rebase,\n"
    "                   pull.rebase and pull.ff — and is never overridden.\n"
    "      --push       Push the current branch, in the terminal, without opening\n"
    "                   a window. A branch with no upstream yet is given one.\n"
    "  -h, --help       Show this message and exit.\n"
    "  -V, --version    Show the version and exit.\n"
    "\n"
    "norn drives the git command line, so it uses your ~/.gitconfig, your hooks\n"
    "and your credential helpers, and never writes to either.\n";

/*! What the command line asked for, once parsed. */
struct Invocation {
    std::string m_directory;
    bool m_commit_mode = false;
    bool m_push = false;
    bool m_pull = false;

    /*! Push and pull run in the terminal; everything else opens a window. */
    bool is_headless() const
    {
        return m_push || m_pull;
    }
    /*! Set when an option was not understood; the message is already printed. */
    bool m_failed = false;
};

Invocation parse(const std::vector<std::string> &arguments)
{
    Invocation invocation;
    bool options_ended = false;

    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const std::string &argument = arguments[i];

        if (!options_ended && argument == "--") {
            options_ended = true;
        } else if (!options_ended && (argument == "-c" || argument == "--commit")) {
            invocation.m_commit_mode = true;
        } else if (!options_ended && argument == "--push") {
            invocation.m_push = true;
        } else if (!options_ended && argument == "--pull") {
            invocation.m_pull = true;
        } else if (!options_ended && argument.size() > 1 && argument[0] == '-') {
            std::fprintf(stderr, "norn: unrecognised option '%s'\n\n%s", argument.c_str(), s_usage);
            invocation.m_failed = true;
            return invocation;
        } else if (invocation.m_directory.empty()) {
            invocation.m_directory = argument;
        } else {
            // One window per repository, so a second path has nowhere to go.
            std::fprintf(stderr, "norn: only one directory can be given\n\n%s", s_usage);
            invocation.m_failed = true;
            return invocation;
        }
    }

    if (invocation.m_push && invocation.m_pull) {
        std::fprintf(stderr, "norn: --push and --pull cannot be combined\n\n%s", s_usage);
        invocation.m_failed = true;
        return invocation;
    }
    if (invocation.m_commit_mode && invocation.is_headless()) {
        std::fprintf(stderr, "norn: --commit opens a window, so it cannot be combined with --push or --pull\n\n%s", s_usage);
        invocation.m_failed = true;
        return invocation;
    }

    if (invocation.m_directory.empty()) {
        invocation.m_directory = Glib::get_current_dir();
    }

    return invocation;
}

/*! Runs one remote operation with no window, reporting to the terminal. */
int run_headless(const Invocation &invocation)
{
    // No Gtk::Application here, so the wrapper types have to be registered by hand
    // or Glib::wrap cannot wrap the streams GitJob builds its pipes from.
    Glib::init();
    Gio::init();

    const RepositoryLocator locator = RepositoryLocator::locate(invocation.m_directory);
    if (!locator.is_found()) {
        std::fprintf(stderr, "norn: %s\n", locator.error_text().c_str());
        return 1;
    }

    Repository repository(locator.toplevel());
    RemoteService remote_service(repository);
    const Glib::RefPtr<Glib::MainLoop> loop = Glib::MainLoop::create();

    int exit_code = 0;
    bool started = false;
    bool progress_pending = false;
    Glib::ustring last_phase;
    // Progress overwrites one line with \r, which only means anything on a
    // terminal. Redirected to a file or a pipe it would run every phase together
    // on one unreadable line, so there it becomes one line per phase instead.
    const bool interactive = isatty(fileno(stderr)) != 0;
    std::string head_before;
    std::string branch;

    // Progress rewrites its own line, the way git's does, and goes to stderr so
    // that stdout stays a clean report a script can read.
    remote_service.signal_progress().connect([&](const Glib::ustring &phase, int current, int total) {
        if (!interactive) {
            if (phase != last_phase) {
                std::fprintf(stderr, "%s\n", phase.c_str());
                last_phase = phase;
            }
            return;
        }

        // \033[K clears whatever the previous, possibly longer, phase left behind.
        if (total > 0) {
            std::fprintf(stderr, "\r%s: %d/%d\033[K", phase.c_str(), current, total);
        } else {
            std::fprintf(stderr, "\r%s\033[K", phase.c_str());
        }
        std::fflush(stderr);
        progress_pending = true;
    });

    const auto end_progress_line = [&progress_pending] {
        if (progress_pending) {
            std::fputc('\n', stderr);
            progress_pending = false;
        }
    };

    remote_service.signal_remote_message().connect([&](const Glib::ustring &message) {
        end_progress_line();
        std::fprintf(stderr, "remote: %s\n", message.c_str());
    });

    const auto fail = [&](const Glib::ustring &summary, const Glib::ustring &detail) {
        end_progress_line();
        std::fprintf(stderr, "norn: %s\n", summary.c_str());
        if (!detail.empty()) {
            std::fprintf(stderr, "%s\n", detail.c_str());
        }
        exit_code = 1;
        loop->quit();
    };

    remote_service.signal_failed().connect(fail);
    repository.signal_operation_failed().connect(fail);

    remote_service.signal_pushed().connect([&] {
        end_progress_line();
        std::printf("Pushed %s.\n", branch.c_str());
        loop->quit();
    });

    remote_service.signal_pulled().connect([&] {
        end_progress_line();
        // The status that follows the pull carries the new HEAD, so the report waits
        // for it rather than asking git a second time.
        repository.signal_status_changed().connect([&] {
            const std::string &head_after = repository.status().m_head_oid;
            if (head_after == head_before) {
                std::printf("Already up to date.\n");
            } else {
                std::printf("Updated %s: %s..%s\n", branch.c_str(), head_before.substr(0, 9).c_str(), head_after.substr(0, 9).c_str());
            }
            loop->quit();
        });
    });

    repository.signal_status_changed().connect([&] {
        if (started) {
            return;
        }
        started = true;

        const StatusSnapshot &status = repository.status();
        branch = status.m_branch;
        head_before = status.m_head_oid;

        if (status.m_is_unborn) {
            fail("This branch has no commits yet.", {});
            return;
        }
        if (status.m_is_detached || status.m_branch.empty()) {
            fail("HEAD is detached, so there is no branch to push or pull.", {});
            return;
        }

        if (invocation.m_pull) {
            if (status.m_upstream.empty()) {
                fail("This branch has no upstream branch to pull from.", {});
                return;
            }
            const std::string remote = status.m_upstream.substr(0, status.m_upstream.find('/'));
            std::fprintf(stderr, "Fetching from %s…\n", remote.c_str());
            remote_service.pull(remote, status.m_upstream);
            return;
        }

        // With no upstream yet, the first push should establish one — the same
        // choice the window makes, so the two agree.
        const bool set_upstream = status.m_upstream.empty();
        const std::string remote = set_upstream ? "origin" : status.m_upstream.substr(0, status.m_upstream.find('/'));
        std::fprintf(stderr, "Pushing %s to %s…\n", status.m_branch.c_str(), remote.c_str());
        remote_service.push(remote, status.m_branch, set_upstream, false);
    });

    repository.open();
    loop->run();

    return exit_code;
}
}

int main(int argc, char *argv[])
{
    // --help and --version are answered before the application exists, so they work
    // with no display attached — over ssh, or from a script asking what this is.
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--") {
            break;
        }
        if (argument == "-h" || argument == "--help") {
            std::fputs(s_usage, stdout);
            return 0;
        }
        if (argument == "-V" || argument == "--version") {
            std::printf("norn %s\n", NORN_VERSION);
            return 0;
        }
    }

    // Push and pull never open a window, so they answer here rather than through
    // the application: no display is needed, and the exit code is the result.
    {
        const std::vector<std::string> arguments(argv, argv + argc);
        const Invocation invocation = parse(arguments);
        if (invocation.m_failed) {
            return 2;
        }
        if (invocation.is_headless()) {
            return run_headless(invocation);
        }
    }

    // HANDLES_COMMAND_LINE rather than the default, so a path argument is resolved
    // here instead of being treated as a file to open by the application framework.
    auto app = Gtk::Application::create("de.synthelicz.Norn", Gio::APPLICATION_HANDLES_COMMAND_LINE | Gio::APPLICATION_NON_UNIQUE);

    Settings::instance().load();

    app->signal_command_line().connect(
        [app](const Glib::RefPtr<Gio::ApplicationCommandLine> &command_line) {
            int count = 0;
            char **raw = command_line->get_arguments(count);
            const std::vector<std::string> arguments(raw, raw + count);
            g_strfreev(raw);

            const Invocation invocation = parse(arguments);
            if (invocation.m_failed) {
                return 2;
            }

            const RepositoryLocator locator = RepositoryLocator::locate(invocation.m_directory);

            auto *window = new MainWindow(locator.is_found() ? locator.toplevel() : std::string());
            if (!locator.is_found()) {
                window->show_message(locator.error_text(), locator.result() != RepositoryLocator::Result::NotARepository);
            }

            app->add_window(*window);
            window->show();

            // After show(), so the focus lands on a widget that is already realised.
            if (invocation.m_commit_mode) {
                window->start_in_commit_mode();
            }

            return 0;
        },
        false);

    return app->run(argc, argv);
}

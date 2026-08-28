/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/repositorylocator.h"
#include "core/settings.h"
#include "ui/mainwindow.h"

#include <glibmm/miscutils.h>

#include <gtkmm/application.h>

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
    "  -h, --help       Show this message and exit.\n"
    "  -V, --version    Show the version and exit.\n"
    "\n"
    "norn drives the git command line, so it uses your ~/.gitconfig, your hooks\n"
    "and your credential helpers, and never writes to either.\n";

/*! What the command line asked for, once parsed. */
struct Invocation {
    std::string m_directory;
    bool m_commit_mode = false;
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

    if (invocation.m_directory.empty()) {
        invocation.m_directory = Glib::get_current_dir();
    }

    return invocation;
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

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/repositorylocator.h"
#include "core/settings.h"
#include "ui/mainwindow.h"

#include <glibmm/miscutils.h>
#include <glibmm/optioncontext.h>

#include <gtkmm/application.h>

#include <iostream>

int main(int argc, char *argv[])
{
    // HANDLES_COMMAND_LINE rather than the default, so a path argument is resolved
    // here instead of being treated as a file to open by the application framework.
    auto app = Gtk::Application::create("de.synthelicz.Norn", Gio::APPLICATION_HANDLES_COMMAND_LINE | Gio::APPLICATION_NON_UNIQUE);

    Settings::instance().load();

    app->signal_command_line().connect(
        [app](const Glib::RefPtr<Gio::ApplicationCommandLine> &command_line) {
            int count = 0;
            char **argv = command_line->get_arguments(count);

            // One window per repository, so each invocation opens its own.
            const std::string requested = count > 1 ? std::string(argv[1]) : Glib::get_current_dir();

            const RepositoryLocator locator = RepositoryLocator::locate(requested);

            auto *window = new MainWindow(locator.is_found() ? locator.toplevel() : std::string());
            if (!locator.is_found()) {
                window->show_message(locator.error_text(), locator.result() != RepositoryLocator::Result::NotARepository);
            }

            app->add_window(*window);
            window->show();

            g_strfreev(argv);
            return 0;
        },
        false);

    return app->run(argc, argv);
}

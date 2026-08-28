/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingsdialog.h"

#include "core/settings.h"

#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>

namespace
{
/*! A labelled row in a settings grid. */
void add_row(Gtk::Grid &grid, int row, const Glib::ustring &label, Gtk::Widget &widget)
{
    auto *text = Gtk::manage(new Gtk::Label(label));
    text->set_xalign(0.0F);
    grid.attach(*text, 0, row, 1, 1);
    grid.attach(widget, 1, row, 1, 1);
}

Gtk::Grid *make_page(Gtk::Notebook &pages, const Glib::ustring &title)
{
    auto *grid = Gtk::manage(new Gtk::Grid());
    grid->set_row_spacing(8);
    grid->set_column_spacing(12);
    grid->set_margin_start(12);
    grid->set_margin_end(12);
    grid->set_margin_top(12);
    grid->set_margin_bottom(12);
    pages.append_page(*grid, title);
    return grid;
}
}

SettingsDialog::SettingsDialog(Gtk::Window &parent)
    : Gtk::Dialog("Preferences", parent, true)
{
    build_ui();
    show_all_children();
}

void SettingsDialog::build_ui()
{
    set_default_size(460, 340);

    add_button("_Close", Gtk::RESPONSE_CLOSE);
    add_button("_Apply", Gtk::RESPONSE_APPLY);
    set_default_response(Gtk::RESPONSE_APPLY);

    const Settings &settings = Settings::instance();

    m_context_lines.set_range(0, 50);
    m_context_lines.set_increments(1, 5);
    m_context_lines.set_value(settings.context_lines());
    m_context_lines.set_tooltip_text("How much unchanged text to show around each change.");

    m_max_diff_lines.set_range(100, 1000000);
    m_max_diff_lines.set_increments(100, 1000);
    m_max_diff_lines.set_value(settings.max_diff_lines());
    m_max_diff_lines.set_tooltip_text("A generated file can produce a diff long enough to make the view unusable.");

    m_history_page_size.set_range(100, 10000);
    m_history_page_size.set_increments(100, 500);
    m_history_page_size.set_value(settings.history_page_size());

    m_show_graph.set_active(settings.show_graph());

    Gtk::Grid *view = make_page(m_pages, "View");
    add_row(*view, 0, "Context lines:", m_context_lines);
    add_row(*view, 1, "Stop drawing a diff after:", m_max_diff_lines);
    add_row(*view, 2, "Commits loaded at a time:", m_history_page_size);
    view->attach(m_show_graph, 0, 3, 2, 1);

    m_sign_off.set_active(settings.sign_off_by_default());
    m_subject_soft_limit.set_range(20, 200);
    m_subject_soft_limit.set_increments(1, 10);
    m_subject_soft_limit.set_value(settings.subject_soft_limit());
    m_subject_soft_limit.set_tooltip_text("The summary counter turns into a warning past this length.");

    Gtk::Grid *commit = make_page(m_pages, "Commits");
    commit->attach(m_sign_off, 0, 0, 2, 1);
    add_row(*commit, 1, "Warn when the summary passes:", m_subject_soft_limit);

    auto *note = Gtk::manage(new Gtk::Label("Your name, email address, signing key and line ending handling come from your "
                                            "Git configuration, so that norn and your terminal agree."));
    note->set_xalign(0.0F);
    note->set_line_wrap(true);
    note->set_sensitive(false);
    note->set_margin_top(12);
    commit->attach(*note, 0, 2, 2, 1);

    m_auto_refresh.set_active(settings.auto_refresh());
    m_avoid_locks.set_active(settings.avoid_optional_locks());
    m_avoid_locks.set_tooltip_text("Slower, because git can no longer cache what it learned about the working tree. "
                                   "Worth enabling only for a repository shared with another process, or on a network mount.");

    Gtk::Grid *git = make_page(m_pages, "Git");
    git->attach(m_auto_refresh, 0, 0, 2, 1);
    git->attach(m_avoid_locks, 0, 1, 2, 1);

    get_content_area()->pack_start(m_pages, Gtk::PACK_EXPAND_WIDGET);
}

void SettingsDialog::apply()
{
    Settings &settings = Settings::instance();

    settings.set_context_lines(m_context_lines.get_value_as_int());
    settings.set_max_diff_lines(m_max_diff_lines.get_value_as_int());
    settings.set_history_page_size(m_history_page_size.get_value_as_int());
    settings.set_show_graph(m_show_graph.get_active());

    settings.set_sign_off_by_default(m_sign_off.get_active());
    settings.set_subject_soft_limit(m_subject_soft_limit.get_value_as_int());

    settings.set_auto_refresh(m_auto_refresh.get_active());
    settings.set_avoid_optional_locks(m_avoid_locks.get_active());

    settings.save();
}

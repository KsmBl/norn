/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <gtkmm/checkbutton.h>
#include <gtkmm/dialog.h>
#include <gtkmm/notebook.h>
#include <gtkmm/spinbutton.h>

/*!
 * The settings window.
 *
 * Deliberately short. Anything that belongs to the repository rather than to this
 * application — the user's name, whether commits are signed, how line endings are
 * handled — stays in their Git configuration, where their terminal can see it too.
 * Duplicating those here would create two places to look and one of them would be
 * wrong; the dialog says so rather than leaving the omission to be discovered.
 */
class SettingsDialog : public Gtk::Dialog
{
public:
    explicit SettingsDialog(Gtk::Window &parent);

    /*! Writes the edited values back and notifies every open window. */
    void apply();

private:
    void build_ui();

    Gtk::Notebook m_pages;

    Gtk::SpinButton m_context_lines;
    Gtk::SpinButton m_max_diff_lines;
    Gtk::SpinButton m_history_page_size;
    Gtk::CheckButton m_show_graph{"Draw branch lines beside the commit list"};

    Gtk::CheckButton m_sign_off{"Add a Signed-off-by trailer"};
    Gtk::SpinButton m_subject_soft_limit;

    Gtk::CheckButton m_auto_refresh{"Reload when the repository changes on disk"};
    Gtk::CheckButton m_avoid_locks{"Avoid taking repository locks"};
};

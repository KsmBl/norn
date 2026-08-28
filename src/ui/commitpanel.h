/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>

#include <string>

class CommitService;
class Repository;

/*!
 * The commit message editor.
 *
 * Subject and body are separate fields rather than one text area, because the
 * subject has different rules — one line, kept short — and separating them is the
 * simplest way to make the blank line between them impossible to get wrong.
 */
class CommitPanel : public Gtk::Box
{
public:
    CommitPanel(Repository &repository, CommitService &commit_service);

    /*! The assembled message: subject, blank line, body. */
    std::string message() const;

    bool is_amending() const;

    void clear();

    /*! Re-reads the settings that affect this panel. */
    void apply_settings();

    /*! The user asked to commit and then push in one go. */
    sigc::signal<void()> &signal_commit_and_push_requested()
    {
        return m_signal_commit_and_push_requested;
    }

private:
    void build_ui();
    void update_state();
    void do_commit();
    void on_amend_toggled();

    Repository &m_repository;
    CommitService &m_commit_service;

    Gtk::Box m_subject_row{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Entry m_subject;
    Gtk::Label m_counter;
    Gtk::ScrolledWindow m_body_scroller;
    Gtk::TextView m_body;
    Gtk::Box m_options{Gtk::ORIENTATION_HORIZONTAL};

    Gtk::CheckButton m_amend{"Amend last commit"};
    Gtk::CheckButton m_sign_off{"Sign off"};
    Gtk::CheckButton m_no_verify{"Skip hooks"};
    Gtk::Button m_commit{"Commit"};
    Gtk::Button m_commit_and_push{"Commit and Push"};

    /*! Whatever was typed before amend mode replaced it, so unticking can restore it. */
    Glib::ustring m_saved_subject;
    Glib::ustring m_saved_body;

    /*! Length past which the summary counter warns; from settings. */
    int m_subject_soft_limit = 50;

    sigc::signal<void()> m_signal_commit_and_push_requested;
};

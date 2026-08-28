/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/rebasetodo.h"

#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/infobar.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treeview.h>

#include <vector>

/*!
 * The interactive rebase plan editor: a reorderable list of commits, each with an
 * action.
 *
 * Rows are shown oldest first, matching the order git applies them, so "squash
 * into the one above" means what it looks like.
 */
class RebaseTodoDialog : public Gtk::Dialog
{
public:
    RebaseTodoDialog(Gtk::Window &parent, const std::string &upstream, const std::vector<RebaseStep> &steps);

    /*! The edited plan. */
    std::vector<RebaseStep> steps() const;

private:
    class Columns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Columns()
        {
            add(m_action_label);
            add(m_commit);
            add(m_subject);
            add(m_action);
            add(m_strikethrough);
        }

        Gtk::TreeModelColumn<Glib::ustring> m_action_label;
        Gtk::TreeModelColumn<Glib::ustring> m_commit;
        Gtk::TreeModelColumn<Glib::ustring> m_subject;
        Gtk::TreeModelColumn<int> m_action;
        Gtk::TreeModelColumn<bool> m_strikethrough;
    };

    void build_ui(const std::string &upstream);
    void populate(const std::vector<RebaseStep> &steps);
    void set_action_for_selection(RebaseStep::Action action);
    void move_selection(int offset);
    void update_warnings();

    Columns m_columns;
    Glib::RefPtr<Gtk::ListStore> m_store;
    Gtk::ScrolledWindow m_scroller;
    Gtk::TreeView m_view;
    Gtk::InfoBar m_warning;
    Gtk::Label m_warning_label;
};

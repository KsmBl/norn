/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/diffservice.h"
#include "statuscolumns.h"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/menu.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treeview.h>

#include <string>
#include <vector>

class ConflictService;
class IndexService;
class Repository;

/*!
 * The staging area: conflicted, staged and unstaged file lists.
 *
 * Three lists rather than two, because a conflicted path is neither staged nor
 * unstaged and needs its own resolution actions.
 */
class WorkingCopyView : public Gtk::Box
{
public:
    WorkingCopyView(Repository &repository, IndexService &index_service, ConflictService &conflict_service);

    void stage_selected();
    void unstage_selected();
    /*!
     * Discards the selected unstaged changes after confirming. Untracked files are
     * deleted rather than restored, since there is nothing to restore them from.
     */
    void discard_selected();
    void stage_all();
    void unstage_all();

    bool has_unstaged_selection() const;
    bool has_staged_selection() const;

    /*! The current file changed; @p path is empty when nothing is selected. */
    sigc::signal<void(const std::string &, DiffSide, DiffMode, bool)> &signal_current_file_changed()
    {
        return m_signal_current_file_changed;
    }
    sigc::signal<void()> &signal_selection_changed()
    {
        return m_signal_selection_changed;
    }
    sigc::signal<void(const std::string &)> &signal_edit_requested()
    {
        return m_signal_edit_requested;
    }
    sigc::signal<void(const std::string &)> &signal_open_externally_requested()
    {
        return m_signal_open_externally_requested;
    }

private:
    /*! One titled, scrolling list; the shape all three sections share. */
    struct Section {
        Gtk::Box m_box{Gtk::ORIENTATION_VERTICAL};
        Gtk::Label m_label;
        Gtk::ScrolledWindow m_scroller;
        Gtk::TreeView m_view;
        Glib::RefPtr<Gtk::ListStore> m_store;
    };

    void build_section(Section &section, StatusSection which);
    void refresh();
    void on_selection_changed(Section &section, StatusSection which);
    bool on_button_press(GdkEventButton *event, Section &section, StatusSection which);
    void show_context_menu(Section &section, StatusSection which, GdkEventButton *event);
    void build_conflict_menu(const Gtk::TreeModel::Row &row);

    std::vector<std::string> selected_paths(const Section &section) const;
    std::vector<std::string> all_paths(const Section &section) const;

    Repository &m_repository;
    IndexService &m_index_service;
    ConflictService &m_conflict_service;

    StatusColumns m_columns;
    Section m_conflicted;
    Section m_staged;
    Section m_unstaged;
    Gtk::Paned m_splitter{Gtk::ORIENTATION_VERTICAL};
    Gtk::Paned m_lower_splitter{Gtk::ORIENTATION_VERTICAL};

    /*! Kept alive for as long as it is shown. */
    Gtk::Menu m_menu;

    /*! Cleared once something has been selected, so the initial pick happens once. */
    bool m_needs_initial_selection = true;
    /*! Guards against the reselection that clearing the other lists would cause. */
    bool m_updating_selection = false;

    sigc::signal<void(const std::string &, DiffSide, DiffMode, bool)> m_signal_current_file_changed;
    sigc::signal<void()> m_signal_selection_changed;
    sigc::signal<void(const std::string &)> m_signal_edit_requested;
    sigc::signal<void(const std::string &)> m_signal_open_externally_requested;
};

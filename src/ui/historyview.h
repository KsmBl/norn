/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/commitrecord.h"
#include "core/graphlayout.h"
#include "graphcellrenderer.h"

#include <gtkmm/box.h>
#include <gtkmm/liststore.h>
#include <gtkmm/menu.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <gtkmm/treeview.h>

#include <string>
#include <vector>

class RebaseService;
class RefService;
class Repository;

/*!
 * The commit history: a graph on the left, the selected commit's details and its
 * diff below.
 *
 * The log is read a page at a time as the user scrolls, so opening a repository
 * with a long history costs one page rather than the whole log.
 */
class HistoryView : public Gtk::Box
{
public:
    HistoryView(Repository &repository, RefService &ref_service, RebaseService &rebase_service);

    /*! Reloads the history from the top. */
    void reload();

    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> &signal_failed()
    {
        return m_signal_failed;
    }
    /*! The user asked to rewrite history from @p upstream onwards. */
    sigc::signal<void(const std::string &)> &signal_interactive_rebase_requested()
    {
        return m_signal_interactive_rebase_requested;
    }

private:
    class Columns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Columns()
        {
            add(m_subject_markup);
            add(m_author);
            add(m_date);
            add(m_short_commit);
            add(m_commit);
            add(m_row_index);
        }

        Gtk::TreeModelColumn<Glib::ustring> m_subject_markup;
        Gtk::TreeModelColumn<Glib::ustring> m_author;
        Gtk::TreeModelColumn<Glib::ustring> m_date;
        Gtk::TreeModelColumn<Glib::ustring> m_short_commit;
        Gtk::TreeModelColumn<std::string> m_commit;
        /*! Index into m_commits and m_graph_rows. */
        Gtk::TreeModelColumn<int> m_row_index;
    };

    void build_ui();
    void load_page();
    void append_page(const std::vector<CommitRecord> &page);
    void on_selection_changed();
    bool on_scroll_near_end();
    bool on_button_press(GdkEventButton *event);
    void show_context_menu(const CommitRecord &commit, GdkEventButton *event);
    void on_graph_cell_data(Gtk::CellRenderer *renderer, const Gtk::TreeModel::iterator &iter);

    Repository &m_repository;
    RefService &m_ref_service;
    RebaseService &m_rebase_service;

    Columns m_columns;
    Glib::RefPtr<Gtk::ListStore> m_store;
    GraphLayout m_layout;
    std::vector<CommitRecord> m_commits;
    std::vector<GraphRow> m_graph_rows;

    Gtk::Paned m_splitter{Gtk::ORIENTATION_VERTICAL};
    Gtk::ScrolledWindow m_scroller;
    Gtk::TreeView m_view;
    GraphCellRenderer *m_graph_renderer = nullptr;

    Gtk::Box m_detail_box{Gtk::ORIENTATION_VERTICAL};
    Gtk::ScrolledWindow m_detail_scroller;
    Gtk::TextView m_detail;
    Gtk::Menu m_menu;

    /*! False once a page comes back shorter than requested. */
    bool m_has_more = true;
    /*! True while a page request is outstanding, so scrolling cannot pile them up. */
    bool m_loading = false;
    /*! Invalidates in-flight pages after a reload. */
    unsigned long m_generation = 0;
    /*! Cleared once the first page has been requested. */
    bool m_needs_initial_load = true;
    /*! Cleared once a commit has been selected, so the initial pick happens once. */
    bool m_needs_initial_selection = true;

    sigc::signal<void(const Glib::ustring &, const Glib::ustring &)> m_signal_failed;
    sigc::signal<void(const std::string &)> m_signal_interactive_rebase_requested;
};

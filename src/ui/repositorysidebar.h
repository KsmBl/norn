/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <gtkmm/box.h>
#include <gtkmm/menu.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treeview.h>

#include <set>
#include <string>

class RefService;
class Repository;

/*!
 * The side pane: branches, remotes, tags, stashes, worktrees and submodules in one
 * tree, with the operations for each reachable from its context menu.
 *
 * One tree rather than a panel per category, so the pane stays the shape a file
 * manager's Places pane has instead of turning into six competing panels.
 */
class RepositorySidebar : public Gtk::Box
{
public:
    RepositorySidebar(Repository &repository, RefService &ref_service);
    ~RepositorySidebar() override;

    void create_branch();
    void create_tag();
    void stash_changes();
    void add_worktree();

    /*! A worktree or submodule should be opened as a repository of its own. */
    sigc::signal<void(const std::string &)> &signal_open_repository_requested()
    {
        return m_signal_open_repository_requested;
    }

private:
    enum class NodeKind {
        Section,
        LocalBranch,
        Remote,
        RemoteBranch,
        Tag,
        Stash,
        Worktree,
        Submodule,
    };

    class Columns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Columns()
        {
            add(m_key);
            add(m_icon);
            add(m_display);
            add(m_tooltip);
            add(m_kind);
            add(m_name);
            add(m_path);
            add(m_commit);
            add(m_is_head);
            add(m_stash_index);
            add(m_stash_object);
            add(m_weight);
            add(m_removable);
            add(m_has_contents);
        }

        /*! Identity of a row across a rebuild, so expansion and selection survive one. */
        Gtk::TreeModelColumn<std::string> m_key;
        Gtk::TreeModelColumn<Glib::ustring> m_icon;
        Gtk::TreeModelColumn<Glib::ustring> m_display;
        Gtk::TreeModelColumn<Glib::ustring> m_tooltip;
        Gtk::TreeModelColumn<int> m_kind;
        /*! Short ref name, or the remote's name for a Remote row. */
        Gtk::TreeModelColumn<std::string> m_name;
        Gtk::TreeModelColumn<std::string> m_path;
        Gtk::TreeModelColumn<std::string> m_commit;
        Gtk::TreeModelColumn<bool> m_is_head;
        Gtk::TreeModelColumn<int> m_stash_index;
        Gtk::TreeModelColumn<std::string> m_stash_object;
        Gtk::TreeModelColumn<int> m_weight;
        /*! False for the main worktree and the one currently open. */
        Gtk::TreeModelColumn<bool> m_removable;
        /*! False for a section with nothing under it, which renders it dimmed. */
        Gtk::TreeModelColumn<bool> m_has_contents;
    };

    void build_ui();
    void rebuild();
    /*! Everything the pane renders, flattened, so an unchanged refresh is cheap to spot. */
    std::string signature_of(const Glib::RefPtr<Gtk::TreeStore> &store) const;
    /*! Re-opens every row the user had not collapsed, and re-selects what was selected. */
    void restore_view_state(const std::string &selected_key);
    /*! Puts the scroll position back once the view has been laid out again. */
    void restore_scroll(double value);
    void on_row_expansion_changed(const Gtk::TreeModel::iterator &iterator, bool expanded);
    bool on_button_press(GdkEventButton *event);
    void show_context_menu(const Gtk::TreeModel::Row &row, GdkEventButton *event);
    void on_row_activated(const Gtk::TreeModel::Path &path, Gtk::TreeViewColumn *column);

    /*! Asks for a single line of text; returns false if the user cancelled. */
    bool ask_for_text(const Glib::ustring &title, const Glib::ustring &prompt, Glib::ustring &value, const Glib::ustring &initial = {});
    bool confirm(const Glib::ustring &question, const Glib::ustring &detail, const Glib::ustring &accept_label);

    Repository &m_repository;
    RefService &m_ref_service;

    Columns m_columns;
    Glib::RefPtr<Gtk::TreeStore> m_store;
    Gtk::ScrolledWindow m_scroller;
    Gtk::TreeView m_view;
    Gtk::Menu m_menu;

    /*! Keys of the rows the user closed. Everything absent from it opens by default. */
    std::set<std::string> m_collapsed;
    /*! Set while rebuild() re-opens rows, so its own expansions are not recorded. */
    bool m_restoring_view_state = false;
    /*! What the pane last rendered. An identical refresh is dropped rather than rebuilt. */
    std::string m_signature;
    /*! Owned, because it fires after this widget could have been destroyed. */
    sigc::connection m_scroll_restore;

    sigc::signal<void(const std::string &)> m_signal_open_repository_requested;
};

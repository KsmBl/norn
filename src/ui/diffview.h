/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/diffservice.h"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/buttonbox.h>
#include <gtkmm/infobar.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treeview.h>

#include <string>

/*!
 * Shows one file's diff and lets hunks or individual lines be staged from it.
 *
 * Which actions are offered depends on which side is being shown: a staged diff
 * can be unstaged, an unstaged one can be staged or discarded.
 */
class DiffView : public Gtk::Box
{
public:
    explicit DiffView(DiffService &diff_service);

    /*! Shows @p path from @p side. An empty path clears the view. */
    void show_file(const std::string &path, DiffSide side, DiffMode mode = DiffMode::Changes, bool conflicted = false);

    /*! Re-reads the current file, after the index changed underneath it. */
    void reload();

private:
    /*! Rows are hunk headers and lines, flattened; a diff reads as one document. */
    class Columns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Columns()
        {
            add(m_old_line);
            add(m_new_line);
            add(m_text);
            add(m_background);
            add(m_foreground);
            add(m_hunk_index);
            add(m_line_index);
            add(m_is_change);
            add(m_is_header);
        }

        Gtk::TreeModelColumn<Glib::ustring> m_old_line;
        Gtk::TreeModelColumn<Glib::ustring> m_new_line;
        Gtk::TreeModelColumn<Glib::ustring> m_text;
        Gtk::TreeModelColumn<Glib::ustring> m_background;
        Gtk::TreeModelColumn<Glib::ustring> m_foreground;
        Gtk::TreeModelColumn<int> m_hunk_index;
        /*! -1 for a hunk header row. */
        Gtk::TreeModelColumn<int> m_line_index;
        Gtk::TreeModelColumn<bool> m_is_change;
        Gtk::TreeModelColumn<bool> m_is_header;
    };

    void build_ui();
    void populate(const DiffFile &file);
    void on_diff_ready(const std::string &path, DiffSide side, const DiffFile &file);
    void on_diff_empty(const std::string &path, DiffSide side);
    void update_actions();
    void apply_selection(PatchBuilder::Direction direction, bool whole_hunks);

    DiffService &m_diff_service;

    Columns m_columns;
    Glib::RefPtr<Gtk::ListStore> m_store;
    DiffFile m_file;

    Gtk::Label m_header;
    Gtk::InfoBar m_notice;
    Gtk::Label m_notice_label;
    Gtk::ScrolledWindow m_scroller;
    Gtk::TreeView m_view;
    Gtk::ButtonBox m_buttons{Gtk::ORIENTATION_HORIZONTAL};

    Gtk::Button m_stage_hunk{"Stage Hunk"};
    Gtk::Button m_stage_lines{"Stage Lines"};
    Gtk::Button m_unstage_hunk{"Unstage Hunk"};
    Gtk::Button m_unstage_lines{"Unstage Lines"};
    Gtk::Button m_discard{"Discard Hunk"};

    std::string m_path;
    DiffSide m_side = DiffSide::Unstaged;
    DiffMode m_mode = DiffMode::Changes;
    bool m_conflicted = false;
};

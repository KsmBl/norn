/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/statusentry.h"

#include <gtkmm/liststore.h>
#include <gtkmm/treemodelcolumn.h>

/*!
 * Columns for the staged, unstaged and conflicted file lists.
 *
 * One record shared by all three, because they show the same thing and differ only
 * in which entries they contain and which status letter is the relevant one.
 */
class StatusColumns : public Gtk::TreeModel::ColumnRecord
{
public:
    StatusColumns()
    {
        add(m_icon);
        add(m_display);
        add(m_tooltip);
        add(m_path);
        add(m_untracked);
        add(m_conflicted);
        add(m_conflict_code);
        add(m_submodule);
        add(m_status_letter);
    }

    Gtk::TreeModelColumn<Glib::ustring> m_icon;
    Gtk::TreeModelColumn<Glib::ustring> m_display;
    Gtk::TreeModelColumn<Glib::ustring> m_tooltip;
    /*! The raw path, for handing back to git as a literal pathspec. */
    Gtk::TreeModelColumn<std::string> m_path;
    Gtk::TreeModelColumn<bool> m_untracked;
    Gtk::TreeModelColumn<bool> m_conflicted;
    Gtk::TreeModelColumn<Glib::ustring> m_conflict_code;
    Gtk::TreeModelColumn<bool> m_submodule;
    Gtk::TreeModelColumn<char> m_status_letter;
};

/*! Which of the three lists a row belongs to. */
enum class StatusSection {
    Staged,
    Unstaged,
    Conflicted,
};

/*!
 * Fills @p store with the entries from @p snapshot belonging to @p section.
 *
 * Staged and unstaged are separate lists rather than one filtered list, because a
 * path with staged edits and further unstaged edits on top legitimately belongs in
 * both at once.
 */
void fill_status_store(const Glib::RefPtr<Gtk::ListStore> &store, const StatusColumns &columns, const class StatusSnapshot &snapshot, StatusSection section);

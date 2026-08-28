/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "statuscolumns.h"

#include "core/statussnapshot.h"

#include <algorithm>

namespace
{
/*! The status letter this section cares about for @p entry. */
char letter_for(const StatusEntry &entry, StatusSection section)
{
    if (entry.m_kind == StatusEntry::Kind::Untracked) {
        return '?';
    }
    if (entry.m_kind == StatusEntry::Kind::Unmerged) {
        return 'U';
    }
    return section == StatusSection::Staged ? entry.m_index_status : entry.m_worktree_status;
}

Glib::ustring icon_for(const StatusEntry &entry, StatusSection section)
{
    if (entry.m_kind == StatusEntry::Kind::Unmerged) {
        return "dialog-warning-symbolic";
    }

    switch (letter_for(entry, section)) {
    case 'A':
    case '?':
        // Not a "modified" icon: an untracked or newly added file is not a change to
        // something, it is something that was not there before.
        return "list-add-symbolic";
    case 'D':
        return "edit-delete-symbolic";
    default:
        return "document-edit-symbolic";
    }
}

Glib::ustring description_for(const StatusEntry &entry, StatusSection section)
{
    if (entry.m_kind == StatusEntry::Kind::Unmerged) {
        const std::string code = entry.conflict_code();
        if (code == "DD") {
            return "Deleted by both sides";
        }
        if (code == "AU") {
            return "Added by us";
        }
        if (code == "UD") {
            return "Deleted by them";
        }
        if (code == "UA") {
            return "Added by them";
        }
        if (code == "DU") {
            return "Deleted by us";
        }
        if (code == "AA") {
            return "Added by both sides";
        }
        return "Modified by both sides";
    }

    switch (letter_for(entry, section)) {
    case 'M':
        return "Modified";
    case 'A':
        return "Added";
    case 'D':
        return "Deleted";
    case 'R':
        return Glib::ustring::compose("Renamed from %1", entry.m_orig_path);
    case 'C':
        return Glib::ustring::compose("Copied from %1", entry.m_orig_path);
    case 'T':
        return "Type changed";
    case '?':
        return "Untracked";
    default:
        return {};
    }
}
}

void fill_status_store(const Glib::RefPtr<Gtk::ListStore> &store, const StatusColumns &columns, const StatusSnapshot &snapshot, StatusSection section)
{
    std::vector<StatusEntry> matching;
    for (const StatusEntry &entry : snapshot.m_entries) {
        const bool wanted = (section == StatusSection::Staged && entry.is_staged()) //
            || (section == StatusSection::Unstaged && entry.is_unstaged()) //
            || (section == StatusSection::Conflicted && entry.is_conflicted());
        if (wanted) {
            matching.push_back(entry);
        }
    }

    std::sort(matching.begin(), matching.end(), [](const StatusEntry &lhs, const StatusEntry &rhs) {
        return lhs.m_path < rhs.m_path;
    });

    store->clear();

    for (const StatusEntry &entry : matching) {
        Gtk::TreeModel::Row row = *store->append();

        row[columns.m_icon] = icon_for(entry, section);
        row[columns.m_path] = entry.m_path;

        // A rename reads as the move it is, rather than as an unexplained new name.
        row[columns.m_display] = entry.m_kind == StatusEntry::Kind::RenamedOrCopied && section == StatusSection::Staged
            ? Glib::ustring::compose("%1 → %2", entry.m_orig_path, entry.m_path)
            : Glib::ustring(entry.m_path);

        const Glib::ustring description = description_for(entry, section);
        row[columns.m_tooltip] = entry.m_is_submodule ? Glib::ustring::compose("%1 (submodule)", description) : description;

        row[columns.m_untracked] = entry.m_kind == StatusEntry::Kind::Untracked;
        row[columns.m_conflicted] = entry.is_conflicted();
        row[columns.m_conflict_code] = entry.conflict_code();
        row[columns.m_submodule] = entry.m_is_submodule;
        row[columns.m_status_letter] = letter_for(entry, section);
    }
}

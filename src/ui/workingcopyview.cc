/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "workingcopyview.h"

#include "core/conflictservice.h"
#include "core/indexservice.h"
#include "core/repository.h"

#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/separatormenuitem.h>

namespace
{
/*! Starting height of the conflicted section when it is shown at all. */
constexpr int s_conflicted_height = 150;

/*! Label text for a section, with its count. */
Glib::ustring section_label(const char *singular, const char *plural, int count)
{
    return count == 1 ? Glib::ustring::compose(singular, count) : Glib::ustring::compose(plural, count);
}
}

WorkingCopyView::WorkingCopyView(Repository &repository, IndexService &index_service, ConflictService &conflict_service)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL)
    , m_repository(repository)
    , m_index_service(index_service)
    , m_conflict_service(conflict_service)
{
    build_section(m_conflicted, StatusSection::Conflicted);
    build_section(m_staged, StatusSection::Staged);
    build_section(m_unstaged, StatusSection::Unstaged);

    // Conflicts on top, since they are what needs attention. They get a starting
    // height of their own rather than being squeezed to one row: the whole point of
    // the section is that the list is readable without scrolling it first.
    m_conflicted.m_box.set_size_request(-1, s_conflicted_height);

    m_lower_splitter.pack1(m_staged.m_box, true, false);
    m_lower_splitter.pack2(m_unstaged.m_box, true, false);

    m_splitter.pack1(m_conflicted.m_box, false, false);
    m_splitter.pack2(m_lower_splitter, true, false);

    pack_start(m_splitter, Gtk::PACK_EXPAND_WIDGET);

    m_repository.signal_status_changed().connect(sigc::mem_fun(*this, &WorkingCopyView::refresh));
    refresh();

    show_all_children();
    // Only shown when there is actually a conflict to resolve.
    m_conflicted.m_box.hide();
}

void WorkingCopyView::build_section(Section &section, StatusSection which)
{
    section.m_label.set_xalign(0.0F);
    section.m_label.set_margin_start(6);
    section.m_label.set_margin_top(4);
    section.m_label.set_margin_bottom(2);
    section.m_box.pack_start(section.m_label, Gtk::PACK_SHRINK);

    section.m_store = Gtk::ListStore::create(m_columns);
    section.m_view.set_model(section.m_store);
    section.m_view.set_headers_visible(false);
    section.m_view.set_enable_search(true);
    section.m_view.set_search_column(m_columns.m_display);
    section.m_view.set_tooltip_column(m_columns.m_tooltip.index());
    section.m_view.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);

    // Icon then name, the way a file manager lists things.
    auto *column = Gtk::manage(new Gtk::TreeViewColumn());
    auto *icon = Gtk::manage(new Gtk::CellRendererPixbuf());
    icon->property_stock_size() = Gtk::ICON_SIZE_MENU;
    column->pack_start(*icon, false);
    column->add_attribute(icon->property_icon_name(), m_columns.m_icon);

    auto *text = Gtk::manage(new Gtk::CellRendererText());
    // Paths matter most at the right-hand end, so elide the directory prefix.
    text->property_ellipsize() = Pango::ELLIPSIZE_START;
    column->pack_start(*text, true);
    column->add_attribute(text->property_text(), m_columns.m_display);

    section.m_view.append_column(*column);

    section.m_scroller.add(section.m_view);
    section.m_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    // Frameless: panes sit flush against each other rather than each being a box.
    section.m_scroller.set_shadow_type(Gtk::SHADOW_NONE);
    section.m_box.pack_start(section.m_scroller, Gtk::PACK_EXPAND_WIDGET);

    section.m_view.get_selection()->signal_changed().connect([this, &section, which] {
        on_selection_changed(section, which);
    });

    section.m_view.signal_button_press_event().connect(
        [this, &section, which](GdkEventButton *event) {
            return on_button_press(event, section, which);
        },
        false);

    // Double-clicking is the fastest way to move one file across, so make it work in
    // the direction the list implies.
    section.m_view.signal_row_activated().connect([this, which](const Gtk::TreeModel::Path &, Gtk::TreeViewColumn *) {
        if (which == StatusSection::Unstaged) {
            stage_selected();
        } else if (which == StatusSection::Staged) {
            unstage_selected();
        }
    });
}

void WorkingCopyView::refresh()
{
    const StatusSnapshot &snapshot = m_repository.status();

    m_updating_selection = true;
    fill_status_store(m_conflicted.m_store, m_columns, snapshot, StatusSection::Conflicted);
    fill_status_store(m_staged.m_store, m_columns, snapshot, StatusSection::Staged);
    fill_status_store(m_unstaged.m_store, m_columns, snapshot, StatusSection::Unstaged);
    m_updating_selection = false;

    const int conflicted = static_cast<int>(m_conflicted.m_store->children().size());
    const int staged = static_cast<int>(m_staged.m_store->children().size());
    const int unstaged = static_cast<int>(m_unstaged.m_store->children().size());

    m_conflicted.m_box.set_visible(conflicted > 0);
    m_conflicted.m_label.set_markup("<b>" + section_label("%1 conflicting file", "%1 conflicting files", conflicted) + "</b>");
    m_staged.m_label.set_markup("<b>" + section_label("%1 staged change", "%1 staged changes", staged) + "</b>");
    m_unstaged.m_label.set_markup("<b>" + section_label("%1 unstaged change", "%1 unstaged changes", unstaged) + "</b>");

    // Open on something rather than an empty diff pane. Conflicts first, since those
    // are what needs attention; otherwise whatever is waiting to be staged.
    if (m_needs_initial_selection) {
        for (Section *section : {&m_conflicted, &m_unstaged, &m_staged}) {
            if (!section->m_store->children().empty()) {
                section->m_view.get_selection()->select(section->m_store->children().begin());
                m_needs_initial_selection = false;
                break;
            }
        }
    }

    m_signal_selection_changed.emit();
}

void WorkingCopyView::on_selection_changed(Section &section, StatusSection which)
{
    if (m_updating_selection) {
        return;
    }

    m_signal_selection_changed.emit();

    const std::vector<Gtk::TreeModel::Path> selected = section.m_view.get_selection()->get_selected_rows();
    if (selected.empty()) {
        return;
    }

    // Selecting in one list clears the others, so the diff view is never ambiguous
    // about which side it is showing.
    m_updating_selection = true;
    for (Section *other : {&m_conflicted, &m_staged, &m_unstaged}) {
        if (other != &section) {
            other->m_view.get_selection()->unselect_all();
        }
    }
    m_updating_selection = false;

    const Gtk::TreeModel::Row row = *section.m_store->get_iter(selected.front());

    const bool conflicted = row[m_columns.m_conflicted];
    const bool untracked = row[m_columns.m_untracked];
    // An untracked file has no diff, and a conflicted one's real diff is a combined
    // diff; in both cases the whole file is what needs showing.
    const DiffMode mode = (conflicted || untracked) ? DiffMode::WholeFile : DiffMode::Changes;
    const DiffSide side = which == StatusSection::Staged ? DiffSide::Staged : DiffSide::Unstaged;

    m_signal_current_file_changed.emit(row[m_columns.m_path], side, mode, conflicted);
}

std::vector<std::string> WorkingCopyView::selected_paths(const Section &section) const
{
    std::vector<std::string> paths;
    for (const Gtk::TreeModel::Path &path : section.m_view.get_selection()->get_selected_rows()) {
        paths.push_back((*section.m_store->get_iter(path))[m_columns.m_path]);
    }
    return paths;
}

std::vector<std::string> WorkingCopyView::all_paths(const Section &section) const
{
    std::vector<std::string> paths;
    for (const Gtk::TreeModel::Row &row : section.m_store->children()) {
        paths.push_back(row[m_columns.m_path]);
    }
    return paths;
}

bool WorkingCopyView::has_unstaged_selection() const
{
    return m_unstaged.m_view.get_selection()->count_selected_rows() > 0;
}

bool WorkingCopyView::has_staged_selection() const
{
    return m_staged.m_view.get_selection()->count_selected_rows() > 0;
}

void WorkingCopyView::stage_selected()
{
    m_index_service.stage(selected_paths(m_unstaged));
}

void WorkingCopyView::unstage_selected()
{
    m_index_service.unstage(selected_paths(m_staged));
}

void WorkingCopyView::stage_all()
{
    m_index_service.stage(all_paths(m_unstaged));
}

void WorkingCopyView::unstage_all()
{
    m_index_service.unstage(all_paths(m_staged));
}

void WorkingCopyView::discard_selected()
{
    // Tracked and untracked need different commands: `restore` has nothing to
    // restore an untracked file from, so those have to be deleted outright.
    std::vector<std::string> tracked;
    std::vector<std::string> untracked;
    Glib::ustring names;

    for (const Gtk::TreeModel::Path &path : m_unstaged.m_view.get_selection()->get_selected_rows()) {
        const Gtk::TreeModel::Row row = *m_unstaged.m_store->get_iter(path);
        if (!names.empty()) {
            names += "\n";
        }
        names += row[m_columns.m_display];

        if (row[m_columns.m_untracked]) {
            untracked.push_back(row[m_columns.m_path]);
        } else {
            tracked.push_back(row[m_columns.m_path]);
        }
    }

    if (tracked.empty() && untracked.empty()) {
        return;
    }

    const std::size_t count = tracked.size() + untracked.size();

    // There is no undo for this: the content never entered git, so no reflog or
    // stash can bring it back.
    Gtk::MessageDialog dialog(*dynamic_cast<Gtk::Window *>(get_toplevel()),
                              count == 1 ? "Discard the change to this file?" : Glib::ustring::compose("Discard the changes to these %1 files?", count),
                              false,
                              Gtk::MESSAGE_WARNING,
                              Gtk::BUTTONS_NONE,
                              true);
    dialog.set_secondary_text(untracked.empty() ? Glib::ustring::compose("This cannot be undone.\n\n%1", names)
                                                : Glib::ustring::compose("Untracked files will be deleted. This cannot be undone.\n\n%1", names));
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_Discard", Gtk::RESPONSE_ACCEPT);
    dialog.set_default_response(Gtk::RESPONSE_CANCEL);

    if (dialog.run() != Gtk::RESPONSE_ACCEPT) {
        return;
    }

    if (!tracked.empty()) {
        m_index_service.discard(tracked);
    }
    if (!untracked.empty()) {
        m_index_service.delete_untracked(untracked);
    }
}

bool WorkingCopyView::on_button_press(GdkEventButton *event, Section &section, StatusSection which)
{
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) {
        return false;
    }

    Gtk::TreeModel::Path path;
    if (!section.m_view.get_path_at_pos(static_cast<int>(event->x), static_cast<int>(event->y), path)) {
        return false;
    }

    // Right-clicking outside the selection acts on the row under the pointer, which
    // is what every file manager does.
    if (!section.m_view.get_selection()->is_selected(path)) {
        section.m_view.get_selection()->unselect_all();
        section.m_view.get_selection()->select(path);
    }

    show_context_menu(section, which, event);
    return true;
}

void WorkingCopyView::show_context_menu(Section &section, StatusSection which, GdkEventButton *event)
{
    m_menu.foreach ([this](Gtk::Widget &child) {
        m_menu.remove(child);
    });

    const std::vector<Gtk::TreeModel::Path> selected = section.m_view.get_selection()->get_selected_rows();
    if (selected.empty()) {
        return;
    }

    const Gtk::TreeModel::Row row = *section.m_store->get_iter(selected.front());

    const auto add_item = [this](const Glib::ustring &label, const sigc::slot<void()> &slot) {
        auto *item = Gtk::manage(new Gtk::MenuItem(label, true));
        item->signal_activate().connect(slot);
        m_menu.append(*item);
    };

    if (which == StatusSection::Conflicted) {
        build_conflict_menu(row);
    } else if (which == StatusSection::Unstaged) {
        add_item("_Stage", sigc::mem_fun(*this, &WorkingCopyView::stage_selected));
        m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item("_Discard Changes…", sigc::mem_fun(*this, &WorkingCopyView::discard_selected));
    } else {
        add_item("_Unstage", sigc::mem_fun(*this, &WorkingCopyView::unstage_selected));
    }

    // A deleted file has nothing to open, and a submodule is a directory rather
    // than something an editor can show.
    const char letter = row[m_columns.m_status_letter];
    const bool openable = !row[m_columns.m_submodule] && letter != 'D';
    if (openable) {
        const std::string path = row[m_columns.m_path];
        m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item("_Edit", [this, path] {
            m_signal_edit_requested.emit(path);
        });
        add_item("Open _With…", [this, path] {
            m_signal_open_externally_requested.emit(path);
        });
    }

    m_menu.show_all();
    m_menu.popup_at_pointer(reinterpret_cast<GdkEvent *>(event));
}

void WorkingCopyView::build_conflict_menu(const Gtk::TreeModel::Row &row)
{
    const std::string path = row[m_columns.m_path];
    const Glib::ustring code = row[m_columns.m_conflict_code];

    const auto add_item = [this](const Glib::ustring &label, const sigc::slot<void()> &slot) {
        auto *item = Gtk::manage(new Gtk::MenuItem(label, true));
        item->signal_activate().connect(slot);
        m_menu.append(*item);
    };

    // The sensible actions differ by conflict kind. Only "both modified" and "both
    // added" have two versions of the content to choose between; the rest are
    // delete-versus-modify conflicts where the real question is whether the file
    // should exist at all, and offering a text merge for those is misleading.
    const bool both_have_content = code == "UU" || code == "AA";
    const bool deleted_by_us = code == "DU";
    const bool deleted_by_them = code == "UD";
    const bool deleted_by_both = code == "DD";

    if (deleted_by_both) {
        add_item("Accept the _deletion", [this, path] {
            m_conflict_service.remove_conflicted(path);
        });
        return;
    }

    if (both_have_content) {
        add_item("Keep _our version", [this, path] {
            m_conflict_service.take_ours(path);
        });
        add_item("Keep _their version", [this, path] {
            m_conflict_service.take_theirs(path);
        });
        m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
        add_item("_Restore conflict markers", [this, path] {
            m_conflict_service.restore_markers(path);
        });
    } else if (deleted_by_us) {
        add_item("Keep it _deleted", [this, path] {
            m_conflict_service.remove_conflicted(path);
        });
        add_item("Restore _their version", [this, path] {
            m_conflict_service.take_theirs(path);
        });
    } else if (deleted_by_them) {
        add_item("Keep _our version", [this, path] {
            m_conflict_service.take_ours(path);
        });
        add_item("Accept the _deletion", [this, path] {
            m_conflict_service.remove_conflicted(path);
        });
    } else {
        // Added by only one side: keep it or drop it.
        add_item("_Keep the file", [this, path] {
            m_conflict_service.mark_resolved(path);
        });
        add_item("_Drop the file", [this, path] {
            m_conflict_service.remove_conflicted(path);
        });
    }

    m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
    add_item("_Mark as resolved", [this, path] {
        m_conflict_service.mark_resolved(path);
    });
}

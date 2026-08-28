/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "diffview.h"

#include "core/settings.h"

#include <gtkmm/cellrenderertext.h>

#include <map>
#include <set>

namespace
{
/*!
 * Tints for added and removed lines.
 *
 * Chosen light enough that the selection highlight still reads over them, and
 * given explicit foregrounds so they stay legible under a dark theme too.
 */
constexpr const char *s_added_background = "rgba(38, 162, 105, 0.20)";
constexpr const char *s_removed_background = "rgba(224, 27, 36, 0.20)";
constexpr const char *s_header_background = "rgba(127, 127, 127, 0.16)";

Glib::ustring line_number(int value)
{
    return value > 0 ? Glib::ustring::format(value) : Glib::ustring();
}
}

DiffView::DiffView(DiffService &diff_service)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL)
    , m_diff_service(diff_service)
{
    build_ui();

    m_diff_service.signal_diff_ready().connect(sigc::mem_fun(*this, &DiffView::on_diff_ready));
    m_diff_service.signal_diff_empty().connect(sigc::mem_fun(*this, &DiffView::on_diff_empty));
    // Applying part of a diff changes the rest of it, so the view has to be re-read
    // rather than left showing offsets that have shifted underneath it.
    m_diff_service.signal_applied().connect(sigc::mem_fun(*this, &DiffView::reload));

    update_actions();
}

void DiffView::build_ui()
{
    m_header.set_xalign(0.0F);
    m_header.set_margin_start(6);
    m_header.set_margin_top(4);
    m_header.set_margin_bottom(4);
    m_header.set_ellipsize(Pango::ELLIPSIZE_START);
    pack_start(m_header, Gtk::PACK_SHRINK);

    dynamic_cast<Gtk::Container *>(m_notice.get_content_area())->add(m_notice_label);
    m_notice_label.set_line_wrap(true);
    m_notice.set_show_close_button(false);
    m_notice.set_no_show_all(true);
    pack_start(m_notice, Gtk::PACK_SHRINK);

    m_store = Gtk::ListStore::create(m_columns);
    m_view.set_model(m_store);
    m_view.set_headers_visible(false);
    m_view.set_enable_search(false);
    m_view.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);

    const auto add_number_column = [this](const Gtk::TreeModelColumn<Glib::ustring> &column) {
        auto *renderer = Gtk::manage(new Gtk::CellRendererText());
        renderer->property_family() = "monospace";
        renderer->property_xalign() = 1.0F;
        // Dimmed: the gutter is a reference, not the content.
        renderer->property_foreground() = "gray";

        auto *view_column = Gtk::manage(new Gtk::TreeViewColumn());
        view_column->pack_start(*renderer, false);
        view_column->add_attribute(renderer->property_text(), column);
        view_column->add_attribute(renderer->property_cell_background(), m_columns.m_background);
        m_view.append_column(*view_column);
    };

    add_number_column(m_columns.m_old_line);
    add_number_column(m_columns.m_new_line);

    auto *text = Gtk::manage(new Gtk::CellRendererText());
    text->property_family() = "monospace";
    auto *text_column = Gtk::manage(new Gtk::TreeViewColumn());
    text_column->pack_start(*text, true);
    text_column->add_attribute(text->property_text(), m_columns.m_text);
    text_column->add_attribute(text->property_cell_background(), m_columns.m_background);
    text_column->add_attribute(text->property_foreground(), m_columns.m_foreground);
    m_view.append_column(*text_column);

    m_scroller.add(m_view);
    m_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_scroller.set_shadow_type(Gtk::SHADOW_NONE);
    pack_start(m_scroller, Gtk::PACK_EXPAND_WIDGET);

    m_buttons.set_layout(Gtk::BUTTONBOX_START);
    m_buttons.set_spacing(4);
    m_buttons.set_margin_top(4);
    m_buttons.set_margin_bottom(4);
    m_buttons.set_margin_start(6);

    for (Gtk::Button *button : {&m_stage_hunk, &m_stage_lines, &m_unstage_hunk, &m_unstage_lines, &m_discard}) {
        button->set_no_show_all(true);
        m_buttons.pack_start(*button, Gtk::PACK_SHRINK);
    }
    pack_start(m_buttons, Gtk::PACK_SHRINK);

    m_stage_hunk.signal_clicked().connect([this] {
        apply_selection(PatchBuilder::Direction::Stage, true);
    });
    m_stage_lines.signal_clicked().connect([this] {
        apply_selection(PatchBuilder::Direction::Stage, false);
    });
    m_unstage_hunk.signal_clicked().connect([this] {
        apply_selection(PatchBuilder::Direction::Unstage, true);
    });
    m_unstage_lines.signal_clicked().connect([this] {
        apply_selection(PatchBuilder::Direction::Unstage, false);
    });
    m_discard.signal_clicked().connect([this] {
        apply_selection(PatchBuilder::Direction::Discard, true);
    });

    m_view.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &DiffView::update_actions));

    show_all_children();
    m_notice.hide();
}

void DiffView::show_file(const std::string &path, DiffSide side, DiffMode mode, bool conflicted)
{
    m_path = path;
    m_side = side;
    m_mode = mode;
    m_conflicted = conflicted;

    if (path.empty()) {
        m_store->clear();
        m_file = DiffFile();
        m_header.set_text({});
        m_notice.hide();
        update_actions();
        return;
    }

    m_header.set_text(path);
    m_diff_service.request_diff(path, side, mode);
}

void DiffView::reload()
{
    if (!m_path.empty()) {
        m_diff_service.request_diff(m_path, m_side, m_mode);
    }
}

void DiffView::populate(const DiffFile &file)
{
    m_store->clear();
    m_file = file;

    const int max_lines = Settings::instance().max_diff_lines();
    int emitted = 0;

    for (std::size_t hunk_index = 0; hunk_index < file.m_hunks.size(); ++hunk_index) {
        const DiffHunk &hunk = file.m_hunks[hunk_index];

        Gtk::TreeModel::Row header = *m_store->append();
        header[m_columns.m_text] = Glib::ustring::compose("@@ -%1,%2 +%3,%4 @@%5",
                                                          hunk.m_old_start,
                                                          hunk.m_old_count,
                                                          hunk.m_new_start,
                                                          hunk.m_new_count,
                                                          hunk.m_heading.empty() ? Glib::ustring() : Glib::ustring(" " + hunk.m_heading));
        header[m_columns.m_background] = s_header_background;
        header[m_columns.m_foreground] = "gray";
        header[m_columns.m_hunk_index] = static_cast<int>(hunk_index);
        header[m_columns.m_line_index] = -1;
        header[m_columns.m_is_change] = false;
        header[m_columns.m_is_header] = true;

        for (std::size_t line_index = 0; line_index < hunk.m_lines.size(); ++line_index) {
            if (++emitted > max_lines) {
                Gtk::TreeModel::Row truncated = *m_store->append();
                truncated[m_columns.m_text] = "… diff truncated; it is longer than the configured limit.";
                truncated[m_columns.m_foreground] = "gray";
                truncated[m_columns.m_line_index] = -1;
                truncated[m_columns.m_hunk_index] = -1;
                truncated[m_columns.m_is_header] = true;
                return;
            }

            const DiffLine &line = hunk.m_lines[line_index];

            Gtk::TreeModel::Row row = *m_store->append();
            row[m_columns.m_old_line] = line_number(line.m_old_line);
            row[m_columns.m_new_line] = line_number(line.m_new_line);
            row[m_columns.m_hunk_index] = static_cast<int>(hunk_index);
            row[m_columns.m_line_index] = static_cast<int>(line_index);
            row[m_columns.m_is_change] = line.is_change();
            row[m_columns.m_is_header] = false;

            switch (line.m_kind) {
            case DiffLine::Kind::Added:
                row[m_columns.m_text] = "+" + line.m_text;
                row[m_columns.m_background] = s_added_background;
                break;
            case DiffLine::Kind::Removed:
                row[m_columns.m_text] = "-" + line.m_text;
                row[m_columns.m_background] = s_removed_background;
                break;
            case DiffLine::Kind::NoNewline:
                row[m_columns.m_text] = "\\ No newline at end of file";
                row[m_columns.m_foreground] = "gray";
                break;
            case DiffLine::Kind::Context:
                row[m_columns.m_text] = " " + line.m_text;
                break;
            }
        }
    }
}

void DiffView::on_diff_ready(const std::string &path, DiffSide side, const DiffFile &file)
{
    // A late answer for a file the user has already navigated away from.
    if (path != m_path || side != m_side) {
        return;
    }

    populate(file);

    m_notice.hide();
    if (m_conflicted) {
        m_notice.set_message_type(Gtk::MESSAGE_WARNING);
        m_notice_label.set_text("This file is conflicting. Shown below is the working tree copy, with git's conflict markers in place.");
        m_notice.show();
    } else if (file.m_is_binary) {
        m_notice.set_message_type(Gtk::MESSAGE_INFO);
        m_notice_label.set_text("This is a binary file, so there is nothing to show line by line.");
        m_notice.show();
    } else if (file.m_is_rename) {
        m_notice.set_message_type(Gtk::MESSAGE_INFO);
        m_notice_label.set_text("This file was renamed. Renames are staged whole, since there is no partial rename to express.");
        m_notice.show();
    }

    update_actions();
}

void DiffView::on_diff_empty(const std::string &path, DiffSide side)
{
    if (path != m_path || side != m_side) {
        return;
    }

    m_store->clear();
    m_file = DiffFile();

    m_notice.set_message_type(Gtk::MESSAGE_INFO);
    m_notice_label.set_text(side == DiffSide::Staged ? "Nothing is staged for this file." : "This file has no unstaged changes.");
    m_notice.show();

    update_actions();
}

void DiffView::update_actions()
{
    const bool has_selection = m_view.get_selection()->count_selected_rows() > 0;
    // Staging part of a conflicted file is not meaningful; resolution comes first.
    const bool partial = m_file.supports_partial_staging() && !m_store->children().empty() && !m_conflicted;
    const bool unstaged_side = m_side == DiffSide::Unstaged;

    m_stage_hunk.set_visible(unstaged_side);
    m_stage_lines.set_visible(unstaged_side);
    m_discard.set_visible(unstaged_side);
    m_unstage_hunk.set_visible(!unstaged_side);
    m_unstage_lines.set_visible(!unstaged_side);

    for (Gtk::Button *button : {&m_stage_hunk, &m_stage_lines, &m_unstage_hunk, &m_unstage_lines, &m_discard}) {
        button->set_sensitive(partial && has_selection);
    }
}

void DiffView::apply_selection(PatchBuilder::Direction direction, bool whole_hunks)
{
    const std::vector<Gtk::TreeModel::Path> selected = m_view.get_selection()->get_selected_rows();
    if (selected.empty()) {
        return;
    }

    if (whole_hunks) {
        std::set<int> hunks;
        for (const Gtk::TreeModel::Path &path : selected) {
            const Gtk::TreeModel::Row row = *m_store->get_iter(path);
            const int hunk = row[m_columns.m_hunk_index];
            if (hunk >= 0) {
                hunks.insert(hunk);
            }
        }
        m_diff_service.apply_hunks(m_file, hunks, direction);
        return;
    }

    std::map<int, std::set<int>> lines;
    for (const Gtk::TreeModel::Path &path : selected) {
        const Gtk::TreeModel::Row row = *m_store->get_iter(path);
        const int hunk_index = row[m_columns.m_hunk_index];
        if (hunk_index < 0) {
            continue;
        }

        if (row[m_columns.m_is_header]) {
            // Selecting the header means the whole hunk.
            const DiffHunk &hunk = m_file.m_hunks[static_cast<std::size_t>(hunk_index)];
            for (std::size_t i = 0; i < hunk.m_lines.size(); ++i) {
                if (hunk.m_lines[i].is_change()) {
                    lines[hunk_index].insert(static_cast<int>(i));
                }
            }
            continue;
        }

        // Context lines are not changes; including them would mean nothing.
        if (row[m_columns.m_is_change]) {
            lines[hunk_index].insert(row[m_columns.m_line_index]);
        }
    }

    m_diff_service.apply_lines(m_file, lines, direction);
}

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "historyview.h"

#include "core/parsers/logparser.h"
#include "core/rebaseservice.h"
#include "core/refservice.h"
#include "core/repository.h"
#include "core/settings.h"

#include <glibmm/markup.h>

#include <gtkmm/cellrenderertext.h>
#include <gtkmm/clipboard.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/separatormenuitem.h>

namespace
{
/*! Shortens refs/heads/main to main, refs/tags/v1 to v1, and so on. */
Glib::ustring shorten_ref(const std::string &ref)
{
    std::string name = ref;
    for (const char *prefix : {"refs/heads/", "refs/remotes/", "refs/tags/"}) {
        const std::size_t at = name.find(prefix);
        if (at != std::string::npos) {
            name.erase(at, std::string(prefix).size());
        }
    }
    return name;
}

/*!
 * Subject with its ref decorations as coloured chips in front of it.
 *
 * Pango markup rather than a custom renderer: chips are just a coloured span, and
 * a renderer would be a lot of drawing code for the same result.
 */
Glib::ustring subject_markup(const CommitRecord &commit)
{
    Glib::ustring markup;

    for (const std::string &ref : commit.m_refs) {
        const Glib::ustring name = shorten_ref(ref);
        // HEAD is what the eye needs to find, so it is coloured differently.
        const char *colour = ref.find("HEAD") != std::string::npos ? "#2ea043" : "#4a90d9";
        markup += Glib::ustring::compose("<span foreground=\"%1\">[%2]</span> ", colour, Glib::Markup::escape_text(name));
    }

    markup += Glib::Markup::escape_text(commit.m_subject);
    return markup;
}

/*! Starting height of the commit detail pane. */
constexpr int s_detail_height = 220;

/*! "2026-08-27 15:04" from git's ISO-8601 output, without the timezone noise. */
Glib::ustring short_date(const std::string &iso)
{
    if (iso.size() < 16) {
        return iso;
    }
    return iso.substr(0, 10) + " " + iso.substr(11, 5);
}
}

HistoryView::HistoryView(Repository &repository, RefService &ref_service, RebaseService &rebase_service)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL)
    , m_repository(repository)
    , m_ref_service(ref_service)
    , m_rebase_service(rebase_service)
{
    build_ui();
    show_all_children();
}

void HistoryView::build_ui()
{
    m_store = Gtk::ListStore::create(m_columns);
    m_view.set_model(m_store);
    m_view.set_headers_visible(true);
    m_view.set_enable_search(true);
    m_view.set_search_column(m_columns.m_subject_markup);
    m_view.get_selection()->set_mode(Gtk::SELECTION_SINGLE);

    // Graph column: a custom renderer fed from a cell data function, since the row
    // it draws is plain C++ data rather than anything with a GType.
    auto *graph_column = Gtk::manage(new Gtk::TreeViewColumn());
    m_graph_renderer = Gtk::manage(new GraphCellRenderer());
    graph_column->pack_start(*m_graph_renderer, false);
    graph_column->set_cell_data_func(*m_graph_renderer, sigc::mem_fun(*this, &HistoryView::on_graph_cell_data));
    graph_column->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);
    m_view.append_column(*graph_column);

    auto *subject = Gtk::manage(new Gtk::CellRendererText());
    subject->property_ellipsize() = Pango::ELLIPSIZE_END;
    auto *subject_column = Gtk::manage(new Gtk::TreeViewColumn("Subject"));
    subject_column->pack_start(*subject, true);
    subject_column->add_attribute(subject->property_markup(), m_columns.m_subject_markup);
    subject_column->set_expand(true);
    subject_column->set_resizable(true);
    m_view.append_column(*subject_column);

    m_view.append_column("Author", m_columns.m_author);
    m_view.append_column("Date", m_columns.m_date);
    m_view.append_column("Commit", m_columns.m_short_commit);

    for (int i = 2; i < 5; ++i) {
        if (Gtk::TreeViewColumn *column = m_view.get_column(i)) {
            column->set_resizable(true);
        }
    }

    m_scroller.add(m_view);
    m_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_scroller.set_shadow_type(Gtk::SHADOW_NONE);

    m_detail.set_editable(false);
    m_detail.set_monospace(true);
    m_detail.set_wrap_mode(Gtk::WRAP_NONE);
    m_detail_scroller.add(m_detail);
    m_detail_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_detail_scroller.set_shadow_type(Gtk::SHADOW_NONE);
    m_detail_box.pack_start(m_detail_scroller, Gtk::PACK_EXPAND_WIDGET);

    m_splitter.pack1(m_scroller, true, false);
    m_splitter.pack2(m_detail_box, false, false);
    // A height request rather than a paned position, for the same reason the main
    // window's panes use one: a position is clamped against an allocation that does
    // not exist yet, and is silently reduced to nothing.
    m_detail_box.set_size_request(-1, s_detail_height);
    pack_start(m_splitter, Gtk::PACK_EXPAND_WIDGET);

    m_view.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &HistoryView::on_selection_changed));
    m_view.signal_button_press_event().connect(sigc::mem_fun(*this, &HistoryView::on_button_press), false);

    // Pull the next page as the user approaches the bottom, rather than reading the
    // whole log up front.
    m_scroller.get_vadjustment()->signal_value_changed().connect([this] {
        on_scroll_near_end();
    });

    // The first page waits until the tab is actually looked at.
    signal_map().connect([this] {
        if (m_needs_initial_load) {
            m_needs_initial_load = false;
            reload();
        }
    });
}

void HistoryView::reload()
{
    ++m_generation;
    m_store->clear();
    m_commits.clear();
    m_graph_rows.clear();
    m_layout.reset();
    m_has_more = true;
    m_loading = false;
    m_needs_initial_selection = true;

    load_page();
}

void HistoryView::load_page()
{
    if (m_loading || !m_has_more) {
        return;
    }
    m_loading = true;

    const unsigned long generation = m_generation;
    const int skip = static_cast<int>(m_commits.size());
    const int limit = Settings::instance().history_page_size();

    GitCommand command(GitLane::Read, LogParser::log_arguments(limit, skip));
    command.m_label = "Reading history";

    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job, generation, limit] {
        if (generation != m_generation) {
            // The view was reloaded while this page was in flight.
            return;
        }

        m_loading = false;

        if (!job->succeeded()) {
            // An unborn branch has no commits and git exits non-zero; that is an
            // empty history rather than an error worth reporting.
            m_has_more = false;
            if (!m_commits.empty()) {
                m_signal_failed.emit("Could not read the history.", job->error_text());
            }
            return;
        }

        const std::vector<CommitRecord> page = LogParser::parse(job->stdout_data());
        if (page.empty()) {
            m_has_more = false;
            return;
        }

        // A short page means the traversal is exhausted.
        if (static_cast<int>(page.size()) < limit) {
            m_has_more = false;
        }

        append_page(page);
    });
}

void HistoryView::append_page(const std::vector<CommitRecord> &page)
{
    for (const CommitRecord &commit : page) {
        const int index = static_cast<int>(m_commits.size());

        m_commits.push_back(commit);
        m_graph_rows.push_back(m_layout.place(commit));

        Gtk::TreeModel::Row row = *m_store->append();
        row[m_columns.m_subject_markup] = subject_markup(commit);
        row[m_columns.m_author] = commit.m_author_name;
        row[m_columns.m_date] = short_date(commit.m_author_date);
        row[m_columns.m_short_commit] = commit.m_abbreviated_name;
        row[m_columns.m_commit] = commit.m_object_name;
        row[m_columns.m_row_index] = index;
    }

    // Open on the newest commit rather than an empty detail pane.
    if (m_needs_initial_selection && !m_store->children().empty()) {
        m_needs_initial_selection = false;
        m_view.get_selection()->select(m_store->children().begin());
    }
}

void HistoryView::on_graph_cell_data(Gtk::CellRenderer *renderer, const Gtk::TreeModel::iterator &iter)
{
    const int index = (*iter)[m_columns.m_row_index];
    if (index < 0 || static_cast<std::size_t>(index) >= m_graph_rows.size()) {
        return;
    }

    dynamic_cast<GraphCellRenderer *>(renderer)->set_graph_row(m_graph_rows[static_cast<std::size_t>(index)]);
}

bool HistoryView::on_scroll_near_end()
{
    const Glib::RefPtr<Gtk::Adjustment> adjustment = m_scroller.get_vadjustment();

    // Within one page-height of the bottom.
    if (adjustment->get_value() + adjustment->get_page_size() * 2 >= adjustment->get_upper()) {
        load_page();
    }
    return false;
}

void HistoryView::on_selection_changed()
{
    const Gtk::TreeModel::iterator iter = m_view.get_selection()->get_selected();
    if (!iter) {
        m_detail.get_buffer()->set_text({});
        return;
    }

    const int index = (*iter)[m_columns.m_row_index];
    if (index < 0 || static_cast<std::size_t>(index) >= m_commits.size()) {
        return;
    }

    const CommitRecord commit = m_commits[static_cast<std::size_t>(index)];

    // --stat alongside the patch, so a large commit is legible before scrolling.
    GitCommand command(GitLane::Read, {"show", "--no-color", "--stat", "--patch", commit.m_object_name});
    command.m_label = "Reading commit " + commit.m_abbreviated_name;
    command.m_dedupe_key = "show:" + commit.m_object_name;

    const std::string expected = commit.m_object_name;
    GitJob *job = m_repository.runner().run(command);
    job->signal_finished().connect([this, job, expected] {
        // The user may have moved on while this was loading.
        const Gtk::TreeModel::iterator current = m_view.get_selection()->get_selected();
        if (!current) {
            return;
        }
        const int index = (*current)[m_columns.m_row_index];
        if (index < 0 || static_cast<std::size_t>(index) >= m_commits.size() || m_commits[static_cast<std::size_t>(index)].m_object_name != expected) {
            return;
        }

        m_detail.get_buffer()->set_text(job->stdout_data());
    });
}

bool HistoryView::on_button_press(GdkEventButton *event)
{
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) {
        return false;
    }

    Gtk::TreeModel::Path path;
    if (!m_view.get_path_at_pos(static_cast<int>(event->x), static_cast<int>(event->y), path)) {
        return false;
    }

    m_view.get_selection()->select(path);

    const int index = (*m_store->get_iter(path))[m_columns.m_row_index];
    if (index < 0 || static_cast<std::size_t>(index) >= m_commits.size()) {
        return false;
    }

    show_context_menu(m_commits[static_cast<std::size_t>(index)], event);
    return true;
}

void HistoryView::show_context_menu(const CommitRecord &commit, GdkEventButton *event)
{
    m_menu.foreach ([this](Gtk::Widget &child) {
        m_menu.remove(child);
    });

    const auto add_item = [this](const Glib::ustring &label, const sigc::slot<void()> &slot) {
        auto *item = Gtk::manage(new Gtk::MenuItem(label, true));
        item->signal_activate().connect(slot);
        m_menu.append(*item);
    };

    auto *parent = dynamic_cast<Gtk::Window *>(get_toplevel());

    const auto ask_for_name = [parent](const Glib::ustring &title, const Glib::ustring &prompt, Glib::ustring &value) {
        Gtk::Dialog dialog(title, *parent, true);
        dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
        dialog.add_button("_OK", Gtk::RESPONSE_ACCEPT);
        dialog.set_default_response(Gtk::RESPONSE_ACCEPT);

        Gtk::Box *content = dialog.get_content_area();
        content->set_spacing(6);
        content->set_margin_start(12);
        content->set_margin_end(12);
        content->set_margin_top(12);

        auto *label = Gtk::manage(new Gtk::Label(prompt));
        label->set_xalign(0.0F);
        content->pack_start(*label, Gtk::PACK_SHRINK);

        auto *entry = Gtk::manage(new Gtk::Entry());
        entry->set_activates_default(true);
        content->pack_start(*entry, Gtk::PACK_SHRINK);

        dialog.show_all_children();

        if (dialog.run() != Gtk::RESPONSE_ACCEPT || entry->get_text().empty()) {
            return false;
        }
        value = entry->get_text();
        return true;
    };

    const CommitRecord captured = commit;

    add_item("Create branch here…", [this, captured, ask_for_name] {
        Glib::ustring name;
        if (ask_for_name("New Branch", Glib::ustring::compose("Name for a branch starting at %1:", captured.m_abbreviated_name), name)) {
            m_ref_service.create_branch(name, captured.m_object_name, true);
        }
    });

    add_item("Tag this commit…", [this, captured, ask_for_name] {
        Glib::ustring name;
        if (ask_for_name("New Tag", Glib::ustring::compose("Name for a tag on %1:", captured.m_abbreviated_name), name)) {
            m_ref_service.create_tag(name, captured.m_object_name, {});
        }
    });

    m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

    add_item("Check out this commit", [this, captured, parent] {
        Gtk::MessageDialog dialog(*parent,
                                  Glib::ustring::compose("Check out %1?", captured.m_abbreviated_name),
                                  false,
                                  Gtk::MESSAGE_QUESTION,
                                  Gtk::BUTTONS_NONE,
                                  true);
        dialog.set_secondary_text("HEAD will not be on a branch afterwards, so new commits would not belong to one.");
        dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
        dialog.add_button("Check _Out", Gtk::RESPONSE_ACCEPT);
        dialog.set_default_response(Gtk::RESPONSE_CANCEL);

        if (dialog.run() == Gtk::RESPONSE_ACCEPT) {
            m_ref_service.checkout_detached(captured.m_object_name);
        }
    });

    m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

    add_item("Cherry-pick onto the current branch", [this, captured] {
        m_rebase_service.cherry_pick(captured.m_object_name);
    });

    add_item("Revert this commit", [this, captured, parent] {
        Gtk::MessageDialog dialog(*parent,
                                  Glib::ustring::compose("Add a commit that undoes %1?", captured.m_abbreviated_name),
                                  false,
                                  Gtk::MESSAGE_QUESTION,
                                  Gtk::BUTTONS_NONE,
                                  true);
        dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
        dialog.add_button("_Revert", Gtk::RESPONSE_ACCEPT);
        dialog.set_default_response(Gtk::RESPONSE_CANCEL);

        if (dialog.run() == Gtk::RESPONSE_ACCEPT) {
            m_rebase_service.revert(captured.m_object_name);
        }
    });

    m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

    add_item("Rewrite history after this commit…", [this, captured] {
        // The commit itself is the upstream: everything after it is what gets
        // replayed, which is the same thing `rebase -i <commit>` means.
        m_signal_interactive_rebase_requested.emit(captured.m_object_name);
    });

    m_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

    add_item("Copy commit ID", [captured] {
        Gtk::Clipboard::get()->set_text(captured.m_object_name);
    });

    m_menu.show_all();
    m_menu.popup_at_pointer(reinterpret_cast<GdkEvent *>(event));
}

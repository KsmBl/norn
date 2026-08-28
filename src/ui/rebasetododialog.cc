/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "rebasetododialog.h"

#include <gtkmm/box.h>
#include <gtkmm/buttonbox.h>
#include <gtkmm/cellrenderertext.h>

namespace
{
Glib::ustring action_label(RebaseStep::Action action)
{
    switch (action) {
    case RebaseStep::Action::Pick:
        return "pick";
    case RebaseStep::Action::Reword:
        return "reword";
    case RebaseStep::Action::Edit:
        return "edit";
    case RebaseStep::Action::Squash:
        return "squash";
    case RebaseStep::Action::Fixup:
        return "fixup";
    case RebaseStep::Action::Drop:
        return "drop";
    case RebaseStep::Action::Break:
        return "break";
    }
    return {};
}

Glib::ustring action_description(RebaseStep::Action action)
{
    switch (action) {
    case RebaseStep::Action::Pick:
        return "Keep this commit as it is.";
    case RebaseStep::Action::Reword:
        return "Keep the commit, but stop to change its message.";
    case RebaseStep::Action::Edit:
        return "Stop after this commit so it can be changed.";
    case RebaseStep::Action::Squash:
        return "Fold into the commit above, combining both messages.";
    case RebaseStep::Action::Fixup:
        return "Fold into the commit above, discarding this message.";
    case RebaseStep::Action::Drop:
        return "Leave this commit out entirely.";
    case RebaseStep::Action::Break:
        return "Stop here without applying anything.";
    }
    return {};
}
}

RebaseTodoDialog::RebaseTodoDialog(Gtk::Window &parent, const std::string &upstream, const std::vector<RebaseStep> &steps)
    : Gtk::Dialog("Rewrite History", parent, true)
{
    build_ui(upstream);
    populate(steps);
    update_warnings();

    show_all_children();
    m_warning.hide();
    update_warnings();
}

void RebaseTodoDialog::build_ui(const std::string &upstream)
{
    set_default_size(760, 520);

    add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    add_button("_Start Rebase", Gtk::RESPONSE_ACCEPT);
    set_default_response(Gtk::RESPONSE_CANCEL);

    Gtk::Box *content = get_content_area();
    content->set_spacing(6);
    content->set_margin_start(12);
    content->set_margin_end(12);
    content->set_margin_top(12);
    content->set_margin_bottom(6);

    auto *intro = Gtk::manage(new Gtk::Label(
        Glib::ustring::compose("These commits will be replayed onto “%1”, oldest first. Reorder them, or change what happens to each.", upstream)));
    intro->set_xalign(0.0F);
    intro->set_line_wrap(true);
    content->pack_start(*intro, Gtk::PACK_SHRINK);

    dynamic_cast<Gtk::Container *>(m_warning.get_content_area())->add(m_warning_label);
    m_warning_label.set_line_wrap(true);
    m_warning.set_show_close_button(false);
    m_warning.set_no_show_all(true);
    content->pack_start(m_warning, Gtk::PACK_SHRINK);

    auto *body = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));

    m_store = Gtk::ListStore::create(m_columns);
    m_view.set_model(m_store);
    m_view.set_headers_visible(false);
    m_view.set_reorderable(true);
    m_view.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);

    const auto add_column = [this](const Gtk::TreeModelColumn<Glib::ustring> &column, bool monospace, bool expand) {
        auto *renderer = Gtk::manage(new Gtk::CellRendererText());
        if (monospace) {
            renderer->property_family() = "monospace";
        }
        renderer->property_ellipsize() = expand ? Pango::ELLIPSIZE_END : Pango::ELLIPSIZE_NONE;

        auto *view_column = Gtk::manage(new Gtk::TreeViewColumn());
        view_column->pack_start(*renderer, expand);
        view_column->add_attribute(renderer->property_text(), column);
        // A dropped commit is struck through, so a plan that discards work says so
        // at a glance rather than only in the keyword.
        view_column->add_attribute(renderer->property_strikethrough(), m_columns.m_strikethrough);
        view_column->set_expand(expand);
        m_view.append_column(*view_column);
    };

    add_column(m_columns.m_action_label, true, false);
    add_column(m_columns.m_commit, true, false);
    add_column(m_columns.m_subject, false, true);

    m_scroller.add(m_view);
    m_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_scroller.set_shadow_type(Gtk::SHADOW_IN);
    body->pack_start(m_scroller, Gtk::PACK_EXPAND_WIDGET);

    auto *side = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));

    const auto add_action_button = [this, side](RebaseStep::Action action) {
        auto *button = Gtk::manage(new Gtk::Button(action_label(action)));
        button->set_tooltip_text(action_description(action));
        button->signal_clicked().connect([this, action] {
            set_action_for_selection(action);
        });
        side->pack_start(*button, Gtk::PACK_SHRINK);
    };

    add_action_button(RebaseStep::Action::Pick);
    add_action_button(RebaseStep::Action::Reword);
    add_action_button(RebaseStep::Action::Edit);
    add_action_button(RebaseStep::Action::Squash);
    add_action_button(RebaseStep::Action::Fixup);
    add_action_button(RebaseStep::Action::Drop);

    auto *spacer = Gtk::manage(new Gtk::Label());
    spacer->set_size_request(-1, 12);
    side->pack_start(*spacer, Gtk::PACK_SHRINK);

    auto *up = Gtk::manage(new Gtk::Button("Move Up"));
    up->signal_clicked().connect([this] {
        move_selection(-1);
    });
    side->pack_start(*up, Gtk::PACK_SHRINK);

    auto *down = Gtk::manage(new Gtk::Button("Move Down"));
    down->signal_clicked().connect([this] {
        move_selection(1);
    });
    side->pack_start(*down, Gtk::PACK_SHRINK);

    body->pack_start(*side, Gtk::PACK_SHRINK);
    content->pack_start(*body, Gtk::PACK_EXPAND_WIDGET);

    m_store->signal_row_deleted().connect([this](const Gtk::TreeModel::Path &) {
        update_warnings();
    });
}

void RebaseTodoDialog::populate(const std::vector<RebaseStep> &steps)
{
    for (const RebaseStep &step : steps) {
        Gtk::TreeModel::Row row = *m_store->append();
        row[m_columns.m_action_label] = action_label(step.m_action);
        row[m_columns.m_commit] = step.m_commit;
        row[m_columns.m_subject] = step.m_subject;
        row[m_columns.m_action] = static_cast<int>(step.m_action);
        row[m_columns.m_strikethrough] = step.m_action == RebaseStep::Action::Drop;
    }
}

void RebaseTodoDialog::set_action_for_selection(RebaseStep::Action action)
{
    for (const Gtk::TreeModel::Path &path : m_view.get_selection()->get_selected_rows()) {
        Gtk::TreeModel::Row row = *m_store->get_iter(path);
        row[m_columns.m_action] = static_cast<int>(action);
        row[m_columns.m_action_label] = action_label(action);
        row[m_columns.m_strikethrough] = action == RebaseStep::Action::Drop;
    }
    update_warnings();
}

void RebaseTodoDialog::move_selection(int offset)
{
    const std::vector<Gtk::TreeModel::Path> selected = m_view.get_selection()->get_selected_rows();
    if (selected.size() != 1) {
        return;
    }

    const Gtk::TreeModel::iterator iter = m_store->get_iter(selected.front());
    const int row = selected.front()[0];
    const int target = row + offset;

    if (target < 0 || target >= static_cast<int>(m_store->children().size())) {
        return;
    }

    Gtk::TreeModel::iterator other = m_store->children().begin();
    std::advance(other, target);
    m_store->iter_swap(iter, other);

    update_warnings();
}

void RebaseTodoDialog::update_warnings()
{
    if (!m_store->children().empty()) {
        const auto first = static_cast<RebaseStep::Action>(static_cast<int>((*m_store->children().begin())[m_columns.m_action]));
        // A squash or fixup folds into the commit above it, so there has to be one.
        if (first == RebaseStep::Action::Squash || first == RebaseStep::Action::Fixup) {
            m_warning.set_message_type(Gtk::MESSAGE_ERROR);
            m_warning_label.set_text("The first commit cannot be squashed or fixed up: there is nothing above it to fold into.");
            m_warning.show();
            set_response_sensitive(Gtk::RESPONSE_ACCEPT, false);
            return;
        }
    }

    set_response_sensitive(Gtk::RESPONSE_ACCEPT, true);

    int dropped = 0;
    for (const Gtk::TreeModel::Row &row : m_store->children()) {
        if (static_cast<RebaseStep::Action>(static_cast<int>(row[m_columns.m_action])) == RebaseStep::Action::Drop) {
            ++dropped;
        }
    }

    if (dropped > 0) {
        m_warning.set_message_type(Gtk::MESSAGE_WARNING);
        m_warning_label.set_text(dropped == 1 ? "1 commit will be discarded." : Glib::ustring::compose("%1 commits will be discarded.", dropped));
        m_warning.show();
        return;
    }

    m_warning.hide();
}

std::vector<RebaseStep> RebaseTodoDialog::steps() const
{
    std::vector<RebaseStep> steps;

    for (const Gtk::TreeModel::Row &row : m_store->children()) {
        RebaseStep step;
        step.m_action = static_cast<RebaseStep::Action>(static_cast<int>(row[m_columns.m_action]));
        step.m_commit = Glib::ustring(row[m_columns.m_commit]).raw();
        step.m_subject = Glib::ustring(row[m_columns.m_subject]).raw();
        steps.push_back(step);
    }

    return steps;
}

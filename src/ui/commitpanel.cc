/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "commitpanel.h"

#include "core/commitservice.h"
#include "core/repository.h"
#include "core/settings.h"

namespace
{
/*! Past this, tools start truncating. */
constexpr int s_subject_hard_limit = 72;

std::string trimmed(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}
}

CommitPanel::CommitPanel(Repository &repository, CommitService &commit_service)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL)
    , m_repository(repository)
    , m_commit_service(commit_service)
{
    build_ui();

    m_repository.signal_status_changed().connect(sigc::mem_fun(*this, &CommitPanel::update_state));
    m_commit_service.signal_committed().connect(sigc::mem_fun(*this, &CommitPanel::clear));

    m_commit_service.signal_head_message_ready().connect([this](const Glib::ustring &message) {
        if (!m_amend.get_active()) {
            return;
        }
        const Glib::ustring::size_type newline = message.find('\n');
        if (newline == Glib::ustring::npos) {
            m_subject.set_text(message);
            m_body.get_buffer()->set_text({});
        } else {
            m_subject.set_text(message.substr(0, newline));
            m_body.get_buffer()->set_text(trimmed(message.substr(newline + 1)));
        }
        update_state();
    });

    apply_settings();
    update_state();
    show_all_children();
}

void CommitPanel::build_ui()
{
    set_margin_start(6);
    set_margin_end(6);
    set_margin_bottom(6);
    set_spacing(4);

    m_subject.set_placeholder_text("Summary of the change");
    m_subject_row.set_spacing(6);
    m_subject_row.pack_start(m_subject, Gtk::PACK_EXPAND_WIDGET);
    m_counter.set_width_chars(3);
    m_subject_row.pack_start(m_counter, Gtk::PACK_SHRINK);
    pack_start(m_subject_row, Gtk::PACK_SHRINK);

    m_body.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    m_body.set_accepts_tab(false);
    m_body_scroller.add(m_body);
    m_body_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_body_scroller.set_shadow_type(Gtk::SHADOW_IN);
    m_body_scroller.set_size_request(-1, 80);
    pack_start(m_body_scroller, Gtk::PACK_EXPAND_WIDGET);

    m_amend.set_tooltip_text("Replace the previous commit instead of adding a new one.");
    m_sign_off.set_tooltip_text("Append a Signed-off-by trailer.");
    m_no_verify.set_tooltip_text("Do not run the pre-commit and commit-msg hooks.");

    m_options.set_spacing(12);
    m_options.pack_start(m_amend, Gtk::PACK_SHRINK);
    m_options.pack_start(m_sign_off, Gtk::PACK_SHRINK);
    m_options.pack_start(m_no_verify, Gtk::PACK_SHRINK);
    m_options.pack_end(m_commit_and_push, Gtk::PACK_SHRINK);
    m_options.pack_end(m_commit, Gtk::PACK_SHRINK);
    pack_start(m_options, Gtk::PACK_SHRINK);

    m_subject.signal_changed().connect(sigc::mem_fun(*this, &CommitPanel::update_state));
    m_amend.signal_toggled().connect(sigc::mem_fun(*this, &CommitPanel::on_amend_toggled));
    m_commit.signal_clicked().connect(sigc::mem_fun(*this, &CommitPanel::do_commit));
    m_commit_and_push.signal_clicked().connect([this] {
        do_commit();
        m_signal_commit_and_push_requested.emit();
    });
}

std::string CommitPanel::message() const
{
    const std::string subject = trimmed(m_subject.get_text());
    const std::string body = trimmed(m_body.get_buffer()->get_text());

    if (body.empty()) {
        return subject + "\n";
    }
    // The blank line between subject and body is what makes git treat the first
    // line as the summary.
    return subject + "\n\n" + body + "\n";
}

bool CommitPanel::is_amending() const
{
    return m_amend.get_active();
}

void CommitPanel::on_amend_toggled()
{
    if (m_amend.get_active()) {
        m_saved_subject = m_subject.get_text();
        m_saved_body = m_body.get_buffer()->get_text();
        m_commit_service.request_head_message();
    } else {
        m_subject.set_text(m_saved_subject);
        m_body.get_buffer()->set_text(m_saved_body);
    }
    update_state();
}

void CommitPanel::clear()
{
    m_subject.set_text({});
    m_body.get_buffer()->set_text({});
    m_saved_subject.clear();
    m_saved_body.clear();
    // Leaving amend ticked after a successful amend would make the next commit
    // silently rewrite the one just made.
    m_amend.set_active(false);
    update_state();
}

void CommitPanel::apply_settings()
{
    m_subject_soft_limit = Settings::instance().subject_soft_limit();
    // Only the default; an explicit tick during this session is not overridden.
    if (!m_sign_off.get_active()) {
        m_sign_off.set_active(Settings::instance().sign_off_by_default());
    }
    update_state();
}

void CommitPanel::update_state()
{
    const int length = static_cast<int>(trimmed(m_subject.get_text()).length());
    m_counter.set_text(Glib::ustring::format(length));
    m_counter.set_tooltip_text(Glib::ustring::compose("Keep the summary under %1 characters so it is not truncated.", s_subject_hard_limit));
    m_counter.set_sensitive(length <= m_subject_soft_limit);

    const StatusSnapshot &status = m_repository.status();

    // Amending needs no staged changes, since it can just reword; a fresh commit does.
    bool has_staged = false;
    for (const StatusEntry &entry : status.m_entries) {
        if (entry.is_staged()) {
            has_staged = true;
            break;
        }
    }

    const bool blocked_by_conflicts = status.has_conflicts();
    const bool can_commit = length > 0 && !blocked_by_conflicts && (has_staged || m_amend.get_active());

    m_commit.set_sensitive(can_commit);
    m_commit_and_push.set_sensitive(can_commit);

    if (blocked_by_conflicts) {
        m_commit.set_tooltip_text("Resolve the conflicts before committing.");
    } else if (!has_staged && !m_amend.get_active()) {
        m_commit.set_tooltip_text("Stage something to commit.");
    } else if (length == 0) {
        m_commit.set_tooltip_text("Write a summary first.");
    } else {
        m_commit.set_tooltip_text({});
    }

    // Nothing to amend on a branch with no commits yet.
    m_amend.set_sensitive(!status.m_is_unborn);
}

void CommitPanel::do_commit()
{
    CommitOptions options;
    options.m_message = message();
    options.m_amend = m_amend.get_active();
    options.m_sign_off = m_sign_off.get_active();
    options.m_no_verify = m_no_verify.get_active();

    m_commit_service.commit(options);
}

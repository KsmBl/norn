/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "messageeditordialog.h"

#include <glib/gstdio.h>

#include <gtkmm/box.h>
#include <gtkmm/label.h>

#include <fstream>

MessageEditorDialog::MessageEditorDialog(Gtk::Window &parent, const std::string &file)
    : Gtk::Dialog("Commit Message", parent, true)
    , m_file(file)
{
    set_default_size(640, 420);

    add_button("_Abort", Gtk::RESPONSE_CANCEL);
    add_button("_OK", Gtk::RESPONSE_ACCEPT);
    set_default_response(Gtk::RESPONSE_ACCEPT);

    Gtk::Box *content = get_content_area();
    content->set_spacing(6);
    content->set_margin_start(12);
    content->set_margin_end(12);
    content->set_margin_top(12);

    auto *label = Gtk::manage(new Gtk::Label("Git is waiting for this message. Lines beginning with # are ignored."));
    label->set_xalign(0.0F);
    label->set_line_wrap(true);
    content->pack_start(*label, Gtk::PACK_SHRINK);

    m_editor.set_monospace(true);
    m_editor.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    m_scroller.add(m_editor);
    m_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_scroller.set_shadow_type(Gtk::SHADOW_IN);
    content->pack_start(m_scroller, Gtk::PACK_EXPAND_WIDGET);

    std::ifstream source(m_file, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
    if (g_utf8_validate(contents.data(), static_cast<gssize>(contents.size()), nullptr)) {
        m_editor.get_buffer()->set_text(contents);
    }

    show_all_children();
}

bool MessageEditorDialog::save() const
{
    std::string text = m_editor.get_buffer()->get_text();
    if (text.empty() || text.back() != '\n') {
        text.push_back('\n');
    }

    const std::string temporary = m_file + ".norn.tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) {
            return false;
        }
    }

    if (g_rename(temporary.c_str(), m_file.c_str()) != 0) {
        g_unlink(temporary.c_str());
        return false;
    }

    return true;
}

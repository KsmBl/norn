/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "editorview.h"

#include <glibmm/convert.h>
#include <glibmm/fileutils.h>

#include <glib/gstdio.h>

#include <fstream>

namespace
{
/*! Forwards GtkTextBuffer's modified-changed to the owning EditorView. */
void on_modified_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    auto *view = static_cast<EditorView *>(user_data);
    view->signal_modified_changed().emit(gtk_text_buffer_get_modified(buffer) != FALSE);
}
}

EditorView::EditorView(const std::string &path)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL)
    , m_path(path)
{
    m_buffer = gtk_source_buffer_new(nullptr);
    m_view = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(m_buffer));

    gtk_source_view_set_show_line_numbers(m_view, TRUE);
    gtk_source_view_set_highlight_current_line(m_view, TRUE);
    gtk_source_view_set_auto_indent(m_view, TRUE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(m_view), TRUE);

    // The language is guessed from the filename, the same way any editor does it.
    GtkSourceLanguageManager *languages = gtk_source_language_manager_get_default();
    if (GtkSourceLanguage *language = gtk_source_language_manager_guess_language(languages, m_path.c_str(), nullptr)) {
        gtk_source_buffer_set_language(m_buffer, language);
    }

    m_modified_handler = g_signal_connect(m_buffer, "modified-changed", G_CALLBACK(on_modified_changed), this);

    m_scroller.add(*Glib::wrap(GTK_WIDGET(m_view)));
    m_scroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    m_scroller.set_shadow_type(Gtk::SHADOW_NONE);
    pack_start(m_scroller, Gtk::PACK_EXPAND_WIDGET);

    load();
    show_all_children();
}

EditorView::~EditorView()
{
    if (m_buffer != nullptr && m_modified_handler != 0) {
        g_signal_handler_disconnect(m_buffer, m_modified_handler);
    }
    if (m_buffer != nullptr) {
        g_object_unref(m_buffer);
    }
}

void EditorView::load()
{
    std::ifstream file(m_path, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    // A GtkTextBuffer only accepts valid UTF-8; anything else has to be rejected
    // rather than silently mangled into replacement characters.
    if (!g_utf8_validate(contents.data(), static_cast<gssize>(contents.size()), nullptr)) {
        gtk_text_buffer_set_text(GTK_TEXT_BUFFER(m_buffer), "This file is not valid UTF-8, so it cannot be edited here.", -1);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(m_view), FALSE);
        gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(m_buffer), FALSE);
        return;
    }

    // Loading counts as a change until told otherwise, so the modified flag is
    // cleared afterwards or the tab would open already marked dirty.
    gtk_source_buffer_begin_not_undoable_action(m_buffer);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(m_buffer), contents.data(), static_cast<gint>(contents.size()));
    gtk_source_buffer_end_not_undoable_action(m_buffer);
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(m_buffer), FALSE);
}

bool EditorView::is_modified() const
{
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(m_buffer)) != FALSE;
}

bool EditorView::save()
{
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(m_buffer), &start, &end);

    gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(m_buffer), &start, &end, FALSE);
    const std::string contents = text != nullptr ? text : "";
    g_free(text);

    // Written to a temporary alongside and renamed into place, so a reader — git
    // included — never sees a half-written file.
    const std::string temporary = m_path + ".norn.tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!file) {
            return false;
        }
    }

    if (g_rename(temporary.c_str(), m_path.c_str()) != 0) {
        g_unlink(temporary.c_str());
        return false;
    }

    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(m_buffer), FALSE);
    m_signal_saved.emit();
    return true;
}

/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <gtkmm/box.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/widget.h>

#include <gtksourceview/gtksource.h>

#include <string>

/*!
 * An editable view of one file from the working tree, backed by GtkSourceView.
 *
 * GtkSourceView rather than a plain Gtk::TextView so the file arrives with the
 * syntax highlighting, line numbers and bracket matching the rest of the desktop
 * already provides — it is what Mousepad, Xfce's own editor, is built on.
 *
 * It has no C++ binding, so this wraps the C API directly rather than pretending
 * otherwise.
 */
class EditorView : public Gtk::Box
{
public:
    explicit EditorView(const std::string &path);
    ~EditorView() override;

    const std::string &path() const
    {
        return m_path;
    }

    bool is_modified() const;

    /*! Writes the buffer back to disk. */
    bool save();

    /*! The buffer became dirty or clean, so a tab label can show it. */
    sigc::signal<void(bool)> &signal_modified_changed()
    {
        return m_signal_modified_changed;
    }
    /*! The file was written, so the repository status is now stale. */
    sigc::signal<void()> &signal_saved()
    {
        return m_signal_saved;
    }

private:
    void load();

    std::string m_path;
    Gtk::ScrolledWindow m_scroller;

    GtkSourceView *m_view = nullptr;
    GtkSourceBuffer *m_buffer = nullptr;
    gulong m_modified_handler = 0;

    sigc::signal<void(bool)> m_signal_modified_changed;
    sigc::signal<void()> m_signal_saved;
};

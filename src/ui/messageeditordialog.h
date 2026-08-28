/*
    SPDX-FileCopyrightText: 2026 KsmBL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <gtkmm/dialog.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>

#include <string>

/*!
 * Edits a commit message file on git's behalf.
 *
 * Shown when git asks for an editor during an operation norn cannot supply a
 * message for up front — rewording inside a rebase, most of all. Accepting writes
 * the file back and lets git carry on; cancelling leaves it untouched and aborts
 * the step.
 */
class MessageEditorDialog : public Gtk::Dialog
{
public:
    MessageEditorDialog(Gtk::Window &parent, const std::string &file);

    /*!
     * Writes the edited text back to the file.
     *
     * Written to a temporary in the same directory and renamed over the original,
     * so git can never observe a half-written message.
     */
    bool save() const;

private:
    std::string m_file;
    Gtk::ScrolledWindow m_scroller;
    Gtk::TextView m_editor;
};
